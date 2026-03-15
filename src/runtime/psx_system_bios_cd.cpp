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
bool traceCdCallbackEnabled()
{
    if (const char* env = std::getenv("PSXRECOMP_TRACE_CD_CALLBACK"))
    {
        return env[0] == '1';
    }
    return false;
}

/// CD-ROM command bytes (PSX-SPX nomenclature).
constexpr u8 CDCMD_GETSTAT = 0x01;
constexpr u8 CDCMD_SETLOC = 0x02;
constexpr u8 CDCMD_READN = 0x06;
constexpr u8 CDCMD_READS = 0x1B;
constexpr u8 CDCMD_INIT = 0x0A;
constexpr u8 CDCMD_SETMODE = 0x0E;
constexpr u8 CDCMD_SEEKL = 0x15;

/// PSX-SPX CdAsyncReadSector mode: compute effective bytes-per-sector.
///   bit4 set  → 0x918 (XA ADPCM/sub-header data)
///   bit5 set  → 0x924 (whole sector minus sync, 2340 bytes)
///   neither   → 0x800 (2048 bytes, user data only)
u32 computeCdAsyncSectorBytes(u32 mode)
{
    if (mode & 0x10u)
    {
        return 0x918u;
    }
    if (mode & 0x20u)
    {
        return 0x924u;
    }
    return 0x800u;
}

/// PSX-SPX CdAsyncReadSector mode: select read command.
///   bit8 set  → ReadS (0x1B, for streaming/XA sectors)
///   bit8 clear → ReadN (0x06, for normal sectors)
u8 computeCdAsyncReadCommand(u32 mode)
{
    return (mode & 0x100u) ? CDCMD_READS : CDCMD_READN;
}

void drainCdromResponse(Cdrom& cdrom)
{
    while ((cdrom.readStatus() & (1u << 5)) != 0u)
    {
        (void)cdrom.readResponse();
    }
}
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
        m_biosCdrom.asyncReadMode = 0;
        m_biosCdrom.asyncReadSectorBytes = 0;

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
        m_biosCdrom.asyncReadMode = 0;
        m_biosCdrom.asyncReadSectorBytes = 0;
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

        // Acknowledge and drain the Setloc response before issuing SeekL.
        m_cdrom.writeInterruptFlags(0x07u);
        drainCdromResponse(m_cdrom);
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
    //   $a2 = read mode (low 8 bits → Setmode; bit8 → ReadN vs ReadS)
    //
    // Issues Setmode(a2 & 0xFF), then ReadN or ReadS depending on
    // a2 bit8.  Effective bytes per sector are also mode-dependent:
    //   bit4 set  → 0x918 bytes
    //   bit5 set  → 0x924 bytes
    //   neither   → 0x800 bytes
    // Each INT1 (data-ready) causes the interrupt handler to copy one
    // sector of the computed size to the destination buffer.
    // ---------------------------------------------------------------
    case 0x7E:
    {
        const u8 readCmd = computeCdAsyncReadCommand(a2);
        const u32 sectorBytes = computeCdAsyncSectorBytes(a2);

        if (traceCdCallbackEnabled())
        {
            std::ostringstream msg;
            msg << "event=cd_async_read_sector_start"
                << " cmd=" << ((readCmd == CDCMD_READS) ? "ReadS" : "ReadN") << " mode=0x"
                << std::hex << a2 << " count=" << std::dec << a0 << " dst=0x" << std::hex << a1
                << " sector_bytes=" << sectorBytes;
            m_logger.log(LogLevel::Info, "cdcb_trace", msg.str());
        }

        m_biosCdrom.asyncReadBuffer = a1;
        m_biosCdrom.asyncReadCount = a0;
        m_biosCdrom.asyncSectorsRead = 0;
        m_biosCdrom.asyncReadMode = a2;
        m_biosCdrom.asyncReadSectorBytes = sectorBytes;

        const u64 requestedBytes = static_cast<u64>(a0) * sectorBytes;
        if (a0 == 0 || requestedBytes > MemoryMap::RAM_SIZE)
        {
            std::ostringstream warn;
            warn << "CdAsyncReadSector suspicious request sectors=" << std::dec << a0
                 << " bytes=" << requestedBytes << " dst=0x" << std::hex << a1;
            m_logger.log(LogLevel::Warn, "load", warn.str());
        }
        else
        {
            const RamCopyBounds bounds = planRamCopy(a1, static_cast<u32>(requestedBytes));
            if (!bounds.destinationInRam || bounds.destinationOverflow)
            {
                logRamCopyWarning("CdAsyncReadSector", a1, static_cast<u32>(requestedBytes), bounds,
                                  static_cast<u32>(requestedBytes));
            }
        }

        m_cdrom.writeInterruptFlags(0x07u);

        // Set mode first (low 8 bits only — bit8 is ReadN/ReadS selector, not a Setmode bit).
        m_cdrom.writeParam(static_cast<u8>(a2 & 0xFFu));
        m_cdrom.writeCommand(CDCMD_SETMODE);
        m_cdrom.writeInterruptFlags(0x07u);
        drainCdromResponse(m_cdrom);

        // Start reading with the mode-selected command.
        m_cdrom.writeCommand(readCmd);

        regs[2] = 1;
        {
            std::ostringstream msg;
            msg << "CdAsyncReadSector (A0 0x7E) count=" << std::dec << a0 << " dst=0x" << std::hex
                << a1 << " mode=0x" << a2 << " cmd=0x" << static_cast<unsigned>(readCmd)
                << " sectorBytes=0x" << sectorBytes;
            m_logger.log(LogLevel::Debug, "bios", msg.str());
        }
        return true;
    }

    // ---------------------------------------------------------------
    // A0:81  CdAsyncSetMode
    //
    //   $a0 = mode byte (see PSX-SPX CdlSetmode)
    //
    // Issues a Setmode command. INT3 is the command-acknowledge response.
    // ---------------------------------------------------------------
    case 0x81:
    {
        m_cdrom.writeInterruptFlags(0x07u);
        m_cdrom.writeParam(static_cast<u8>(a0 & 0xFF));
        m_cdrom.writeCommand(CDCMD_SETMODE);
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
