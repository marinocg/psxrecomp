#include "psxrecomp/runtime/diag_tracepoints.h"

#include "psxrecomp/runtime/logger.h"
#include "psxrecomp/runtime/memory_map.h"
#include "psxrecomp/runtime/psx_system.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>

namespace psxrecomp
{
namespace runtime
{

DiagTracepointEngine::DiagTracepointEngine() = default;

namespace
{

constexpr size_t kMaxRecentHits = 32;

std::string toLower(std::string text)
{
    for (char& ch : text)
    {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return text;
}

bool tryResolveRegisterIndex(const std::string& name, u32& outIndex)
{
    const std::string lowered = toLower(name);
    if (lowered == "zero")
        outIndex = Registers::ZERO;
    else if (lowered == "at")
        outIndex = Registers::AT;
    else if (lowered == "v0")
        outIndex = Registers::V0;
    else if (lowered == "v1")
        outIndex = Registers::V1;
    else if (lowered == "a0")
        outIndex = Registers::A0;
    else if (lowered == "a1")
        outIndex = Registers::A1;
    else if (lowered == "a2")
        outIndex = Registers::A2;
    else if (lowered == "a3")
        outIndex = Registers::A3;
    else if (lowered == "t0")
        outIndex = Registers::T0;
    else if (lowered == "t1")
        outIndex = Registers::T1;
    else if (lowered == "t2")
        outIndex = Registers::T2;
    else if (lowered == "t3")
        outIndex = Registers::T3;
    else if (lowered == "t4")
        outIndex = Registers::T4;
    else if (lowered == "t5")
        outIndex = Registers::T5;
    else if (lowered == "t6")
        outIndex = Registers::T6;
    else if (lowered == "t7")
        outIndex = Registers::T7;
    else if (lowered == "s0")
        outIndex = Registers::S0;
    else if (lowered == "s1")
        outIndex = Registers::S1;
    else if (lowered == "s2")
        outIndex = Registers::S2;
    else if (lowered == "s3")
        outIndex = Registers::S3;
    else if (lowered == "s4")
        outIndex = Registers::S4;
    else if (lowered == "s5")
        outIndex = Registers::S5;
    else if (lowered == "s6")
        outIndex = Registers::S6;
    else if (lowered == "s7")
        outIndex = Registers::S7;
    else if (lowered == "t8")
        outIndex = Registers::T8;
    else if (lowered == "t9")
        outIndex = Registers::T9;
    else if (lowered == "k0")
        outIndex = Registers::K0;
    else if (lowered == "k1")
        outIndex = Registers::K1;
    else if (lowered == "gp")
        outIndex = Registers::GP;
    else if (lowered == "sp")
        outIndex = Registers::SP;
    else if (lowered == "fp" || lowered == "s8")
        outIndex = Registers::FP;
    else if (lowered == "ra")
        outIndex = Registers::RA;
    else
        return false;
    return true;
}

std::vector<TracepointRegisterValue> captureRegisterValues(const TracepointConfig& config,
                                                           const u32* regs, size_t regCount)
{
    std::vector<TracepointRegisterValue> values;
    if (regs == nullptr || regCount == 0)
    {
        return values;
    }
    values.reserve(config.registers.size());
    for (const std::string& name : config.registers)
    {
        u32 index = 0;
        if (!tryResolveRegisterIndex(name, index) || index >= regCount)
        {
            continue;
        }
        values.push_back({name, regs[index]});
    }
    return values;
}

bool sameRegisterValues(const std::vector<TracepointRegisterValue>& lhs,
                        const std::vector<TracepointRegisterValue>& rhs)
{
    if (lhs.size() != rhs.size())
    {
        return false;
    }
    for (size_t i = 0; i < lhs.size(); ++i)
    {
        if (lhs[i].name != rhs[i].name || lhs[i].value != rhs[i].value)
        {
            return false;
        }
    }
    return true;
}

bool sameMemorySamples(const std::vector<TracepointMemorySampleValue>& lhs,
                       const std::vector<TracepointMemorySampleValue>& rhs)
{
    if (lhs.size() != rhs.size())
    {
        return false;
    }
    for (size_t i = 0; i < lhs.size(); ++i)
    {
        if (lhs[i].name != rhs[i].name || lhs[i].address != rhs[i].address ||
            lhs[i].width != rhs[i].width || lhs[i].valid != rhs[i].valid ||
            lhs[i].values != rhs[i].values)
        {
            return false;
        }
    }
    return true;
}

void appendRegisterValues(std::ostringstream& msg,
                          const std::vector<TracepointRegisterValue>& registerValues)
{
    if (registerValues.empty())
    {
        return;
    }
    msg << " regs=";
    for (size_t i = 0; i < registerValues.size(); ++i)
    {
        if (i != 0)
        {
            msg << ",";
        }
        msg << registerValues[i].name << "=0x" << std::hex << registerValues[i].value;
    }
}

bool tryReadRamSample(const PsxSystem& system, Address address, u32 width, u32& outValue)
{
    const Address physical = address & 0x1FFFFFFFu;
    if (!isMainRamAddress(physical, width))
    {
        return false;
    }
    const Address offset = foldMainRamAddress(physical);
    const u8* ram = system.getRam();
    if (ram == nullptr || offset > (MemoryMap::RAM_SIZE - width))
    {
        return false;
    }
    outValue = 0;
    std::memcpy(&outValue, ram + offset, width);
    return true;
}

std::vector<TracepointMemorySampleValue> captureMemorySamples(const TracepointConfig& config,
                                                              const u32* regs, size_t regCount,
                                                              const PsxSystem* system)
{
    std::vector<TracepointMemorySampleValue> samples;
    if (regs == nullptr || regCount == 0 || system == nullptr)
    {
        return samples;
    }
    samples.reserve(config.memorySamples.size());
    for (const auto& sampleConfig : config.memorySamples)
    {
        TracepointMemorySampleValue sample;
        sample.name = sampleConfig.name;
        sample.width = sampleConfig.width;
        u32 baseIndex = 0;
        if (!tryResolveRegisterIndex(sampleConfig.baseRegister, baseIndex) || baseIndex >= regCount)
        {
            samples.push_back(std::move(sample));
            continue;
        }
        sample.address = regs[baseIndex] + sampleConfig.offset;
        const u32 clampedWidth =
            sampleConfig.width == 1 || sampleConfig.width == 2 || sampleConfig.width == 4
                ? sampleConfig.width
                : 1u;
        const u32 clampedCount = std::max<u32>(1u, sampleConfig.count);
        sample.width = clampedWidth;
        sample.values.reserve(clampedCount);
        sample.valid = true;
        for (u32 index = 0; index < clampedCount; ++index)
        {
            const Address currentAddress = sample.address + (index * clampedWidth);
            u32 value = 0;
            if (!tryReadRamSample(*system, currentAddress, clampedWidth, value))
            {
                sample.valid = false;
                sample.values.clear();
                break;
            }
            sample.values.push_back(value);
        }
        samples.push_back(std::move(sample));
    }
    return samples;
}

void appendMemorySamples(std::ostringstream& msg,
                         const std::vector<TracepointMemorySampleValue>& samples)
{
    if (samples.empty())
    {
        return;
    }
    msg << " samples=";
    for (size_t i = 0; i < samples.size(); ++i)
    {
        if (i != 0)
        {
            msg << ",";
        }
        msg << samples[i].name << "@0x" << std::hex << samples[i].address << "=[";
        if (!samples[i].valid)
        {
            msg << "unavailable";
        }
        else
        {
            for (size_t valueIndex = 0; valueIndex < samples[i].values.size(); ++valueIndex)
            {
                if (valueIndex != 0)
                {
                    msg << " ";
                }
                if (samples[i].width == 1)
                {
                    msg.width(2);
                    msg.fill('0');
                    msg << (samples[i].values[valueIndex] & 0xFFu);
                    msg.fill(' ');
                }
                else if (samples[i].width == 2)
                {
                    msg << "0x" << samples[i].values[valueIndex];
                }
                else
                {
                    msg << "0x" << samples[i].values[valueIndex];
                }
            }
        }
        msg << "]";
    }
}

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

bool DiagTracepointEngine::consumeLogBudget(ActiveRange& range, RuntimeLogger* logger)
{
    if (range.suppressCurrentVisit)
    {
        return false;
    }
    if (range.config->maxLogEvents == 0 || range.emittedLogCount < range.config->maxLogEvents)
    {
        ++range.emittedLogCount;
        return true;
    }
    ++range.suppressedByLimitCount;
    if (!range.limitLogged && logger != nullptr)
    {
        std::ostringstream msg;
        msg << "tracepoint=" << range.config->name
            << " event=log_limit max=" << range.config->maxLogEvents
            << " suppressed=" << range.suppressedByLimitCount;
        logger->log(LogLevel::Info, "tracepoint", msg.str());
        range.limitLogged = true;
    }
    return false;
}

void DiagTracepointEngine::observePc(Address pc, Address resumeAddress,
                                     u32 callbackCommitGeneration, u32 irqStatus, u32 irqMask,
                                     const u32* regs, size_t regCount, const PsxSystem* system,
                                     RuntimeLogger* logger)
{
    for (auto& range : m_ranges)
    {
        if (range.pendingBranchDecision && pc != range.pendingBranchPc)
        {
            const bool tookTaken = pc == range.pendingBranchTakenPc;
            const bool tookNotTaken = pc == range.pendingBranchNotTakenPc;
            if (tookTaken || tookNotTaken)
            {
                TracepointHit hit;
                hit.config = range.config;
                hit.pc = pc;
                hit.branchPc = range.pendingBranchPc;
                hit.nextPc = pc;
                hit.callerPc = range.pendingBranchCallerPc;
                hit.resumeAddress = range.pendingBranchResumeAddress;
                hit.callbackCommitGeneration = range.pendingBranchCallbackCommitGeneration;
                hit.irqStatus = range.pendingBranchIrqStatus;
                hit.irqMask = range.pendingBranchIrqMask;
                hit.irqPendingMasked = range.pendingBranchIrqPendingMasked;
                hit.registerValues = range.pendingBranchRegisterValues;
                hit.memorySamples = range.pendingBranchMemorySamples;
                hit.isBranchDecision = true;
                hit.branchTaken = tookTaken;
                appendRecentHit(m_recentHits, hit);

                if (logger != nullptr && consumeLogBudget(range, logger))
                {
                    std::ostringstream msg;
                    msg << "tracepoint=" << range.config->name << " event=branch branch_pc=0x"
                        << std::hex << range.pendingBranchPc << " next_pc=0x" << pc
                        << " result=" << (tookTaken ? "taken" : "not_taken");
                    if (range.config->captureContext)
                    {
                        msg << " caller=0x" << range.pendingBranchCallerPc << " resume=0x"
                            << range.pendingBranchResumeAddress << " callback_gen=" << std::dec
                            << range.pendingBranchCallbackCommitGeneration << std::hex
                            << " irq_pending=0x" << range.pendingBranchIrqPendingMasked;
                    }
                    appendRegisterValues(msg, range.pendingBranchRegisterValues);
                    appendMemorySamples(msg, range.pendingBranchMemorySamples);
                    logger->log(LogLevel::Info, "tracepoint", msg.str());
                }
            }
            range.pendingBranchDecision = false;
            range.pendingBranchRegisterValues.clear();
            range.pendingBranchMemorySamples.clear();
        }

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
            range.entryRegisterValues = captureRegisterValues(*range.config, regs, regCount);
            range.entryMemorySamples = captureMemorySamples(*range.config, regs, regCount, system);
            if (range.config->callerHistogram)
            {
                ++range.callerHistogram[range.entryCallerPc];
            }

            const bool sameRepeatedSignature =
                range.entryCallerPc == range.lastRepeatedCallerPc &&
                range.entryResumeAddress == range.lastRepeatedResumeAddress &&
                range.entryCallbackCommitGeneration == range.lastRepeatedCallbackCommitGeneration &&
                range.entryIrqPendingMasked == range.lastRepeatedIrqPendingMasked &&
                sameRegisterValues(range.entryRegisterValues, range.lastRepeatedRegisterValues) &&
                sameMemorySamples(range.entryMemorySamples, range.lastRepeatedMemorySamples);
            if (sameRepeatedSignature)
            {
                ++range.repeatCount;
                range.suppressCurrentVisit = range.config->repeatThreshold > 0 &&
                                             range.repeatCount > range.config->repeatThreshold;
                if (range.suppressCurrentVisit)
                {
                    ++range.suppressedVisitCount;
                }
            }
            else
            {
                if (range.suppressedVisitCount > 0 && logger != nullptr)
                {
                    std::ostringstream suppressed;
                    suppressed << "tracepoint=" << range.config->name
                               << " event=suppressed count=" << std::dec
                               << range.suppressedVisitCount << std::hex << " caller=0x"
                               << range.lastRepeatedCallerPc << " resume=0x"
                               << range.lastRepeatedResumeAddress << " callback_gen=" << std::dec
                               << range.lastRepeatedCallbackCommitGeneration << std::hex
                               << " irq_pending=0x" << range.lastRepeatedIrqPendingMasked;
                    appendRegisterValues(suppressed, range.lastRepeatedRegisterValues);
                    logger->log(LogLevel::Info, "tracepoint", suppressed.str());
                }
                range.lastRepeatedCallerPc = range.entryCallerPc;
                range.lastRepeatedResumeAddress = range.entryResumeAddress;
                range.lastRepeatedCallbackCommitGeneration = callbackCommitGeneration;
                range.lastRepeatedIrqPendingMasked = irqPendingMasked;
                range.lastRepeatedRegisterValues = range.entryRegisterValues;
                range.lastRepeatedMemorySamples = range.entryMemorySamples;
                range.repeatCount = 1;
                range.repeatLogged = false;
                range.suppressCurrentVisit = false;
                range.suppressedVisitCount = 0;
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
            hit.registerValues = range.entryRegisterValues;
            hit.memorySamples = range.entryMemorySamples;
            hit.isEntry = true;
            appendRecentHit(m_recentHits, hit);

            if (range.config->logBranches &&
                (range.config->branchTakenPc != 0 || range.config->branchNotTakenPc != 0))
            {
                range.pendingBranchDecision = true;
                range.pendingBranchPc = pc;
                range.pendingBranchTakenPc = range.config->branchTakenPc;
                range.pendingBranchNotTakenPc = range.config->branchNotTakenPc;
                range.pendingBranchCallerPc = range.entryCallerPc;
                range.pendingBranchResumeAddress = resumeAddress;
                range.pendingBranchCallbackCommitGeneration = callbackCommitGeneration;
                range.pendingBranchIrqStatus = irqStatus;
                range.pendingBranchIrqMask = irqMask;
                range.pendingBranchIrqPendingMasked = irqPendingMasked;
                range.pendingBranchRegisterValues = range.entryRegisterValues;
                range.pendingBranchMemorySamples = range.entryMemorySamples;
            }

            if (logger != nullptr && consumeLogBudget(range, logger))
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
                appendRegisterValues(msg, range.entryRegisterValues);
                appendMemorySamples(msg, range.entryMemorySamples);
                logger->log(LogLevel::Info, "tracepoint", msg.str());

                if (range.config->repeatThreshold > 0 &&
                    range.repeatCount >= range.config->repeatThreshold && !range.repeatLogged)
                {
                    std::ostringstream repeat;
                    repeat << "tracepoint=" << range.config->name
                           << " event=repeat count=" << std::dec << range.repeatCount << std::hex
                           << " caller=0x" << range.entryCallerPc << " resume=0x"
                           << range.entryResumeAddress << " callback_gen=" << std::dec
                           << range.entryCallbackCommitGeneration << std::hex << " irq_pending=0x"
                           << range.entryIrqPendingMasked;
                    appendRegisterValues(repeat, range.entryRegisterValues);
                    appendMemorySamples(repeat, range.entryMemorySamples);
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
            hit.memorySamples = range.entryMemorySamples;
            hit.isExit = true;
            appendRecentHit(m_recentHits, hit);

            if (logger != nullptr && consumeLogBudget(range, logger))
            {
                std::ostringstream msg;
                msg << "tracepoint=" << range.config->name << " event=exit pc=0x" << std::hex << pc;
                if (range.config->captureContext)
                {
                    msg << " caller=0x" << range.entryCallerPc << " return=0x" << pc << " resume=0x"
                        << range.entryResumeAddress << " callback_gen=" << std::dec
                        << range.entryCallbackCommitGeneration << std::hex << " irq_pending=0x"
                        << range.entryIrqPendingMasked;
                }
                appendMemorySamples(msg, range.entryMemorySamples);
                logger->log(LogLevel::Info, "tracepoint", msg.str());
            }
            range.suppressCurrentVisit = false;
        }
    }
    m_previousPc = pc;
}

void DiagTracepointEngine::recordMmioRead(Address mmioAddress, u32 value, Address pc,
                                          RuntimeLogger* logger)
{
    for (auto& range : m_ranges)
    {
        if (!range.inside)
        {
            continue;
        }
        for (Address watchAddr : range.config->mmioReads)
        {
            if (watchAddr == mmioAddress && logger != nullptr && consumeLogBudget(range, logger))
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
        os << "  [" << (hit.config != nullptr ? hit.config->name : "?") << "] ";
        if (hit.isBranchDecision)
        {
            os << "branch pc=0x" << std::hex << hit.branchPc << " next=0x" << hit.nextPc
               << " result=" << (hit.branchTaken ? "taken" : "not_taken");
        }
        else
        {
            os << (hit.isEntry ? "entry" : "exit") << " pc=0x" << std::hex << hit.pc;
        }
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
        appendRegisterValues(os, hit.registerValues);
        appendMemorySamples(os, hit.memorySamples);
        os << "\n";
    }
    for (const auto& range : m_ranges)
    {
        if (range.config->callerHistogram && !range.callerHistogram.empty())
        {
            std::vector<std::pair<Address, u32>> sorted(range.callerHistogram.begin(),
                                                        range.callerHistogram.end());
            std::sort(sorted.begin(), sorted.end(),
                      [](const auto& lhs, const auto& rhs)
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
        }
        if (range.config->repeatThreshold > 0 && range.repeatCount >= range.config->repeatThreshold)
        {
            os << "Repeat signature [" << range.config->name << "]: caller=0x" << std::hex
               << range.lastRepeatedCallerPc << " resume=0x" << range.lastRepeatedResumeAddress
               << " callback_gen=" << std::dec << range.lastRepeatedCallbackCommitGeneration
               << std::hex << " irq_pending=0x" << range.lastRepeatedIrqPendingMasked
               << " count=" << std::dec << range.repeatCount;
            appendRegisterValues(os, range.lastRepeatedRegisterValues);
            appendMemorySamples(os, range.lastRepeatedMemorySamples);
            os << "\n";
        }
        if (range.suppressedVisitCount > 0)
        {
            os << "Suppressed repeats [" << range.config->name << "]: count=" << std::dec
               << range.suppressedVisitCount << std::hex << " caller=0x"
               << range.lastRepeatedCallerPc << " resume=0x" << range.lastRepeatedResumeAddress
               << " callback_gen=" << std::dec << range.lastRepeatedCallbackCommitGeneration
               << std::hex << " irq_pending=0x" << range.lastRepeatedIrqPendingMasked;
            appendRegisterValues(os, range.lastRepeatedRegisterValues);
            appendMemorySamples(os, range.lastRepeatedMemorySamples);
            os << "\n";
        }
        if (range.suppressedByLimitCount > 0)
        {
            os << "Log limit [" << range.config->name << "]: max=" << std::dec
               << range.config->maxLogEvents << " suppressed=" << range.suppressedByLimitCount
               << "\n";
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
