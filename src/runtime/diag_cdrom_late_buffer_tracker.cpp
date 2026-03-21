#include "psxrecomp/runtime/diag_cdrom_late_buffer_tracker.h"

#include <iomanip>
#include <sstream>

namespace psxrecomp
{
namespace runtime
{

namespace
{
constexpr Address REV2_HEADER_BUFFER_START = 0x80166490u;
constexpr Address REV2_HEADER_BUFFER_END = 0x801664A0u;
constexpr Address REV2_ROLLING_BUFFER_START = 0x80025000u;
constexpr Address REV2_ROLLING_BUFFER_END = 0x80028000u;
constexpr Address REV2_FRAME_BUFFER_START = 0x80186000u;
constexpr Address REV2_FRAME_BUFFER_END = 0x80188000u;

bool rangesOverlap(Address aStart, Address aSize, Address bStart, Address bSize)
{
    const uint64_t aEnd = static_cast<uint64_t>(aStart) + static_cast<uint64_t>(aSize);
    const uint64_t bEnd = static_cast<uint64_t>(bStart) + static_cast<uint64_t>(bSize);
    return static_cast<uint64_t>(aStart) < bEnd && static_cast<uint64_t>(bStart) < aEnd;
}
} // namespace

void DiagCdromLateBufferTracker::setEnabled(bool enabled)
{
    m_enabled = enabled;
    reset();
}

bool DiagCdromLateBufferTracker::isEnabled() const
{
    return m_enabled;
}

void DiagCdromLateBufferTracker::reset()
{
    m_records = {};
    m_recordHead = 0;
    m_recordCount = 0;
    m_inFlight = {};
    m_inFlightValid = false;
    m_inFlightCapturedBytes = 0;
}

void DiagCdromLateBufferTracker::beginCdromDma(u32 sourceGeneration, u32 sourceLba,
                                               Address destination, u32 requestedBytes)
{
    if (!m_enabled)
    {
        return;
    }

    m_inFlight = {};
    m_inFlight.sourceGeneration = sourceGeneration;
    m_inFlight.sourceLba = sourceLba;
    m_inFlight.destination = destination;
    m_inFlight.requestedBytes = requestedBytes;
    m_inFlightValid = true;
    m_inFlightCapturedBytes = 0;
}

void DiagCdromLateBufferTracker::noteCdromDmaWord(u32 value)
{
    if (!m_enabled || !m_inFlightValid || m_inFlightCapturedBytes >= m_inFlight.firstBytes.size())
    {
        return;
    }

    for (u32 byteIndex = 0; byteIndex < sizeof(u32); ++byteIndex)
    {
        if (m_inFlightCapturedBytes >= m_inFlight.firstBytes.size())
        {
            break;
        }
        m_inFlight.firstBytes[m_inFlightCapturedBytes] =
            static_cast<u8>((value >> (byteIndex * 8)) & 0xFFu);
        ++m_inFlightCapturedBytes;
    }
    m_inFlight.capturedBytes = m_inFlightCapturedBytes;
}

void DiagCdromLateBufferTracker::endCdromDma(u32 actualBytes)
{
    if (!m_enabled || !m_inFlightValid)
    {
        return;
    }

    m_inFlight.actualBytes = actualBytes;
    if (actualBytes >= MIN_SECTOR_SIZED_TRANSFER_BYTES)
    {
        m_records[m_recordHead] = m_inFlight;
        m_recordHead = (m_recordHead + 1u) % RECORD_CAPACITY;
        if (m_recordCount < RECORD_CAPACITY)
        {
            ++m_recordCount;
        }
    }

    m_inFlight = {};
    m_inFlightValid = false;
    m_inFlightCapturedBytes = 0;
}

void DiagCdromLateBufferTracker::noteCpuRead(Address readerPc, Address address, u8 size)
{
    if (!m_enabled || size == 0 || m_recordCount == 0)
    {
        return;
    }

    for (size_t i = 0; i < m_recordCount; ++i)
    {
        const size_t index = (m_recordHead + RECORD_CAPACITY - 1u - i) % RECORD_CAPACITY;
        Record& record = m_records[index];
        if (record.actualBytes < MIN_SECTOR_SIZED_TRANSFER_BYTES)
        {
            continue;
        }
        if (!rangesOverlap(record.destination, record.actualBytes, address, size))
        {
            continue;
        }

        if (!record.laterCpuRead)
        {
            record.laterCpuRead = true;
            record.firstReaderPc = readerPc;
            record.firstReadAddress = address;
            record.consumer = classifyConsumer(readerPc);
        }
        const ConsumerClass consumer = classifyConsumer(readerPc);
        if (!record.laterParserRead &&
            (consumer == ConsumerClass::ParserHotLoop || consumer == ConsumerClass::RequestPath ||
             consumer == ConsumerClass::AckPath))
        {
            record.laterParserRead = true;
            record.parserConsumer = consumer;
            record.firstParserReadPc = readerPc;
            record.firstParserReadAddress = address;
        }
        break;
    }
}

size_t DiagCdromLateBufferTracker::recordCount() const
{
    return m_recordCount;
}

const DiagCdromLateBufferTracker::Record*
DiagCdromLateBufferTracker::recentRecord(size_t index) const
{
    if (index >= m_recordCount)
    {
        return nullptr;
    }

    const size_t physical = (m_recordHead + RECORD_CAPACITY - 1u - index) % RECORD_CAPACITY;
    return &m_records[physical];
}

DiagCdromLateBufferTracker::DestinationClass
DiagCdromLateBufferTracker::classifyDestination(Address destination) const
{
    if (destination >= REV2_HEADER_BUFFER_START && destination < REV2_HEADER_BUFFER_END)
    {
        return DestinationClass::HeaderBuffer;
    }
    if (destination >= REV2_ROLLING_BUFFER_START && destination < REV2_ROLLING_BUFFER_END)
    {
        return DestinationClass::RollingSectorBuffer;
    }
    if (destination >= REV2_FRAME_BUFFER_START && destination < REV2_FRAME_BUFFER_END)
    {
        return DestinationClass::FrameAssemblyBuffer;
    }
    return DestinationClass::Unknown;
}

DiagCdromLateBufferTracker::ConsumerClass DiagCdromLateBufferTracker::classifyConsumer(Address pc)
{
    if (pc >= 0x154718u && pc <= 0x154790u)
    {
        return ConsumerClass::ParserHotLoop;
    }
    if (pc >= 0x15D1A8u && pc <= 0x15D24Cu)
    {
        return ConsumerClass::RequestPath;
    }
    if (pc >= 0x15ABF8u && pc <= 0x15ACF4u)
    {
        return ConsumerClass::AckPath;
    }
    if (pc != 0)
    {
        return ConsumerClass::Other;
    }
    return ConsumerClass::None;
}

const char* DiagCdromLateBufferTracker::destinationClassLabel(DestinationClass value)
{
    switch (value)
    {
    case DestinationClass::HeaderBuffer:
        return "header_buffer";
    case DestinationClass::RollingSectorBuffer:
        return "rolling_sector_buffer";
    case DestinationClass::FrameAssemblyBuffer:
        return "frame_assembly_buffer";
    default:
        return "unknown";
    }
}

const char* DiagCdromLateBufferTracker::consumerClassLabel(ConsumerClass value)
{
    switch (value)
    {
    case ConsumerClass::ParserHotLoop:
        return "parser_hotloop";
    case ConsumerClass::RequestPath:
        return "request_path";
    case ConsumerClass::AckPath:
        return "ack_path";
    case ConsumerClass::Other:
        return "other";
    default:
        return "none";
    }
}

std::string DiagCdromLateBufferTracker::formatSummary() const
{
    std::ostringstream os;
    os << "=== CDROM Late Buffer Summary ===\n";
    if (m_recordCount == 0)
    {
        os << "  none\n";
        return os.str();
    }

    const Record* lastMeaningful = nullptr;
    os << "  recent sector-sized DMA3 writes (newest first):\n";
    for (size_t i = 0; i < m_recordCount; ++i)
    {
        const Record& record = *recentRecord(i);
        if (lastMeaningful == nullptr && (record.laterParserRead || record.laterCpuRead))
        {
            lastMeaningful = &record;
        }

        os << "    gen=" << record.sourceGeneration << " lba=" << record.sourceLba << " dst=0x"
           << std::hex << record.destination << std::dec << " len=" << record.actualBytes
           << " class=" << destinationClassLabel(classifyDestination(record.destination))
           << " later_read=" << (record.laterCpuRead ? "yes" : "no")
           << " parser_read=" << (record.laterParserRead ? "yes" : "no");
        if (record.laterCpuRead)
        {
            os << " consumer=" << consumerClassLabel(record.consumer) << " first_reader_pc=0x"
               << std::hex << record.firstReaderPc << " first_read_addr=0x"
               << record.firstReadAddress << std::dec;
        }
        if (record.laterParserRead)
        {
            os << " parser_consumer=" << consumerClassLabel(record.parserConsumer)
               << " first_parser_pc=0x" << std::hex << record.firstParserReadPc
               << " first_parser_addr=0x" << record.firstParserReadAddress << std::dec;
        }
        os << "\n";

        os << "      payload16=";
        const size_t payloadBytes =
            std::min<size_t>(record.capturedBytes, record.firstBytes.size());
        if (payloadBytes == 0)
        {
            os << "none";
        }
        else
        {
            for (size_t byteIndex = 0; byteIndex < payloadBytes; ++byteIndex)
            {
                if (byteIndex != 0)
                {
                    os << ' ';
                }
                os << std::hex << std::setw(2) << std::setfill('0')
                   << static_cast<unsigned>(record.firstBytes[byteIndex]);
            }
            os << std::dec << std::setfill(' ');
        }
        os << "\n";
    }

    if (lastMeaningful != nullptr)
    {
        os << "  last_meaningful_sector: gen=" << lastMeaningful->sourceGeneration
           << " lba=" << lastMeaningful->sourceLba << " dst=0x" << std::hex
           << lastMeaningful->destination << std::dec
           << " class=" << destinationClassLabel(classifyDestination(lastMeaningful->destination))
           << " consumer="
           << consumerClassLabel(lastMeaningful->laterParserRead ? lastMeaningful->parserConsumer
                                                                 : lastMeaningful->consumer)
           << " parser_read=" << (lastMeaningful->laterParserRead ? "yes" : "no") << "\n";
    }
    else
    {
        os << "  last_meaningful_sector: none\n";
    }

    return os.str();
}

} // namespace runtime
} // namespace psxrecomp
