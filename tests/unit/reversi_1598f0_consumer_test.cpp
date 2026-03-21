#include "psxrecomp/runtime/mips_unaligned_access.h"

#include <array>
#include <cassert>
#include <cstddef>

namespace
{
using psxrecomp::Address;
using psxrecomp::runtime::PsxSystem;

constexpr Address kCapturedSourceAddress = 0x80187158u;
constexpr Address kStageBufferAddress = 0x80185558u;
constexpr Address kStackAddress = 0x801FF100u;
constexpr psxrecomp::u8 kSentinelByte = 0xCCu;
constexpr psxrecomp::u32 kSentinelWord = 0xCCCCCCCCu;

enum class UnalignedMode
{
    CorrectMerge,
    LegacyPlainWord,
};

struct ConsumerTrace
{
    bool entryHasData = false;
    bool exitBranchTaken = false;
    psxrecomp::u32 mergedSectorWord = 0u;
    psxrecomp::u32 stackedWord = 0u;
    psxrecomp::u32 copiedWord = 0u;
    psxrecomp::u32 finalV1 = 0u;
    psxrecomp::u32 neighborWord = 0u;
    std::array<psxrecomp::u8, 8> stackWindow = {};
    std::array<psxrecomp::u8, 12> stageWindow = {};
};

void seedBytes(PsxSystem& system, Address address, size_t count, psxrecomp::u8 value)
{
    for (size_t index = 0; index < count; ++index)
    {
        system.write<psxrecomp::u8>(address + static_cast<Address>(index), value);
    }
}

template <size_t Count>
std::array<psxrecomp::u8, Count> readBytes(PsxSystem& system, Address address)
{
    std::array<psxrecomp::u8, Count> bytes = {};
    for (size_t index = 0; index < Count; ++index)
    {
        bytes[index] = system.read<psxrecomp::u8>(address + static_cast<Address>(index));
    }
    return bytes;
}

void writeBytes(PsxSystem& system, Address address, const std::array<psxrecomp::u8, 16>& bytes)
{
    for (size_t index = 0; index < bytes.size(); ++index)
    {
        system.write<psxrecomp::u8>(address + static_cast<Address>(index), bytes[index]);
    }
}

psxrecomp::u32 loadWordLeft(PsxSystem& system, Address address, psxrecomp::u32 value,
                            UnalignedMode mode)
{
    if (mode == UnalignedMode::LegacyPlainWord)
    {
        return system.read<psxrecomp::u32>(address);
    }
    return psxrecomp::runtime::loadWordLeft(system, address, value);
}

psxrecomp::u32 loadWordRight(PsxSystem& system, Address address, psxrecomp::u32 value,
                             UnalignedMode mode)
{
    if (mode == UnalignedMode::LegacyPlainWord)
    {
        return system.read<psxrecomp::u32>(address);
    }
    return psxrecomp::runtime::loadWordRight(system, address, value);
}

void storeWordLeft(PsxSystem& system, Address address, psxrecomp::u32 value, UnalignedMode mode)
{
    if (mode == UnalignedMode::LegacyPlainWord)
    {
        system.write<psxrecomp::u32>(address, value);
        return;
    }
    psxrecomp::runtime::storeWordLeft(system, address, value);
}

void storeWordRight(PsxSystem& system, Address address, psxrecomp::u32 value, UnalignedMode mode)
{
    if (mode == UnalignedMode::LegacyPlainWord)
    {
        system.write<psxrecomp::u32>(address, value);
        return;
    }
    psxrecomp::runtime::storeWordRight(system, address, value);
}

psxrecomp::u32 arithmeticShiftRight(psxrecomp::u32 value, unsigned shift)
{
    return static_cast<psxrecomp::u32>(static_cast<psxrecomp::s32>(value) >> shift);
}

void multiplySigned(psxrecomp::u32 lhs, psxrecomp::u32 rhs, psxrecomp::u32& lo, psxrecomp::u32& hi)
{
    const auto product = static_cast<psxrecomp::s64>(static_cast<psxrecomp::s32>(lhs)) *
                         static_cast<psxrecomp::s64>(static_cast<psxrecomp::s32>(rhs));
    lo = static_cast<psxrecomp::u32>(product);
    hi = static_cast<psxrecomp::u32>(static_cast<psxrecomp::u64>(product) >> 32);
}

psxrecomp::u32 runConsumer0x15C1BC(PsxSystem& system, psxrecomp::u32 a0, Address destination)
{
    using psxrecomp::u32;

    u32 v0 = destination;
    u32 v1 = 0x1B4E81B5u;
    u32 a1 = 0x88888889u;
    u32 a2 = 0u;
    u32 a3 = 0u;
    u32 t0 = 0u;
    u32 t1 = 0u;
    u32 t3 = 0u;
    u32 lo = 0u;
    u32 hi = 0u;

    a0 += 150u;
    multiplySigned(a0, v1, lo, hi);
    v1 = hi;
    a3 = arithmeticShiftRight(v1, 3);
    v1 = arithmeticShiftRight(a0, 31);
    a3 -= v1;

    multiplySigned(a3, a1, lo, hi);
    t1 = 0x66666667u;
    a1 = a3 << 2;
    a1 += a3;
    v1 = a1 << 4;
    a2 = hi;
    v1 -= a1;
    a0 -= v1;

    multiplySigned(a0, t1, lo, hi);
    v1 = arithmeticShiftRight(a3, 31);
    t0 = a2 + a3;
    t0 = arithmeticShiftRight(t0, 5);
    t0 -= v1;
    v1 = t0 << 4;
    v1 -= t0;
    a1 = hi;
    v1 <<= 2;
    a3 -= v1;

    multiplySigned(a3, t1, lo, hi);
    v1 = arithmeticShiftRight(a0, 31);
    a1 = arithmeticShiftRight(a1, 2);
    a1 -= v1;
    a2 = a1 << 4;
    v1 = a1 << 2;
    v1 += a1;
    v1 <<= 1;
    a0 -= v1;
    t3 = hi;
    a2 += a0;
    system.write<psxrecomp::u8>(v0 + 2u, static_cast<psxrecomp::u8>(a2));

    v1 = arithmeticShiftRight(a3, 31);
    multiplySigned(t0, t1, lo, hi);
    a0 = arithmeticShiftRight(t3, 2);
    a0 -= v1;
    a1 = a0 << 4;
    v1 = a0 << 2;
    v1 += a0;
    v1 <<= 1;
    a3 -= v1;
    a1 += a3;
    system.write<psxrecomp::u8>(v0 + 1u, static_cast<psxrecomp::u8>(a1));

    v1 = arithmeticShiftRight(t0, 31);
    t1 = hi;
    a0 = arithmeticShiftRight(t1, 2);
    a0 -= v1;
    a1 = a0 << 4;
    v1 = a0 << 2;
    v1 += a0;
    v1 <<= 1;
    t0 -= v1;
    a1 += t0;
    system.write<psxrecomp::u8>(v0 + 0u, static_cast<psxrecomp::u8>(a1));

    return v1;
}

ConsumerTrace runConsumerPath(const std::array<psxrecomp::u8, 16>& payload, UnalignedMode mode)
{
    ConsumerTrace trace;
    PsxSystem system;
    assert(system.initialize());

    writeBytes(system, kCapturedSourceAddress, payload);
    seedBytes(system, kStackAddress + 24u, 12u, kSentinelByte);
    seedBytes(system, kStageBufferAddress, 16u, kSentinelByte);

    psxrecomp::u32 v0 = system.read<psxrecomp::u8>(kCapturedSourceAddress + 0u);
    trace.entryHasData = v0 != 0u;

    v0 = loadWordLeft(system, kCapturedSourceAddress + 5u, v0, mode);
    v0 = loadWordRight(system, kCapturedSourceAddress + 2u, v0, mode);
    trace.mergedSectorWord = v0;

    storeWordLeft(system, kStackAddress + 27u, v0, mode);
    storeWordRight(system, kStackAddress + 24u, v0, mode);
    trace.stackedWord = system.read<psxrecomp::u32>(kStackAddress + 24u);

    trace.finalV1 = runConsumer0x15C1BC(system, trace.stackedWord, kStageBufferAddress);

    const Address nextStage = kStageBufferAddress + 4u;
    psxrecomp::u32 v1 = trace.finalV1;
    v1 = loadWordLeft(system, kCapturedSourceAddress + 13u, v1, mode);
    v1 = loadWordRight(system, kCapturedSourceAddress + 10u, v1, mode);
    trace.copiedWord = v1;

    storeWordLeft(system, nextStage + 3u, v1, mode);
    storeWordRight(system, nextStage + 0u, v1, mode);

    trace.exitBranchTaken = true;
    trace.neighborWord = system.read<psxrecomp::u32>(kStageBufferAddress + 8u);
    trace.stackWindow = readBytes<8>(system, kStackAddress + 24u);
    trace.stageWindow = readBytes<12>(system, kStageBufferAddress + 0u);
    return trace;
}
} // namespace

