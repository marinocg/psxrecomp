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
    m_envWriteRanges.clear();
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
        m_envWriteRanges.push_back(range);
    }

    std::sort(m_envWriteRanges.begin(), m_envWriteRanges.end(),
              [](const EnvRange& a, const EnvRange& b) { return a.start < b.start; });
}

bool DiagWatchpointEngine::shouldWatchRamWrite(Address address, u8 size) const
{
    return shouldWatchRamAccess(WatchpointKind::RamWrite, address, size);
}

bool DiagWatchpointEngine::shouldWatchRamRead(Address address, u8 size) const
{
    return shouldWatchRamAccess(WatchpointKind::RamRead, address, size);
}

bool DiagWatchpointEngine::shouldWatchMmioWrite(Address address, u8 size) const
{
    return shouldWatchMmioAccess(WatchpointKind::MmioWrite, address, size);
}

bool DiagWatchpointEngine::shouldWatchMmioRead(Address address, u8 size) const
{
    return shouldWatchMmioAccess(WatchpointKind::MmioRead, address, size);
}

void DiagWatchpointEngine::recordRamWrite(Address writerPc, Address address, u8 size, u32 oldValue,
                                          u32 newValue, RuntimeLogger* logger,
                                          Address resumeAddress)
{
    recordRamAccess(WatchpointKind::RamWrite, writerPc, address, size, oldValue, newValue, logger,
                    resumeAddress);
}

void DiagWatchpointEngine::recordRamRead(Address readerPc, Address address, u8 size, u32 value,
                                         RuntimeLogger* logger, Address resumeAddress)
{
    recordRamAccess(WatchpointKind::RamRead, readerPc, address, size, value, value, logger,
                    resumeAddress);
}

void DiagWatchpointEngine::recordMmioWrite(Address writerPc, Address address, u8 size, u32 value,
                                           RuntimeLogger* logger, Address resumeAddress)
{
    recordMmioAccess(WatchpointKind::MmioWrite, writerPc, address, size, value, logger,
                     resumeAddress);
}

void DiagWatchpointEngine::recordMmioRead(Address readerPc, Address address, u8 size, u32 value,
                                          RuntimeLogger* logger, Address resumeAddress)
{
    recordMmioAccess(WatchpointKind::MmioRead, readerPc, address, size, value, logger,
                     resumeAddress);
}

bool DiagWatchpointEngine::shouldWatchRamAccess(WatchpointKind kind, Address address, u8 size) const
{
    const Address physical = normalizePhysical(address);
    if (physical >= MemoryMap::RAM_SIZE)
    {
        return false;
    }
    const Address accessEnd = physical + size - 1;

    for (const auto& wp : m_configs)
    {
        if (wp.kind != kind)
        {
            continue;
        }
        const Address wpStart = normalizePhysical(wp.rangeStart);
        const Address wpEnd = normalizePhysical(wp.rangeEnd);
        if (physical <= wpEnd && accessEnd >= wpStart)
        {
            return true;
        }
    }

    if (kind != WatchpointKind::RamWrite)
    {
        return false;
    }

    for (const auto& range : m_envWriteRanges)
    {
        if (accessEnd < range.start)
        {
            break;
        }
        if (physical <= range.end && accessEnd >= range.start)
        {
            return true;
        }
    }

    return false;
}

bool DiagWatchpointEngine::shouldWatchMmioAccess(WatchpointKind kind, Address address,
                                                 u8 size) const
{
    const Address accessStart = address;
    const Address accessEnd = accessStart + size - 1;
    for (const auto& wp : m_configs)
    {
        if (wp.kind != kind)
        {
            continue;
        }
        if (accessStart <= wp.rangeEnd && accessEnd >= wp.rangeStart)
        {
            return true;
        }
    }
    return false;
}

