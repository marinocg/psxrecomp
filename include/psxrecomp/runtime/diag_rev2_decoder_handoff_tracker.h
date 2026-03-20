#pragma once

#include "psxrecomp/types.h"

#include <array>
#include <cstddef>
#include <string>

namespace psxrecomp
{
namespace runtime
{

class PsxSystem;
class RuntimeLogger;

class DiagRev2DecoderHandoffTracker
{
  public:
    enum class WriteKind : u8
    {
        CpuStore,
        Dma,
        Memcpy,
        Memset,
    };

    struct RangeWriteEvent
    {
        bool valid = false;
        WriteKind kind = WriteKind::CpuStore;
        Address writerPc = 0;
        Address address = 0;
        u32 size = 0;
        std::string sourceTag;
        std::string detail;
        bool wroteAnyNonzero = false;
    };

    struct SelectorSnapshot
    {
        bool valid = false;
        Address capturePc = 0;
        Address payloadPointer = 0;
        Address descriptorPointer = 0;
        u16 descriptorState = 0;
        u32 descriptorLength = 0;
        u16 field16 = 0;
        u16 field18 = 0;
        u32 queueIndex = 0;
        bool payloadInExpectedArena = false;
        std::array<u8, 16> payloadBytes{};
        u8 payloadByteCount = 0;
    };

    struct DecoderSegment
    {
        bool valid = false;
        u32 segmentIndex = 0;
        Address inputPointer = 0;
        Address outputStart = 0;
        Address outputMax = 0;
        u32 iterationCount = 0;
        u32 descriptorLengthHint = 0;
        bool zeroInputAtEntry = false;
        bool exceededLengthHint = false;
    };

    static constexpr size_t INPUT_WINDOW_BYTES = 64u;
    static constexpr size_t SEGMENT_RING_CAPACITY = 4u;

    void setEnabled(bool enabled);
    bool isEnabled() const;

    void reset();

    void observePc(Address pc, const u32* regs, size_t regCount, const PsxSystem& system,
                   RuntimeLogger* logger);

    void noteScalarWrite(const PsxSystem& system, Address writerPc, Address address, u8 size,
                         u32 value, WriteKind kind);
    void noteBulkCopy(const PsxSystem& system, Address writerPc, Address destination,
                      const u8* source, u32 length, const std::string& sourceTag,
                      const std::string& detail);
    void noteBulkFill(const PsxSystem& system, Address writerPc, Address destination, u8 value,
                      u32 length, const std::string& sourceTag, const std::string& detail);

    std::string formatSummary() const;

  private:
    static constexpr Address INPUT_RANGE_START = 0x80070400u;
    static constexpr Address INPUT_RANGE_END = INPUT_RANGE_START + INPUT_WINDOW_BYTES;
    static constexpr Address EXPECTED_PAYLOAD_ARENA_START = 0x80070000u;
    static constexpr Address EXPECTED_PAYLOAD_ARENA_END = 0x80080000u;

    void noteRangeWrite(const PsxSystem& system, const RangeWriteEvent& event);
    void updateInputShadow(const PsxSystem& system);
    void captureSelectorSnapshot(Address capturePc, Address payloadPointer,
                                 Address descriptorPointer, u32 queueIndex,
                                 const PsxSystem& system);
    void captureCallerSnapshot(const u32* regs, size_t regCount, const PsxSystem& system);
    void beginDecoderSegment(const u32* regs, size_t regCount, const PsxSystem& system,
                             RuntimeLogger* logger);
    void updateDecoderLoopProgress(const u32* regs, size_t regCount);
    void finalizeDecoderSegment();
    void maybeEmitZeroPayloadGuard(RuntimeLogger* logger) const;
    std::string currentDiagnosis() const;

    static bool overlapsInputRange(Address address, u32 size);
    static const char* writeKindLabel(WriteKind kind);
    static bool isAllZero(const std::array<u8, INPUT_WINDOW_BYTES>& bytes);
    static bool anyNonzero(const std::array<u8, INPUT_WINDOW_BYTES>& bytes);

    bool m_enabled = false;
    bool m_inputEverWritten = false;
    bool m_inputEverNonzero = false;
    bool m_inputClearedAfterNonzero = false;
    bool m_zeroPayloadGuardLogged = false;
    Address m_clearingPc = 0;

    RangeWriteEvent m_firstWrite{};
    RangeWriteEvent m_lastWrite{};
    SelectorSnapshot m_lastSelector{};
    SelectorSnapshot m_lastCallerSnapshot{};
    std::array<u8, INPUT_WINDOW_BYTES> m_inputShadow{};

    DecoderSegment m_currentSegment{};
    std::array<DecoderSegment, SEGMENT_RING_CAPACITY> m_recentSegments{};
    size_t m_recentSegmentHead = 0;
    size_t m_recentSegmentCount = 0;
    u32 m_nextSegmentIndex = 1;

    u32 m_hotspot159dccHits = 0;
    u32 m_hotspot15a394Hits = 0;
};

} // namespace runtime
} // namespace psxrecomp
