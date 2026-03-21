#pragma once

#include "psxrecomp/types.h"

#include <array>
#include <cstddef>
#include <string>

namespace psxrecomp
{
namespace runtime
{

class DiagCdromLateBufferTracker
{
  public:
    enum class DestinationClass : u8
    {
        HeaderBuffer,
        RollingSectorBuffer,
        FrameAssemblyBuffer,
        Unknown,
    };

    enum class ConsumerClass : u8
    {
        None,
        ParserHotLoop,
        RequestPath,
        AckPath,
        Other,
    };

    struct Record
    {
        u32 sourceGeneration = 0;
        u32 sourceLba = 0;
        Address destination = 0;
        u32 requestedBytes = 0;
        u32 actualBytes = 0;
        std::array<u8, 16> firstBytes{};
        u8 capturedBytes = 0;
        bool laterCpuRead = false;
        Address firstReaderPc = 0;
        Address firstReadAddress = 0;
        ConsumerClass consumer = ConsumerClass::None;
        bool laterParserRead = false;
        ConsumerClass parserConsumer = ConsumerClass::None;
        Address firstParserReadPc = 0;
        Address firstParserReadAddress = 0;
    };

    static constexpr size_t RECORD_CAPACITY = 8u;

    void setEnabled(bool enabled);
    bool isEnabled() const;

    void reset();

    void beginCdromDma(u32 sourceGeneration, u32 sourceLba, Address destination,
                       u32 requestedBytes);
    void noteCdromDmaWord(u32 value);
    void endCdromDma(u32 actualBytes);
    void noteCpuRead(Address readerPc, Address address, u8 size);

    size_t recordCount() const;
    const Record* recentRecord(size_t index) const;

    std::string formatSummary() const;

  private:
    static constexpr u32 MIN_SECTOR_SIZED_TRANSFER_BYTES = 2048u;

    DestinationClass classifyDestination(Address destination) const;
    static ConsumerClass classifyConsumer(Address pc);
    static const char* destinationClassLabel(DestinationClass value);
    static const char* consumerClassLabel(ConsumerClass value);

    bool m_enabled = false;
    std::array<Record, RECORD_CAPACITY> m_records{};
    size_t m_recordHead = 0;
    size_t m_recordCount = 0;
    Record m_inFlight{};
    bool m_inFlightValid = false;
    u8 m_inFlightCapturedBytes = 0;
};

} // namespace runtime
} // namespace psxrecomp
