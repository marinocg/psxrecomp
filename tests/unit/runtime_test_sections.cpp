#include "runtime_test_sections.h"
#include "runtime_test_timer_sections.h"

#include "psxrecomp/runtime/psx_system.h"
#include "psxrecomp/runtime/resource_pack.h"

#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>

namespace MemoryMap = psxrecomp::MemoryMap;

namespace
{
constexpr psxrecomp::u32 encodeGteCommand(psxrecomp::u32 bits)
{
    return 0x4A000000u | (bits & 0x01FFFFFFu);
}
} // namespace

void runRuntimeStateSerializationChecks(psxrecomp::runtime::PsxSystem& system)
{
    using psxrecomp::runtime::InterruptController;

    [[maybe_unused]] auto checksum1 = system.stateChecksum();
    auto state = system.serializeState();
    [[maybe_unused]] const auto cdromStatusBefore =
        system.readMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 0);

    system.writeMmioExplicit<psxrecomp::u32>(psxrecomp::runtime::Mmio::GPU_GP0, 0xAABBCCDDu);
    assert(system.gpu().fifoDepth() > 0);
    system.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 0, 0x02u);
    assert((system.readMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 0) &
            0x03u) == 0x02u);

    assert(system.deserializeState(state));
    [[maybe_unused]] auto checksum2 = system.stateChecksum();
    assert(checksum1 == checksum2);
    assert(system.gpu().fifoDepth() == 0);
    assert(system.readMmioExplicit<psxrecomp::u32>(psxrecomp::runtime::Mmio::GPU_GP0) == 0);
    assert(system.readMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 0) ==
           cdromStatusBefore);

    auto stateWithInterrupts = state;
    const size_t interruptStateOffset = sizeof(psxrecomp::u32) + MemoryMap::RAM_SIZE +
                                        sizeof(psxrecomp::u32) + MemoryMap::SCRATCHPAD_SIZE +
                                        sizeof(psxrecomp::u32) + MemoryMap::BIOS_SIZE;
    stateWithInterrupts[interruptStateOffset + 0] = 0xAA;
    stateWithInterrupts[interruptStateOffset + 1] = 0x55;
    stateWithInterrupts[interruptStateOffset + 2] = 0x00;
    stateWithInterrupts[interruptStateOffset + 3] = 0xF0;
    stateWithInterrupts[interruptStateOffset + 4] = 0x0F;
    stateWithInterrupts[interruptStateOffset + 5] = 0x00;
    stateWithInterrupts[interruptStateOffset + 6] = 0x00;
    stateWithInterrupts[interruptStateOffset + 7] = 0x00;
    assert(system.deserializeState(stateWithInterrupts));
    assert(system.interrupts().readStatus() == (0xF00055AAu & InterruptController::ValidLineMask));
    assert(system.interrupts().readMask() == 0x0000000Fu);

    auto stateWithJunk = state;
    const auto firstRamByteBeforeFailedLoad = system.read<psxrecomp::u8>(MemoryMap::RAM_BASE);
    stateWithJunk[4] = static_cast<psxrecomp::u8>(firstRamByteBeforeFailedLoad ^ 0xFFu);
    stateWithJunk.push_back(0x99);
    assert(!system.deserializeState(stateWithJunk));
    assert(system.read<psxrecomp::u8>(MemoryMap::RAM_BASE) == firstRamByteBeforeFailedLoad);

    system.gte().mtc2(6, 0x11223344u);
    system.gte().ctc2(24, 0x55667788u);
    system.gte().exec(encodeGteCommand(0x00002Du));
    const auto gteState = system.serializeState();

    system.tickCpuCycles(5);
    system.gte().mtc2(6, 0xAABBCCDDu);
    system.gte().ctc2(24, 0xEEFF0011u);

    assert(system.deserializeState(gteState));
    assert(system.gte().busyCyclesRemaining() == 5u);
    [[maybe_unused]] const auto gteCpuBeforeRead = system.cpuCyclesElapsed();
    assert(system.gte().mfc2(6) == 0x11223344u);
    assert(system.cpuCyclesElapsed() == gteCpuBeforeRead + 5u);
    assert(system.gte().cfc2(24) == 0x55667788u);

    system.writeMmioExplicit<psxrecomp::u32>(psxrecomp::runtime::Mmio::MDEC_BASE + 4,
                                             (1u << 30) | (1u << 29));
    system.writeMmioExplicit<psxrecomp::u32>(psxrecomp::runtime::Mmio::MDEC_BASE + 0, 0x38000001u);
    const auto mdecState = system.serializeState();

    system.writeMmioExplicit<psxrecomp::u32>(psxrecomp::runtime::Mmio::MDEC_BASE + 4, (1u << 31));
    assert(system.deserializeState(mdecState));
    assert((system.readMmioExplicit<psxrecomp::u32>(psxrecomp::runtime::Mmio::MDEC_BASE + 4) &
            (1u << 29)) != 0u);
    assert((system.readMmioExplicit<psxrecomp::u32>(psxrecomp::runtime::Mmio::MDEC_BASE + 4) &
            (1u << 28)) != 0u);
}

