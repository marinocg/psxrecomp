#include "psxrecomp/runtime/psx_system.h"

#include "irq_trace_utils.h"

#include <array>
#include <sstream>
#include <utility>

namespace psxrecomp
{
namespace runtime
{

namespace
{
const char* interruptLineName(InterruptLine line)
{
    switch (line)
    {
    case InterruptLine::VBlank:
        return "VBlank";
    case InterruptLine::Gpu:
        return "Gpu";
    case InterruptLine::Cdrom:
        return "Cdrom";
    case InterruptLine::Dma:
        return "Dma";
    case InterruptLine::Timer0:
        return "Timer0";
    case InterruptLine::Timer1:
        return "Timer1";
    case InterruptLine::Timer2:
        return "Timer2";
    case InterruptLine::Controller:
        return "Controller";
    case InterruptLine::Sio:
        return "Sio";
    case InterruptLine::Spu:
        return "Spu";
    case InterruptLine::Pio:
        return "Pio";
    default:
        return "Unknown";
    }
}

std::string formatPendingLineOrder(u32 pendingMasked)
{
    constexpr InterruptLine kPriorityOrder[] = {
        InterruptLine::VBlank, InterruptLine::Gpu,        InterruptLine::Cdrom,
        InterruptLine::Dma,    InterruptLine::Timer0,     InterruptLine::Timer1,
        InterruptLine::Timer2, InterruptLine::Controller, InterruptLine::Sio,
        InterruptLine::Spu,    InterruptLine::Pio,
    };

    std::ostringstream out;
    bool first = true;
    for (InterruptLine line : kPriorityOrder)
    {
        const u32 bit = static_cast<u32>(line);
        if ((pendingMasked & bit) == 0)
        {
            continue;
        }
        if (!first)
        {
            out << ",";
        }
        out << interruptLineName(line);
        first = false;
    }

    return out.str();
}

constexpr u32 STATUS_IEC_BIT = 1u << 0;
constexpr u32 STATUS_IM0_IM1_MASK = 0x00000300u;
constexpr u32 CAUSE_IP0_IP1_MASK = 0x00000300u;
constexpr std::array<u16, 5> CDROM_IRQ_EVENT_SPECS = {
    0x0010u, // INT1 -> data-ready style event
    0x0020u, // INT2 -> command completion
    0x0020u, // INT3 -> command completion for single-response commands (eg. Getstat)
    0x0080u, // INT4 -> end-of-read style event
    0x8000u, // INT5 -> error
};

bool traceCdCallbackEnabled()
{
    if (const char* env = std::getenv("PSXRECOMP_TRACE_CD_CALLBACK"))
    {
        return env[0] == '1';
    }
    return false;
}

const char* cdromCommandLabel(u8 command)
{
    switch (command)
    {
    case 0x01:
        return "CdlNop";
    case 0x0A:
        return "CdlInit";
    case 0x1C:
        return "CdlReset";
    default:
        return nullptr;
    }
}

class IrqExceptionExitGuard
{
  public:
    explicit IrqExceptionExitGuard(Cop0& cop0) : m_cop0(cop0) {}

    IrqExceptionExitGuard(const IrqExceptionExitGuard&) = delete;
    IrqExceptionExitGuard& operator=(const IrqExceptionExitGuard&) = delete;

    ~IrqExceptionExitGuard()
    {
        m_cop0.rfe();
    }

