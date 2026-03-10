#include "psxrecomp/runtime/diag_watchpoints.h"

#include "psxrecomp/runtime/logger.h"
#include "psxrecomp/runtime/memory_map.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace psxrecomp
{
namespace runtime
{

namespace
{

Address normalizePhysical(Address address)
{
    return address & 0x1FFFFFFFu;
}

u32 maskValueForSize(u32 value, u8 size)
{
    switch (size)
    {
    case 1:
        return value & 0xFFu;
    case 2:
        return value & 0xFFFFu;
    default:
        return value;
    }
}

bool parseAddressToken(const std::string& text, Address& address)
{
    if (text.empty())
    {
        return false;
    }
    size_t consumed = 0;
    try
    {
        const auto parsed = std::stoull(text, &consumed, 0);
        if (consumed != text.size())
        {
            return false;
        }
        address = static_cast<Address>(parsed);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

} // namespace

DiagWatchpointEngine::DiagWatchpointEngine() = default;

void DiagWatchpointEngine::configure(const std::vector<WatchpointConfig>& configs,
                                     const DiagMemoryMapConfig& memMap)
{
    m_configs = configs;
    m_memMap = memMap;
    m_events.clear();
    m_trapViolation = false;
}

void DiagWatchpointEngine::mergeEnvWatchedRanges()
{
    const char* env = std::getenv("PSXRECOMP_WATCH_WRITE");
    if (env == nullptr || env[0] == '\0')
    {
        return;
    }

    std::string spec(env);
    std::vector<std::string> tokens;
    std::string current;
    for (char ch : spec)
    {
        if (ch == ',' || ch == ';' || std::isspace(static_cast<unsigned char>(ch)))
        {
            if (!current.empty())
            {
                tokens.push_back(current);
                current.clear();
            }
            continue;
        }
        current.push_back(ch);
    }
    if (!current.empty())
    {
        tokens.push_back(current);
    }

    for (const std::string& token : tokens)
    {
        EnvRange range{};
        const size_t dash = token.find('-');
        if (dash == std::string::npos)
        {
            Address address = 0;
            if (!parseAddressToken(token, address))
            {
                continue;
            }
            range.start = normalizePhysical(address);
            range.end = range.start;
        }
        else
        {
            Address start = 0;
            Address end = 0;
            if (!parseAddressToken(token.substr(0, dash), start) ||
                !parseAddressToken(token.substr(dash + 1), end))
            {
                continue;
            }
            range.start = normalizePhysical(start);
            range.end = normalizePhysical(end);
            if (range.end < range.start)
            {
                std::swap(range.start, range.end);
            }
        }

        if (range.start >= MemoryMap::RAM_SIZE)
        {
            continue;
        }
        range.end = std::min<Address>(range.end, MemoryMap::RAM_SIZE - 1);
        m_envRanges.push_back(range);
    }

    std::sort(m_envRanges.begin(), m_envRanges.end(),
              [](const EnvRange& a, const EnvRange& b) { return a.start < b.start; });
}

bool DiagWatchpointEngine::shouldWatchRamWrite(Address address, u8 size) const
{
    const Address physical = normalizePhysical(address);
    if (physical >= MemoryMap::RAM_SIZE)
    {
        return false;
    }
    const Address writeEnd = physical + size - 1;

    for (const auto& wp : m_configs)
    {
        if (wp.kind != WatchpointKind::RamWrite)
        {
            continue;
        }
        const Address wpStart = normalizePhysical(wp.rangeStart);
        const Address wpEnd = normalizePhysical(wp.rangeEnd);
        if (physical <= wpEnd && writeEnd >= wpStart)
        {
            return true;
        }
    }

    for (const auto& range : m_envRanges)
    {
        if (writeEnd < range.start)
        {
            break;
        }
        if (physical <= range.end && writeEnd >= range.start)
        {
            return true;
        }
    }

    return false;
}

void DiagWatchpointEngine::recordRamWrite(Address writerPc, Address address, u8 size,
                                          u32 oldValue, u32 newValue, RuntimeLogger* logger,
                                          Address resumeAddress)
{
    const Address physical = normalizePhysical(address);
    const Address writeEnd = physical + size - 1;

    for (const auto& wp : m_configs)
    {
        if (wp.kind != WatchpointKind::RamWrite)
        {
            continue;
        }
        const Address wpStart = normalizePhysical(wp.rangeStart);
        const Address wpEnd = normalizePhysical(wp.rangeEnd);
        if (physical > wpEnd || writeEnd < wpStart)
        {
            continue;
        }

        bool violated = !wp.predicate.type.empty() && !evaluatePredicate(wp.predicate, newValue);

        WatchpointEvent event;
        event.config = &wp;
        event.writerPc = writerPc;
        event.address = address;
        event.size = size;
        event.oldValue = oldValue;
        event.newValue = newValue;
        event.predicateViolated = violated;
        event.resumeAddress = resumeAddress;

        if (m_events.size() >= EVENT_RING_SIZE)
        {
            m_events.erase(m_events.begin());
        }
        m_events.push_back(event);

        if (violated && (wp.action == WatchpointAction::Trap ||
                         wp.action == WatchpointAction::TrapOnFirstViolation))
        {
            m_trapViolation = true;
        }

        if (logger != nullptr &&
            (wp.action == WatchpointAction::Log || wp.action == WatchpointAction::Summarize ||
             violated))
        {
            std::ostringstream msg;
            msg << "watchpoint=" << wp.name << " pc=0x" << std::hex << writerPc << " addr=0x"
                << (0x80000000u | physical) << " size=" << std::dec
                << static_cast<unsigned>(size) << " old=0x" << std::hex
                << maskValueForSize(oldValue, size) << " new=0x"
                << maskValueForSize(newValue, size);
            if (resumeAddress != 0)
            {
                msg << " resumed_at=0x" << std::hex << resumeAddress;
            }
            if (violated)
            {
                msg << " VIOLATED";
            }
            logger->log(LogLevel::Info, "watchpoint", msg.str());
        }
    }
}

bool DiagWatchpointEngine::hasTrapViolation() const
{
    return m_trapViolation;
}

std::string DiagWatchpointEngine::formatSummary() const
{
    std::ostringstream os;
    os << "Watchpoint events (newest first):\n";
    if (m_events.empty())
    {
        os << "  none\n";
        return os.str();
    }
    const size_t count = std::min<size_t>(m_events.size(), 16);
    for (size_t i = 0; i < count; ++i)
    {
        const auto& e = m_events[m_events.size() - 1 - i];
        os << "  [" << (e.config != nullptr ? e.config->name : "env") << "] pc=0x" << std::hex
           << e.writerPc << " addr=0x" << (0x80000000u | (e.address & 0x1FFFFFFFu))
           << " size=" << std::dec << static_cast<unsigned>(e.size) << " old=0x" << std::hex
           << maskValueForSize(e.oldValue, e.size) << " new=0x"
           << maskValueForSize(e.newValue, e.size);
        if (e.predicateViolated)
        {
            os << " VIOLATED";
        }
        if (e.resumeAddress != 0)
        {
            os << " resumed_at=0x" << std::hex << e.resumeAddress;
        }
        os << "\n";
    }
    return os.str();
}

size_t DiagWatchpointEngine::watchpointCount() const
{
    return m_configs.size();
}

size_t DiagWatchpointEngine::eventCount() const
{
    return m_events.size();
}

void DiagWatchpointEngine::clearEvents()
{
    m_events.clear();
    m_trapViolation = false;
}

bool DiagWatchpointEngine::evaluatePredicate(const WatchpointPredicate& pred, u32 value) const
{
    if (pred.type == "aligned_pointer_in_region")
    {
        return isAlignedPointerInRegion(value, pred.regionStart, pred.regionEnd);
    }
    if (pred.type == "value_in_range")
    {
        return value >= pred.valueMin && value <= pred.valueMax;
    }
    if (pred.type == "nonzero")
    {
        return value != 0;
    }
    if (pred.type == "bitmask_invariant")
    {
        return (value & pred.bitmask) == pred.bitmask;
    }
    if (pred.type == "region_membership")
    {
        return value >= pred.regionStart && value < pred.regionEnd;
    }
    return true;
}

Address DiagWatchpointEngine::normalizeAddress(Address address) const
{
    return address & 0x1FFFFFFFu;
}

bool DiagWatchpointEngine::isAlignedPointerInRegion(u32 value, Address regionStart,
                                                    Address regionEnd) const
{
    if (value == 0)
    {
        return true; // null is allowed
    }
    return (value & 0x3u) == 0u && value >= regionStart && value < regionEnd;
}

} // namespace runtime
} // namespace psxrecomp
