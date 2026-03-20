#include "psxrecomp/runtime/diag_rev2_decoder_handoff_tracker.h"

#include <iomanip>
#include <sstream>

namespace psxrecomp
{
namespace runtime
{

namespace
{

std::string formatBytes(const u8* bytes, size_t count)
{
    std::ostringstream os;
    for (size_t i = 0; i < count; ++i)
    {
        if (i != 0)
        {
            os << ' ';
        }
        os << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(bytes[i]);
    }
    return os.str();
}

} // namespace

std::string DiagRev2DecoderHandoffTracker::currentDiagnosis() const
{
    const Address selectorPayload = m_lastCallerSnapshot.valid ? m_lastCallerSnapshot.payloadPointer
                                                               : m_lastSelector.payloadPointer;
    if (selectorPayload != 0 && m_currentSegment.valid &&
        selectorPayload != m_currentSegment.inputPointer)
    {
        return "decoder received wrong slot pointer";
    }
    if (!m_inputEverWritten)
    {
        return "never populated";
    }
    if (m_inputEverNonzero && m_inputClearedAfterNonzero)
    {
        return "populated nonzero then cleared";
    }
    if (!m_inputEverNonzero)
    {
        return "populated zero-only by producer";
    }
    return "populated nonzero and still nonzero";
}

std::string DiagRev2DecoderHandoffTracker::formatSummary() const
{
    std::ostringstream os;
    os << "=== Rev2 Decoder Handoff Summary ===\n";
    os << "  input_range=0x" << std::hex << INPUT_RANGE_START << "..0x" << (INPUT_RANGE_END - 1u)
       << std::dec << "\n";
    os << "  diagnosis: " << currentDiagnosis();
    if (m_inputClearedAfterNonzero && m_clearingPc != 0)
    {
        os << " by PC 0x" << std::hex << m_clearingPc << std::dec;
    }
    os << "\n";
    os << "  ever_nonzero_before_decoder: " << (m_inputEverNonzero ? "yes" : "no") << "\n";
    os << "  later_cleared: " << (m_inputClearedAfterNonzero ? "yes" : "no") << "\n";
    os << "  current_input16: " << formatBytes(m_inputShadow.data(), 16) << "\n";

    auto appendWrite = [&os](const char* label, const RangeWriteEvent& event)
    {
        os << "  " << label << ": ";
        if (!event.valid)
        {
            os << "none\n";
            return;
        }
        os << "pc=0x" << std::hex << event.writerPc << std::dec
           << " kind=" << writeKindLabel(event.kind) << " addr=0x" << std::hex << event.address
           << std::dec << " size=" << event.size
           << " nonzero=" << (event.wroteAnyNonzero ? "yes" : "no");
        if (!event.sourceTag.empty())
        {
            os << " source=" << event.sourceTag;
        }
        if (!event.detail.empty())
        {
            os << " detail=" << event.detail;
        }
        os << "\n";
    };
    appendWrite("first_write", m_firstWrite);
    appendWrite("last_write_before_decoder", m_lastWrite);

    auto appendSelector = [&os](const char* label, const SelectorSnapshot& snapshot)
    {
        os << "  " << label << ": ";
        if (!snapshot.valid)
        {
            os << "none\n";
            return;
        }
        os << "pc=0x" << std::hex << snapshot.capturePc << " payload=0x" << snapshot.payloadPointer
           << " descriptor=0x" << snapshot.descriptorPointer << std::dec << " state=0x" << std::hex
           << snapshot.descriptorState << std::dec << " len=" << snapshot.descriptorLength
           << " f16=" << snapshot.field16 << " f18=" << snapshot.field18
           << " queue_index=" << snapshot.queueIndex
           << " arena=" << (snapshot.payloadInExpectedArena ? "yes" : "no") << "\n";
        if (snapshot.payloadByteCount != 0)
        {
            os << "    payload16="
               << formatBytes(snapshot.payloadBytes.data(), snapshot.payloadByteCount) << "\n";
        }
    };
    appendSelector("selector_15a00c", m_lastSelector);
    appendSelector("caller_152304", m_lastCallerSnapshot);

    os << "  semantic_hotspots: 0x159dcc_hits=" << m_hotspot159dccHits
       << " 0x15a394_hits=" << m_hotspot15a394Hits << "\n";

    os << "  decoder_segments (newest first):\n";
    if (!m_currentSegment.valid && m_recentSegmentCount == 0)
    {
        os << "    none\n";
    }
    else
    {
        auto appendSegment = [&os](const DecoderSegment& segment)
        {
            os << "    segment=" << segment.segmentIndex << " input=0x" << std::hex
               << segment.inputPointer << " out_start=0x" << segment.outputStart << " out_max=0x"
               << segment.outputMax << std::dec << " produced="
               << (segment.outputMax >= segment.outputStart
                       ? segment.outputMax - segment.outputStart
                       : 0u)
               << " iterations=" << segment.iterationCount
               << " zero_input=" << (segment.zeroInputAtEntry ? "yes" : "no")
               << " len_hint=" << segment.descriptorLengthHint
               << " span_exceeds_len_hint=" << (segment.exceededLengthHint ? "yes" : "no") << "\n";
        };
        if (m_currentSegment.valid)
        {
            appendSegment(m_currentSegment);
        }
        for (size_t i = 0; i < m_recentSegmentCount; ++i)
        {
            const size_t index =
                (m_recentSegmentHead + SEGMENT_RING_CAPACITY - 1u - i) % SEGMENT_RING_CAPACITY;
            appendSegment(m_recentSegments[index]);
        }
    }

    return os.str();
}

} // namespace runtime
} // namespace psxrecomp