void runRuntimeLoggingAndDumpChecks(psxrecomp::runtime::PsxSystem& system)
{
    using psxrecomp::runtime::DmaController;
    using psxrecomp::runtime::DmaPort;
    using psxrecomp::runtime::LogLevel;

    bool sawInfo = false;
    system.logger().setMinLevel(LogLevel::Info);
    system.logger().setCallback(
        [&sawInfo](const psxrecomp::runtime::LogEvent& event)
        {
            if (event.level == LogLevel::Info && event.category == "bios")
            {
                sawInfo = true;
            }
        });

    psxrecomp::u32 regs[32] = {'A'};
    system.callBiosSyscall(0x3F, regs, 32);
    assert(sawInfo);

    system.callBiosSyscall(0x3F, regs, 1);
    auto lastEvent = system.logger().lastEvent();
    assert(lastEvent.has_value());
    assert(lastEvent->level == LogLevel::Warn);

    [[maybe_unused]] const auto dmaTransfersBeforeFrame = system.debugOverlay().dmaTransfers();
    const psxrecomp::Address spuBase =
        DmaController::ChannelBase +
        DmaController::ChannelStride * static_cast<psxrecomp::Address>(DmaPort::Spu);
    constexpr psxrecomp::Address spuDmaAddress = 0x00016000u;
    system.write<psxrecomp::u32>(spuDmaAddress, 0xA5A5A5A5u);
    system.write<psxrecomp::u32>(spuBase + 0x0, spuDmaAddress);
    system.write<psxrecomp::u32>(spuBase + 0x4, 0x00000001u);
    system.write<psxrecomp::u32>(spuBase + 0x8, 0x01000001u);
    assert(system.debugOverlay().dmaTransfers() > dmaTransfersBeforeFrame);

    system.runFrame();
    assert(system.debugOverlay().frameCounter() >= 1);
    assert(system.debugOverlay().dmaTransfers() >= dmaTransfersBeforeFrame + 1);

    auto ramDump = system.dumpRam();
    auto vramDump = system.dumpVram();
    auto spuDump = system.dumpSpuRam();
    assert(ramDump.size() == MemoryMap::RAM_SIZE);
    assert(!vramDump.empty());
    assert(!spuDump.empty());
}

