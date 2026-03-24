// psx_system_inl.h — PsxSystem template method bodies (included at end of psx_system.h)
#pragma once

#include <cstring>

namespace psxrecomp
{
namespace runtime
{

template <typename T>
T PsxSystem::read(Address address)
{
    Address physical = normalizeAddress(address);
    if (isMainRamAddress(physical, static_cast<Address>(sizeof(T))))
    {
        const Address offset = foldMainRamAddress(physical);
        const u8 readSize = static_cast<u8>(sizeof(T));
        const T value = readFromRegion<T>(m_ram.data(), offset, MemoryMap::RAM_SIZE);
        if (m_diagWatchpoints.shouldWatchRamRead(physical, readSize))
        {
            m_diagWatchpoints.recordRamRead(m_debugOverlay.lastProgramCounter(), physical,
                                            readSize, static_cast<u32>(value), &m_logger,
                                            m_lastResumeAddress);
        }
        if (m_diagCdromLateBufferTracker.isEnabled() && !m_inDmaTransfer)
        {
            m_diagCdromLateBufferTracker.noteCpuRead(m_debugOverlay.lastProgramCounter(),
                                                     0x80000000u | offset, readSize);
        }
        return value;
    }
    if (isInRange(physical, MemoryMap::SCRATCHPAD_BASE, MemoryMap::SCRATCHPAD_SIZE))
    {
        return readFromRegion<T>(m_scratchpad.data(), physical - MemoryMap::SCRATCHPAD_BASE,
                                 MemoryMap::SCRATCHPAD_SIZE);
    }
    if (isInRange(physical, MemoryMap::BIOS_BASE, MemoryMap::BIOS_SIZE))
    {
        return readFromRegion<T>(m_bios.data(), physical - MemoryMap::BIOS_BASE,
                                 MemoryMap::BIOS_SIZE);
    }
    if (isInRange(physical, MemoryMap::IO_BASE, MemoryMap::IO_SIZE))
    {
        return readMmio<T>(physical);
    }
    return {};
}

template <typename T>
void PsxSystem::write(Address address, T value)
{
    Address physical = normalizeAddress(address);
    if (isMainRamAddress(physical, static_cast<Address>(sizeof(T))))
    {
        const Address offset = foldMainRamAddress(physical);
        const u8 writeSize = static_cast<u8>(sizeof(T));
        const bool callbackTraceActive = m_callbackTrace.hasActiveInvocation();
        const bool shouldTraceWrite =
            callbackTraceActive || m_stallClassifier.shouldWatchRamWrite(physical, writeSize) ||
            m_diagWatchpoints.shouldWatchRamWrite(physical, writeSize);
        if (shouldTraceWrite)
        {
            const T oldValue = readFromRegion<T>(m_ram.data(), offset, MemoryMap::RAM_SIZE);
            writeToRegion<T>(m_ram.data(), offset, MemoryMap::RAM_SIZE, value);
            if (m_diagRev2DecoderHandoffTracker.isEnabled())
            {
                const auto writeKind = m_inDmaTransfer
                                           ? DiagRev2DecoderHandoffTracker::WriteKind::Dma
                                           : DiagRev2DecoderHandoffTracker::WriteKind::CpuStore;
                m_diagRev2DecoderHandoffTracker.noteScalarWrite(
                    *this, m_debugOverlay.lastProgramCounter(), 0x80000000u | physical, writeSize,
                    static_cast<u32>(value), writeKind);
            }
            if (callbackTraceActive)
            {
                m_callbackTrace.recordRamWrite(0x80000000u | physical, writeSize,
                                               static_cast<u32>(oldValue),
                                               static_cast<u32>(value));
            }
            if (m_stallClassifier.shouldWatchRamWrite(physical, writeSize))
            {
                m_stallClassifier.recordRamWrite(m_debugOverlay.lastProgramCounter(), physical,
                                                 writeSize, static_cast<u32>(oldValue),
                                                 static_cast<u32>(value));
            }
            if (m_diagWatchpoints.shouldWatchRamWrite(physical, writeSize))
            {
                m_diagWatchpoints.recordRamWrite(m_debugOverlay.lastProgramCounter(), physical,
                                                 writeSize, static_cast<u32>(oldValue),
                                                 static_cast<u32>(value), &m_logger,
                                                 m_lastResumeAddress);
            }
        }
        else
        {
            writeToRegion<T>(m_ram.data(), offset, MemoryMap::RAM_SIZE, value);
            if (m_diagRev2DecoderHandoffTracker.isEnabled())
            {
                const auto writeKind = m_inDmaTransfer
                                           ? DiagRev2DecoderHandoffTracker::WriteKind::Dma
                                           : DiagRev2DecoderHandoffTracker::WriteKind::CpuStore;
                m_diagRev2DecoderHandoffTracker.noteScalarWrite(
                    *this, m_debugOverlay.lastProgramCounter(), 0x80000000u | physical, writeSize,
                    static_cast<u32>(value), writeKind);
            }
        }
        return;
    }
    if (isInRange(physical, MemoryMap::SCRATCHPAD_BASE, MemoryMap::SCRATCHPAD_SIZE))
    {
        writeToRegion<T>(m_scratchpad.data(), physical - MemoryMap::SCRATCHPAD_BASE,
                         MemoryMap::SCRATCHPAD_SIZE, value);
        return;
    }
    if (isInRange(physical, MemoryMap::IO_BASE, MemoryMap::IO_SIZE))
    {
        writeMmio<T>(physical, value);
    }
}

template <typename T>
T PsxSystem::readMmioExplicit(Address address)
{
    return readMmio<T>(normalizeAddress(address));
}

template <typename T>
void PsxSystem::writeMmioExplicit(Address address, T value)
{
    writeMmio<T>(normalizeAddress(address), value);
}

template <typename T>
T PsxSystem::readFromRegion(const u8* base, Address offset, Address size) const
{
    static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4,
                  "Unsupported read size for runtime MMIO");
    if (!base || size < sizeof(T) || offset > (size - sizeof(T)))
    {
        return {};
    }
    T value{};
    std::memcpy(&value, base + offset, sizeof(T));
    return value;
}

template <typename T>
void PsxSystem::writeToRegion(u8* base, Address offset, Address size, T value)
{
    static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4,
                  "Unsupported write size for runtime MMIO");
    if (!base || size < sizeof(T) || offset > (size - sizeof(T)))
    {
        return;
    }
    std::memcpy(base + offset, &value, sizeof(T));
}

template <typename T>
T PsxSystem::readMmio(Address address)
{
    static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4,
                  "Unsupported MMIO read size");
    if constexpr (sizeof(T) == 1)
    {
        return static_cast<T>(readMmio8(address));
    }
    if constexpr (sizeof(T) == 2)
    {
        return static_cast<T>(readMmio16(address));
    }
    return static_cast<T>(readMmio32(address));
}

template <typename T>
void PsxSystem::writeMmio(Address address, T value)
{
    static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4,
                  "Unsupported MMIO write size");
    if constexpr (sizeof(T) == 1)
    {
        writeMmio8(address, static_cast<u8>(value));
    }
    else if constexpr (sizeof(T) == 2)
    {
        writeMmio16(address, static_cast<u16>(value));
    }
    else
    {
        writeMmio32(address, static_cast<u32>(value));
    }
}

} // namespace runtime
} // namespace psxrecomp
