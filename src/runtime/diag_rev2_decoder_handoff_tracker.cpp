#include "psxrecomp/runtime/diag_rev2_decoder_handoff_tracker.h"

#include "psxrecomp/runtime/logger.h"
#include "psxrecomp/runtime/memory_map.h"
#include "psxrecomp/runtime/psx_system.h"

#include <algorithm>
#include <cstring>
#include <iomanip>

namespace psxrecomp
{
namespace runtime
{

namespace
{

bool readRamBytes(const PsxSystem& system, Address address, u8* out, size_t size)
{
    const Address physical = address & 0x1FFFFFFFu;
    if (!isMainRamAddress(physical, static_cast<Address>(size)))
    {
        return false;
    }

    const Address offset = foldMainRamAddress(physical);
    if (offset > MemoryMap::RAM_SIZE - size)
    {
        return false;
    }

    std::memcpy(out, system.getRam() + offset, size);
    return true;
}

template <typename T> bool readRamValue(const PsxSystem& system, Address address, T& out)
{
    static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4);
    const Address physical = address & 0x1FFFFFFFu;
    if (!isMainRamAddress(physical, static_cast<Address>(sizeof(T))))
    {
        return false;
    }

    const Address offset = foldMainRamAddress(physical);
    if (offset > MemoryMap::RAM_SIZE - sizeof(T))
    {
        return false;
    }

