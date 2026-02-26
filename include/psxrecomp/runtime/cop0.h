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
        Epc = 14
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

    void exceptionEnter(ExceptionCode code, u32 pc, bool inDelaySlot,
                        std::optional<u32> badVaddr = std::nullopt);
    void rfe();

  private:
    static constexpr u32 StatusModeBitsMask = 0x3Fu;
    static constexpr u32 CauseExcCodeMask = 0x7Cu;
    static constexpr u32 CauseBranchDelayBit = 0x80000000u;

    std::array<u32, 32> m_registers{};
};

} // namespace runtime
} // namespace psxrecomp
