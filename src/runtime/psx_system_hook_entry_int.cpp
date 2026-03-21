#include "psxrecomp/runtime/psx_system.h"

#include <sstream>

namespace psxrecomp
{
namespace runtime
{

namespace
{

constexpr size_t REG_V0 = 2;
constexpr size_t REG_S0 = 16;
constexpr size_t REG_S7 = 23;
constexpr size_t REG_GP = 28;
constexpr size_t REG_SP = 29;
constexpr size_t REG_FP = 30;
constexpr size_t REG_RA = 31;

bool isValidHookEntryIntResumeAddress(u32 address)
{
    return address >= 0x80000000u && isMainRamAddress(address & 0x1FFFFFFFu, sizeof(u32)) &&
           (address & 0x3u) == 0u;
}

} // namespace

void PsxSystem::invokeHookEntryIntHandler()
{
    m_inHookEntryIntHandler = true;
    try
    {
        u32 resumeAddress = 0;
        bool resumeAddressValid = false;
        u32 savedSp = 0;
        u32 savedFp = 0;
        u32 savedGp = 0;
        std::array<u32, 8> savedS{};
        DescriptorInvalidReason invalidReason = DescriptorInvalidReason::NeverInitialized;
        const Address descriptorPhysical = normalizeAddress(m_hookEntryInt.descriptorAddress);
        const Address descriptorOffset = foldMainRamAddress(descriptorPhysical);
        if (m_hookEntryInt.descriptorAddress != 0 && isMainRamAddress(descriptorPhysical, 0x30u) &&
            descriptorOffset <= MemoryMap::RAM_SIZE - 0x30u)
        {
            // PSX-SPX: HookEntryInt resumes like longjmp(setjmp_buf, 1).
            resumeAddress =
                readFromRegion<u32>(m_ram.data(), descriptorOffset + 0x00u, MemoryMap::RAM_SIZE);

            // Some games zero the descriptor as part of their longjmp before the HookEntryInt
            // invoke fires, retaining the resume address only in a CPU register (not memory).
            // When the live descriptor is fully zeroed, fall back to the install-time snapshot
            // so the resume can still be performed correctly.
            bool usingSnapshot = false;
            if (resumeAddress == 0)
            {
                u32 snapshotResume = 0;
                if (m_hookEntryIntTrace.readInstallSnapshot(0x00u, snapshotResume) &&
                    snapshotResume != 0 && isValidHookEntryIntResumeAddress(snapshotResume))
                {
                    resumeAddress = snapshotResume;
                    usingSnapshot = true;
                }
            }

            // Helper to read a descriptor word from the snapshot or live RAM.
            const auto readDesc = [&](Address offset) -> u32
            {
                if (usingSnapshot)
                {
                    u32 val = 0;
                    m_hookEntryIntTrace.readInstallSnapshot(offset, val);
                    return val;
                }
                return readFromRegion<u32>(m_ram.data(), descriptorOffset + offset,
                                           MemoryMap::RAM_SIZE);
            };

            // Classify descriptor validity.
            if (resumeAddress == 0)
            {
                // Check if the entire descriptor is zeroed.
                bool allZero = true;
                for (Address i = 0; i < 0x30u; i += 4)
                {
                    if (readFromRegion<u32>(m_ram.data(), descriptorOffset + i,
                                            MemoryMap::RAM_SIZE) != 0)
                    {
                        allZero = false;
                        break;
                    }
                }
                invalidReason =
                    allZero ? DescriptorInvalidReason::Zeroed : DescriptorInvalidReason::Clobbered;
            }
            else if ((resumeAddress & 0x3u) != 0u)
            {
                invalidReason = DescriptorInvalidReason::UnalignedAddress;
            }
            else if (!isValidHookEntryIntResumeAddress(resumeAddress))
            {
                invalidReason = DescriptorInvalidReason::InvalidAddress;
            }
            else if (m_hookEntryIntTrace.wasDescriptorClobberedSinceInstall(
                         m_ram.data(), descriptorOffset, MemoryMap::RAM_SIZE) &&
                     !usingSnapshot)
            {
                // Address is valid but descriptor was modified since install.
                invalidReason = DescriptorInvalidReason::Clobbered;
            }
            else
            {
                invalidReason = DescriptorInvalidReason::Valid;
            }

            resumeAddressValid = isValidHookEntryIntResumeAddress(resumeAddress);
            if (resumeAddressValid)
            {
                invalidReason = DescriptorInvalidReason::Valid;
                m_pendingCallbackRegisters = {};
                m_pendingCallbackRegisterMask.fill(false);
                m_pendingCallbackRegisters[REG_V0] = 1;
                m_pendingCallbackRegisterMask[REG_V0] = true;
                m_pendingCallbackRegisters[REG_RA] = resumeAddress;
                m_pendingCallbackRegisterMask[REG_RA] = true;
                savedSp = readDesc(0x04u);
                savedFp = readDesc(0x08u);
                savedGp = readDesc(0x2Cu);
                m_pendingCallbackRegisters[REG_SP] = savedSp;
                m_pendingCallbackRegisterMask[REG_SP] = true;
                m_pendingCallbackRegisters[REG_FP] = savedFp;
                m_pendingCallbackRegisterMask[REG_FP] = true;
                for (size_t reg = REG_S0; reg <= REG_S7; ++reg)
                {
                    const Address offset =
                        static_cast<Address>(0x0Cu + (reg - REG_S0) * sizeof(u32));
                    const u32 val = readDesc(offset);
                    m_pendingCallbackRegisters[reg] = val;
                    m_pendingCallbackRegisterMask[reg] = true;
                    savedS[reg - REG_S0] = val;
                }
                m_pendingCallbackRegisters[REG_GP] = savedGp;
                m_pendingCallbackRegisterMask[REG_GP] = true;
                m_hasPendingCallbackRegisters = true;
            }
            else if (resumeAddress != 0)
            {
                std::ostringstream msg;
                msg << "Ignoring HookEntryInt resume address 0x" << std::hex << resumeAddress
                    << " from descriptor 0x" << m_hookEntryInt.descriptorAddress;
                m_logger.log(LogLevel::Warn, "bios", msg.str());
            }
        }

        m_hookEntryIntTrace.beginInvocation(m_debugOverlay.lastProgramCounter(),
                                            m_hookEntryInt.descriptorAddress, resumeAddress,
                                            resumeAddressValid, savedSp, savedFp, savedGp, savedS,
                                            invalidReason, m_callbackContextCommitGeneration);

        if (resumeAddressValid)
        {
            try
            {
                (void)invokeCallbackRaw(resumeAddress, m_hookEntryInt.descriptorAddress);
            }
            catch (...)
            {
                validateAllocatorHeapBoundary("HookEntryInt", resumeAddress);
                throw;
            }
            validateAllocatorHeapBoundary("HookEntryInt", resumeAddress);
        }
        m_hookEntryIntTrace.finishInvocation(m_callbackContextCommitGeneration);
    }
    catch (...)
    {
        m_hookEntryIntTrace.finishInvocation(m_callbackContextCommitGeneration);
        m_hasPendingCallbackRegisters = false;
        m_pendingCallbackRegisterMask.fill(false);
        m_inHookEntryIntHandler = false;
        throw;
    }
    m_hasPendingCallbackRegisters = false;
    m_pendingCallbackRegisterMask.fill(false);
    m_inHookEntryIntHandler = false;
}

} // namespace runtime
} // namespace psxrecomp