  private:
    Cop0& m_cop0;
};
} // namespace

bool PsxSystem::serviceBiosCdromInterrupt()
{
    if (!m_biosCdrom.initialized || !m_cdrom.hasIrqRequest())
    {
        return false;
    }

    const u8 irqType = static_cast<u8>(m_cdrom.readInterruptFlags() & 0x07u);
    if (irqType < 1u || irqType > CDROM_IRQ_EVENT_SPECS.size())
    {
        return false;
    }

    const bool traceCdCallback = traceCdCallbackEnabled();
    const Cdrom::DebugSnapshot beforeSnapshot = m_cdrom.debugSnapshot();
    const char* trackedCommand = cdromCommandLabel(beforeSnapshot.currentCommand);
    if (traceCdCallback)
    {
        std::ostringstream msg;
        msg << "event=cdrom_irq phase=before irq=INT" << std::dec << static_cast<unsigned>(irqType)
            << " command=" << (trackedCommand != nullptr ? trackedCommand : "Other") << " "
            << describeBiosCdromState();
        m_logger.log(LogLevel::Info, "cdcb_trace", msg.str());
    }

    // INT1 (data-ready): copy sector data for CdAsyncReadSector.
    if (irqType == 1u && m_biosCdrom.asyncReadCount > 0)
    {
        // Use the sector size recorded by A0:7E, defaulting to 0x800 if zero
        // (e.g. from a saved state that predates this field).
        const u32 sectorBytes =
            m_biosCdrom.asyncReadSectorBytes != 0 ? m_biosCdrom.asyncReadSectorBytes : 0x800u;
        if (sectorBytes != 0x800u && sectorBytes != 0x918u && sectorBytes != 0x924u)
        {
            std::ostringstream warn;
            warn << "INT1 unexpected asyncReadSectorBytes=0x" << std::hex << sectorBytes
                 << " mode=0x" << m_biosCdrom.asyncReadMode;
            m_logger.log(LogLevel::Warn, "cdrom", warn.str());
        }
        const char* readCmdLabel = (m_biosCdrom.asyncReadMode & 0x100u) ? "ReadS" : "ReadN";
        const u32 dstAddr =
            m_biosCdrom.asyncReadBuffer + m_biosCdrom.asyncSectorsRead * sectorBytes;
        // Enable data-buffer reads (request register bit 7 = BFRD) before
        // accessing the FIFO. On real hardware the BIOS writes 0x80 to the
        // request register here, triggering the 0→1 transition that loads
        // the active sector into the data FIFO. Without this, readData()
        // returns 0x00 because REQUEST_ENABLE_BUFFER_READ is clear.
        m_cdrom.enableDataRead();
        std::vector<u8> sectorData(sectorBytes);
        for (u32 i = 0; i < sectorBytes; ++i)
        {
            sectorData[i] = m_cdrom.readData();
        }
        std::ostringstream detail;
        detail << "irq=INT1 cmd=" << readCmdLabel << " mode=0x" << std::hex
               << m_biosCdrom.asyncReadMode << " sector_index=" << std::dec
               << m_biosCdrom.asyncSectorsRead << " bytes_copied=" << sectorBytes
               << " sectors_remaining=" << (m_biosCdrom.asyncReadCount - 1u);
        copyBufferToRam(dstAddr, sectorData.data(), sectorBytes, sectorBytes,
                        m_debugOverlay.lastProgramCounter(), "CdAsyncReadSector", detail.str());
        if (traceCdCallback)
        {
            m_logger.log(LogLevel::Info, "cdcb_trace", detail.str());
        }
        ++m_biosCdrom.asyncSectorsRead;
        --m_biosCdrom.asyncReadCount;

        // Drain the response FIFO (stat byte placed there when INT1 was
        // published) before acknowledging. On real hardware the BIOS reads
        // response bytes before acking; without this drain,
        // publishNextInterruptEvent() is blocked by its !m_responseFifo.empty()
        // guard and the next sector's INT1 is never promoted.
        while ((m_cdrom.readStatus() & 0x20u) != 0)
        {
            (void)m_cdrom.readResponse();
        }
        // Fully acknowledge INT1 so the game's handler doesn't see the
        // CDROM interrupt and try to DMA from an already-drained FIFO.
        // On real PSX the BIOS handler has higher priority and fully
        // handles each sector before HookEntryInt sees the interrupt.
        m_cdrom.writeInterruptFlags(0x07u);

        if (m_biosCdrom.asyncReadCount == 0)
        {
            // All sectors read — issue Pause so the CDROM controller stops
            // reading and generates INT3 (ack) then INT2 (paused). INT2
            // delivers EventSpec::CommandDone (0x0020) which the game waits
            // on via TestEvent/WaitEvent.
            constexpr u8 CDCMD_PAUSE = 0x09;
            m_cdrom.writeCommand(CDCMD_PAUSE);
            if (traceCdCallback)
            {
                std::ostringstream msg;
                msg << "event=cdrom_bios_read_complete total_sectors=" << std::dec
                    << m_biosCdrom.asyncSectorsRead << " pause_issued=1";
                m_logger.log(LogLevel::Info, "cdcb_trace", msg.str());
            }
        }
        else
        {
            // Prepare the data FIFO for the next sector so the subsequent
            // INT1 finds valid data. This mirrors how the real CDROM
            // controller pre-loads sector data before signalling INT1.
            m_cdrom.loadNextSectorToFifo();
        }
    }

    // INT3 (first ack): copy status byte for CdAsyncGetStatus.
    if (irqType == 3u && m_biosCdrom.asyncResultPtr != 0)
    {
        const u8 stat = m_cdrom.readResponse();
        copyBufferToRam(m_biosCdrom.asyncResultPtr, &stat, 1, 1,
                        m_debugOverlay.lastProgramCounter(), "CdAsyncGetStatus",
                        "irq=INT3 response_byte=0");
        m_biosCdrom.asyncResultPtr = 0;
    }

    std::vector<u32> callbacks =
        m_events.deliverByClassSpec(EventClass::Cdrom, CDROM_IRQ_EVENT_SPECS[irqType - 1u]);
    auto genericCallbacks = m_events.deliverByClassSpec(EventClass::Cdrom, EventSpec::Interrupted);
    callbacks.insert(callbacks.end(), genericCallbacks.begin(), genericCallbacks.end());

    for (u32 address : callbacks)
    {
        invokeCallback(address);
    }

    if (traceCdCallback)
    {
        std::ostringstream msg;
        msg << "event=cdrom_irq phase=after irq=INT" << std::dec << static_cast<unsigned>(irqType)
            << " callbacks=" << callbacks.size() << " " << describeBiosCdromState();
        if (trackedCommand != nullptr && irqType == 3u)
        {
            msg << " completion=" << trackedCommand;
        }
        m_logger.log(LogLevel::Info, "cdcb_trace", msg.str());
    }

    validateAllocatorHeapBoundary("CD IRQ callback", static_cast<Address>(irqType));

    return true;
}

void PsxSystem::setCallbackInvoker(CallbackInvoker invoker)
{
    // Store the raw invoker bridge (calls into the generated module).
    m_callbackInvoker = std::move(invoker);

    // The dispatcher should invoke callbacks through the system so that
    // ReturnFromException works (requires m_inCallbackInvocation=true).
    m_dispatcher.setCallbackInvoker([this](u32 address) -> u32
                                    { return this->invokeCallbackRaw(address); });
}

void PsxSystem::serviceIrqWork(u32 pendingMasked)
{
    u32 pendingForHook = pendingMasked;
    u32 pendingForKernelEvents = pendingMasked;

    // PSX-accurate ordering: walk SysEnqIntRP priority chains before HookEntryInt.
    // On real hardware the BIOS exception handler invokes the linked-list
    // handlers installed by SysEnqIntRP(priority, struc) *before* transferring
    // control to HookEntryInt. Chain handlers (e.g., CDROM interrupt handler
    // registered at priority 2) read hardware registers, acknowledge their IRQ
    // source, and update game state so that the HookEntryInt handler only has
    // to deal with software-level dispatch of the remaining pending set.
    if (pendingForHook != 0u)
    {
        bool chainFiredRfe = false;
        try
        {
            chainFiredRfe = dispatchIrqChains();
        }
        catch (const ReturnFromExceptionSignal&)
        {
            // Should not happen (dispatchIrqChains already catches RFE), but
            // handle defensively.
            syncCop0InterruptPending();
            return;
        }

        if (chainFiredRfe)
        {
            if (traceIrqFlowEnabled())
            {
                m_logger.log(LogLevel::Info, "irq_trace",
                             "event=return_from_exception source=irq_chain_pre_hook");
            }
            try
            {
                m_dispatcher.servicePendingMask(m_interrupts, m_events, m_criticalSectionDepth,
                                                pendingForKernelEvents, &m_logger);
            }
            catch (const ReturnFromExceptionSignal&)
            {
                if (traceIrqFlowEnabled())
                {
                    m_logger.log(LogLevel::Info, "irq_trace",
                                 "event=return_from_exception source=dispatcher_pending");
                }
            }
            syncCop0InterruptPending();
            return;
        }

        // Chain handlers may have acknowledged interrupts; re-read the mask.
        syncCop0InterruptPending();
        pendingForHook = m_interrupts.readStatus() & m_interrupts.readMask();
        pendingForKernelEvents = pendingForHook;
    }

    // PSX-SPX: CDROM IRQs expose five sub-interrupt types that the BIOS maps
    // to separate kernel events during _96_init. Handle those before any
    // generic kernel-event dispatch. Recompute the remaining hardware-pending
    // mask afterward so HookEntryInt only runs for IRQ lines that are still
    // visible to the BIOS handler.
    if ((pendingForHook & static_cast<u32>(InterruptLine::Cdrom)) != 0u)
    {
        pendingForKernelEvents &= ~static_cast<u32>(InterruptLine::Cdrom);
        try
        {
            (void)serviceBiosCdromInterrupt();
        }
        catch (const ReturnFromExceptionSignal&)
        {
            if (traceIrqFlowEnabled())
            {
                m_logger.log(LogLevel::Info, "irq_trace",
                             "event=return_from_exception source=bios_cdrom_dispatch");
            }
            syncCop0InterruptPending();
            return;
        }

        syncCop0InterruptPending();
        if (!m_cdrom.hasIrqRequest())
        {
            m_interrupts.writeStatus(~static_cast<u32>(InterruptLine::Cdrom));
            syncCop0InterruptPending();
        }
        pendingForHook = m_interrupts.readStatus() & m_interrupts.readMask();
    }

    // PSX-accurate interrupt ordering: on real hardware, CDROM sectors arrive
    // between VBlanks so the CDROM handler fires and processes data before the
    // next VBlank handler polls for it.  The batched-cycle model in the
    // recompiler can cause CDROM INT1 and VBlank to become pending in the
    // same serviceInterrupts() call.  When both are visible in I_STAT, a game
    // handler that iterates bits from low to high would process VBlank (bit 0)
    // before CDROM (bit 2), causing decompression polling to miss the data.
    //
    // Fix: when VBlank and at least one non-VBlank IRQ are pending together,
    // deliver the non-VBlank interrupts first in a separate HookEntryInt
    // invocation.  The game's CDROM handler runs, DMA's sector data, and
    // updates its ring-buffer status.  Then VBlank is restored and delivered
    // normally so the VBlank handler finds the data ready.
    if (pendingForHook != 0u && m_criticalSectionDepth == 0 &&
        m_hookEntryInt.descriptorAddress != 0 && !m_inHookEntryIntHandler)
    {
        constexpr u32 vblankBit = static_cast<u32>(InterruptLine::VBlank);
        const bool hasVBlank = (pendingForHook & vblankBit) != 0;
        const bool hasOtherIrqs = (pendingForHook & ~vblankBit) != 0;

        if (traceIrqFlowEnabled())
        {
            std::ostringstream msg;
            msg << "event=split_check pendingForHook=0x" << std::hex << pendingForHook
                << " hasVBlank=" << hasVBlank << " hasOtherIrqs=" << hasOtherIrqs;
            m_logger.log(LogLevel::Info, "irq_trace", msg.str());
        }

        if (hasVBlank && hasOtherIrqs)
        {
            // Phase 1: deliver non-VBlank interrupts only.
            m_interrupts.writeStatus(~vblankBit);
            syncCop0InterruptPending();

            if (traceIrqFlowEnabled())
            {
                std::ostringstream msg;
                msg << "event=hook_entry_int_invoke_split phase=non_vblank descriptor=0x"
                    << std::hex << m_hookEntryInt.descriptorAddress << " pending=0x"
                    << (m_interrupts.readStatus() & m_interrupts.readMask());
                m_logger.log(LogLevel::Info, "irq_trace", msg.str());
            }

            try
            {
                invokeHookEntryIntHandler();
            }
            catch (const ReturnFromExceptionSignal&)
            {
                // First callback completed via ReturnFromException.
                // COP0 is still in exception mode; the caller's
                // IrqExceptionExitGuard (or the in-flight depth guard) handles
                // cleanup.
            }

            // Phase 2: restore VBlank for normal delivery below.
            m_interrupts.raise(InterruptLine::VBlank);
            syncCop0InterruptPending();
            pendingForHook = m_interrupts.readStatus() & m_interrupts.readMask();
        }
    }

    // HookEntryInt must run while the remaining IRQ status bits are still
    // visible. Demo SDKs use it as their primary hardware IRQ fan-out path.
    if (pendingForHook != 0u && m_criticalSectionDepth == 0 &&
        m_hookEntryInt.descriptorAddress != 0 && !m_inHookEntryIntHandler)
    {
        if (traceIrqFlowEnabled())
        {
            std::ostringstream msg;
            msg << "event=hook_entry_int_invoke descriptor=0x" << std::hex
                << m_hookEntryInt.descriptorAddress;
            m_logger.log(LogLevel::Info, "irq_trace", msg.str());
        }
        try
        {
            invokeHookEntryIntHandler();
        }
        catch (const ReturnFromExceptionSignal&)
        {
            if (traceIrqFlowEnabled())
            {
                m_logger.log(LogLevel::Info, "irq_trace",
                             "event=return_from_exception source=hook_entry_int");
            }
            try
            {
                m_dispatcher.servicePendingMask(m_interrupts, m_events, m_criticalSectionDepth,
                                                pendingForKernelEvents, &m_logger);
            }
            catch (const ReturnFromExceptionSignal&)
            {
                if (traceIrqFlowEnabled())
                {
                    m_logger.log(LogLevel::Info, "irq_trace",
                                 "event=return_from_exception source=dispatcher_pending");
                }
            }
            syncCop0InterruptPending();
            return;
        }
    }

    const u32 pendingAfterHook = m_interrupts.readStatus() & m_interrupts.readMask();

    if (pendingAfterHook != 0 && traceIrqFlowEnabled())
    {
        std::ostringstream msg;
        msg << "event=irq_dispatch_order source=service_interrupts pending_masked=0x" << std::hex
            << pendingAfterHook << " lines=" << formatPendingLineOrder(pendingAfterHook);
        m_logger.log(LogLevel::Info, "irq_trace", msg.str());
    }

    // Kernel event delivery (OpenEvent/EnableEvent model).
    // A callback may execute ReturnFromException to abort further handling.
    // COP0 Status restore is handled by the caller's epilogue.
    try
    {
        m_dispatcher.servicePendingMask(m_interrupts, m_events, m_criticalSectionDepth,
                                        pendingForKernelEvents, &m_logger);
    }
    catch (const ReturnFromExceptionSignal&)
    {
        if (traceIrqFlowEnabled())
        {
            m_logger.log(LogLevel::Info, "irq_trace",
                         "event=return_from_exception source=dispatcher_pending");
        }
        syncCop0InterruptPending();
        return;
    }

    syncCop0InterruptPending();
}

void PsxSystem::serviceInterrupts()
{
    syncLevelInterruptSources();

    // Record CD-ROM / IRQ state snapshot for stall classification.
    m_stallClassifier.recordCdromIrqState(
        m_cdrom.readInterruptFlags(), static_cast<u16>(m_interrupts.readStatus()),
        static_cast<u16>(m_interrupts.readMask()), m_cdrom.hasIrqRequest());

    const u32 pendingMasked = m_interrupts.readStatus() & m_interrupts.readMask();
    const u32 cop0Status = m_cop0.mfc0(Cop0::RegisterIndex::Status);
    const u32 cop0Cause = m_cop0.mfc0(Cop0::RegisterIndex::Cause);
    const u32 swPendingMasked =
        (cop0Cause & CAUSE_IP0_IP1_MASK) & (cop0Status & STATUS_IM0_IM1_MASK);
    const bool swPending = (cop0Status & STATUS_IEC_BIT) != 0u && swPendingMasked != 0u;
    const bool cop0IrqPending = pendingMasked != 0u || swPending;
    const bool irqTakeEligible = m_cop0.shouldTakeInterruptException();

    // Fresh exception entry: COP0 must be ready to take an interrupt and no
    // callback may currently be executing (we are not yet inside any handler).
    const bool canEnterFreshIrqException =
        cop0IrqPending && m_criticalSectionDepth == 0 && !m_inCallbackInvocation && irqTakeEligible;

    // In-flight service: we are already inside a callback/exception context.
    // Service pending IRQ work without entering a new COP0 exception.
    // m_irqServiceDepth == 0 prevents accidental recursive full dispatch.
    const bool canContinueCurrentIrqService =
        cop0IrqPending && m_criticalSectionDepth == 0 && m_irqServiceDepth == 0 &&
        m_inCallbackInvocation && m_cop0.isInExceptionMode();

    // Diagnostic: detect persistent IRQ delivery blockage.
    if (cop0IrqPending && !canEnterFreshIrqException && !canContinueCurrentIrqService)
    {
        ++m_irqBlockedConsecutive;
        if (m_irqBlockedConsecutive == 64u)
        {
            std::ostringstream msg;
            msg << "event=irq_blocked_diagnostic pending_masked=0x" << std::hex << pendingMasked
                << " cop0_status=0x" << cop0Status << " cop0_cause=0x" << cop0Cause
                << " critical_depth=" << std::dec << m_criticalSectionDepth
                << " in_callback=" << (m_inCallbackInvocation ? 1 : 0)
                << " in_hook=" << (m_inHookEntryIntHandler ? 1 : 0)
                << " take_eligible=" << (irqTakeEligible ? 1 : 0)
                << " exception_mode=" << (m_cop0.isInExceptionMode() ? 1 : 0)
                << " irq_service_depth=" << m_irqServiceDepth << " pc=0x" << std::hex
                << m_debugOverlay.lastProgramCounter() << " timer2_counter=" << std::dec
                << m_timers.readCounter(2) << " timer2_hw_target=" << m_timers.readTarget(2)
                << " sw_target_0x80188F8C=0x" << std::hex
                << readFromRegion<u32>(m_ram.data(), 0x00088F8Cu, MemoryMap::RAM_SIZE)
                << " sw_duration_0x80188F90=0x"
                << readFromRegion<u32>(m_ram.data(), 0x00088F90u, MemoryMap::RAM_SIZE)
                << " vblank_cb_table=[";
            for (int i = 0; i < 8; ++i)
            {
                if (i > 0)
                    msg << ",";
                msg << "0x"
                    << readFromRegion<u32>(m_ram.data(), 0x00066590u + static_cast<u32>(i * 4),
                                           MemoryMap::RAM_SIZE);
            }
            msg << "] vblank_count="
                << readFromRegion<u32>(m_ram.data(), 0x000665B0u, MemoryMap::RAM_SIZE);
            m_logger.log(LogLevel::Error, "irq_diag", msg.str());
        }
    }
    else
    {
        m_irqBlockedConsecutive = 0;
    }

    if (traceIrqFlowEnabled())
    {
        std::ostringstream msg;
        msg << "event=service_interrupts pending_masked=0x" << std::hex << pendingMasked
            << " sw_pending_masked=0x" << swPendingMasked << " status=0x"
            << m_interrupts.readStatus() << " mask=0x" << m_interrupts.readMask()
            << " critical_depth=" << std::dec << m_criticalSectionDepth
            << " in_callback=" << (m_inCallbackInvocation ? 1 : 0)
            << " cop0_sw_pending=" << (swPending ? 1 : 0)
            << " cop0_irq_take_eligible=" << (irqTakeEligible ? 1 : 0)
            << " can_enter_fresh=" << (canEnterFreshIrqException ? 1 : 0)
            << " can_continue_current=" << (canContinueCurrentIrqService ? 1 : 0);
        m_logger.log(LogLevel::Info, "irq_trace", msg.str());
    }

    if (!cop0IrqPending)
    {
        // Still allow the event dispatcher to flush deferred callbacks.
        try
        {
            m_dispatcher.serviceInterrupts(m_interrupts, m_events, m_criticalSectionDepth,
                                           &m_logger);
        }
        catch (const ReturnFromExceptionSignal&)
        {
            if (traceIrqFlowEnabled())
            {
                m_logger.log(LogLevel::Info, "irq_trace",
                             "event=return_from_exception source=dispatcher_empty_pending");
            }
            syncCop0InterruptPending();
            return;
        }
        syncCop0InterruptPending();
        return;
    }

    if (!canEnterFreshIrqException && !canContinueCurrentIrqService)
    {
        // Keep pending state visible via Cause.IP bits, but do not dispatch
        // BIOS/IRQ callbacks: COP0 interrupt masks do not allow a fresh
        // exception entry, and we are not currently inside an exception that
        // could service this work in-flight.
        syncCop0InterruptPending();
        return;
    }

    if (canEnterFreshIrqException)
    {
        // Enter a fresh COP0 interrupt exception and service all pending work.
        // rfe() is issued by IrqExceptionExitGuard when this scope exits.
        m_cop0.exceptionEnter(Cop0::ExceptionCode::Interrupt,
                              m_debugOverlay.lastProgramCounter(), false);
        IrqExceptionExitGuard irqExitGuard(m_cop0);
        serviceIrqWork(pendingMasked);
    }
    else
    {
        // In-flight path: already inside a COP0 exception/callback context.
        // Service pending IRQ work without entering a new exception or calling
        // rfe().  The depth guard prevents accidental recursive full dispatch.
        struct DepthGuard
        {
            u32& depth;
            explicit DepthGuard(u32& d) : depth(d) { ++depth; }
            ~DepthGuard() { --depth; }
        } depthGuard(m_irqServiceDepth);
        serviceIrqWork(pendingMasked);
    }
}

PsxSystem::CallbackContextDisposition
PsxSystem::consumePendingCallbackRegisters(std::array<u32, 32>& regsInOut)
{
    if (!m_hasPendingCallbackRegisters)
    {
        return CallbackContextDisposition::RestoreSaved;
    }

    for (size_t reg = 0; reg < m_pendingCallbackRegisters.size(); ++reg)
    {
        if (m_pendingCallbackRegisterMask[reg])
        {
            regsInOut[reg] = m_pendingCallbackRegisters[reg];
        }
    }
    m_hookEntryIntTrace.noteCommittedResume(m_pendingCallbackRegisters[31]);
    m_hasPendingCallbackRegisters = false;
    m_pendingCallbackRegisterMask.fill(false);
    ++m_callbackContextCommitGeneration;
    return CallbackContextDisposition::CommitMutated;
}

u32 PsxSystem::callbackContextCommitGeneration() const
{
    return m_callbackContextCommitGeneration;
}

} // namespace runtime
} // namespace psxrecomp
