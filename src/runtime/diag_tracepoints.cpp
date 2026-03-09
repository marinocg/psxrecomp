#include "psxrecomp/runtime/diag_tracepoints.h"

#include "psxrecomp/runtime/logger.h"

#include <algorithm>
#include <sstream>

namespace psxrecomp
{
namespace runtime
{

DiagTracepointEngine::DiagTracepointEngine() = default;

void DiagTracepointEngine::configure(const std::vector<TracepointConfig>& configs)
{
    m_configs = configs;
    m_ranges.clear();
    m_recentHits.clear();
    for (const auto& config : m_configs)
    {
        ActiveRange range;
        range.config = &config;
        range.inside = false;
        m_ranges.push_back(range);
    }
}

void DiagTracepointEngine::observePc(Address pc, RuntimeLogger* logger)
{
    for (auto& range : m_ranges)
    {
        const bool inRange =
            pc >= range.config->pcRangeStart && pc <= range.config->pcRangeEnd;

        if (inRange && !range.inside)
        {
            range.inside = true;

            TracepointHit hit;
            hit.config = range.config;
            hit.pc = pc;
            hit.isEntry = true;
            if (m_recentHits.size() >= MAX_RECENT_HITS)
            {
                m_recentHits.erase(m_recentHits.begin());
            }
            m_recentHits.push_back(hit);

            if (logger != nullptr)
            {
                std::ostringstream msg;
                msg << "tracepoint=" << range.config->name << " event=entry pc=0x" << std::hex
                    << pc;
                logger->log(LogLevel::Info, "tracepoint", msg.str());
            }
        }
        else if (!inRange && range.inside)
        {
            range.inside = false;

            TracepointHit hit;
            hit.config = range.config;
            hit.pc = pc;
            hit.isExit = true;
            if (m_recentHits.size() >= MAX_RECENT_HITS)
            {
                m_recentHits.erase(m_recentHits.begin());
            }
            m_recentHits.push_back(hit);

            if (logger != nullptr)
            {
                std::ostringstream msg;
                msg << "tracepoint=" << range.config->name << " event=exit pc=0x" << std::hex
                    << pc;
                logger->log(LogLevel::Info, "tracepoint", msg.str());
            }
        }
    }
}

void DiagTracepointEngine::recordMmioRead(Address mmioAddress, u32 value, Address pc,
                                          RuntimeLogger* logger)
{
    for (const auto& range : m_ranges)
    {
        if (!range.inside)
        {
            continue;
        }
        for (Address watchAddr : range.config->mmioReads)
        {
            if (watchAddr == mmioAddress && logger != nullptr)
            {
                std::ostringstream msg;
                msg << "tracepoint=" << range.config->name << " event=mmio_read pc=0x"
                    << std::hex << pc << " addr=0x" << mmioAddress << " value=0x" << value;
                logger->log(LogLevel::Info, "tracepoint", msg.str());
            }
        }
    }
}

bool DiagTracepointEngine::hasTracepoints() const
{
    return !m_ranges.empty();
}

bool DiagTracepointEngine::isInTracedRange(Address pc) const
{
    for (const auto& range : m_ranges)
    {
        if (pc >= range.config->pcRangeStart && pc <= range.config->pcRangeEnd)
        {
            return true;
        }
    }
    return false;
}

std::string DiagTracepointEngine::formatRecentTraces() const
{
    std::ostringstream os;
    os << "Recent tracepoint hits (newest first):\n";
    if (m_recentHits.empty())
    {
        os << "  none\n";
        return os.str();
    }
    const size_t count = std::min<size_t>(m_recentHits.size(), 16);
    for (size_t i = 0; i < count; ++i)
    {
        const auto& hit = m_recentHits[m_recentHits.size() - 1 - i];
        os << "  [" << (hit.config != nullptr ? hit.config->name : "?") << "] "
           << (hit.isEntry ? "entry" : "exit") << " pc=0x" << std::hex << hit.pc << "\n";
    }
    return os.str();
}

size_t DiagTracepointEngine::tracepointCount() const
{
    return m_ranges.size();
}

} // namespace runtime
} // namespace psxrecomp
