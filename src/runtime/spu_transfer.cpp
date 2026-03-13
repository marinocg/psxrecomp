#include "psxrecomp/runtime/spu.h"

#include <algorithm>

namespace psxrecomp
{
namespace runtime
{

namespace
{
constexpr u32 kTransferModeMask = 0x0030u;
constexpr u16 kTransferModeManualWrite = 0x0010u;
constexpr u16 kTransferModeDmaWrite = 0x0020u;
constexpr u16 kTransferModeDmaRead = 0x0030u;
constexpr u32 kManualTransferBusyCycles = 0x300u;
constexpr u32 kTransferControlNormal = 0x0004u;
constexpr u32 kSpuRamBytes = static_cast<u32>(Spu::RamWordCount * sizeof(u32));

u32 normalizeTransferByteAddress(u32 byteAddress)
{
    return kSpuRamBytes == 0 ? 0u : (byteAddress % kSpuRamBytes);
}

u16 readHalfword(const std::vector<u32>& ramWords, u32 byteAddress)
{
    const u32 normalized = normalizeTransferByteAddress(byteAddress) & ~1u;
    const size_t wordIndex = static_cast<size_t>((normalized / sizeof(u32)) % ramWords.size());
    const u32 shift = (normalized & 0x2u) != 0 ? 16u : 0u;
    return static_cast<u16>((ramWords[wordIndex] >> shift) & 0xFFFFu);
}

void writeHalfword(std::vector<u32>& ramWords, u32 byteAddress, u16 value)
{
    const u32 normalized = normalizeTransferByteAddress(byteAddress) & ~1u;
    const size_t wordIndex = static_cast<size_t>((normalized / sizeof(u32)) % ramWords.size());
    const u32 shift = (normalized & 0x2u) != 0 ? 16u : 0u;
    const u32 laneMask = 0xFFFFu << shift;
    ramWords[wordIndex] = (ramWords[wordIndex] & ~laneMask) | (static_cast<u32>(value) << shift);
}
} // namespace

u32 Spu::normalizeRamByteAddress(u32 byteAddress)
{
    constexpr u32 kSpuRamBytes = static_cast<u32>(RamWordCount * sizeof(u32));
    return kSpuRamBytes == 0 ? 0u : (byteAddress % kSpuRamBytes);
}

void Spu::writeDma(u32 value)
{
    m_lastDmaWord = value;
    maybeTriggerIrqRange(m_currentTransferAddress, sizeof(value));
    writeHalfword(m_ram, m_currentTransferAddress, static_cast<u16>(value & 0xFFFFu));
    writeHalfword(m_ram, m_currentTransferAddress + 2u, static_cast<u16>(value >> 16));
    m_currentTransferAddress = normalizeRamByteAddress(m_currentTransferAddress + 4u);
}

u32 Spu::readDma()
{
    maybeTriggerIrqRange(m_currentTransferAddress, sizeof(u32));
    const u32 low = static_cast<u32>(readHalfword(m_ram, m_currentTransferAddress));
    const u32 high = static_cast<u32>(readHalfword(m_ram, m_currentTransferAddress + 2u));
    m_currentTransferAddress = normalizeRamByteAddress(m_currentTransferAddress + 4u);
    return low | (high << 16);
}

bool Spu::canTransferDma(bool fromRam) const
{
    if (!transferControlNormal())
    {
        return false;
    }

    const u16 transferMode = m_appliedControlBits & kTransferModeMask;
    if (fromRam)
    {
        return transferMode == kTransferModeDmaWrite;
    }
    return transferMode == kTransferModeDmaRead && m_dmaReadRequestDelayCyclesRemaining == 0 &&
           m_controlApplyCyclesRemaining == 0;
}

bool Spu::transferControlNormal() const
{
    return m_registers[registerIndex(RegisterMap::TransferControl)] == kTransferControlNormal;
}

void Spu::runManualWriteTransfer()
{
    if ((m_appliedControlBits & kTransferModeMask) != kTransferModeManualWrite ||
        !transferControlNormal() || m_transferFifo.empty())
    {
        return;
    }

    const size_t halfwordCount = m_transferFifo.size();
    while (!m_transferFifo.empty())
    {
        maybeTriggerIrqRange(m_currentTransferAddress, sizeof(u16));
        writeHalfword(m_ram, m_currentTransferAddress, m_transferFifo.front());
        m_transferFifo.pop_front();
        m_currentTransferAddress = normalizeRamByteAddress(m_currentTransferAddress + 2u);
    }

    const u32 busyCycles =
        kManualTransferBusyCycles + static_cast<u32>(std::min<size_t>(halfwordCount, 0x40u)) * 8u;
    m_busyCyclesRemaining = std::max(m_busyCyclesRemaining, busyCycles);
}

void Spu::maybeTriggerIrqRange(u32 startByteAddress, u32 byteCount)
{
    if (!irqControlEnabled() || byteCount == 0)
    {
        return;
    }

    const u32 irqByteAddress =
        normalizeRamByteAddress(static_cast<u32>(readRegister(RegisterMap::IrqAddress)) * 8u);
    const u32 normalizedStart = normalizeRamByteAddress(startByteAddress);
    for (u32 offset = 0; offset < byteCount; ++offset)
    {
        if (normalizeRamByteAddress(normalizedStart + offset) == irqByteAddress)
        {
            m_irqFlag = true;
            return;
        }
    }
}

} // namespace runtime
} // namespace psxrecomp
