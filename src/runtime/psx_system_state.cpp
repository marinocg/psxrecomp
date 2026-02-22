#include "psxrecomp/runtime/psx_system.h"

#include <algorithm>
#include <sstream>

namespace psxrecomp
{
namespace runtime
{

namespace
{
void appendU32(std::vector<u8>& out, u32 value)
{
    out.push_back(static_cast<u8>(value & 0xFF));
    out.push_back(static_cast<u8>((value >> 8) & 0xFF));
    out.push_back(static_cast<u8>((value >> 16) & 0xFF));
    out.push_back(static_cast<u8>((value >> 24) & 0xFF));
}

bool consumeU32(const std::vector<u8>& data, size_t& cursor, u32& out)
{
    if (cursor + sizeof(u32) > data.size())
    {
        return false;
    }
    out = static_cast<u32>(data[cursor]) | (static_cast<u32>(data[cursor + 1]) << 8) |
          (static_cast<u32>(data[cursor + 2]) << 16) | (static_cast<u32>(data[cursor + 3]) << 24);
    cursor += sizeof(u32);
    return true;
}

uint64_t fnv1a64(const std::vector<u8>& bytes)
{
    uint64_t hash = 1469598103934665603ull;
    for (u8 byte : bytes)
    {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}
} // namespace

std::vector<u8> PsxSystem::serializeState() const
{
    std::vector<u8> state;
    state.reserve(sizeof(u32) * 7 + m_ram.size() + m_scratchpad.size() + m_bios.size());

    appendU32(state, static_cast<u32>(m_ram.size()));
    state.insert(state.end(), m_ram.begin(), m_ram.end());

    appendU32(state, static_cast<u32>(m_scratchpad.size()));
    state.insert(state.end(), m_scratchpad.begin(), m_scratchpad.end());

    appendU32(state, static_cast<u32>(m_bios.size()));
    state.insert(state.end(), m_bios.begin(), m_bios.end());

    appendU32(state, m_interrupts.readStatus());
    appendU32(state, m_interrupts.readMask());
    appendU32(state, m_spu.cyclesElapsed());
    appendU32(state, m_gpu.readStatus());
    return state;
}

bool PsxSystem::deserializeState(const std::vector<u8>& state)
{
    size_t cursor = 0;
    u32 ramSize = 0;
    u32 scratchpadSize = 0;
    u32 biosSize = 0;
    u32 irqStatus = 0;
    u32 irqMask = 0;
    u32 spuCycles = 0;
    u32 gpuStatus = 0;

    auto readBlob = [&state, &cursor](u32 blobSize, std::vector<u8>& out)
    {
        if (cursor > state.size() || blobSize > (state.size() - cursor))
        {
            return false;
        }

        out.assign(state.begin() + static_cast<std::ptrdiff_t>(cursor),
                   state.begin() + static_cast<std::ptrdiff_t>(cursor + blobSize));
        cursor += blobSize;
        return true;
    };

    std::vector<u8> ramCopy;
    std::vector<u8> scratchpadCopy;
    std::vector<u8> biosCopy;

    if (!consumeU32(state, cursor, ramSize) || ramSize != m_ram.size() ||
        !readBlob(ramSize, ramCopy) || !consumeU32(state, cursor, scratchpadSize) ||
        scratchpadSize != m_scratchpad.size() || !readBlob(scratchpadSize, scratchpadCopy) ||
        !consumeU32(state, cursor, biosSize) || biosSize != m_bios.size() ||
        !readBlob(biosSize, biosCopy) || !consumeU32(state, cursor, irqStatus) ||
        !consumeU32(state, cursor, irqMask) || !consumeU32(state, cursor, spuCycles) ||
        !consumeU32(state, cursor, gpuStatus) || cursor != state.size())
    {
        return false;
    }

    std::copy(ramCopy.begin(), ramCopy.end(), m_ram.begin());
    std::copy(scratchpadCopy.begin(), scratchpadCopy.end(), m_scratchpad.begin());
    std::copy(biosCopy.begin(), biosCopy.end(), m_bios.begin());

    m_interrupts.restoreState(irqStatus, irqMask);

    m_spu.reset();
    m_spu.tick(spuCycles);

    m_gpu.reset();
    m_gpu.restoreStatus(gpuStatus);

    m_cdrom.reset();
    m_input.reset();
    m_dma.reset();
    m_scheduler.reset();
    m_debugOverlay.reset();
    m_timers.reset();
    m_criticalSectionDepth = 0;
    m_customExitHandler = 0;
    m_inCustomExitHandler = false;
    m_inCallbackInvocation = false;
    m_frameCount = 0;
    m_cpuCycles = 0;
    m_gpuDrainCarry = 0;
    m_videoSchedulePrimed = false;
    primeVideoSchedule();
    return true;
}

uint64_t PsxSystem::stateChecksum() const
{
    return fnv1a64(serializeState());
}

void PsxSystem::callBiosSyscall(u32 code, u32* regs, size_t regCount)
{
    if (regs == nullptr || regCount == 0)
    {
        m_logger.log(LogLevel::Warn, "bios", "BIOS syscall called with empty register file");
        return;
    }

    std::ostringstream stream;
    switch (code)
    {
    case 0x00:
    {
        // On PS1, syscall(0) dispatches kernel critical-section helpers
        // using a0 as a subcommand (1=enter, 2=exit).
        const u32 subcommand = regCount > 4 ? regs[4] : 0;
        if (subcommand == 1)
        {
            // PSX EnterCriticalSection returns non-zero when interrupts were
            // previously enabled (outermost enter), zero when already critical.
            regs[2] = m_criticalSectionDepth == 0 ? 1u : 0u;
            ++m_criticalSectionDepth;
            stream << "BIOS EnterCriticalSection depth=" << m_criticalSectionDepth;
            m_logger.log(LogLevel::Debug, "bios", stream.str());
            return;
        }
        if (subcommand == 2)
        {
            regs[2] = m_criticalSectionDepth > 0 ? 1u : 0u;
            if (m_criticalSectionDepth > 0)
            {
                --m_criticalSectionDepth;
            }
            stream << "BIOS ExitCriticalSection depth=" << m_criticalSectionDepth;
            m_logger.log(LogLevel::Debug, "bios", stream.str());
            return;
        }

        stream << "BIOS syscall(0) stub subcommand a0=0x" << std::hex << subcommand;
        m_logger.log(LogLevel::Warn, "bios", stream.str());
        return;
    }
    case 0x01:
        stream << "BIOS syscall(1) stub";
        m_logger.log(LogLevel::Warn, "bios", stream.str());
        return;
    case 0x3F:
        if (regCount <= 4)
        {
            m_logger.log(LogLevel::Warn, "bios", "BIOS Putchar called without a0 register");
            return;
        }
        stream << "BIOS Putchar: '" << static_cast<char>(regs[4] & 0xFF) << "'";
        m_logger.log(LogLevel::Info, "bios", stream.str());
        return;
    default:
        stream << "BIOS syscall stub invoked: code=0x" << std::hex << code << std::dec
               << ", regs=" << regCount;
        if (regCount > 4)
        {
            stream << ", a0=0x" << std::hex << regs[4];
        }
        m_logger.log(LogLevel::Warn, "bios", stream.str());
        return;
    }
}

} // namespace runtime
} // namespace psxrecomp
