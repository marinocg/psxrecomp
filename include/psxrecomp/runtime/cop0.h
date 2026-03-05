#pragma once

#include "psxrecomp/types.h"

#include <array>
#include <optional>

namespace psxrecomp
{
namespace runtime
{

class Cop0
{
  public:
    enum RegisterIndex : u8
    {
        BadVAddr = 8,
        Status = 12,
        Cause = 13,
        Epc = 14,
        PrId = 15
    };

    enum class ExceptionCode : u32
    {
        Interrupt = 0,
        AddressErrorLoad = 4,
        AddressErrorStore = 5,
        Syscall = 8,
        Breakpoint = 9,
        ReservedInstruction = 10,
        CoprocessorUnusable = 11,
        ArithmeticOverflow = 12
    };

    void reset();

    u32 mfc0(u8 rd) const;
    void mtc0(u8 rd, u32 value);

    void setHardwareInterruptPending(bool pending);
    bool irqEnableHw0() const;
    bool shouldTakeInterruptException() const;
    bool isInExceptionMode() const;
    void restoreState(u32 badVaddr, u32 status, u32 cause, u32 epc);

    void exceptionEnter(ExceptionCode code, u32 pc, bool inDelaySlot,
                        std::optional<u32> badVaddr = std::nullopt);
    void rfe();

  private:
    static constexpr u32 StatusCurrentModeMask = 0x3u;
    static constexpr u32 StatusModeBitsMask = 0x3Fu;
    static constexpr u32 StatusCurrentInterruptEnableBit = 0x1u;
    static constexpr u32 StatusInterruptMaskIp0Ip2Bits = 0x00000700u; // IM0..IM2
    static constexpr u32 StatusInterruptMaskIp2Bit = 1u << 10;        // IM2 (masks Cause.IP2)
    static constexpr u32 CauseSoftwareInterruptPendingMask = 0x00000300u;
    static constexpr u32 CauseIrqControllerPendingBit = 1u << 10;       // IP2
    static constexpr u32 CauseInterruptPendingIp0Ip2Mask = 0x00000700u; // IP0..IP2
    static constexpr u32 CauseExcCodeMask = 0x7Cu;
    static constexpr u32 CauseBranchDelayBit = 0x80000000u;

    std::array<u32, 32> m_registers{};
};

} // namespace runtime
} // namespace psxrecomp
