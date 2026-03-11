#include "psxrecomp/runtime/callback_trace.h"

#include <algorithm>
#include <sstream>

namespace psxrecomp
{
namespace runtime
{

namespace
{

constexpr size_t MAX_RECENT_CALLBACK_ENTRIES = 24;
constexpr size_t MAX_SIGNATURE_SUMMARY = 4;
constexpr size_t MAX_WRITE_SUMMARY = 3;
constexpr size_t MAX_REGISTER_SUMMARY = 8;
constexpr size_t MAX_RETURN_SITE_SUMMARY = 4;

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

std::vector<CallbackTraceEntry::WriteHotspot> summarizeWrites(
    const std::unordered_map<Address, CallbackTraceEngine::ActiveWriteInfo>& writes)
{
    std::vector<std::pair<Address, CallbackTraceEngine::ActiveWriteInfo>> sorted(writes.begin(),
                                                                                 writes.end());
    std::sort(sorted.begin(), sorted.end(), [](const auto& lhs, const auto& rhs)
              {
                  if (lhs.second.count != rhs.second.count)
                  {
                      return lhs.second.count > rhs.second.count;
                  }
                  return lhs.first < rhs.first;
              });

    std::vector<CallbackTraceEntry::WriteHotspot> result;
    const size_t count = std::min<size_t>(sorted.size(), MAX_WRITE_SUMMARY);
    result.reserve(count);
    for (size_t index = 0; index < count; ++index)
    {
        CallbackTraceEntry::WriteHotspot hotspot;
        hotspot.address = sorted[index].first;
        hotspot.count = sorted[index].second.count;
        hotspot.lastOldValue = sorted[index].second.lastOldValue;
        hotspot.lastNewValue = sorted[index].second.lastNewValue;
        result.push_back(hotspot);
    }
    return result;
}

std::string formatWriteSummary(const std::vector<CallbackTraceEntry::WriteHotspot>& writes)
{
    if (writes.empty())
    {
        return "none";
    }
    std::ostringstream os;
    for (size_t index = 0; index < writes.size(); ++index)
    {
        const auto& write = writes[index];
        if (index != 0)
        {
            os << ",";
        }
        os << "0x" << std::hex << write.address << "x" << std::dec << write.count << "(0x"
           << std::hex << write.lastOldValue << "->0x" << write.lastNewValue << ")";
    }
    return os.str();
}

std::vector<std::pair<Address, u32>> summarizeReturnSites(
    const std::unordered_map<Address, u32>& counts)
{
    std::vector<std::pair<Address, u32>> sorted(counts.begin(), counts.end());
    std::sort(sorted.begin(), sorted.end(), [](const auto& lhs, const auto& rhs)
              {
                  if (lhs.second != rhs.second)
                  {
                      return lhs.second > rhs.second;
                  }
                  return lhs.first < rhs.first;
              });
    if (sorted.size() > MAX_RETURN_SITE_SUMMARY)
    {
        sorted.resize(MAX_RETURN_SITE_SUMMARY);
    }
    return sorted;
}

std::string formatReturnSiteSummary(const std::vector<std::pair<Address, u32>>& sites)
{
    if (sites.empty())
    {
        return "none";
    }
    std::ostringstream os;
    for (size_t index = 0; index < sites.size(); ++index)
    {
        if (index != 0)
        {
            os << ",";
        }
        os << "0x" << std::hex << sites[index].first << "x" << std::dec << sites[index].second;
    }
    return os.str();
}

std::vector<std::string> summarizeRegisters(const std::unordered_map<std::string, u32>& counts)
{
    std::vector<std::pair<std::string, u32>> sorted(counts.begin(), counts.end());
    std::sort(sorted.begin(), sorted.end(), [](const auto& lhs, const auto& rhs)
              {
                  if (lhs.second != rhs.second)
                  {
                      return lhs.second > rhs.second;
                  }
                  return lhs.first < rhs.first;
              });

    std::vector<std::string> result;
    const size_t count = std::min<size_t>(sorted.size(), MAX_REGISTER_SUMMARY);
    result.reserve(count);
    for (size_t index = 0; index < count; ++index)
    {
        result.push_back(sorted[index].first + "x" + std::to_string(sorted[index].second));
    }
    return result;
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
                                          bool cop0InterruptEligibleBefore)
{
    ActiveInvocation invocation;
    invocation.entryPc = entryPc;
    invocation.descriptorAddress = descriptorAddress;
    invocation.returnSite = returnSite;
    invocation.callbackGenerationBefore = callbackGenerationBefore;
    invocation.irqStatusBefore = irqStatusBefore;
    invocation.irqMaskBefore = irqMaskBefore;
    invocation.cop0InterruptEligibleBefore = cop0InterruptEligibleBefore;
    m_activeInvocations.push_back(invocation);
}

bool CallbackTraceEngine::hasActiveInvocation() const
{
    return !m_activeInvocations.empty();
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
                                           u32 irqMaskAfter, bool cop0InterruptEligibleAfter,
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
    entry.cop0InterruptEligibleBefore = invocation.cop0InterruptEligibleBefore;
    entry.cop0InterruptEligibleAfter = cop0InterruptEligibleAfter;
    entry.totalRamWrites = invocation.totalRamWrites;
    entry.topWriteAddresses = summarizeWrites(invocation.ramWrites);
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

    CallbackPathAggregate& aggregate = m_pathAggregates[{entry.entryPc, entry.exitPc,
                                                         entry.descriptorAddress}];
    ++aggregate.count;
    aggregate.totalRamWrites += entry.totalRamWrites;
    ++aggregate.returnSiteCounts[entry.returnSite];
    for (const auto& write : invocation.ramWrites)
    {
        ActiveWriteInfo& info = aggregate.ramWrites[write.first];
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
        << " rfe=" << std::dec << (entry.threwReturnFromException ? 1 : 0) << " gen_before="
        << entry.callbackGenerationBefore << " gen_after=" << entry.callbackGenerationAfter
        << std::hex << " irq_before=0x" << entry.irqStatusBefore << "/0x" << entry.irqMaskBefore
        << " irq_after=0x" << entry.irqStatusAfter << "/0x" << entry.irqMaskAfter
        << " writes=" << std::dec << entry.totalRamWrites << " top_writes="
        << formatWriteSummary(entry.topWriteAddresses) << " regs="
        << joinRegisterNames(entry.committedRegisters) << " cop0_before="
        << (entry.cop0InterruptEligibleBefore ? 1 : 0) << " cop0_after="
        << (entry.cop0InterruptEligibleAfter ? 1 : 0) << " repeat="
        << entry.consecutiveRepeatCount;
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
           << " descriptor=0x" << entry.descriptorAddress << " return_site=0x"
           << entry.returnSite << " rfe=" << std::dec << (entry.threwReturnFromException ? 1 : 0)
           << " gen=" << entry.callbackGenerationBefore << "->" << entry.callbackGenerationAfter
           << std::hex << " irq=0x" << entry.irqStatusBefore << "/0x" << entry.irqMaskBefore
           << " -> 0x" << entry.irqStatusAfter << "/0x" << entry.irqMaskAfter << std::dec
           << " cop0=" << (entry.cop0InterruptEligibleBefore ? 1 : 0) << "->"
           << (entry.cop0InterruptEligibleAfter ? 1 : 0) << " writes=" << entry.totalRamWrites
           << " top_writes=" << formatWriteSummary(entry.topWriteAddresses) << " regs="
           << joinRegisterNames(entry.committedRegisters) << " repeat="
           << entry.consecutiveRepeatCount << "\n";
    }

    std::vector<std::pair<CallbackRepeatSignature, u32>> sortedSignatures(m_signatureCounts.begin(),
                                                                          m_signatureCounts.end());
    std::sort(sortedSignatures.begin(), sortedSignatures.end(), [](const auto& lhs, const auto& rhs)
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

    std::vector<std::pair<CallbackPathKey, CallbackPathAggregate>> sortedPaths(m_pathAggregates.begin(),
                                                                               m_pathAggregates.end());
    std::sort(sortedPaths.begin(), sortedPaths.end(), [](const auto& lhs, const auto& rhs)
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
           << " total_writes=" << item.second.totalRamWrites << " top_writes="
           << formatWriteSummary(summarizeWrites(item.second.ramWrites)) << " regs="
           << joinRegisterNames(summarizeRegisters(item.second.committedRegisterCounts))
           << " return_sites="
           << formatReturnSiteSummary(summarizeReturnSites(item.second.returnSiteCounts)) << "\n";
    }

    if (m_hasLastSignature)
    {
        os << "Last callback repeat path: entry=0x" << std::hex << m_lastSignature.entryPc
           << " exit=0x" << m_lastSignature.exitPc << " descriptor=0x"
           << m_lastSignature.descriptorAddress << " return_site=0x"
           << m_lastSignature.returnSite << std::dec << " repeat=" << m_lastSignatureRepeatCount
           << "\n";
    }
    return os.str();
}

} // namespace runtime
} // namespace psxrecomp