void runRuntimeInterruptAndTimerChecks(psxrecomp::runtime::PsxSystem& system)
{
    using psxrecomp::Address;
    using psxrecomp::runtime::Cop0;
    using psxrecomp::runtime::DmaController;
    using psxrecomp::runtime::DmaPort;
    using psxrecomp::runtime::InterruptController;
    using psxrecomp::runtime::InterruptLine;

    auto setCdromIndex = [&system](psxrecomp::u8 index)
    {
        system.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 0, index);
        assert((system.readMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 0) &
                0x03u) == (index & 0x03u));
    };

    // syscall(0) critical-section wrappers should return prior state in v0.
    std::array<psxrecomp::u32, 32> syscallRegs{};
    syscallRegs[4] = 1; // EnterCriticalSection
    system.callBiosSyscall(0, syscallRegs.data(), syscallRegs.size());
    assert(syscallRegs[2] == 1u);
    system.callBiosSyscall(0, syscallRegs.data(), syscallRegs.size());
    assert(syscallRegs[2] == 0u);

    syscallRegs[4] = 2; // ExitCriticalSection
    system.callBiosSyscall(0, syscallRegs.data(), syscallRegs.size());
    assert(syscallRegs[2] == 1u);
    system.callBiosSyscall(0, syscallRegs.data(), syscallRegs.size());
    assert(syscallRegs[2] == 1u);
    system.callBiosSyscall(0, syscallRegs.data(), syscallRegs.size());
    assert(syscallRegs[2] == 0u);

    // HookEntryInt callback should not run while inside a critical section.
    constexpr Address hookDescriptorAddress = 0x00001000;
    constexpr psxrecomp::u32 hookCallbackAddress = 0x80002000u;
    std::array<psxrecomp::u32, 32> hookRegs{};
    hookRegs[9] = 0x13; // setjmp
    hookRegs[4] = hookDescriptorAddress;
    hookRegs[31] = hookCallbackAddress;
    hookRegs[29] = 0x80002100u;
    hookRegs[30] = 0x80002120u;
    system.callBiosVector(0xA0, hookRegs.data(), hookRegs.size());

    hookRegs = {};
    hookRegs[9] = 0x19; // HookEntryInt
    hookRegs[4] = hookDescriptorAddress;
    system.callBiosVector(0xB0, hookRegs.data(), hookRegs.size());

    psxrecomp::u32 hookInvocations = 0;
    system.setCallbackInvoker(
        [&hookInvocations, hookCallbackAddress](psxrecomp::u32 address) -> psxrecomp::u32
        {
            if (address == hookCallbackAddress)
            {
                ++hookInvocations;
            }
            return 0;
        });

    system.interrupts().restoreState(static_cast<psxrecomp::u32>(InterruptLine::VBlank),
                                     static_cast<psxrecomp::u32>(InterruptLine::VBlank));
    syscallRegs[4] = 1; // EnterCriticalSection
    system.callBiosSyscall(0, syscallRegs.data(), syscallRegs.size());
    system.serviceInterrupts();
    assert(hookInvocations == 0u);
    syscallRegs[4] = 2; // ExitCriticalSection
    system.callBiosSyscall(0, syscallRegs.data(), syscallRegs.size());
    system.serviceInterrupts();
    assert(hookInvocations == 1u);

    hookRegs = {};
    hookRegs[9] = 0x18; // ResetEntryInt
    system.callBiosVector(0xB0, hookRegs.data(), hookRegs.size());
    system.setCallbackInvoker(psxrecomp::runtime::CallbackInvoker{});

    // IRQ delivery should populate COP0, and serviceInterrupts() should
    // restore Status low mode bits when callback flow aborts via ReturnFromException.
    constexpr Address cop0HookDescriptorAddress = 0x00001100;
    constexpr psxrecomp::u32 cop0HookCallbackAddress = 0x80003000u;
    std::array<psxrecomp::u32, 32> cop0HookRegs{};
    cop0HookRegs[9] = 0x13; // setjmp
    cop0HookRegs[4] = cop0HookDescriptorAddress;
    cop0HookRegs[31] = cop0HookCallbackAddress;
    cop0HookRegs[29] = 0x80003100u;
    cop0HookRegs[30] = 0x80003120u;
    system.callBiosVector(0xA0, cop0HookRegs.data(), cop0HookRegs.size());

    cop0HookRegs = {};
    cop0HookRegs[9] = 0x19; // HookEntryInt
    cop0HookRegs[4] = cop0HookDescriptorAddress;
    system.callBiosVector(0xB0, cop0HookRegs.data(), cop0HookRegs.size());

    bool cop0ReturnInvoked = false;
    system.setCallbackInvoker(
        [&system, &cop0ReturnInvoked,
         cop0HookCallbackAddress](psxrecomp::u32 address) -> psxrecomp::u32
        {
            if (address == cop0HookCallbackAddress)
            {
                cop0ReturnInvoked = true;
                std::array<psxrecomp::u32, 32> regs{};
                regs[9] = 0x17; // ReturnFromException
                system.callBiosVector(0xB0, regs.data(), regs.size());
            }
            return 0;
        });

    // Enable IEc and IM2 (CPU interrupt line fed by I_STAT&I_MASK).
    system.cop0().mtc0(Cop0::RegisterIndex::Status, 0x040Bu);
    system.debugOverlay().setLastProgramCounter(0x80023456u);
    system.interrupts().restoreState(static_cast<psxrecomp::u32>(InterruptLine::VBlank),
                                     static_cast<psxrecomp::u32>(InterruptLine::VBlank));
    system.serviceInterrupts();
    assert(cop0ReturnInvoked);
    assert(system.cop0().mfc0(Cop0::RegisterIndex::Epc) == 0x80023456u);
    assert((system.cop0().mfc0(Cop0::RegisterIndex::Cause) & 0x7Cu) == 0u);
    assert((system.cop0().mfc0(Cop0::RegisterIndex::Status) & 0x3Fu) == 0x0Bu);

    // Cause.IP2 should track I_STAT&I_MASK pending state (VBlank here).
    [[maybe_unused]] constexpr psxrecomp::u32 kCauseIp2Bit = 1u << 10;
    system.interrupts().restoreState(0u, static_cast<psxrecomp::u32>(InterruptLine::VBlank));
    system.tickCpuCycles(1);
    assert((system.cop0().mfc0(Cop0::RegisterIndex::Cause) & kCauseIp2Bit) == 0u);
    system.interrupts().raise(InterruptLine::VBlank);
    system.tickCpuCycles(1);
    assert((system.cop0().mfc0(Cop0::RegisterIndex::Cause) & kCauseIp2Bit) != 0u);
    system.writeMmioExplicit<psxrecomp::u32>(psxrecomp::runtime::Mmio::INTERRUPT_STATUS,
                                             ~static_cast<psxrecomp::u32>(InterruptLine::VBlank));
    system.tickCpuCycles(1);
    assert((system.cop0().mfc0(Cop0::RegisterIndex::Cause) & kCauseIp2Bit) == 0u);

    cop0HookRegs = {};
    cop0HookRegs[9] = 0x18; // ResetEntryInt
    system.callBiosVector(0xB0, cop0HookRegs.data(), cop0HookRegs.size());
    system.setCallbackInvoker(psxrecomp::runtime::CallbackInvoker{});

    // IRQ delivery should restore the COP0 Status mode stack even when no callback
    // executes ReturnFromException (PSX-SPX: exception enter shift + RFE pop).
    constexpr psxrecomp::u32 irqRestoreStatusLow6 = 0x0Bu;
    const psxrecomp::u32 vblankLine = static_cast<psxrecomp::u32>(InterruptLine::VBlank);
    system.cop0().mtc0(Cop0::RegisterIndex::Status, (1u << 10) | irqRestoreStatusLow6);
    system.debugOverlay().setLastProgramCounter(0x80024444u);
    system.interrupts().restoreState(vblankLine, vblankLine);
    system.serviceInterrupts();
    assert((system.cop0().mfc0(Cop0::RegisterIndex::Status) & 0x3Fu) == irqRestoreStatusLow6);
    assert(system.cop0().mfc0(Cop0::RegisterIndex::Epc) == 0x80024444u);

    // COP0 software interrupt pending (Cause.IP0) should trigger IRQ
    // exception entry/exit when IM0 + IEc are enabled.
    constexpr psxrecomp::u32 causeIp0Bit = 1u << 8;
    system.interrupts().restoreState(0u, 0u);
    system.cop0().mtc0(Cop0::RegisterIndex::Cause, causeIp0Bit);
    system.cop0().mtc0(Cop0::RegisterIndex::Status, causeIp0Bit | irqRestoreStatusLow6);
    system.debugOverlay().setLastProgramCounter(0x80025555u);
    system.serviceInterrupts();
    assert(system.cop0().mfc0(Cop0::RegisterIndex::Epc) == 0x80025555u);
    assert((system.cop0().mfc0(Cop0::RegisterIndex::Status) & 0x3Fu) == irqRestoreStatusLow6);
    assert((system.cop0().mfc0(Cop0::RegisterIndex::Cause) & causeIp0Bit) != 0u);
    system.cop0().mtc0(Cop0::RegisterIndex::Cause, 0u);

    // IRQ callback delivery must be gated by COP0 IEc + IM2 (Cause.IP2 mask).
    constexpr psxrecomp::u32 irqGateCallbackAddress = 0x80003100u;
    psxrecomp::u32 irqGateCallbackCount = 0;
    const psxrecomp::u32 irqGateEventHandle = system.events().openEvent(
        psxrecomp::runtime::EventClass::VBlank, psxrecomp::runtime::EventSpec::Counter,
        psxrecomp::runtime::EventMode::Callback, irqGateCallbackAddress);
    assert(irqGateEventHandle != 0xFFFFFFFFu);
    system.events().enableEvent(irqGateEventHandle);
    system.setCallbackInvoker(
        [&irqGateCallbackCount, irqGateCallbackAddress](psxrecomp::u32 address) -> psxrecomp::u32
        {
            if (address == irqGateCallbackAddress)
            {
                ++irqGateCallbackCount;
            }
            return 0;
        });

    system.interrupts().restoreState(vblankLine, vblankLine);
    system.cop0().mtc0(Cop0::RegisterIndex::Status, 1u << 10); // IM2 only, IEc=0
    system.serviceInterrupts();
    assert(irqGateCallbackCount == 0u);
    assert((system.interrupts().readStatus() & vblankLine) != 0u);

    system.cop0().mtc0(Cop0::RegisterIndex::Status, (1u << 10) | (1u << 0)); // IM2 + IEc
    system.serviceInterrupts();
    assert(irqGateCallbackCount == 1u);
    assert((system.interrupts().readStatus() & vblankLine) == 0u);
    system.events().closeEvent(irqGateEventHandle);
    system.setCallbackInvoker(psxrecomp::runtime::CallbackInvoker{});

    Address spuBase = DmaController::ChannelBase +
                      DmaController::ChannelStride * static_cast<Address>(DmaPort::Spu);
    system.write<psxrecomp::u32>(0x00011000, 0xABCDEF01);
    system.write<psxrecomp::u32>(spuBase + 0x0, 0x00011000);
    system.write<psxrecomp::u32>(spuBase + 0x4, 0x00000001);
    system.write<psxrecomp::u32>(spuBase + 0x8, 0x01000001);

    assert(system.spu().lastDmaWord() == 0xABCDEF01);

    setCdromIndex(0);
    system.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 1, 0x19);
    assert(system.readMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 1) ==
           0x19);
    assert(system.readMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 1) ==
           0x00);

    // 1F801800 index must select the logical register bank used by offsets 1..3.
    setCdromIndex(1);
    system.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 2, 0x05);
    setCdromIndex(0);
    [[maybe_unused]] const psxrecomp::u8 hintMask =
        system.readMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 3);
    assert((hintMask & 0x1Fu) == 0x05u);
    assert((hintMask & 0xE0u) == 0xE0u);

    setCdromIndex(0);
    system.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 2, 0x00);
    system.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 1, 0x0E);
    assert(system.readMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 1) ==
           0x00);

    setCdromIndex(2);
    system.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 1, 0xAA);
    setCdromIndex(0);
    assert(system.readMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 1) ==
           0x00);

    setCdromIndex(3);
    system.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 2, 0xFF);
    setCdromIndex(0);
    [[maybe_unused]] const psxrecomp::u8 hintMaskAfterIndex3Write =
        system.readMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 3);
    assert((hintMaskAfterIndex3Write & 0x1Fu) == 0x05u);
    assert((hintMaskAfterIndex3Write & 0xE0u) == 0xE0u);

    // Drain any pre-existing CD-ROM IRQ/response state from earlier command checks.
    for (int i = 0; i < 128; ++i)
    {
        setCdromIndex(0);
        const psxrecomp::u8 status =
            system.readMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 0);
        const bool responseReady = (status & (1u << 5)) != 0u;

        setCdromIndex(1);
        const psxrecomp::u8 currentFlags =
            system.readMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 3);
        const bool irqActive = (currentFlags & 0x07u) != 0u;

        if (!responseReady && !irqActive)
        {
            break;
        }

        if (responseReady)
        {
            setCdromIndex(0);
            (void)system.readMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 1);
        }
        else
        {
            setCdromIndex(1);
            system.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 3, 0x07);
            system.writeMmioExplicit<psxrecomp::u32>(
                psxrecomp::runtime::Mmio::INTERRUPT_STATUS,
                ~static_cast<psxrecomp::u32>(InterruptLine::Cdrom));
        }
    }

    // CD-ROM IRQ queue: INT3 must remain visible until ACKed, and unread response
    // bytes must survive ACK. The queued INT2 promotes only after the prior
    // response stream is consumed.
    system.interrupts().restoreState(0, static_cast<psxrecomp::u32>(InterruptLine::Cdrom));
    setCdromIndex(1);
    system.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 2, 0x07);
    setCdromIndex(0);
    system.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 1, 0x09);
    assert((system.interrupts().readStatus() & static_cast<psxrecomp::u32>(InterruptLine::Cdrom)) !=
           0u);
    system.writeMmioExplicit<psxrecomp::u32>(psxrecomp::runtime::Mmio::INTERRUPT_STATUS,
                                             ~static_cast<psxrecomp::u32>(InterruptLine::Cdrom));
    assert((system.interrupts().readStatus() & static_cast<psxrecomp::u32>(InterruptLine::Cdrom)) !=
           0u);
    setCdromIndex(1);
    [[maybe_unused]] const psxrecomp::u8 hintStatusIndex1 =
        system.readMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 3);
    assert((hintStatusIndex1 & 0x07u) == 0x03u);
    assert((hintStatusIndex1 & 0xE0u) == 0xE0u);
    setCdromIndex(3);
    [[maybe_unused]] const psxrecomp::u8 hintStatusIndex3 =
        system.readMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 3);
    assert((hintStatusIndex3 & 0x07u) == 0x03u);
    assert((hintStatusIndex3 & 0xE0u) == 0xE0u);
    setCdromIndex(1);
    system.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 3, 0x07);
    [[maybe_unused]] const psxrecomp::u8 clearedHintStatus =
        system.readMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 3);
    assert((clearedHintStatus & 0x07u) == 0x00u);
    assert((clearedHintStatus & 0xE0u) == 0xE0u);
    system.writeMmioExplicit<psxrecomp::u32>(psxrecomp::runtime::Mmio::INTERRUPT_STATUS,
                                             ~static_cast<psxrecomp::u32>(InterruptLine::Cdrom));
    assert((system.interrupts().readStatus() & static_cast<psxrecomp::u32>(InterruptLine::Cdrom)) ==
           0u);

    setCdromIndex(0);
    [[maybe_unused]] const psxrecomp::u8 statusAfterAckBeforeRead =
        system.readMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 0);
    assert((statusAfterAckBeforeRead & (1u << 5)) != 0u);

    // Drain the prior INT3 response byte; this should allow queued INT2 to promote.
    (void)system.readMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 1);
    setCdromIndex(1);
    [[maybe_unused]] const psxrecomp::u8 promotedHintStatus =
        system.readMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 3);
    assert((promotedHintStatus & 0x07u) == 0x02u);
    assert((promotedHintStatus & 0xE0u) == 0xE0u);
    system.writeMmioExplicit<psxrecomp::u32>(psxrecomp::runtime::Mmio::INTERRUPT_STATUS,
                                             ~static_cast<psxrecomp::u32>(InterruptLine::Cdrom));
    assert((system.interrupts().readStatus() & static_cast<psxrecomp::u32>(InterruptLine::Cdrom)) !=
           0u);

    setCdromIndex(0);
    [[maybe_unused]] const psxrecomp::u8 statusAfterQueuePromote =
        system.readMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 0);
    assert((statusAfterQueuePromote & (1u << 5)) != 0u);
    (void)system.readMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 1);
    [[maybe_unused]] const psxrecomp::u8 statusAfterReadingPromotedResponse =
        system.readMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 0);
    assert((statusAfterReadingPromotedResponse & (1u << 5)) == 0u);

    setCdromIndex(1);
    system.writeMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 3, 0x07);
    [[maybe_unused]] const psxrecomp::u8 finalClearedHintStatus =
        system.readMmioExplicit<psxrecomp::u8>(psxrecomp::runtime::Mmio::CDROM_BASE + 3);
    assert((finalClearedHintStatus & 0x07u) == 0x00u);
    assert((finalClearedHintStatus & 0xE0u) == 0xE0u);
    system.writeMmioExplicit<psxrecomp::u32>(psxrecomp::runtime::Mmio::INTERRUPT_STATUS,
                                             ~static_cast<psxrecomp::u32>(InterruptLine::Cdrom));
    assert((system.interrupts().readStatus() & static_cast<psxrecomp::u32>(InterruptLine::Cdrom)) ==
           0u);

    // 16-bit I_STAT/I_MASK accesses must behave like hardware low-halfword accesses.
    system.interrupts().restoreState(0xFFFFFFFFu, 0xFFFFFFFFu);
    assert(system.interrupts().readStatus() == InterruptController::ValidLineMask);
    assert(system.interrupts().readMask() == InterruptController::ValidLineMask);
    system.interrupts().restoreState(0, 0);
    system.interrupts().raise(InterruptLine::VBlank);
    assert(system.readMmioExplicit<psxrecomp::u16>(psxrecomp::runtime::Mmio::INTERRUPT_STATUS) ==
           static_cast<psxrecomp::u16>(InterruptLine::VBlank));
    system.writeMmioExplicit<psxrecomp::u16>(
        psxrecomp::runtime::Mmio::INTERRUPT_STATUS,
        static_cast<psxrecomp::u16>(~static_cast<psxrecomp::u16>(InterruptLine::VBlank)));
    assert((system.interrupts().readStatus() &
            static_cast<psxrecomp::u32>(InterruptLine::VBlank)) == 0u);
    const auto lowMask = static_cast<psxrecomp::u16>(InterruptLine::VBlank) |
                         static_cast<psxrecomp::u16>(InterruptLine::Dma);
    system.writeMmioExplicit<psxrecomp::u16>(psxrecomp::runtime::Mmio::INTERRUPT_MASK, lowMask);
    assert(system.readMmioExplicit<psxrecomp::u16>(psxrecomp::runtime::Mmio::INTERRUPT_MASK) ==
           lowMask);

    runRuntimeTimerChecks(system);
}
