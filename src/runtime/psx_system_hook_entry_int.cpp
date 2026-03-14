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
        const Address descriptorPhysical = normalizeAddress(m_hookEntryInt.descriptorAddress);
        const Address descriptorOffset = foldMainRamAddress(descriptorPhysical);
        if (m_hookEntryInt.descriptorAddress != 0 && isMainRamAddress(descriptorPhysical, 0x30u) &&
            descriptorOffset <= MemoryMap::RAM_SIZE - 0x30u)
        {
            // PSX-SPX: HookEntryInt resumes like longjmp(setjmp_buf, 1).
            resumeAddress =
                readFromRegion<u32>(m_ram.data(), descriptorOffset + 0x00u, MemoryMap::RAM_SIZE);
            resumeAddressValid = isValidHookEntryIntResumeAddress(resumeAddress);
            if (resumeAddressValid)
            {
                m_pendingCallbackRegisters = {};
                m_pendingCallbackRegisterMask.fill(false);
                m_pendingCallbackRegisters[REG_V0] = 1;
                m_pendingCallbackRegisterMask[REG_V0] = true;
                m_pendingCallbackRegisters[REG_RA] = resumeAddress;
                m_pendingCallbackRegisterMask[REG_RA] = true;
                savedSp = readFromRegion<u32>(m_ram.data(), descriptorOffset + 0x04u,
                                              MemoryMap::RAM_SIZE);
                savedFp = readFromRegion<u32>(m_ram.data(), descriptorOffset + 0x08u,
                                              MemoryMap::RAM_SIZE);
                savedGp = readFromRegion<u32>(m_ram.data(), descriptorOffset + 0x2Cu,
                                              MemoryMap::RAM_SIZE);
                m_pendingCallbackRegisters[REG_SP] = savedSp;
                m_pendingCallbackRegisterMask[REG_SP] = true;
                m_pendingCallbackRegisters[REG_FP] = savedFp;
                m_pendingCallbackRegisterMask[REG_FP] = true;
                for (size_t reg = REG_S0; reg <= REG_S7; ++reg)
                {
                    const Address offset =
                        static_cast<Address>(0x0Cu + (reg - REG_S0) * sizeof(u32));
                    m_pendingCallbackRegisters[reg] = readFromRegion<u32>(
                        m_ram.data(), descriptorOffset + offset, MemoryMap::RAM_SIZE);
                    m_pendingCallbackRegisterMask[reg] = true;
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

        m_hookEntryIntTrace.beginInvocation(
            m_debugOverlay.lastProgramCounter(), m_hookEntryInt.descriptorAddress, resumeAddress,
            resumeAddressValid, savedSp, savedFp, savedGp, m_callbackContextCommitGeneration);

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
