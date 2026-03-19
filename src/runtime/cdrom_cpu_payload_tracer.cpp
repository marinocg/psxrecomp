// PR-RV28: Post-stream CPU payload tracer.
//
// Records the exact byte layout of every CPU-visible sector delivered after
// the most recent XA-enabled ReadS/ReadN (up to CPU_RECORD_CAPACITY = 32).
//
// Capture point:  publishNextInterruptEvent() — INT1 + buffered sector present.
// Finalization:   next INT1 writes back dma3Started/drained flags.
//
// Payload mode encoding:
//   0 = user2048   — Mode1 or Mode2/Form1 user data (2048 bytes from raw[24])
//   1 = form2-2324 — Mode2/Form2 non-ADPCM (2324 bytes from raw[24])
//   2 = raw2340    — Whole-sector mode (2340 bytes from raw[12])
//   3 = other      — Unexpected size

#include "psxrecomp/runtime/cdrom.h"

#include "cdrom_shared.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace psxrecomp
{
namespace runtime
{

void Cdrom::snapshotCpuSector(u32 lba, const std::vector<u8>& sector)
{
    if (!m_streamStarted || m_cpuRecordCount >= CPU_RECORD_CAPACITY || sector.empty())
    {
        return;
    }

    CpuSectorRecord& rec = m_cpuRecords[m_cpuRecordCount++];
    rec = CpuSectorRecord{};
    rec.lba = lba;
    rec.fifoBytes = sector.size();

    if (sector.size() == cdrom_detail::USER_SECTOR_BYTES)
    {
        rec.payloadMode = 0; // user2048
    }
    else if (sector.size() == cdrom_detail::XA_FORM2_USER_BYTES)
    {
        rec.payloadMode = 1; // form2-2324
    }
    else if (sector.size() == cdrom_detail::WHOLE_SECTOR_BYTES)
    {
        rec.payloadMode = 2; // raw2340
    }
    else
    {
        rec.payloadMode = 3; // other
    }

    // XA subheader is only present in the payload for raw2340 mode.
    // In raw2340: sector[0..11] = sync, sector[12..15] = addr+mode,
    // sector[4] = subheader[0] (file), at raw offset RAW_SUBHEADER_OFFSET(16)
    // relative to raw[0], so at sector[16-12=4..7].
    if (rec.payloadMode == 2 && sector.size() >= 12u)
    {
        // Subheader at offset 4 (raw offset 16 - RAW_SYNC_OFFSET 12).
        // Validate duplicate: bytes [4..7] must equal [8..11].
        if (sector[4] == sector[8] && sector[5] == sector[9] &&
            sector[6] == sector[10] && sector[7] == sector[11])
        {
            rec.hasXaSub  = true;
            rec.xaFile    = sector[4];
            rec.xaChannel = sector[5];
            rec.xaSubmode = sector[6];
            rec.xaCoding  = sector[7];
        }
    }

    // Capture first and last 16 bytes of the payload.
    const size_t n = sector.size();
    const size_t take = std::min(n, size_t{16});
    for (size_t i = 0; i < take; ++i)
    {
        rec.firstBytes[i] = sector[i];
    }
    if (n > 16u)
    {
        const size_t lastOff = n - 16u;
        for (size_t i = 0; i < 16u; ++i)
        {
            rec.lastBytes[i] = sector[lastOff + i];
        }
    }
    else
    {
        rec.lastBytes = rec.firstBytes;
    }
}

std::string Cdrom::formatCpuPayloadSummary() const
{
    if (!m_streamStarted || m_cpuRecordCount == 0)
    {
        return "(no post-stream CPU sectors recorded)\n";
    }

    // Finalize the last record with current flags if not yet done.
    const bool lastFinalized = m_cpuRecords[m_cpuRecordCount - 1].finalized;
    const bool lastDma3 = lastFinalized ? m_cpuRecords[m_cpuRecordCount - 1].dma3Started
                                        : m_phaseFirstDmaFired;
    const bool lastDrained = lastFinalized ? m_cpuRecords[m_cpuRecordCount - 1].drained
                                           : m_phaseDrainFired;

    std::ostringstream os;
    os << "=== Post-Stream CPU Payload Summary ===\n";

    // Count per-mode occurrences.
    u32 modeCounts[4] = {0, 0, 0, 0};
    for (size_t i = 0; i < m_cpuRecordCount; ++i)
    {
        ++modeCounts[m_cpuRecords[i].payloadMode & 3u];
    }

    static const char* const kModeLabels[4] = {"user2048", "form2-2324", "raw2340", "other"};

    u8 dominant = 0;
    for (u8 m = 1; m < 4; ++m)
    {
        if (modeCounts[m] > modeCounts[dominant]) dominant = m;
    }
    bool mixed = false;
    for (u8 m = 0; m < 4; ++m)
    {
        if (m != dominant && modeCounts[m] > 0) { mixed = true; break; }
    }

    os << "  sectors_recorded:    " << m_cpuRecordCount << "\n";
    os << "  payload_mode:        " << (mixed ? "mixed" : kModeLabels[dominant]) << "\n";
    for (u8 m = 0; m < 4; ++m)
    {
        if (modeCounts[m] > 0)
        {
            os << "    " << kModeLabels[m] << ": " << modeCounts[m] << "\n";
        }
    }

    // Per-sector detail — first 8 records maximum.
    const size_t detail = std::min(m_cpuRecordCount, size_t{8});
    os << "  sector_detail (first " << detail << " of " << m_cpuRecordCount << "):\n";

    for (size_t i = 0; i < detail; ++i)
    {
        const CpuSectorRecord& r = m_cpuRecords[i];
        const bool isDma3  = (i + 1 == m_cpuRecordCount) ? lastDma3  : r.dma3Started;
        const bool isDrain = (i + 1 == m_cpuRecordCount) ? lastDrained : r.drained;

        os << "    [" << i << "] lba=" << r.lba
           << " size=" << r.fifoBytes
           << " mode=" << kModeLabels[r.payloadMode & 3u];
        os << " dma3=" << (isDma3 ? "yes" : "no")
           << " drain=" << (isDrain ? "yes" : "no");
        if (r.hasXaSub)
        {
            os << " xa{f=" << unsigned(r.xaFile)
               << ",ch=" << unsigned(r.xaChannel)
               << ",sub=0x" << std::hex << unsigned(r.xaSubmode) << std::dec
               << ",cod=0x" << std::hex << unsigned(r.xaCoding) << std::dec << "}";
        }
        os << "\n";

        os << "      first16: ";
        for (size_t b = 0; b < 16; ++b)
        {
            os << " " << std::hex << std::setw(2) << std::setfill('0')
               << unsigned(r.firstBytes[b]);
        }
        os << "\n";

        os << "      last16: ";
        for (size_t b = 0; b < 16; ++b)
        {
            os << " " << std::hex << std::setw(2) << std::setfill('0')
               << unsigned(r.lastBytes[b]);
        }
        os << std::dec << "\n";

        const size_t finalOff = r.finalized ? r.finalOffset : m_dataFifoConsumedBytes;
        const size_t unread = (r.fifoBytes > finalOff) ? (r.fifoBytes - finalOff) : 0u;
        os << "      read_window: cpu_start=";
        if (r.cpuFirstOffset == ~size_t{0}) { os << "none"; } else { os << r.cpuFirstOffset; }
        os << " cpu_bytes=" << r.cpuBytesRead << " dma_start=";
        if (r.dmaFirstOffset == ~size_t{0}) { os << "none"; } else { os << r.dmaFirstOffset; }
        os << " dma_bytes=" << r.dmaBytesRead
           << " final=" << finalOff << " unread=" << unread << "\n";
    }

    // Access pattern summary across all recorded sectors.
    u32 cpuOnlyCount = 0;
    u32 dmaOnlyCount = 0;
    u32 mixedCount = 0;
    u32 unreadCount = 0;
    for (size_t i = 0; i < m_cpuRecordCount; ++i)
    {
        const CpuSectorRecord& r = m_cpuRecords[i];
        const bool hasCpu = r.cpuBytesRead > 0;
        const bool hasDma = r.dmaBytesRead > 0;
        if      ( hasCpu && !hasDma) ++cpuOnlyCount;
        else if (!hasCpu &&  hasDma) ++dmaOnlyCount;
        else if ( hasCpu &&  hasDma) ++mixedCount;
        else                         ++unreadCount;
    }
    os << "  access_patterns: cpu_only=" << cpuOnlyCount
       << " dma_only=" << dmaOnlyCount
       << " mixed=" << mixedCount
       << " unread=" << unreadCount << "\n";

    return os.str();
}

} // namespace runtime
} // namespace psxrecomp
