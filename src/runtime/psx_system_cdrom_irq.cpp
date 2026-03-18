#include "psxrecomp/runtime/psx_system.h"

#include <array>
#include <sstream>

namespace psxrecomp
{
namespace runtime
{

namespace
{

constexpr std::array<u16, 5> CDROM_IRQ_EVENT_SPECS = {
    0x0010u, // INT1 -> data-ready (CommandAck / 0x0010)
    0x0020u, // INT2 -> complete (CommandDone / 0x0020)
    0x0020u, // INT3 -> acknowledge; delivers CommandDone (0x0020) per PSX-SPX BIOS handler
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

    // Track whether the type-specific path already acknowledged the interrupt.
    bool interruptAcknowledged = false;

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
        // enableDataRead() is scoped to this BIOS-owned CdAsyncReadSector path only.
        // Game-managed reads must arm BFRD themselves via the request register.
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
        interruptAcknowledged = true;

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

    // When the BIOS owns the CDROM operation (CdAsyncReadSector or
    // CdAsyncGetStatus), the type-specific paths above already drain
    // responses and acknowledge. For other IRQ types originating from
    // BIOS-initiated operations (e.g. the Pause INT3+INT2 sequence after
    // the last async sector), drain and ack so publishNextInterruptEvent()
    // is unblocked and the queue can advance.
    //
    // When the GAME manages its own CDROM reads (e.g. PSY-Q library
    // CdRead/CdReadSync), we must NOT drain or ack here — the game's
    // event callbacks need to read response bytes and see the interrupt
    // type. The game's HookEntryInt handler will ack instead.
    const bool biosOwnsOperation =
        m_biosCdrom.asyncReadCount > 0 || m_biosCdrom.asyncSectorsRead > 0;
    if (!interruptAcknowledged && biosOwnsOperation)
    {
        while ((m_cdrom.readStatus() & 0x20u) != 0)
        {
            (void)m_cdrom.readResponse();
        }
        m_cdrom.writeInterruptFlags(0x07u);
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

} // namespace runtime
} // namespace psxrecomp
