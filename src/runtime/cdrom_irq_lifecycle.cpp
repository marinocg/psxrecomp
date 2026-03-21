#include "psxrecomp/runtime/cdrom.h"

#include "cdrom_shared.h"

#include <array>
#include <iomanip>
#include <sstream>

namespace psxrecomp
{
namespace runtime
{

namespace
{
constexpr size_t kNoInt1Record = ~size_t{0};
constexpr size_t kInt1RecordCapacity = 8u;

const char* irqLabel(u8 irqType)
{
    switch (irqType)
    {
    case 1:
        return "INT1";
    case 2:
        return "INT2";
    case 3:
        return "INT3";
    case 4:
        return "INT4";
    case 5:
        return "INT5";
    default:
        return "INT?";
    }
}

size_t int1RecordPhysicalIndex(size_t head, size_t count, size_t logicalIndex)
{
    const size_t oldest = (head + kInt1RecordCapacity - count) % kInt1RecordCapacity;
    return (oldest + logicalIndex) % kInt1RecordCapacity;
}
} // namespace

Cdrom::Int1GenerationRecord* Cdrom::findInt1RecordByGeneration(u32 publishGeneration)
{
    if (publishGeneration == 0)
    {
        return nullptr;
    }

    for (size_t i = 0; i < m_int1RecordCount; ++i)
    {
        Int1GenerationRecord& record =
            m_int1Records[int1RecordPhysicalIndex(m_int1RecordHead, m_int1RecordCount, i)];
        if (record.publishGeneration == publishGeneration)
        {
            return &record;
        }
    }

    return nullptr;
}

const Cdrom::Int1GenerationRecord* Cdrom::findInt1RecordByGeneration(u32 publishGeneration) const
{
    if (publishGeneration == 0)
    {
        return nullptr;
    }

    for (size_t i = 0; i < m_int1RecordCount; ++i)
    {
        const Int1GenerationRecord& record =
            m_int1Records[int1RecordPhysicalIndex(m_int1RecordHead, m_int1RecordCount, i)];
        if (record.publishGeneration == publishGeneration)
        {
            return &record;
        }
    }

    return nullptr;
}

void Cdrom::noteIrqCallbackDispatch(u8 irqType, bool callbacksDispatched)
{
    if (!callbacksDispatched || irqType < 1u || irqType > IRQ_LIFECYCLE_TYPE_COUNT)
    {
        return;
    }

    const IrqLifecycleRecord& record = m_irqLifecycle[irqType - 1u];
    if (record.publishGeneration == 0)
    {
        return;
    }

    if (m_lastCallbackDispatchType == irqType &&
        m_lastCallbackDispatchGeneration == record.publishGeneration)
    {
        ++m_irqLifecycle[irqType - 1u].callbackRedispatchCount;
    }

    m_lastCallbackDispatchType = irqType;
    m_lastCallbackDispatchGeneration = record.publishGeneration;
}

u32 Cdrom::irqPublishGeneration(u8 irqType) const
{
    if (irqType < 1u || irqType > IRQ_LIFECYCLE_TYPE_COUNT)
    {
        return 0;
    }
    return m_irqLifecycle[irqType - 1u].publishGeneration;
}

u32 Cdrom::currentDrainingInt1Generation() const
{
    return m_drainingInt1RecordValid ? m_drainingInt1PublishGeneration : 0u;
}

u32 Cdrom::currentDrainingLba() const
{
    return m_drainingLba;
}

void Cdrom::notePublishedInt1Generation(u32 publishGeneration)
{
    if (!m_loadedInt1RecordValid)
    {
        return;
    }

    Int1GenerationRecord record = m_loadedInt1Record;
    record.publishGeneration = publishGeneration;
    record.bfrdHighAtPublish = (m_requestControl & cdrom_detail::REQUEST_ENABLE_BUFFER_READ) != 0u;
    m_int1Records[m_int1RecordHead] = record;
    m_liveInt1PublishGeneration = publishGeneration;
    m_liveInt1RecordIndex = m_int1RecordHead;
    m_liveInt1RecordValid = true;
    m_int1RecordHead = (m_int1RecordHead + 1u) % INT1_RECORD_CAPACITY;
    if (m_int1RecordCount < INT1_RECORD_CAPACITY)
    {
        ++m_int1RecordCount;
    }
    m_publishedInt1RecordIndex = m_liveInt1RecordIndex;
    m_publishedInt1PublishGeneration = publishGeneration;
    m_publishedInt1RecordValid = true;
    m_liveInt1AcceptedByBiosAuto = false;
    m_loadedInt1Record = {};
    m_loadedInt1RecordValid = false;
}

void Cdrom::noteInt1BfrdRiseAfterPublish()
{
    if (!m_liveInt1RecordValid)
    {
        return;
    }

    if (Int1GenerationRecord* record = findInt1RecordByGeneration(m_liveInt1PublishGeneration))
    {
        record->sawBfrdAfterPublish = true;
    }
}

void Cdrom::beginInt1DmaTransfer(Address destinationBase)
{
    m_pendingInt1DmaDestination = destinationBase;
    m_pendingInt1DmaDestinationValid = true;
}

void Cdrom::endInt1DmaTransfer()
{
    m_pendingInt1DmaDestination = 0;
    m_pendingInt1DmaDestinationValid = false;
}

void Cdrom::noteInt1DmaBytes(u32 bytesTransferred)
{
    if (bytesTransferred == 0)
    {
        return;
    }

    if (m_drainingInt1RecordValid)
    {
        if (Int1GenerationRecord* record =
                findInt1RecordByGeneration(m_drainingInt1PublishGeneration))
        {
            record->firstDma = true;
            record->totalDmaBytes += bytesTransferred;
            if (!record->hasFirstDmaDestination && m_pendingInt1DmaDestinationValid)
            {
                record->firstDmaDestination = m_pendingInt1DmaDestination;
                record->hasFirstDmaDestination = true;
            }
            return;
        }
    }
}

void Cdrom::noteAckedInt1Generation(bool topLevelCdLineDeasserted)
{
    if (!m_liveInt1RecordValid)
    {
        return;
    }

    if (Int1GenerationRecord* record = findInt1RecordByGeneration(m_liveInt1PublishGeneration))
    {
        record->sawHclrctlAfterPublish = true;
        record->acked = true;
        record->topLevelCdLineDeasserted = topLevelCdLineDeasserted;
    }
}

std::string Cdrom::formatIrqLifecycleSummary() const
{
    std::ostringstream os;
    os << "=== CDROM IRQ Lifecycle Summary ===\n";
    for (u8 irqType = 1; irqType <= IRQ_LIFECYCLE_TYPE_COUNT; ++irqType)
    {
        const IrqLifecycleRecord& record = m_irqLifecycle[irqType - 1u];
        os << "  " << irqLabel(irqType) << " publish_gen=" << std::setw(3)
           << record.publishGeneration << " queued=" << std::setw(3) << record.queuedCount
           << " published=" << std::setw(3) << record.publishedCount << " acked=" << std::setw(3)
           << record.ackedCount << " deasserted=" << std::setw(3) << record.deassertedCount
           << " callback_redispatch_no_new_publish=" << record.callbackRedispatchCount << "\n";
    }

    const IrqLifecycleRecord& int4 = m_irqLifecycle[cdrom_detail::INT4 - 1u];
    os << "  INT4 published: " << int4.publishedCount << "\n";
    os << "  INT4 acked: " << int4.ackedCount << "\n";
    os << "  INT4 redispatched without new publish: " << int4.callbackRedispatchCount << "\n";
    os << "  INT4 HCLRCTL cleared active: " << (m_int4HclrctlClearCount != 0u ? "yes" : "no")
       << "\n";
    os << "  top-level CD IRQ deassert after INT4 ack: "
       << (m_int4TopLevelDeassertAfterAck ? "yes" : "no") << "\n";

    std::vector<Int1GenerationRecord> int1Records;
    int1Records.reserve(m_int1RecordCount);
    for (size_t i = 0; i < m_int1RecordCount; ++i)
    {
        int1Records.push_back(
            m_int1Records[int1RecordPhysicalIndex(m_int1RecordHead, m_int1RecordCount, i)]);
    }

    if (!int1Records.empty())
    {
        auto formatBfrdState = [](bool high) { return high ? "high" : "low"; };
        auto formatYesNo = [](bool value) { return value ? "yes" : "no"; };

        auto formatSubmode = [](const Int1GenerationRecord& record)
        {
            std::ostringstream s;
            if (record.hasXaSub)
            {
                s << "0x" << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<unsigned>(record.xaSubmode);
            }
            else
            {
                s << "none";
            }
            return s.str();
        };

        size_t lastAcked = kNoInt1Record;
        for (size_t i = 0; i < int1Records.size(); ++i)
        {
            if (int1Records[i].acked)
            {
                lastAcked = i;
            }
        }

        size_t firstUnacked = kNoInt1Record;
        const size_t searchStart = lastAcked == kNoInt1Record ? 0u : lastAcked + 1u;
        for (size_t i = searchStart; i < int1Records.size(); ++i)
        {
            if (!int1Records[i].acked)
            {
                firstUnacked = i;
                break;
            }
        }

        os << "  INT1 recent generations:\n";
        for (const Int1GenerationRecord& record : int1Records)
        {
            os << "    gen=" << record.publishGeneration << " lba=" << record.publishedLba
               << " publish_bfrd=" << formatBfrdState(record.bfrdHighAtPublish)
               << " bfrd_rose=" << formatYesNo(record.sawBfrdAfterPublish)
               << " accept=" << formatYesNo(record.accepted)
               << " accepted=" << (record.accepted ? std::to_string(record.acceptedLba) : "none")
               << " dma=" << formatYesNo(record.firstDma) << " dma_bytes=" << record.totalDmaBytes
               << " dma_dst=";
            if (record.hasFirstDmaDestination)
            {
                os << "0x" << std::hex << record.firstDmaDestination << std::dec;
            }
            else
            {
                os << "none";
            }
            os << " hclrctl=" << formatYesNo(record.sawHclrctlAfterPublish)
               << " acked=" << formatYesNo(record.acked)
               << " deassert=" << formatYesNo(record.topLevelCdLineDeasserted);
            if (record.hasXaSub)
            {
                os << " file=" << static_cast<unsigned>(record.xaFile)
                   << " channel=" << static_cast<unsigned>(record.xaChannel) << " submode=0x"
                   << std::hex << std::setw(2) << std::setfill('0')
                   << static_cast<unsigned>(record.xaSubmode) << " coding=0x" << std::setw(2)
                   << static_cast<unsigned>(record.xaCoding) << std::dec << std::setfill(' ');
            }
            else
            {
                os << " file=none channel=none submode=none coding=none";
            }
            os << " eor=" << (record.hasEor ? "yes" : "no")
               << " eof=" << (record.hasEof ? "yes" : "no") << "\n";
        }

        if (lastAcked != kNoInt1Record)
        {
            const Int1GenerationRecord& record = int1Records[lastAcked];
            os << "  last_acked_int1_lba: " << record.publishedLba << "\n";
            os << "  last_acked_submode: " << formatSubmode(record) << "\n";
        }
        else
        {
            os << "  last_acked_int1_lba: none\n";
            os << "  last_acked_submode: none\n";
        }

        if (firstUnacked != kNoInt1Record)
        {
            const Int1GenerationRecord& record = int1Records[firstUnacked];
            os << "  first_unacked_int1_lba: " << record.publishedLba << "\n";
            os << "  first_unacked_submode: " << formatSubmode(record) << "\n";
            os << "  first_unacked_int1_handshake: publish_bfrd="
               << formatBfrdState(record.bfrdHighAtPublish)
               << " bfrd_rose=" << formatYesNo(record.sawBfrdAfterPublish)
               << " accept=" << formatYesNo(record.accepted)
               << " dma=" << formatYesNo(record.firstDma) << " dma_bytes=" << record.totalDmaBytes
               << " dma_dst=";
            if (record.hasFirstDmaDestination)
            {
                os << "0x" << std::hex << record.firstDmaDestination << std::dec;
            }
            else
            {
                os << "none";
            }
            os << " hclrctl=" << formatYesNo(record.sawHclrctlAfterPublish)
               << " ack=" << formatYesNo(record.acked)
               << " top_level_cd_line_deassert=" << formatYesNo(record.topLevelCdLineDeasserted)
               << "\n";
        }
        else
        {
            os << "  first_unacked_int1_lba: none\n";
            os << "  first_unacked_submode: none\n";
            os << "  first_unacked_int1_handshake: none\n";
        }

        bool sameFileChannel = false;
        bool stalledOnSectorAfterEof = false;
        if (lastAcked != kNoInt1Record && firstUnacked != kNoInt1Record)
        {
            const Int1GenerationRecord& acked = int1Records[lastAcked];
            const Int1GenerationRecord& unacked = int1Records[firstUnacked];
            sameFileChannel = acked.hasXaSub && unacked.hasXaSub &&
                              acked.xaFile == unacked.xaFile &&
                              acked.xaChannel == unacked.xaChannel;
            stalledOnSectorAfterEof = (acked.hasEof || acked.hasEor) && sameFileChannel &&
                                      unacked.publishedLba == acked.publishedLba + 1u;
        }
        os << "  same_file_channel = " << (sameFileChannel ? "yes" : "no") << "\n";
        os << "  stalled_on_sector_after_eof = " << (stalledOnSectorAfterEof ? "yes" : "no")
           << "\n";
    }

    return os.str();
}

} // namespace runtime
} // namespace psxrecomp
