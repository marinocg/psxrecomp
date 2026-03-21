#include "psxrecomp/runtime/callback_trace.h"

#include "callback_trace_internal.h"
#include "psxrecomp/runtime/interrupt_controller.h"

#include <algorithm>
#include <sstream>

namespace psxrecomp
{
namespace runtime
{

namespace
{

constexpr size_t MAX_RECENT_CALLBACK_ENTRIES = 24;
constexpr size_t MAX_REGISTER_SUMMARY = 8;
constexpr size_t MAX_SIGNATURE_SUMMARY = callback_trace_internal::MAX_SIGNATURE_SUMMARY;
constexpr u8 CDROM_HINT_LOW_MASK = 0x1Fu;
constexpr u8 CDROM_RAW_IRQ_MASK = 0x07u;

void appendRecentEntry(std::vector<CallbackTraceEntry>& entries, const CallbackTraceEntry& entry)
{
    if (entries.size() >= MAX_RECENT_CALLBACK_ENTRIES)
    {
        entries.erase(entries.begin());
    }
    entries.push_back(entry);
}

size_t combineHash(size_t hash, size_t value)
{
    return hash ^ (value + 0x9e3779b9u + (hash << 6) + (hash >> 2));
}

const char* registerName(size_t index)
{
    static constexpr const char* kNames[32] = {
        "r0", "at", "v0", "v1", "a0", "a1", "a2", "a3", "t0", "t1", "t2",
        "t3", "t4", "t5", "t6", "t7", "s0", "s1", "s2", "s3", "s4", "s5",
        "s6", "s7", "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra",
    };
    return index < 32 ? kNames[index] : "?";
}

std::string joinRegisterNames(const std::vector<std::string>& names)
{
    if (names.empty())
    {
        return "none";
    }
    std::ostringstream os;
    const size_t count = std::min<size_t>(names.size(), MAX_REGISTER_SUMMARY);
    for (size_t index = 0; index < count; ++index)
    {
        if (index != 0)
        {
            os << ",";
        }
        os << names[index];
    }
    if (names.size() > count)
    {
        os << ",...";
    }
    return os.str();
}

bool topLevelCdLineActive(u32 irqStatus)
{
    return (irqStatus & static_cast<u32>(InterruptLine::Cdrom)) != 0u;
}

const char* rawCdIrqName(u8 hintStatus)
{
    switch (hintStatus & CDROM_RAW_IRQ_MASK)
    {
    case 1u:
        return "INT1";
    case 2u:
        return "INT2";
    case 3u:
        return "INT3";
    case 4u:
        return "INT4";
    case 5u:
        return "INT5";
    default:
        return "none";
    }
}

} // namespace

bool CallbackTraceEngine::CallbackRepeatSignature::operator==(
    const CallbackRepeatSignature& rhs) const
{
    return entryPc == rhs.entryPc && exitPc == rhs.exitPc &&
           descriptorAddress == rhs.descriptorAddress && returnSite == rhs.returnSite;
}

size_t CallbackTraceEngine::CallbackRepeatSignatureHash::operator()(
    const CallbackRepeatSignature& signature) const
{
    size_t hash = static_cast<size_t>(signature.entryPc);
    hash = combineHash(hash, static_cast<size_t>(signature.exitPc));
    hash = combineHash(hash, static_cast<size_t>(signature.descriptorAddress));
    hash = combineHash(hash, static_cast<size_t>(signature.returnSite));
    return hash;
}

bool CallbackTraceEngine::CallbackPathKey::operator==(const CallbackPathKey& rhs) const
{
    return entryPc == rhs.entryPc && exitPc == rhs.exitPc &&
           descriptorAddress == rhs.descriptorAddress;
}

size_t CallbackTraceEngine::CallbackPathKeyHash::operator()(const CallbackPathKey& key) const
{
    size_t hash = static_cast<size_t>(key.entryPc);
    hash = combineHash(hash, static_cast<size_t>(key.exitPc));
    hash = combineHash(hash, static_cast<size_t>(key.descriptorAddress));
    return hash;
}

void CallbackTraceEngine::reset()
{
    m_activeInvocations.clear();
    m_recentEntries.clear();
    m_signatureCounts.clear();
    m_pathAggregates.clear();
    m_lastSignature = {};
    m_lastSignatureRepeatCount = 0;
    m_hasLastSignature = false;
}

void CallbackTraceEngine::beginInvocation(Address entryPc, Address descriptorAddress,
                                          Address returnSite, u32 callbackGenerationBefore,
                                          u32 irqStatusBefore, u32 irqMaskBefore,
                                          u8 cdHintStatusBefore, u8 cdHintMaskBefore,
                                          bool cop0InterruptEligibleBefore)
{
    ActiveInvocation invocation;
    invocation.entryPc = entryPc;
    invocation.descriptorAddress = descriptorAddress;
    invocation.returnSite = returnSite;
    invocation.callbackGenerationBefore = callbackGenerationBefore;
    invocation.irqStatusBefore = irqStatusBefore;
    invocation.irqMaskBefore = irqMaskBefore;
    invocation.cdHintStatusBefore = static_cast<u8>(cdHintStatusBefore & CDROM_HINT_LOW_MASK);
    invocation.cdHintMaskBefore = static_cast<u8>(cdHintMaskBefore & CDROM_HINT_LOW_MASK);
    invocation.cop0InterruptEligibleBefore = cop0InterruptEligibleBefore;
    m_activeInvocations.push_back(invocation);
}

bool CallbackTraceEngine::hasActiveInvocation() const
{
    return !m_activeInvocations.empty();
}

void CallbackTraceEngine::setActiveInvocationStackPointer(Address stackPointer)
{
    if (m_activeInvocations.empty())
    {
        return;
    }
    m_activeInvocations.back().entryStackPointer = stackPointer;
}

void CallbackTraceEngine::recordRamWrite(Address address, u8 size, u32 oldValue, u32 newValue)
{
    if (m_activeInvocations.empty())
    {
        return;
    }
    ActiveInvocation& invocation = m_activeInvocations.back();
    ActiveWriteInfo& info = invocation.ramWrites[address];
    ++info.count;
    info.lastOldValue = oldValue;
    info.lastNewValue = newValue;
    info.size = size;
    ++invocation.totalRamWrites;

    std::unordered_map<Address, ActiveWriteInfo>* destination =
        &invocation.persistentRamWritesByAddress;
    u32* destinationCount = &invocation.persistentRamWrites;
    if (callback_trace_internal::isLikelyCallbackStackWrite(address, invocation.entryStackPointer))
    {
        destination = &invocation.stackRamWritesByAddress;
        destinationCount = &invocation.stackRamWrites;
    }

    ActiveWriteInfo& classifiedInfo = (*destination)[address];
    ++classifiedInfo.count;
    classifiedInfo.lastOldValue = oldValue;
    classifiedInfo.lastNewValue = newValue;
    classifiedInfo.size = size;
    ++(*destinationCount);
}

void CallbackTraceEngine::recordCommittedRegisterDelta(const std::array<u32, 32>& before,
                                                       const std::array<u32, 32>& after,
                                                       u32 hiBefore, u32 hiAfter, u32 loBefore,
                                                       u32 loAfter, bool committed)
{
    if (!committed || m_activeInvocations.empty())
    {
        return;
    }

    ActiveInvocation& invocation = m_activeInvocations.back();
    invocation.committedRegisters.clear();
    for (size_t reg = 0; reg < before.size(); ++reg)
    {
        if (before[reg] != after[reg])
        {
            invocation.committedRegisters.push_back(registerName(reg));
        }
    }
    if (hiBefore != hiAfter)
    {
        invocation.committedRegisters.push_back("hi");
    }
    if (loBefore != loAfter)
    {
        invocation.committedRegisters.push_back("lo");
    }
}

void CallbackTraceEngine::finishInvocation(Address exitPc, bool threwReturnFromException,
                                           u32 callbackGenerationAfter, u32 irqStatusAfter,
                                           u32 irqMaskAfter, u8 cdHintStatusAfter,
                                           u8 cdHintMaskAfter, bool cop0InterruptEligibleAfter,
                                           RuntimeLogger* logger, bool emitLogs)
{
    if (m_activeInvocations.empty())
    {
        return;
    }

    const ActiveInvocation invocation = m_activeInvocations.back();
    m_activeInvocations.pop_back();

    CallbackTraceEntry entry;
    entry.entryPc = invocation.entryPc;
    entry.exitPc = exitPc;
    entry.descriptorAddress = invocation.descriptorAddress;
    entry.returnSite = invocation.returnSite;
    entry.threwReturnFromException = threwReturnFromException;
    entry.callbackGenerationBefore = invocation.callbackGenerationBefore;
    entry.callbackGenerationAfter = callbackGenerationAfter;
    entry.irqStatusBefore = invocation.irqStatusBefore;
    entry.irqMaskBefore = invocation.irqMaskBefore;
    entry.irqStatusAfter = irqStatusAfter;
    entry.irqMaskAfter = irqMaskAfter;
    entry.cdHintStatusBefore = invocation.cdHintStatusBefore;
    entry.cdHintMaskBefore = invocation.cdHintMaskBefore;
    entry.cdHintStatusAfter = static_cast<u8>(cdHintStatusAfter & CDROM_HINT_LOW_MASK);
    entry.cdHintMaskAfter = static_cast<u8>(cdHintMaskAfter & CDROM_HINT_LOW_MASK);
    entry.cop0InterruptEligibleBefore = invocation.cop0InterruptEligibleBefore;
    entry.cop0InterruptEligibleAfter = cop0InterruptEligibleAfter;
    entry.totalRamWrites = invocation.totalRamWrites;
    entry.persistentRamWrites = invocation.persistentRamWrites;
    entry.stackRamWrites = invocation.stackRamWrites;
    entry.topWriteAddresses = callback_trace_internal::summarizeWrites(invocation.ramWrites);
    entry.topPersistentWriteAddresses =
        callback_trace_internal::summarizeWrites(invocation.persistentRamWritesByAddress);
    entry.topStackWriteAddresses =
        callback_trace_internal::summarizeWrites(invocation.stackRamWritesByAddress);
    entry.committedRegisters = invocation.committedRegisters;

    const CallbackRepeatSignature signature = {entry.entryPc, entry.exitPc, entry.descriptorAddress,
                                               entry.returnSite};
    ++m_signatureCounts[signature];
    if (m_hasLastSignature && signature == m_lastSignature)
    {
        ++m_lastSignatureRepeatCount;
    }
    else
    {
        m_lastSignature = signature;
        m_lastSignatureRepeatCount = 1;
        m_hasLastSignature = true;
    }
    entry.consecutiveRepeatCount = m_lastSignatureRepeatCount;
    appendRecentEntry(m_recentEntries, entry);

    CallbackPathAggregate& aggregate =
        m_pathAggregates[{entry.entryPc, entry.exitPc, entry.descriptorAddress}];
    ++aggregate.count;
    aggregate.totalRamWrites += entry.totalRamWrites;
    aggregate.persistentRamWrites += entry.persistentRamWrites;
    aggregate.stackRamWrites += entry.stackRamWrites;
    ++aggregate.returnSiteCounts[entry.returnSite];
    for (const auto& write : invocation.ramWrites)
    {
        ActiveWriteInfo& info = aggregate.ramWrites[write.first];
        info.count += write.second.count;
        info.lastOldValue = write.second.lastOldValue;
        info.lastNewValue = write.second.lastNewValue;
        info.size = write.second.size;
    }
    for (const auto& write : invocation.persistentRamWritesByAddress)
    {
        ActiveWriteInfo& info = aggregate.persistentRamWritesByAddress[write.first];
        info.count += write.second.count;
        info.lastOldValue = write.second.lastOldValue;
        info.lastNewValue = write.second.lastNewValue;
        info.size = write.second.size;
    }
    for (const auto& write : invocation.stackRamWritesByAddress)
    {
        ActiveWriteInfo& info = aggregate.stackRamWritesByAddress[write.first];
        info.count += write.second.count;
        info.lastOldValue = write.second.lastOldValue;
        info.lastNewValue = write.second.lastNewValue;
        info.size = write.second.size;
    }
    for (const std::string& regName : entry.committedRegisters)
    {
        ++aggregate.committedRegisterCounts[regName];
    }

    if (!emitLogs || logger == nullptr)
    {
        return;
    }

    std::ostringstream msg;
    msg << "event=callback_path entry=0x" << std::hex << entry.entryPc << " exit=0x" << entry.exitPc
        << " descriptor=0x" << entry.descriptorAddress << " return_site=0x" << entry.returnSite
        << " rfe=" << std::dec << (entry.threwReturnFromException ? 1 : 0)
        << " gen_before=" << entry.callbackGenerationBefore
        << " gen_after=" << entry.callbackGenerationAfter << std::hex << " top_level_irq=0x"
        << entry.irqStatusBefore << "/0x" << entry.irqMaskBefore << " -> 0x" << entry.irqStatusAfter
        << "/0x" << entry.irqMaskAfter
        << " top_level_cd_line=" << (topLevelCdLineActive(entry.irqStatusBefore) ? "on" : "off")
        << "->" << (topLevelCdLineActive(entry.irqStatusAfter) ? "on" : "off")
        << " raw_cd_irq=" << rawCdIrqName(entry.cdHintStatusBefore) << "->"
        << rawCdIrqName(entry.cdHintStatusAfter) << " cd_hint=sts=0x"
        << static_cast<unsigned>(entry.cdHintStatusBefore) << "/msk=0x"
        << static_cast<unsigned>(entry.cdHintMaskBefore) << " -> sts=0x"
        << static_cast<unsigned>(entry.cdHintStatusAfter) << "/msk=0x"
        << static_cast<unsigned>(entry.cdHintMaskAfter) << " writes=" << std::dec
        << entry.totalRamWrites << " persistent_writes=" << entry.persistentRamWrites
        << " stack_writes=" << entry.stackRamWrites << " top_persistent_writes="
        << callback_trace_internal::formatWriteSummary(entry.topPersistentWriteAddresses)
        << " top_stack_writes="
        << callback_trace_internal::formatWriteSummary(entry.topStackWriteAddresses)
        << " regs=" << joinRegisterNames(entry.committedRegisters)
        << " cop0_before=" << (entry.cop0InterruptEligibleBefore ? 1 : 0)
        << " cop0_after=" << (entry.cop0InterruptEligibleAfter ? 1 : 0)
        << " repeat=" << entry.consecutiveRepeatCount;
    logger->log(LogLevel::Info, "callback_trace", msg.str());
}

std::string CallbackTraceEngine::formatRecentCallbacks() const
{
    std::ostringstream os;
    os << "Recent callback paths (newest first):\n";
    if (m_recentEntries.empty())
    {
        os << "  none\n";
        return os.str();
    }

    const size_t recentCount = std::min<size_t>(m_recentEntries.size(), 12);
    for (size_t index = 0; index < recentCount; ++index)
    {
        const CallbackTraceEntry& entry = m_recentEntries[m_recentEntries.size() - 1 - index];
        os << "  entry=0x" << std::hex << entry.entryPc << " exit=0x" << entry.exitPc
           << " descriptor=0x" << entry.descriptorAddress << " return_site=0x" << entry.returnSite
           << " rfe=" << std::dec << (entry.threwReturnFromException ? 1 : 0)
           << " gen=" << entry.callbackGenerationBefore << "->" << entry.callbackGenerationAfter
           << std::hex << " top_level_irq=0x" << entry.irqStatusBefore << "/0x"
           << entry.irqMaskBefore << " -> 0x" << entry.irqStatusAfter << "/0x" << entry.irqMaskAfter
           << " top_level_cd_line=" << (topLevelCdLineActive(entry.irqStatusBefore) ? "on" : "off")
           << "->" << (topLevelCdLineActive(entry.irqStatusAfter) ? "on" : "off")
           << " raw_cd_irq=" << rawCdIrqName(entry.cdHintStatusBefore) << "->"
           << rawCdIrqName(entry.cdHintStatusAfter) << " cd_hint=sts=0x"
           << static_cast<unsigned>(entry.cdHintStatusBefore) << "/msk=0x"
           << static_cast<unsigned>(entry.cdHintMaskBefore) << " -> sts=0x"
           << static_cast<unsigned>(entry.cdHintStatusAfter) << "/msk=0x"
           << static_cast<unsigned>(entry.cdHintMaskAfter) << std::dec
           << " cop0=" << (entry.cop0InterruptEligibleBefore ? 1 : 0) << "->"
           << (entry.cop0InterruptEligibleAfter ? 1 : 0) << " writes=" << entry.totalRamWrites
           << " persistent_writes=" << entry.persistentRamWrites
           << " stack_writes=" << entry.stackRamWrites << " top_persistent_writes="
           << callback_trace_internal::formatWriteSummary(entry.topPersistentWriteAddresses)
           << " top_stack_writes="
           << callback_trace_internal::formatWriteSummary(entry.topStackWriteAddresses)
           << " regs=" << joinRegisterNames(entry.committedRegisters)
           << " repeat=" << entry.consecutiveRepeatCount << "\n";
    }

    std::vector<std::pair<CallbackRepeatSignature, u32>> sortedSignatures(m_signatureCounts.begin(),
                                                                          m_signatureCounts.end());
    std::sort(sortedSignatures.begin(), sortedSignatures.end(),
              [](const auto& lhs, const auto& rhs)
              {
                  if (lhs.second != rhs.second)
                  {
                      return lhs.second > rhs.second;
                  }
                  if (lhs.first.entryPc != rhs.first.entryPc)
                  {
                      return lhs.first.entryPc < rhs.first.entryPc;
                  }
                  if (lhs.first.exitPc != rhs.first.exitPc)
                  {
                      return lhs.first.exitPc < rhs.first.exitPc;
                  }
                  if (lhs.first.descriptorAddress != rhs.first.descriptorAddress)
                  {
                      return lhs.first.descriptorAddress < rhs.first.descriptorAddress;
                  }
                  return lhs.first.returnSite < rhs.first.returnSite;
              });

    os << "Callback repeat signatures:";
    const size_t signatureCount = std::min<size_t>(sortedSignatures.size(), MAX_SIGNATURE_SUMMARY);
    for (size_t index = 0; index < signatureCount; ++index)
    {
        const auto& item = sortedSignatures[index];
        os << (index == 0 ? " " : ", ") << "entry=0x" << std::hex << item.first.entryPc
           << " exit=0x" << item.first.exitPc << " descriptor=0x" << item.first.descriptorAddress
           << " return_site=0x" << item.first.returnSite << std::dec << " count=" << item.second;
    }
    os << "\n";

    std::vector<std::pair<CallbackPathKey, CallbackPathAggregate>> sortedPaths(
        m_pathAggregates.begin(), m_pathAggregates.end());
    std::sort(sortedPaths.begin(), sortedPaths.end(),
              [](const auto& lhs, const auto& rhs)
              {
                  if (lhs.second.count != rhs.second.count)
                  {
                      return lhs.second.count > rhs.second.count;
                  }
                  if (lhs.first.entryPc != rhs.first.entryPc)
                  {
                      return lhs.first.entryPc < rhs.first.entryPc;
                  }
                  if (lhs.first.exitPc != rhs.first.exitPc)
                  {
                      return lhs.first.exitPc < rhs.first.exitPc;
                  }
                  return lhs.first.descriptorAddress < rhs.first.descriptorAddress;
              });

    const size_t pathCount = std::min<size_t>(sortedPaths.size(), MAX_SIGNATURE_SUMMARY);
    for (size_t index = 0; index < pathCount; ++index)
    {
        const auto& item = sortedPaths[index];
        os << "Callback state delta [" << (index + 1) << "]: entry=0x" << std::hex
           << item.first.entryPc << " exit=0x" << item.first.exitPc << " descriptor=0x"
           << item.first.descriptorAddress << std::dec << " count=" << item.second.count
           << " total_writes=" << item.second.totalRamWrites
           << " persistent_writes=" << item.second.persistentRamWrites
           << " stack_writes=" << item.second.stackRamWrites << " top_persistent_writes="
           << callback_trace_internal::formatWriteSummary(callback_trace_internal::summarizeWrites(
                  item.second.persistentRamWritesByAddress))
           << " top_stack_writes="
           << callback_trace_internal::formatWriteSummary(
                  callback_trace_internal::summarizeWrites(item.second.stackRamWritesByAddress))
           << " regs="
           << joinRegisterNames(
                  callback_trace_internal::summarizeRegisters(item.second.committedRegisterCounts))
           << " return_sites="
           << callback_trace_internal::formatReturnSiteSummary(
                  callback_trace_internal::summarizeReturnSites(item.second.returnSiteCounts))
           << "\n";
    }

    if (m_hasLastSignature)
    {
        os << "Last callback repeat path: entry=0x" << std::hex << m_lastSignature.entryPc
           << " exit=0x" << m_lastSignature.exitPc << " descriptor=0x"
           << m_lastSignature.descriptorAddress << " return_site=0x" << m_lastSignature.returnSite
           << std::dec << " repeat=" << m_lastSignatureRepeatCount << "\n";
    }
    return os.str();
}

} // namespace runtime
} // namespace psxrecomp