    std::memcpy(&out, system.getRam() + offset, sizeof(T));
    return true;
}

} // namespace

void DiagRev2DecoderHandoffTracker::setEnabled(bool enabled)
{
    m_enabled = enabled;
    reset();
}

bool DiagRev2DecoderHandoffTracker::isEnabled() const
{
    return m_enabled;
}

void DiagRev2DecoderHandoffTracker::reset()
{
    m_inputEverWritten = false;
    m_inputEverNonzero = false;
    m_inputClearedAfterNonzero = false;
    m_zeroPayloadGuardLogged = false;
    m_clearingPc = 0;
    m_firstWrite = {};
    m_lastWrite = {};
    m_lastSelector = {};
    m_lastCallerSnapshot = {};
    m_inputShadow.fill(0);
    m_currentSegment = {};
    m_recentSegments = {};
    m_recentSegmentHead = 0;
    m_recentSegmentCount = 0;
    m_nextSegmentIndex = 1;
    m_hotspot159dccHits = 0;
    m_hotspot15a394Hits = 0;
}

bool DiagRev2DecoderHandoffTracker::overlapsInputRange(Address address, u32 size)
{
    if (size == 0)
    {
        return false;
    }
    const uint64_t writeStart = static_cast<uint64_t>(address);
    const uint64_t writeEnd = writeStart + size;
    return writeStart < INPUT_RANGE_END && static_cast<uint64_t>(INPUT_RANGE_START) < writeEnd;
}

const char* DiagRev2DecoderHandoffTracker::writeKindLabel(WriteKind kind)
{
    switch (kind)
    {
    case WriteKind::Dma:
        return "DMA";
    case WriteKind::Memcpy:
        return "memcpy";
    case WriteKind::Memset:
        return "memset";
    default:
        return "CPU stores";
    }
}

bool DiagRev2DecoderHandoffTracker::isAllZero(const std::array<u8, INPUT_WINDOW_BYTES>& bytes)
{
    return std::all_of(bytes.begin(), bytes.end(), [](u8 value) { return value == 0; });
}

bool DiagRev2DecoderHandoffTracker::anyNonzero(const std::array<u8, INPUT_WINDOW_BYTES>& bytes)
{
    return std::any_of(bytes.begin(), bytes.end(), [](u8 value) { return value != 0; });
}

void DiagRev2DecoderHandoffTracker::updateInputShadow(const PsxSystem& system)
{
    (void)readRamBytes(system, INPUT_RANGE_START, m_inputShadow.data(), m_inputShadow.size());
}

void DiagRev2DecoderHandoffTracker::noteRangeWrite(const PsxSystem& system,
                                                   const RangeWriteEvent& event)
{
    if (!m_enabled)
    {
        return;
    }

    updateInputShadow(system);
    if (!m_inputEverWritten)
    {
        m_firstWrite = event;
        m_inputEverWritten = true;
    }
    m_lastWrite = event;

    const bool hasNonzeroNow = anyNonzero(m_inputShadow);
    if (hasNonzeroNow)
    {
        m_inputEverNonzero = true;
    }
    else if (m_inputEverNonzero)
    {
        m_inputClearedAfterNonzero = true;
        m_clearingPc = event.writerPc;
    }
}

void DiagRev2DecoderHandoffTracker::noteScalarWrite(const PsxSystem& system, Address writerPc,
                                                    Address address, u8 size, u32 value,
                                                    WriteKind kind)
{
    if (!m_enabled || !overlapsInputRange(address, size))
    {
        return;
    }

    RangeWriteEvent event;
    event.valid = true;
    event.kind = kind;
    event.writerPc = writerPc;
    event.address = address;
    event.size = size;
    event.wroteAnyNonzero = value != 0;
    noteRangeWrite(system, event);
}

void DiagRev2DecoderHandoffTracker::noteBulkCopy(const PsxSystem& system, Address writerPc,
                                                 Address destination, const u8* source, u32 length,
                                                 const std::string& sourceTag,
                                                 const std::string& detail)
{
    if (!m_enabled || !overlapsInputRange(destination, length))
    {
        return;
    }

    RangeWriteEvent event;
    event.valid = true;
    event.kind = WriteKind::Memcpy;
    event.writerPc = writerPc;
    event.address = destination;
    event.size = length;
    event.sourceTag = sourceTag;
    event.detail = detail;
    if (source != nullptr)
    {
        const Address start = std::max<Address>(destination, INPUT_RANGE_START);
        const Address end = std::min<Address>(destination + length, INPUT_RANGE_END);
        const size_t offset = static_cast<size_t>(start - destination);
        const size_t count = static_cast<size_t>(end - start);
        for (size_t i = 0; i < count; ++i)
        {
            if (source[offset + i] != 0)
            {
                event.wroteAnyNonzero = true;
                break;
            }
        }
    }
    noteRangeWrite(system, event);
}

void DiagRev2DecoderHandoffTracker::noteBulkFill(const PsxSystem& system, Address writerPc,
                                                 Address destination, u8 value, u32 length,
                                                 const std::string& sourceTag,
                                                 const std::string& detail)
{
    if (!m_enabled || !overlapsInputRange(destination, length))
    {
        return;
    }

    RangeWriteEvent event;
    event.valid = true;
    event.kind = WriteKind::Memset;
    event.writerPc = writerPc;
    event.address = destination;
    event.size = length;
    event.sourceTag = sourceTag;
    event.detail = detail;
    event.wroteAnyNonzero = value != 0;
    noteRangeWrite(system, event);
}

void DiagRev2DecoderHandoffTracker::captureSelectorSnapshot(Address capturePc,
                                                            Address payloadPointer,
                                                            Address descriptorPointer,
                                                            u32 queueIndex, const PsxSystem& system)
{
    SelectorSnapshot snapshot;
    snapshot.valid = true;
    snapshot.capturePc = capturePc;
    snapshot.payloadPointer = payloadPointer;
    snapshot.descriptorPointer = descriptorPointer;
    snapshot.queueIndex = queueIndex;
    snapshot.payloadInExpectedArena = payloadPointer >= EXPECTED_PAYLOAD_ARENA_START &&
                                      payloadPointer < EXPECTED_PAYLOAD_ARENA_END;
    readRamValue(system, descriptorPointer + 0u, snapshot.descriptorState);
    readRamValue(system, descriptorPointer + 8u, snapshot.descriptorLength);
    readRamValue(system, descriptorPointer + 16u, snapshot.field16);
    readRamValue(system, descriptorPointer + 18u, snapshot.field18);
    if (readRamBytes(system, payloadPointer, snapshot.payloadBytes.data(),
                     snapshot.payloadBytes.size()))
    {
        snapshot.payloadByteCount = static_cast<u8>(snapshot.payloadBytes.size());
    }
    m_lastSelector = snapshot;
}

void DiagRev2DecoderHandoffTracker::captureCallerSnapshot(const u32* regs, size_t regCount,
                                                          const PsxSystem& system)
{
    if (regs == nullptr || regCount < Registers::NUM_REGISTERS)
    {
        return;
    }

    Address payloadPointer = 0;
    Address descriptorPointer = 0;
    readRamValue(system, regs[Registers::SP] + 24u, payloadPointer);
    readRamValue(system, regs[Registers::SP] + 28u, descriptorPointer);
    if (payloadPointer == 0 && descriptorPointer == 0)
    {
        return;
    }

    SelectorSnapshot snapshot;
    snapshot.valid = true;
    snapshot.capturePc = 0x152304u;
    snapshot.payloadPointer = payloadPointer;
    snapshot.descriptorPointer = descriptorPointer;
    snapshot.payloadInExpectedArena = payloadPointer >= EXPECTED_PAYLOAD_ARENA_START &&
                                      payloadPointer < EXPECTED_PAYLOAD_ARENA_END;
    readRamValue(system, descriptorPointer + 0u, snapshot.descriptorState);
    readRamValue(system, descriptorPointer + 8u, snapshot.descriptorLength);
    readRamValue(system, descriptorPointer + 16u, snapshot.field16);
    readRamValue(system, descriptorPointer + 18u, snapshot.field18);
    if (readRamBytes(system, payloadPointer, snapshot.payloadBytes.data(),
                     snapshot.payloadBytes.size()))
    {
        snapshot.payloadByteCount = static_cast<u8>(snapshot.payloadBytes.size());
    }
    m_lastCallerSnapshot = snapshot;
}

void DiagRev2DecoderHandoffTracker::finalizeDecoderSegment()
{
    if (!m_currentSegment.valid)
    {
        return;
    }
    m_recentSegments[m_recentSegmentHead] = m_currentSegment;
    m_recentSegmentHead = (m_recentSegmentHead + 1u) % SEGMENT_RING_CAPACITY;
    if (m_recentSegmentCount < SEGMENT_RING_CAPACITY)
    {
        ++m_recentSegmentCount;
    }
    m_currentSegment = {};
}

void DiagRev2DecoderHandoffTracker::beginDecoderSegment(const u32* regs, size_t regCount,
                                                        const PsxSystem& system,
                                                        RuntimeLogger* logger)
{
    if (regs == nullptr || regCount < Registers::NUM_REGISTERS)
    {
        return;
    }

    if (m_currentSegment.valid)
    {
        finalizeDecoderSegment();
    }

    m_currentSegment.valid = true;
    m_currentSegment.segmentIndex = m_nextSegmentIndex++;
    m_currentSegment.inputPointer = regs[Registers::A0];
    m_currentSegment.outputStart = regs[Registers::A1];
    m_currentSegment.outputMax = regs[Registers::A1];
    m_currentSegment.iterationCount = 0;
    m_currentSegment.descriptorLengthHint = m_lastCallerSnapshot.valid
                                                ? m_lastCallerSnapshot.descriptorLength
                                                : m_lastSelector.descriptorLength;

    std::array<u8, 16> entryBytes{};
    if (readRamBytes(system, regs[Registers::A0], entryBytes.data(), entryBytes.size()))
    {
        m_currentSegment.zeroInputAtEntry =
            std::all_of(entryBytes.begin(), entryBytes.end(), [](u8 value) { return value == 0; });
    }

    maybeEmitZeroPayloadGuard(logger);
}

void DiagRev2DecoderHandoffTracker::updateDecoderLoopProgress(const u32* regs, size_t regCount)
{
    if (!m_currentSegment.valid || regs == nullptr || regCount < Registers::NUM_REGISTERS)
    {
        return;
    }

    ++m_currentSegment.iterationCount;
    m_currentSegment.outputMax = std::max(m_currentSegment.outputMax, regs[Registers::A1]);
    if (m_currentSegment.descriptorLengthHint != 0 &&
        m_currentSegment.outputMax > m_currentSegment.outputStart &&
        (m_currentSegment.outputMax - m_currentSegment.outputStart) >
            m_currentSegment.descriptorLengthHint)
    {
        m_currentSegment.exceededLengthHint = true;
    }
}

void DiagRev2DecoderHandoffTracker::maybeEmitZeroPayloadGuard(RuntimeLogger* logger) const
{
    if (logger == nullptr || m_zeroPayloadGuardLogged || !m_currentSegment.valid ||
        !m_currentSegment.zeroInputAtEntry)
    {
        return;
    }
}

void DiagRev2DecoderHandoffTracker::observePc(Address pc, const u32* regs, size_t regCount,
                                              const PsxSystem& system, RuntimeLogger* logger)
{
    if (!m_enabled)
    {
        return;
    }

    if (pc >= 0x159DCCu && pc <= 0x159DDCu)
    {
        ++m_hotspot159dccHits;
    }
    if (pc >= 0x15A394u && pc <= 0x15A3A4u)
    {
        ++m_hotspot15a394Hits;
    }

    if (pc == 0x15A0BCu && regs != nullptr && regCount >= Registers::NUM_REGISTERS)
    {
        captureSelectorSnapshot(pc, regs[Registers::A0], regs[Registers::A2], regs[Registers::A1],
                                system);
    }
    else if (pc == 0x152304u)
    {
        captureCallerSnapshot(regs, regCount, system);
    }
    else if (pc == 0x15452Cu)
    {
        beginDecoderSegment(regs, regCount, system, logger);
        if (logger != nullptr && !m_zeroPayloadGuardLogged && m_currentSegment.valid &&
            m_currentSegment.zeroInputAtEntry &&
            (m_lastCallerSnapshot.descriptorLength != 0 || m_lastSelector.descriptorLength != 0))
        {
            std::ostringstream msg;
            msg << "rev2_decoder_guard diagnosis=\"" << currentDiagnosis() << "\" input=0x"
                << std::hex << m_currentSegment.inputPointer << " selector_payload=0x"
                << (m_lastCallerSnapshot.valid ? m_lastCallerSnapshot.payloadPointer
                                               : m_lastSelector.payloadPointer)
                << " descriptor=0x"
                << (m_lastCallerSnapshot.valid ? m_lastCallerSnapshot.descriptorPointer
                                               : m_lastSelector.descriptorPointer)
                << std::dec << " descriptor_len="
                << (m_lastCallerSnapshot.valid ? m_lastCallerSnapshot.descriptorLength
                                               : m_lastSelector.descriptorLength);
            if (m_lastWrite.valid)
            {
                msg << " last_writer_pc=0x" << std::hex << m_lastWrite.writerPc << std::dec
                    << " last_writer_kind=" << writeKindLabel(m_lastWrite.kind);
            }
            logger->log(LogLevel::Warn, "diag", msg.str());
            const_cast<DiagRev2DecoderHandoffTracker*>(this)->m_zeroPayloadGuardLogged = true;
        }
    }
    else if (pc == 0x154718u || pc == 0x154758u || pc == 0x15473Cu || pc == 0x154778u)
    {
        updateDecoderLoopProgress(regs, regCount);
    }
}

} // namespace runtime
} // namespace psxrecomp
