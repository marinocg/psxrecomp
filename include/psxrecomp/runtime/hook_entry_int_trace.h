#pragma once

#include "psxrecomp/types.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace psxrecomp
{
namespace runtime
{

/// Reason why a HookEntryInt descriptor was considered invalid at invocation.
enum class DescriptorInvalidReason : u8
{
    Valid = 0,
    NeverInitialized,
    Zeroed,
    Clobbered,
    UnalignedAddress,
    InvalidAddress,
};

struct HookEntryIntTraceEntry
{
    Address installPc = 0;
    Address invokePc = 0;
    Address descriptorAddress = 0;
    Address descriptorResumeAddress = 0;
    Address committedResumeAddress = 0;
    Address returnFromExceptionPc = 0;
    u32 callbackGenerationBefore = 0;
    u32 callbackGenerationAfter = 0;
    u32 savedSp = 0;
    u32 savedFp = 0;
    u32 savedGp = 0;
    std::array<u32, 8> savedS{}; ///< S0..S7 from the descriptor.
    bool descriptorResumeValid = false;
    bool committedResume = false;
    bool threwReturnFromException = false;
    DescriptorInvalidReason invalidReason = DescriptorInvalidReason::Valid;
};

class HookEntryIntTraceEngine
{
  public:
    void reset();
    void recordInstall(Address installPc, Address descriptorAddress);

    /// Snapshot the 0x30-byte descriptor at install time for later comparison.
    void snapshotDescriptorAtInstall(const u8* ram, Address descriptorOffset, u32 ramSize);

    void beginInvocation(Address invokePc, Address descriptorAddress,
                         Address descriptorResumeAddress, bool descriptorResumeValid, u32 savedSp,
                         u32 savedFp, u32 savedGp, const std::array<u32, 8>& savedS,
                         DescriptorInvalidReason invalidReason, u32 callbackGenerationBefore);
    void noteCommittedResume(Address committedResumeAddress);
    void noteReturnFromException(Address pc);
    void finishInvocation(u32 callbackGenerationAfter);
    std::string formatRecentInvocations() const;

    /// Check if the descriptor was changed between install and invocation.
    bool wasDescriptorClobberedSinceInstall(const u8* ram, Address descriptorOffset,
                                            u32 ramSize) const;

    /// Read a 4-byte word at \p wordOffset from the install-time descriptor snapshot.
    /// Returns true and sets \p value if a snapshot exists and offset is in range.
    bool readInstallSnapshot(Address wordOffset, u32& value) const;

  private:
    struct InstallInfo
    {
        Address installPc = 0;
        Address descriptorAddress = 0;
    };

    std::optional<InstallInfo> m_lastInstall;
    std::optional<HookEntryIntTraceEntry> m_activeInvocation;
    std::vector<HookEntryIntTraceEntry> m_recentInvocations;

    /// Snapshot of the 0x30-byte descriptor captured at install time.
    bool m_hasInstallSnapshot = false;
    std::array<u8, 0x30> m_installSnapshot{};
};

} // namespace runtime
} // namespace psxrecomp
