#include "psxrecomp/runtime/psx_system.h"

#include <sstream>

namespace psxrecomp
{
namespace runtime
{

namespace
{
constexpr u32 BIOS_VECTOR_TABLE_POINTER_OFFSET = 24u;
constexpr std::array<u16, 5> BIOS_CDROM_EVENT_SPECS_BOOT = {
    0x0010u, 0x0020u, 0x0040u, 0x0080u, 0x8000u,
};

CpuBootState makeDefaultCpuBootState()
{
    constexpr u32 statusIEcBit = 1u << 0;
    constexpr u32 statusKUcBit = 1u << 1;
    constexpr u32 statusIM2Bit = 1u << 10;

    CpuBootState state;
    state.badVaddr = 0u;
    state.status = statusIEcBit | statusIM2Bit;
    state.status &= ~statusKUcBit;
    state.cause = 0u;
    state.epc = 0u;
    state.interruptMask =
        static_cast<u32>(InterruptLine::VBlank) | static_cast<u32>(InterruptLine::Timer0) |
        static_cast<u32>(InterruptLine::Timer1) | static_cast<u32>(InterruptLine::Timer2) |
        static_cast<u32>(InterruptLine::Dma);
    state.architecturalPc = 0u;
    state.interruptDispatchArmed = false;
    return state;
}
} // namespace

void PsxSystem::boot()
{
    setCpuExecutionPhase(CpuExecutionPhase::BootInitializing);

    // PSX-SPX: hardware reset value is 0x07654321 (all channel enables=0,
    // priorities 1-7).  Pre-enable all seven channels.
    // DICR is initialised with master-enable (bit 23) and per-channel bits (16-22).
    writeMmio32(DmaController::ControlReg, 0x0FEDCBA9u);
    writeMmio32(DmaController::InterruptReg, 0x00FF0000u);

    // PSX-SPX: GetC0Table/GetB0Table expose BIOS-owned writable table roots.
    for (u32 address = BIOS_C0_TABLE_ADDRESS; address < BIOS_B0_HANDLER_TABLE_ADDRESS + 0x40u;
         address += sizeof(u32))
    {
        write<u32>(address, 0u);
    }
    write<u32>(BIOS_C0_TABLE_ADDRESS + BIOS_VECTOR_TABLE_POINTER_OFFSET,
               BIOS_C0_HANDLER_TABLE_ADDRESS);
    write<u32>(BIOS_B0_TABLE_ADDRESS + BIOS_VECTOR_TABLE_POINTER_OFFSET,
               BIOS_B0_HANDLER_TABLE_ADDRESS);

    m_cdrom.primeBootState(m_disc != nullptr);
    m_spu.primeBootState();
    initializeBiosCdromState(0u);
    applyCpuBootState(makeDefaultCpuBootState());

    m_logger.log(LogLevel::Info, "system", "Runtime boot sequence initialized");

    // Diagnostic: dump interrupt handler table.
    constexpr u32 HandlerTableBase = 0x801654ECu;
    constexpr u32 HandlerEntries = 11u;
    const Address htPhysical = normalizeAddress(HandlerTableBase);
    if (isMainRamAddress(htPhysical, HandlerEntries * sizeof(u32)))
    {
        std::ostringstream msg;
        msg << "handler_table_dump base=0x" << std::hex << HandlerTableBase;
        for (u32 i = 0; i < HandlerEntries; ++i)
        {
            msg << " [" << std::dec << i << "]=0x" << std::hex
                << read<u32>(HandlerTableBase + i * 4u);
        }
        m_logger.log(LogLevel::Info, "boot_diag", msg.str());
    }

    // Diagnostic: dump CD driver hardware pointers.
    {
        constexpr u32 addrs[] = {0x80163CECu, 0x80163CF0u, 0x80163CF4u, 0x80163D18u, 0x8016531Cu};
        const char* names[] = {"D3_MADR_ptr", "D3_BCR_ptr", "D3_CHCR_ptr", "DICR_ptr",
                               "CD_STATUS_ptr"};
        std::ostringstream msg;
        msg << "cd_hw_ptrs";
        for (int i = 0; i < 5; ++i)
        {
            msg << " " << names[i] << "=0x" << std::hex << read<u32>(addrs[i]);
        }
        m_logger.log(LogLevel::Info, "boot_diag", msg.str());
    }
}

} // namespace runtime
} // namespace psxrecomp
