// PR-RV28/37: Rolling CPU payload tracer.
//
// Retains a rolling ring of the last 32 accepted/read CPU-visible sectors
// observed while ReadS/ReadN capture is active. A sector starts as a pending
// INT1 publication snapshot and is committed only if software actually accepts
// it (BFRD/enableDataRead) or consumes bytes from it. The ring survives later
// ReadS/ReadN restarts so a fresh unread sector does not hide the late-stream
// DMA pattern that immediately preceded it.

#include "psxrecomp/runtime/cdrom.h"

#include "cdrom_shared.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <vector>

namespace psxrecomp
{
namespace runtime
{
namespace
{
constexpr size_t kNoOffset = ~size_t{0};
constexpr size_t kCpuRecordCapacity = 32u;

size_t cpuRecordPhysicalIndex(size_t head, size_t count, size_t logicalIndex)
{
    const size_t oldest = (head + kCpuRecordCapacity - count) % kCpuRecordCapacity;
    return (oldest + logicalIndex) % kCpuRecordCapacity;
}

const char* payloadModeLabel(u8 payloadMode)
{
    static const char* const kLabels[4] = {"user2048", "form2-2324", "raw2340", "other"};
    return kLabels[payloadMode & 3u];
}

void appendHexBytes(std::ostringstream& os, const std::array<u8, 16>& bytes)
{
    for (u8 byte : bytes)
    {
        os << " " << std::hex << std::setw(2) << std::setfill('0') << unsigned(byte);
    }
    os << std::dec;
}
} // namespace

Cdrom::CpuSectorRecord* Cdrom::currentCpuPayloadRecord()
{
    return m_drainingCpuRecordValid ? &m_drainingCpuRecord : nullptr;
}

const Cdrom::CpuSectorRecord* Cdrom::currentCpuPayloadRecord() const
{
    return m_drainingCpuRecordValid ? &m_drainingCpuRecord : nullptr;
}

void Cdrom::beginCpuPayloadRecord(u32 int1PublishLba, const std::vector<u8>& sector)
{
    m_publishedCpuRecord = {};
    m_publishedCpuRecordValid = false;

    if (!m_cpuPayloadCaptureActive || sector.empty())
    {
        return;
    }

    CpuSectorRecord& record = m_publishedCpuRecord;
    record.int1PublishLba = int1PublishLba;
    record.fifoBytes = sector.size();

    if (sector.size() == cdrom_detail::USER_SECTOR_BYTES)
    {
        record.payloadMode = 0;
    }
    else if (sector.size() == cdrom_detail::XA_FORM2_USER_BYTES)
    {
        record.payloadMode = 1;
    }
    else if (sector.size() == cdrom_detail::WHOLE_SECTOR_BYTES)
    {
        record.payloadMode = 2;
    }
    else
    {
        record.payloadMode = 3;
    }

    if (record.payloadMode == 2 && sector.size() >= 12u && sector[4] == sector[8] &&
        sector[5] == sector[9] && sector[6] == sector[10] && sector[7] == sector[11])
    {
        record.hasXaSub = true;
        record.xaFile = sector[4];
        record.xaChannel = sector[5];
        record.xaSubmode = sector[6];
        record.xaCoding = sector[7];
    }

    const size_t copyCount = std::min(sector.size(), size_t{16});
    for (size_t i = 0; i < copyCount; ++i)
    {
        record.firstBytes[i] = sector[i];
    }
    if (sector.size() > 16u)
    {
        const size_t tailOffset = sector.size() - 16u;
        for (size_t i = 0; i < 16u; ++i)
        {
            record.lastBytes[i] = sector[tailOffset + i];
        }
    }
    else
    {
        record.lastBytes = record.firstBytes;
    }

    m_publishedCpuRecordValid = true;
}

void Cdrom::noteCpuPayloadAccepted(u32 acceptedLba)
{
    if (m_drainingCpuRecordValid || !m_publishedCpuRecordValid)
    {
        return;
    }

    m_drainingCpuRecord = m_publishedCpuRecord;
    m_drainingCpuRecord.accepted = true;
    m_drainingCpuRecord.acceptedLba = acceptedLba;
    m_drainingCpuRecord.finalOffset = 0;
    m_drainingCpuRecord.nextPublishOffset = kNoOffset;
    m_drainingCpuRecordValid = true;
}

void Cdrom::noteCpuPayloadSuperseded()
{
    if (!m_drainingCpuRecordValid || m_drainingCpuRecord.nextPublishOffset != kNoOffset)
    {
        return;
    }

    m_drainingCpuRecord.nextPublishOffset = m_dataFifoConsumedBytes;
    m_drainingCpuRecord.superseded =
        m_drainingCpuRecord.accepted &&
        m_drainingCpuRecord.nextPublishOffset < m_drainingCpuRecord.fifoBytes;
}

void Cdrom::finalizeCpuPayloadRecord(bool supersededByNextInt1)
{
    if (supersededByNextInt1)
    {
        noteCpuPayloadSuperseded();
    }

    if (!m_drainingCpuRecordValid)
    {
        return;
    }

    const auto shouldRetain = [](const CpuSectorRecord& record)
    { return record.accepted || record.cpuBytesRead > 0 || record.dmaBytesRead > 0; };

    CpuSectorRecord record = m_drainingCpuRecord;
    record.dma3Started = m_phaseFirstDmaFired;
    record.drained = m_phaseDrainFired;
    record.finalOffset = m_dataFifoConsumedBytes;
    record.finalized = true;
    if (record.nextPublishOffset == kNoOffset && supersededByNextInt1)
    {
        record.nextPublishOffset = m_dataFifoConsumedBytes;
    }
    record.superseded = record.accepted && record.nextPublishOffset != kNoOffset &&
                        record.nextPublishOffset < record.fifoBytes;

    if (shouldRetain(record))
    {
        m_cpuRecords[m_cpuRecordHead] = record;
        m_cpuRecordHead = (m_cpuRecordHead + 1u) % CPU_RECORD_CAPACITY;
        if (m_cpuRecordCount < CPU_RECORD_CAPACITY)
        {
            ++m_cpuRecordCount;
        }
    }

    m_drainingCpuRecord = {};
    m_drainingCpuRecordValid = false;
}

std::string Cdrom::formatCpuPayloadSummary() const
{
    const auto shouldRetain = [](const CpuSectorRecord& record)
    { return record.accepted || record.cpuBytesRead > 0 || record.dmaBytesRead > 0; };

    std::vector<CpuSectorRecord> records;
    records.reserve(m_cpuRecordCount + 1u);
    for (size_t i = 0; i < m_cpuRecordCount; ++i)
    {
        records.push_back(
            m_cpuRecords[cpuRecordPhysicalIndex(m_cpuRecordHead, m_cpuRecordCount, i)]);
    }

    if (m_drainingCpuRecordValid && shouldRetain(m_drainingCpuRecord))
    {
        CpuSectorRecord live = m_drainingCpuRecord;
        live.dma3Started = m_phaseFirstDmaFired;
        live.drained = m_phaseDrainFired;
        live.finalOffset = m_dataFifoConsumedBytes;
        live.finalized = false;
        live.superseded = live.accepted && live.nextPublishOffset != kNoOffset &&
                          live.nextPublishOffset < live.fifoBytes;
        records.push_back(live);
    }

    if (records.size() > CPU_RECORD_CAPACITY)
    {
        records.erase(records.begin(), records.begin() + static_cast<std::ptrdiff_t>(
                                                             records.size() - CPU_RECORD_CAPACITY));
    }

    if (records.empty())
    {
        return "(no post-stream CPU sectors recorded)\n";
    }

    u32 modeCounts[4] = {0, 0, 0, 0};
    u32 supersededCount = 0;
    u32 acceptedFirst = 0;
    u32 acceptedLast = 0;
    bool haveAcceptedRange = false;

    for (const CpuSectorRecord& record : records)
    {
        ++modeCounts[record.payloadMode & 3u];
        if (record.superseded)
        {
            ++supersededCount;
        }
        if (record.accepted)
        {
            if (!haveAcceptedRange)
            {
                acceptedFirst = record.acceptedLba;
                acceptedLast = record.acceptedLba;
                haveAcceptedRange = true;
            }
            else
            {
                acceptedFirst = std::min(acceptedFirst, record.acceptedLba);
                acceptedLast = std::max(acceptedLast, record.acceptedLba);
            }
        }
    }

    u8 dominantMode = 0;
    for (u8 mode = 1; mode < 4; ++mode)
    {
        if (modeCounts[mode] > modeCounts[dominantMode])
        {
            dominantMode = mode;
        }
    }

    bool mixedModes = false;
    for (u8 mode = 0; mode < 4; ++mode)
    {
        if (mode != dominantMode && modeCounts[mode] > 0)
        {
            mixedModes = true;
            break;
        }
    }

    std::ostringstream os;
    os << "=== Post-Stream CPU Payload Summary ===\n";
    os << "  sectors_recorded:    " << records.size() << "\n";
    os << "  retention:           last " << CPU_RECORD_CAPACITY << " accepted/read sectors\n";
    os << "  int1_publish_lba_range: " << records.front().int1PublishLba << ".."
       << records.back().int1PublishLba << "\n";
    os << "  accepted_lba_range:     ";
    if (haveAcceptedRange)
    {
        os << acceptedFirst << ".." << acceptedLast << "\n";
    }
    else
    {
        os << "none\n";
    }
    os << "  superseded:         " << supersededCount << "\n";
    os << "  payload_mode:        " << (mixedModes ? "mixed" : payloadModeLabel(dominantMode))
       << "\n";
    for (u8 mode = 0; mode < 4; ++mode)
    {
        if (modeCounts[mode] > 0)
        {
            os << "    " << payloadModeLabel(mode) << ": " << modeCounts[mode] << "\n";
        }
    }

    const size_t detailCount = std::min(records.size(), size_t{8});
    const size_t detailStart = records.size() - detailCount;
    os << "  sector_detail (last " << detailCount << " of " << records.size() << "):\n";
    for (size_t i = detailStart; i < records.size(); ++i)
    {
        const CpuSectorRecord& record = records[i];
        const size_t finalOffset = record.finalOffset;
        const size_t unreadBytes =
            record.fifoBytes > finalOffset ? (record.fifoBytes - finalOffset) : 0u;

        os << "    [" << i << "] int1_publish_lba=" << record.int1PublishLba << " accepted_lba=";
        if (record.accepted)
        {
            os << record.acceptedLba;
        }
        else
        {
            os << "none";
        }
        os << " size=" << record.fifoBytes << " mode=" << payloadModeLabel(record.payloadMode)
           << " dma3=" << (record.dma3Started ? "yes" : "no")
           << " drain=" << (record.drained ? "yes" : "no")
           << " superseded=" << (record.superseded ? "yes" : "no");
        if (record.hasXaSub)
        {
            os << " xa{f=" << unsigned(record.xaFile) << ",ch=" << unsigned(record.xaChannel)
               << ",sub=0x" << std::hex << unsigned(record.xaSubmode) << std::dec << ",cod=0x"
               << std::hex << unsigned(record.xaCoding) << std::dec << "}";
        }
        os << "\n";

        os << "      first16:";
        appendHexBytes(os, record.firstBytes);
        os << "\n";

        os << "      last16:";
        appendHexBytes(os, record.lastBytes);
        os << "\n";

        os << "      read_window: cpu_start=";
        if (record.cpuFirstOffset == kNoOffset)
        {
            os << "none";
        }
        else
        {
            os << record.cpuFirstOffset;
        }
        os << " cpu_bytes=" << record.cpuBytesRead << " dma_start=";
        if (record.dmaFirstOffset == kNoOffset)
        {
            os << "none";
        }
        else
        {
            os << record.dmaFirstOffset;
        }
        os << " dma_bytes=" << record.dmaBytesRead << " next_publish=";
        if (record.nextPublishOffset == kNoOffset)
        {
            os << "none";
        }
        else
        {
            os << record.nextPublishOffset;
        }
        os << " final=" << finalOffset << " unread=" << unreadBytes << "\n";
    }

    u32 cpuOnlyCount = 0;
    u32 dmaOnlyCount = 0;
    u32 mixedCount = 0;
    u32 unreadCount = 0;
    for (const CpuSectorRecord& record : records)
    {
        const bool hasCpu = record.cpuBytesRead > 0;
        const bool hasDma = record.dmaBytesRead > 0;
        if (hasCpu && hasDma)
        {
            ++mixedCount;
        }
        else if (hasCpu)
        {
            ++cpuOnlyCount;
        }
        else if (hasDma)
        {
            ++dmaOnlyCount;
        }
        else
        {
            ++unreadCount;
        }
    }
    os << "  access_patterns: cpu_only=" << cpuOnlyCount << " dma_only=" << dmaOnlyCount
       << " mixed=" << mixedCount << " unread=" << unreadCount << "\n";

    return os.str();
}

} // namespace runtime
} // namespace psxrecomp
