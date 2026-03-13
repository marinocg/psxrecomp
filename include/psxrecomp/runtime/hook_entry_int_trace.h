#pragma once

#include "psxrecomp/types.h"

#include <optional>
#include <string>
#include <vector>

namespace psxrecomp
{
namespace runtime
{

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
    bool descriptorResumeValid = false;
    bool committedResume = false;
    bool threwReturnFromException = false;
};

class HookEntryIntTraceEngine
{
  public:
    void reset();
    void recordInstall(Address installPc, Address descriptorAddress);
    void beginInvocation(Address invokePc, Address descriptorAddress,
                         Address descriptorResumeAddress, bool descriptorResumeValid, u32 savedSp,
                         u32 savedFp, u32 savedGp, u32 callbackGenerationBefore);
    void noteCommittedResume(Address committedResumeAddress);
    void noteReturnFromException(Address pc);
    void finishInvocation(u32 callbackGenerationAfter);
    std::string formatRecentInvocations() const;

  private:
    struct InstallInfo
    {
        Address installPc = 0;
        Address descriptorAddress = 0;
    };

    std::optional<InstallInfo> m_lastInstall;
    std::optional<HookEntryIntTraceEntry> m_activeInvocation;
    std::vector<HookEntryIntTraceEntry> m_recentInvocations;
};

} // namespace runtime
} // namespace psxrecomp
