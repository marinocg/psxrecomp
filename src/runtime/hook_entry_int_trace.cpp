#include "psxrecomp/runtime/hook_entry_int_trace.h"

#include <algorithm>
#include <cstring>
#include <sstream>

namespace psxrecomp
{
namespace runtime
{

namespace
{

constexpr size_t MAX_RECENT_INVOCATIONS = 12;

void appendRecent(std::vector<HookEntryIntTraceEntry>& entries, const HookEntryIntTraceEntry& entry)
{
    if (entries.size() >= MAX_RECENT_INVOCATIONS)
    {
        entries.erase(entries.begin());
    }
    entries.push_back(entry);
}

const char* invalidReasonLabel(DescriptorInvalidReason reason)
{
    switch (reason)
    {
    case DescriptorInvalidReason::Valid:
        return "valid";
    case DescriptorInvalidReason::NeverInitialized:
        return "never_initialized";
    case DescriptorInvalidReason::Zeroed:
        return "zeroed";
    case DescriptorInvalidReason::Clobbered:
        return "clobbered";
    case DescriptorInvalidReason::UnalignedAddress:
        return "unaligned";
    case DescriptorInvalidReason::InvalidAddress:
        return "invalid_address";
    default:
        return "unknown";
    }
}

} // namespace

void HookEntryIntTraceEngine::reset()
{
    m_lastInstall.reset();
    m_activeInvocation.reset();
    m_recentInvocations.clear();
    m_hasInstallSnapshot = false;
    m_installSnapshot.fill(0);
}

void HookEntryIntTraceEngine::recordInstall(Address installPc, Address descriptorAddress)
{
    m_lastInstall = InstallInfo{installPc, descriptorAddress};
    m_hasInstallSnapshot = false;
}

void HookEntryIntTraceEngine::snapshotDescriptorAtInstall(const u8* ram, Address descriptorOffset,
                                                          u32 ramSize)
{
    if (descriptorOffset + 0x30u > ramSize)
    {
        m_hasInstallSnapshot = false;
        return;
    }
    std::memcpy(m_installSnapshot.data(), ram + descriptorOffset, 0x30u);
    m_hasInstallSnapshot = true;
}

bool HookEntryIntTraceEngine::wasDescriptorClobberedSinceInstall(const u8* ram,
                                                                 Address descriptorOffset,
                                                                 u32 ramSize) const
{
    if (!m_hasInstallSnapshot || descriptorOffset + 0x30u > ramSize)
    {
        return false;
    }
    return std::memcmp(m_installSnapshot.data(), ram + descriptorOffset, 0x30u) != 0;
}

bool HookEntryIntTraceEngine::readInstallSnapshot(Address wordOffset, u32& value) const
{
    if (!m_hasInstallSnapshot || wordOffset + sizeof(u32) > 0x30u)
    {
        return false;
    }
    std::memcpy(&value, m_installSnapshot.data() + wordOffset, sizeof(u32));
    return true;
}

void HookEntryIntTraceEngine::beginInvocation(Address invokePc, Address descriptorAddress,
                                              Address descriptorResumeAddress,
                                              bool descriptorResumeValid, u32 savedSp, u32 savedFp,
                                              u32 savedGp, const std::array<u32, 8>& savedS,
                                              DescriptorInvalidReason invalidReason,
                                              u32 callbackGenerationBefore)
{
    HookEntryIntTraceEntry entry;
    entry.invokePc = invokePc;
    entry.descriptorAddress = descriptorAddress;
    entry.descriptorResumeAddress = descriptorResumeAddress;
    entry.descriptorResumeValid = descriptorResumeValid;
    entry.savedSp = savedSp;
    entry.savedFp = savedFp;
    entry.savedGp = savedGp;
    entry.savedS = savedS;
    entry.invalidReason = invalidReason;
    entry.callbackGenerationBefore = callbackGenerationBefore;
    if (m_lastInstall.has_value() && m_lastInstall->descriptorAddress == descriptorAddress)
    {
        entry.installPc = m_lastInstall->installPc;
    }
    m_activeInvocation = entry;
}

void HookEntryIntTraceEngine::noteCommittedResume(Address committedResumeAddress)
{
    if (!m_activeInvocation.has_value())
    {
        return;
    }
    m_activeInvocation->committedResumeAddress = committedResumeAddress;
    m_activeInvocation->committedResume = true;
}

void HookEntryIntTraceEngine::noteReturnFromException(Address pc)
{
    if (!m_activeInvocation.has_value())
    {
        return;
    }
    m_activeInvocation->threwReturnFromException = true;
    m_activeInvocation->returnFromExceptionPc = pc;
}

void HookEntryIntTraceEngine::finishInvocation(u32 callbackGenerationAfter)
{
    if (!m_activeInvocation.has_value())
    {
        return;
    }
    m_activeInvocation->callbackGenerationAfter = callbackGenerationAfter;
    appendRecent(m_recentInvocations, *m_activeInvocation);
    m_activeInvocation.reset();
}

std::string HookEntryIntTraceEngine::formatRecentInvocations() const
{
    std::ostringstream os;
    if (m_lastInstall.has_value())
    {
        os << "Last HookEntryInt install: pc=0x" << std::hex << m_lastInstall->installPc
           << " descriptor=0x" << m_lastInstall->descriptorAddress << "\n";
    }
    else
    {
        os << "Last HookEntryInt install: none\n";
    }

    os << "Recent HookEntryInt resumes (newest first):\n";
    if (m_recentInvocations.empty())
    {
        os << "  none\n";
        return os.str();
    }

    for (auto it = m_recentInvocations.rbegin(); it != m_recentInvocations.rend(); ++it)
    {
        os << "  descriptor=0x" << std::hex << it->descriptorAddress << " install_pc=0x"
           << it->installPc << " invoke_pc=0x" << it->invokePc << " desc_resume=0x"
           << it->descriptorResumeAddress
           << " desc_valid=" << (it->descriptorResumeValid ? "yes" : "no");
        if (!it->descriptorResumeValid)
        {
            os << " reason=" << invalidReasonLabel(it->invalidReason);
        }
        if (it->committedResume)
        {
            os << " committed_resume=0x" << it->committedResumeAddress;
        }
        os << " sp=0x" << it->savedSp << " fp=0x" << it->savedFp << " gp=0x" << it->savedGp
           << " gen=" << std::dec << it->callbackGenerationBefore << "->"
           << it->callbackGenerationAfter << " b017=" << (it->threwReturnFromException ? 1 : 0);
        if (it->threwReturnFromException)
        {
            os << " b017_pc=0x" << std::hex << it->returnFromExceptionPc;
        }
        os << "\n";
    }

    return os.str();
}

} // namespace runtime
} // namespace psxrecomp
