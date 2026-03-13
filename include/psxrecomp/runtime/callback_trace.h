#pragma once

#include "psxrecomp/runtime/logger.h"
#include "psxrecomp/types.h"

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace psxrecomp
{
namespace runtime
{

struct CallbackTraceEntry
{
    struct WriteHotspot
    {
        Address address = 0;
        u32 count = 0;
        u32 lastOldValue = 0;
        u32 lastNewValue = 0;
    };

    Address entryPc = 0;
    Address exitPc = 0;
    Address descriptorAddress = 0;
    Address returnSite = 0;
    bool threwReturnFromException = false;
    u32 callbackGenerationBefore = 0;
    u32 callbackGenerationAfter = 0;
    u32 irqStatusBefore = 0;
    u32 irqMaskBefore = 0;
    u32 irqStatusAfter = 0;
    u32 irqMaskAfter = 0;
    bool cop0InterruptEligibleBefore = false;
    bool cop0InterruptEligibleAfter = false;
    u32 consecutiveRepeatCount = 1;
    u32 totalRamWrites = 0;
    u32 persistentRamWrites = 0;
    u32 stackRamWrites = 0;
    std::vector<WriteHotspot> topWriteAddresses;
    std::vector<WriteHotspot> topPersistentWriteAddresses;
    std::vector<WriteHotspot> topStackWriteAddresses;
    std::vector<std::string> committedRegisters;
};

class CallbackTraceEngine
{
  public:
    struct ActiveWriteInfo
    {
        u32 count = 0;
        u32 lastOldValue = 0;
        u32 lastNewValue = 0;
        u8 size = 0;
    };

    void reset();

    void beginInvocation(Address entryPc, Address descriptorAddress, Address returnSite,
                         u32 callbackGenerationBefore, u32 irqStatusBefore, u32 irqMaskBefore,
                         bool cop0InterruptEligibleBefore);

    void finishInvocation(Address exitPc, bool threwReturnFromException,
                          u32 callbackGenerationAfter, u32 irqStatusAfter, u32 irqMaskAfter,
                          bool cop0InterruptEligibleAfter, RuntimeLogger* logger, bool emitLogs);

    bool hasActiveInvocation() const;

    void setActiveInvocationStackPointer(Address stackPointer);

    void recordRamWrite(Address address, u8 size, u32 oldValue, u32 newValue);

    void recordCommittedRegisterDelta(const std::array<u32, 32>& before,
                                      const std::array<u32, 32>& after, u32 hiBefore, u32 hiAfter,
                                      u32 loBefore, u32 loAfter, bool committed);

    std::string formatRecentCallbacks() const;

  private:
    struct ActiveInvocation
    {
        Address entryPc = 0;
        Address descriptorAddress = 0;
        Address returnSite = 0;
        u32 callbackGenerationBefore = 0;
        u32 irqStatusBefore = 0;
        u32 irqMaskBefore = 0;
        bool cop0InterruptEligibleBefore = false;
        u32 totalRamWrites = 0;
        u32 persistentRamWrites = 0;
        u32 stackRamWrites = 0;
        Address entryStackPointer = 0;
        std::unordered_map<Address, ActiveWriteInfo> ramWrites;
        std::unordered_map<Address, ActiveWriteInfo> persistentRamWritesByAddress;
        std::unordered_map<Address, ActiveWriteInfo> stackRamWritesByAddress;
        std::vector<std::string> committedRegisters;
    };

    struct CallbackRepeatSignature
    {
        Address entryPc = 0;
        Address exitPc = 0;
        Address descriptorAddress = 0;
        Address returnSite = 0;

        bool operator==(const CallbackRepeatSignature& rhs) const;
    };

    struct CallbackRepeatSignatureHash
    {
        size_t operator()(const CallbackRepeatSignature& signature) const;
    };

    struct CallbackPathKey
    {
        Address entryPc = 0;
        Address exitPc = 0;
        Address descriptorAddress = 0;

        bool operator==(const CallbackPathKey& rhs) const;
    };

    struct CallbackPathKeyHash
    {
        size_t operator()(const CallbackPathKey& key) const;
    };

    struct CallbackPathAggregate
    {
        u32 count = 0;
        u32 totalRamWrites = 0;
        u32 persistentRamWrites = 0;
        u32 stackRamWrites = 0;
        std::unordered_map<Address, ActiveWriteInfo> ramWrites;
        std::unordered_map<Address, ActiveWriteInfo> persistentRamWritesByAddress;
        std::unordered_map<Address, ActiveWriteInfo> stackRamWritesByAddress;
        std::unordered_map<Address, u32> returnSiteCounts;
        std::unordered_map<std::string, u32> committedRegisterCounts;
    };

    std::vector<ActiveInvocation> m_activeInvocations;
    std::vector<CallbackTraceEntry> m_recentEntries;
    std::unordered_map<CallbackRepeatSignature, u32, CallbackRepeatSignatureHash> m_signatureCounts;
    std::unordered_map<CallbackPathKey, CallbackPathAggregate, CallbackPathKeyHash>
        m_pathAggregates;
    CallbackRepeatSignature m_lastSignature{};
    u32 m_lastSignatureRepeatCount = 0;
    bool m_hasLastSignature = false;
};

} // namespace runtime
} // namespace psxrecomp
