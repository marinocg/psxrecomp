/**
 * @file psx_system_bios_cd.cpp
 * @brief BIOS-level CD-ROM functions (A0 vector: CdInit, CdRemove,
 *        CdAsyncSeekL, CdAsyncGetStatus, CdAsyncReadSector,
 *        CdAsyncSetMode, CdInitSubFunc).
 *
 * These wrap the low-level Cdrom device, issuing hardware commands and
 * leaving completion to the interrupt/event system set up by _96_init.
 */
#include "psxrecomp/runtime/psx_system.h"

#include "bios_helpers.h"

#include <cstring>
#include <sstream>

namespace psxrecomp
{
namespace runtime
{

namespace
{
/// CD-ROM command bytes (PSX-SPX nomenclature).
constexpr u8 CDCMD_GETSTAT = 0x01;
constexpr u8 CDCMD_SETLOC = 0x02;
constexpr u8 CDCMD_READN = 0x06;
constexpr u8 CDCMD_INIT = 0x0A;
constexpr u8 CDCMD_SETMODE = 0x0E;
constexpr u8 CDCMD_SEEKL = 0x15;
} // namespace

bool PsxSystem::callBiosCdFunction(u32 functionId, u32* regs)
{
    const u32 a0 = regs[4];
    const u32 a1 = regs[5];
    const u32 a2 = regs[6];

    switch (functionId)
    {
    // ---------------------------------------------------------------
    // A0:54  CdInit
    //
    // High-level CD-ROM initialisation.  Calls _96_init internally
    // so that the five kernel CD events are opened and the interrupt
    // enable register is written.  Then issues a CdlInit (0x0A)
    // hardware command to reset the drive state.
    // ---------------------------------------------------------------
    case 0x54:
    {
        if (!m_biosCdrom.initialized)
        {
            initializeBiosCdromState(0u);
        }
        // Clear any pending async state from a previous session.
        m_biosCdrom.asyncResultPtr = 0;
        m_biosCdrom.asyncReadBuffer = 0;
        m_biosCdrom.asyncReadCount = 0;
        m_biosCdrom.asyncSectorsRead = 0;

        // Acknowledge any lingering CDROM interrupt so the new Init
        // command can be issued cleanly.
        m_cdrom.writeInterruptFlags(0x07u);

        m_cdrom.writeCommand(CDCMD_INIT);
        regs[2] = 1;
        m_logger.log(LogLevel::Debug, "bios", "CdInit (A0 0x54)");
        return true;
    }

    // ---------------------------------------------------------------
    // A0:56  CdRemove
    //
    // Tear down the CD-ROM subsystem.  The retail BIOS implementation
    // is essentially a no-op (events are NOT closed), mirroring the
    // behaviour of _96_remove (A0:72).
    // ---------------------------------------------------------------
    case 0x56:
    {
        m_biosCdrom.asyncResultPtr = 0;
        m_biosCdrom.asyncReadBuffer = 0;
        m_biosCdrom.asyncReadCount = 0;
        m_biosCdrom.asyncSectorsRead = 0;
        regs[2] = 1;
        m_logger.log(LogLevel::Debug, "bios", "CdRemove (A0 0x56)");
        return true;
    }

    // ---------------------------------------------------------------
    // A0:78  CdAsyncSeekL
    //
    // Seek to a logical position.
    //   $a0 = pointer to a CdlLOC struct in RAM (min, sec, sect, 0).
    //
    // Issues Setloc with the BCD position bytes, then SeekL.
    // Completion arrives as an INT2 → EventSpec::CommandDone event.
    // ---------------------------------------------------------------
    case 0x78:
    {
        const u8* loc = ramPointerConst(m_ram.data(), a0);
        const u8 minute = loc[0];
        const u8 second = loc[1];
        const u8 sector = loc[2];

        m_cdrom.writeInterruptFlags(0x07u);
        m_cdrom.writeParam(minute);
        m_cdrom.writeParam(second);
        m_cdrom.writeParam(sector);
        m_cdrom.writeCommand(CDCMD_SETLOC);

        // Acknowledge the INT3 from Setloc before issuing SeekL.
        m_cdrom.writeInterruptFlags(0x07u);
        m_cdrom.writeCommand(CDCMD_SEEKL);

        regs[2] = 1;
        {
            std::ostringstream msg;
            msg << "CdAsyncSeekL (A0 0x78) loc=" << std::hex << static_cast<int>(minute) << ":"
                << static_cast<int>(second) << ":" << static_cast<int>(sector);
            m_logger.log(LogLevel::Debug, "bios", msg.str());
        }
        return true;
    }

    // ---------------------------------------------------------------
    // A0:7C  CdAsyncGetStatus
    //
    //   $a0 = pointer to an 8-byte result buffer in RAM.
    //
    // Issues a Getstat command.  When the INT3 fires, the interrupt
    // handler copies the first response byte (stat) to the buffer.
    // ---------------------------------------------------------------
    case 0x7C:
    {
        m_biosCdrom.asyncResultPtr = a0;
        m_cdrom.writeInterruptFlags(0x07u);
        m_cdrom.writeCommand(CDCMD_GETSTAT);
        regs[2] = 1;
        m_logger.log(LogLevel::Debug, "bios", "CdAsyncGetStatus (A0 0x7C)");
        return true;
    }

    // ---------------------------------------------------------------
    // A0:7E  CdAsyncReadSector
    //
    //   $a0 = number of sectors to read
    //   $a1 = pointer to destination buffer in RAM
    //   $a2 = read mode (passed to Setmode before ReadN)
    //
    // Issues Setmode($a2), then ReadN.  Each INT1 (data-ready) causes
    // the interrupt handler to copy one 2048-byte sector to the
    // destination buffer.  CdAsyncReadSector stores the bookkeeping
    // so the handler knows where to write.
    // ---------------------------------------------------------------
    case 0x7E:
    {
        m_biosCdrom.asyncReadBuffer = a1;
        m_biosCdrom.asyncReadCount = a0;
        m_biosCdrom.asyncSectorsRead = 0;

        m_cdrom.writeInterruptFlags(0x07u);

        // Set mode first.
        m_cdrom.writeParam(static_cast<u8>(a2 & 0xFF));
        m_cdrom.writeCommand(CDCMD_SETMODE);
        m_cdrom.writeInterruptFlags(0x07u);

        // Start reading.
        m_cdrom.writeCommand(CDCMD_READN);

        regs[2] = 1;
        {
            std::ostringstream msg;
            msg << "CdAsyncReadSector (A0 0x7E) count=" << a0 << " dst=0x" << std::hex << a1
                << " mode=0x" << (a2 & 0xFF);
            m_logger.log(LogLevel::Debug, "bios", msg.str());
        }
        return true;
    }

    // ---------------------------------------------------------------
    // A0:81  CdAsyncSetMode
    //
    //   $a0 = mode byte (see PSX-SPX CdlSetmode)
    //
    // Issues a Setmode command.  INT3 → CommandDone on completion.
    // ---------------------------------------------------------------
    case 0x81:
    {
        m_cdrom.writeInterruptFlags(0x07u);
        m_cdrom.writeParam(static_cast<u8>(a0 & 0xFF));
        m_cdrom.writeCommand(CDCMD_SETMODE);
        m_events.deliverByClassSpec(EventClass::Cdrom, EventSpec::CommandDone);
        regs[2] = 1;
        {
            std::ostringstream msg;
            msg << "CdAsyncSetMode (A0 0x81) mode=0x" << std::hex << (a0 & 0xFF);
            m_logger.log(LogLevel::Debug, "bios", msg.str());
        }
        return true;
    }

    // ---------------------------------------------------------------
    // A0:95  CdInitSubFunc
    //
    // Initialises the internal sub-function table used by the BIOS
    // CD interrupt handler.  On real hardware this patches a jump-
    // table in kernel RAM; here we just ensure _96_init has run.
    // ---------------------------------------------------------------
    case 0x95:
    {
        if (!m_biosCdrom.initialized)
        {
            initializeBiosCdromState(0u);
        }
        regs[2] = 1;
        m_logger.log(LogLevel::Debug, "bios", "CdInitSubFunc (A0 0x95)");
        return true;
    }

    default:
        return false;
    }
}

} // namespace runtime
} // namespace psxrecomp