void DiagWatchpointEngine::recordRamAccess(WatchpointKind kind, Address accessPc, Address address,
                                           u8 size, u32 oldValue, u32 newValue,
                                           RuntimeLogger* logger, Address resumeAddress)
{
    const Address physical = normalizePhysical(address);
    const Address accessEnd = physical + size - 1;
    for (const auto& wp : m_configs)
    {
        if (wp.kind != kind)
        {
            continue;
        }
        const Address wpStart = normalizePhysical(wp.rangeStart);
        const Address wpEnd = normalizePhysical(wp.rangeEnd);
        if (physical > wpEnd || accessEnd < wpStart)
        {
            continue;
        }

        bool violated = !wp.predicate.type.empty() && !evaluatePredicate(wp.predicate, newValue);

        WatchpointEvent event;
        event.config = &wp;
        event.kind = kind;
        event.accessPc = accessPc;
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

        if (logger != nullptr && (wp.action == WatchpointAction::Log ||
                                  wp.action == WatchpointAction::Summarize || violated))
        {
            std::ostringstream msg;
            msg << "watchpoint=" << wp.name
                << (kind == WatchpointKind::RamRead ? " kind=read" : " kind=write") << " pc=0x"
                << std::hex << accessPc << " addr=0x" << (0x80000000u | physical)
                << " size=" << std::dec << static_cast<unsigned>(size);
            if (kind == WatchpointKind::RamRead)
            {
                msg << " value=0x" << std::hex << maskValueForSize(newValue, size);
            }
            else
            {
                msg << " old=0x" << std::hex << maskValueForSize(oldValue, size) << " new=0x"
                    << maskValueForSize(newValue, size);
            }
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

void DiagWatchpointEngine::recordMmioAccess(WatchpointKind kind, Address accessPc, Address address,
                                            u8 size, u32 value, RuntimeLogger* logger,
                                            Address resumeAddress)
{
    const Address accessStart = address;
    const Address accessEnd = accessStart + size - 1;
    for (const auto& wp : m_configs)
    {
        if (wp.kind != kind)
        {
            continue;
        }
        if (accessStart > wp.rangeEnd || accessEnd < wp.rangeStart)
        {
            continue;
        }

        const bool violated = !wp.predicate.type.empty() && !evaluatePredicate(wp.predicate, value);

        WatchpointEvent event;
        event.config = &wp;
        event.kind = kind;
        event.accessPc = accessPc;
        event.address = address;
        event.size = size;
        event.oldValue = value;
        event.newValue = value;
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

        if (logger != nullptr && (wp.action == WatchpointAction::Log ||
                                  wp.action == WatchpointAction::Summarize || violated))
        {
            std::ostringstream msg;
            msg << "watchpoint=" << wp.name
                << (kind == WatchpointKind::MmioRead ? " kind=mmio_read" : " kind=mmio_write")
                << " pc=0x" << std::hex << accessPc << " addr=0x" << address << " size=" << std::dec
                << static_cast<unsigned>(size) << " value=0x" << std::hex
                << maskValueForSize(value, size);
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
        os << "  [" << (e.config != nullptr ? e.config->name : "env") << "] ";
        const bool isRam = e.kind == WatchpointKind::RamRead || e.kind == WatchpointKind::RamWrite;
        if (e.kind == WatchpointKind::RamRead)
        {
            os << "read";
        }
        else if (e.kind == WatchpointKind::RamWrite)
        {
            os << "write";
        }
        else if (e.kind == WatchpointKind::MmioRead)
        {
            os << "mmio_read";
        }
        else
        {
            os << "mmio_write";
        }
        os << " pc=0x" << std::hex << e.accessPc << " addr=0x";
        if (isRam)
        {
            os << (0x80000000u | (e.address & 0x1FFFFFFFu));
        }
        else
        {
            os << e.address;
        }
        os << " size=" << std::dec << static_cast<unsigned>(e.size);
        if (e.kind == WatchpointKind::RamRead || e.kind == WatchpointKind::MmioRead)
        {
            os << " value=0x" << std::hex << maskValueForSize(e.newValue, e.size);
        }
        else if (e.kind == WatchpointKind::MmioWrite)
        {
            os << " value=0x" << std::hex << maskValueForSize(e.newValue, e.size);
        }
        else
        {
            os << " old=0x" << std::hex << maskValueForSize(e.oldValue, e.size) << " new=0x"
               << maskValueForSize(e.newValue, e.size);
        }
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
