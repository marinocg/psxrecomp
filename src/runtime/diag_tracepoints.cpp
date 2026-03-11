#include "psxrecomp/runtime/diag_tracepoints.h"

#include "psxrecomp/runtime/logger.h"

#include <algorithm>
#include <sstream>

namespace psxrecomp
{
namespace runtime
{

DiagTracepointEngine::DiagTracepointEngine() = default;

namespace
{

constexpr size_t kMaxRecentHits = 32;

void appendRecentHit(std::vector<TracepointHit>& hits, const TracepointHit& hit)
{
    if (hits.size() >= kMaxRecentHits)
    {
        hits.erase(hits.begin());
    }
    hits.push_back(hit);
}

} // namespace

void DiagTracepointEngine::configure(const std::vector<TracepointConfig>& configs)
{
    m_configs = configs;
    m_ranges.clear();
    m_recentHits.clear();
    m_previousPc = 0;
    for (const auto& config : m_configs)
    {
        ActiveRange range;
        range.config = &config;
        range.inside = false;
        m_ranges.push_back(range);
    }
}

void DiagTracepointEngine::observePc(Address pc, Address resumeAddress,
                                     u32 callbackCommitGeneration, u32 irqStatus, u32 irqMask,
                                     RuntimeLogger* logger)
{
    for (auto& range : m_ranges)
    {
        const bool inRange = pc >= range.config->pcRangeStart && pc <= range.config->pcRangeEnd;
        const u32 irqPendingMasked = irqStatus & irqMask;

        if (inRange && !range.inside)
        {
            range.inside = true;
            range.entryPc = pc;
            range.entryCallerPc = m_previousPc;
            range.entryResumeAddress = resumeAddress;
            range.entryCallbackCommitGeneration = callbackCommitGeneration;
            range.entryIrqStatus = irqStatus;
            range.entryIrqMask = irqMask;
            range.entryIrqPendingMasked = irqPendingMasked;
            if (range.config->callerHistogram)
            {
                ++range.callerHistogram[range.entryCallerPc];
            }

            const bool sameRepeatedSignature =
                range.entryCallerPc == range.lastRepeatedCallerPc &&
                range.entryResumeAddress == range.lastRepeatedResumeAddress &&
                range.entryCallbackCommitGeneration ==
                    range.lastRepeatedCallbackCommitGeneration &&
                range.entryIrqPendingMasked == range.lastRepeatedIrqPendingMasked;
            if (sameRepeatedSignature)
            {
                ++range.repeatCount;
            }
            else
            {
                range.lastRepeatedCallerPc = range.entryCallerPc;
                range.lastRepeatedResumeAddress = range.entryResumeAddress;
                range.lastRepeatedCallbackCommitGeneration = callbackCommitGeneration;
                range.lastRepeatedIrqPendingMasked = irqPendingMasked;
                range.repeatCount = 1;
                range.repeatLogged = false;
            }

            TracepointHit hit;
            hit.config = range.config;
            hit.pc = pc;
            hit.callerPc = range.entryCallerPc;
            hit.resumeAddress = resumeAddress;
            hit.callbackCommitGeneration = callbackCommitGeneration;
            hit.irqStatus = irqStatus;
            hit.irqMask = irqMask;
            hit.irqPendingMasked = irqPendingMasked;
            hit.isEntry = true;
            appendRecentHit(m_recentHits, hit);

            if (logger != nullptr)
            {
                std::ostringstream msg;
                msg << "tracepoint=" << range.config->name << " event=entry pc=0x" << std::hex
                    << pc;
                if (range.config->captureContext)
                {
                    msg << " caller=0x" << range.entryCallerPc << " resume=0x"
                        << range.entryResumeAddress << " callback_gen=" << std::dec
                        << range.entryCallbackCommitGeneration << std::hex << " irq_status=0x"
                        << range.entryIrqStatus << " irq_mask=0x" << range.entryIrqMask
                        << " irq_pending=0x" << range.entryIrqPendingMasked;
                }
                logger->log(LogLevel::Info, "tracepoint", msg.str());

                if (range.config->repeatThreshold > 0 &&
                    range.repeatCount >= range.config->repeatThreshold && !range.repeatLogged)
                {
                    std::ostringstream repeat;
                    repeat << "tracepoint=" << range.config->name << " event=repeat count="
                           << std::dec << range.repeatCount << std::hex << " caller=0x"
                           << range.entryCallerPc << " resume=0x" << range.entryResumeAddress
                           << " callback_gen=" << std::dec
                           << range.entryCallbackCommitGeneration << std::hex
                           << " irq_pending=0x" << range.entryIrqPendingMasked;
                    logger->log(LogLevel::Info, "tracepoint", repeat.str());
                    range.repeatLogged = true;
                }
            }
        }
        else if (!inRange && range.inside)
        {
            range.inside = false;

            TracepointHit hit;
            hit.config = range.config;
            hit.pc = pc;
            hit.callerPc = range.entryCallerPc;
            hit.returnPc = pc;
            hit.resumeAddress = range.entryResumeAddress;
            hit.callbackCommitGeneration = range.entryCallbackCommitGeneration;
            hit.irqStatus = range.entryIrqStatus;
            hit.irqMask = range.entryIrqMask;
            hit.irqPendingMasked = range.entryIrqPendingMasked;
            hit.isExit = true;
            appendRecentHit(m_recentHits, hit);

            if (logger != nullptr)
            {
                std::ostringstream msg;
                msg << "tracepoint=" << range.config->name << " event=exit pc=0x" << std::hex
                    << pc;
                if (range.config->captureContext)
                {
                    msg << " caller=0x" << range.entryCallerPc << " return=0x" << pc
                        << " resume=0x" << range.entryResumeAddress << " callback_gen="
                        << std::dec << range.entryCallbackCommitGeneration << std::hex
                        << " irq_pending=0x" << range.entryIrqPendingMasked;
                }
                logger->log(LogLevel::Info, "tracepoint", msg.str());
            }
        }
    }
    m_previousPc = pc;
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
                msg << "tracepoint=" << range.config->name << " event=mmio_read pc=0x" << std::hex
                    << pc << " addr=0x" << mmioAddress << " value=0x" << value;
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
           << (hit.isEntry ? "entry" : "exit") << " pc=0x" << std::hex << hit.pc;
        if (hit.callerPc != 0)
        {
            os << " caller=0x" << hit.callerPc;
        }
        if (hit.returnPc != 0)
        {
            os << " return=0x" << hit.returnPc;
        }
        if (hit.resumeAddress != 0 || hit.callbackCommitGeneration != 0 ||
            hit.irqPendingMasked != 0)
        {
            os << " resume=0x" << hit.resumeAddress << " callback_gen=" << std::dec
               << hit.callbackCommitGeneration << std::hex << " irq_pending=0x"
               << hit.irqPendingMasked;
        }
        os << "\n";
    }
    for (const auto& range : m_ranges)
    {
        if (!range.config->callerHistogram || range.callerHistogram.empty())
        {
            continue;
        }
        std::vector<std::pair<Address, u32>> sorted(range.callerHistogram.begin(),
                                                    range.callerHistogram.end());
        std::sort(sorted.begin(), sorted.end(), [](const auto& lhs, const auto& rhs)
                  {
                      if (lhs.second != rhs.second)
                      {
                          return lhs.second > rhs.second;
                      }
                      return lhs.first < rhs.first;
                  });
        os << "Caller histogram [" << range.config->name << "]:";
        const size_t limit = std::min<size_t>(sorted.size(), 4);
        for (size_t index = 0; index < limit; ++index)
        {
            os << (index == 0 ? " " : ", ") << "0x" << std::hex << sorted[index].first << "="
               << std::dec << sorted[index].second;
        }
        os << "\n";
        if (range.config->repeatThreshold > 0 && range.repeatCount >= range.config->repeatThreshold)
        {
            os << "Repeat signature [" << range.config->name << "]: caller=0x" << std::hex
               << range.lastRepeatedCallerPc << " resume=0x" << range.lastRepeatedResumeAddress
               << " callback_gen=" << std::dec << range.lastRepeatedCallbackCommitGeneration
               << std::hex << " irq_pending=0x" << range.lastRepeatedIrqPendingMasked
               << " count=" << std::dec << range.repeatCount << "\n";
        }
    }
    return os.str();
}

size_t DiagTracepointEngine::tracepointCount() const
{
    return m_ranges.size();
}

} // namespace runtime
} // namespace psxrecomp