int main()
{
    using psxrecomp::u8;

    // Captured from the late-buffer summary for the first meaningful 0x1598F0 reader at
    // 0x80187158 in the Reversi 2 regression run.
    const std::array<u8, 16> capturedPayload = {
        0x30, 0x00, 0x16, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x16, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00,
    };

    const ConsumerTrace fixed = runConsumerPath(capturedPayload, UnalignedMode::CorrectMerge);
    const ConsumerTrace legacy = runConsumerPath(capturedPayload, UnalignedMode::LegacyPlainWord);

    assert(fixed.entryHasData);
    assert(fixed.exitBranchTaken);
    assert(fixed.mergedSectorWord == 0x00000016u);
    assert(fixed.stackedWord == 0x00000016u);
    assert(fixed.finalV1 == 0u);
    assert(fixed.copiedWord == 0x00000800u);
    assert(
        (fixed.stackWindow == std::array<u8, 8>{0x16, 0x00, 0x00, 0x00, 0xCC, 0xCC, 0xCC, 0xCC}));
    assert((fixed.stageWindow == std::array<u8, 12>{0x00, 0x02, 0x22, 0xCC, 0x00, 0x08, 0x00, 0x00,
                                                    0xCC, 0xCC, 0xCC, 0xCC}));
    assert(fixed.neighborWord == kSentinelWord);

    // The legacy lowering reconstructs the same words on this exact fixture, but corrupts
    // adjacent bytes because each SWL/SWR becomes a plain unaligned 32-bit store.
    assert(legacy.entryHasData);
    assert(legacy.exitBranchTaken);
    assert(legacy.mergedSectorWord == fixed.mergedSectorWord);
    assert(legacy.stackedWord == fixed.stackedWord);
    assert(legacy.copiedWord == fixed.copiedWord);
    assert(
        (legacy.stackWindow == std::array<u8, 8>{0x16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xCC}));
    assert((legacy.stageWindow == std::array<u8, 12>{0x00, 0x02, 0x22, 0xCC, 0x00, 0x08, 0x00, 0x00,
                                                     0x08, 0x00, 0x00, 0xCC}));
    assert(legacy.neighborWord == 0xCC000008u);

    // This is the first branch-sensitive discriminator for downstream consumers:
    // a clean next-stage neighbor word stays untouched after the fix, while the legacy
    // lowering makes the same predicate flip because it clobbers bytes at +8..+10.
    const bool fixedNeighborIntact = fixed.neighborWord == kSentinelWord;
    const bool legacyNeighborIntact = legacy.neighborWord == kSentinelWord;
    assert(fixedNeighborIntact);
    assert(!legacyNeighborIntact);

    return 0;
}
