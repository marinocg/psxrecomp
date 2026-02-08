#pragma once

#include "psxrecomp/types.h"

#include <cstdlib>
#include <cstring>

namespace psxrecomp
{
namespace runtime
{

/**
 * @brief PSX System interface
 *
 * Provides the runtime environment for recompiled PSX code,
 * including memory management and hardware emulation.
 */
class PsxSystem
{
  public:
    PsxSystem();
    ~PsxSystem();

    /**
     * @brief Initialize the PSX system
     * @return true if successful
     */
    bool initialize();

    /**
     * @brief Run a single frame
     */
    void runFrame();

    /**
     * @brief Read from PSX memory
     * @param address Memory address
     * @return Value at address
     */
    template <typename T> T read(Address address)
    {
        Address physical = normalizeAddress(address);
        if (physical >= MemoryMap::RAM_BASE && physical < MemoryMap::RAM_BASE + MemoryMap::RAM_SIZE)
        {
            return readFromRegion<T>(m_ram, physical - MemoryMap::RAM_BASE, MemoryMap::RAM_SIZE);
        }
        if (physical >= MemoryMap::SCRATCHPAD_BASE &&
            physical < MemoryMap::SCRATCHPAD_BASE + MemoryMap::SCRATCHPAD_SIZE)
        {
            return readFromRegion<T>(m_scratchpad, physical - MemoryMap::SCRATCHPAD_BASE,
                                     MemoryMap::SCRATCHPAD_SIZE);
        }
        if (physical >= MemoryMap::BIOS_BASE &&
            physical < MemoryMap::BIOS_BASE + MemoryMap::BIOS_SIZE)
        {
            return readFromRegion<T>(m_bios, physical - MemoryMap::BIOS_BASE, MemoryMap::BIOS_SIZE);
        }
        if (physical >= MemoryMap::IO_BASE && physical < MemoryMap::IO_BASE + MemoryMap::IO_SIZE)
        {
            return readMmio<T>(physical);
        }
        return {};
    }

    /**
     * @brief Write to PSX memory
     * @param address Memory address
     * @param value Value to write
     */
    template <typename T> void write(Address address, T value)
    {
        Address physical = normalizeAddress(address);
        if (physical >= MemoryMap::RAM_BASE && physical < MemoryMap::RAM_BASE + MemoryMap::RAM_SIZE)
        {
            writeToRegion<T>(m_ram, physical - MemoryMap::RAM_BASE, MemoryMap::RAM_SIZE, value);
            return;
        }
        if (physical >= MemoryMap::SCRATCHPAD_BASE &&
            physical < MemoryMap::SCRATCHPAD_BASE + MemoryMap::SCRATCHPAD_SIZE)
        {
            writeToRegion<T>(m_scratchpad, physical - MemoryMap::SCRATCHPAD_BASE,
                             MemoryMap::SCRATCHPAD_SIZE, value);
            return;
        }
        if (physical >= MemoryMap::IO_BASE && physical < MemoryMap::IO_BASE + MemoryMap::IO_SIZE)
        {
            writeMmio<T>(physical, value);
        }
    }

    /**
     * @brief Get pointer to RAM
     * @return Pointer to 2MB RAM
     */
    u8* getRam();

    void callGpuIntrinsic(Address /*address*/)
    {
        std::abort();
    }
    void callSpuIntrinsic(Address /*address*/)
    {
        std::abort();
    }
    void callCdromIntrinsic(Address /*address*/)
    {
        std::abort();
    }

  private:
    u8* m_ram;        // 2MB main RAM
    u8* m_scratchpad; // 1KB scratchpad
    u8* m_bios;       // 512KB BIOS

    void initMemory();
    void cleanupMemory();

    static Address normalizeAddress(Address address)
    {
        return address & 0x1FFFFFFF;
    }

    template <typename T> T readFromRegion(const u8* base, Address offset, Address size) const
    {
        if (!base || offset + sizeof(T) > size)
        {
            return {};
        }
        T value{};
        std::memcpy(&value, base + offset, sizeof(T));
        return value;
    }

    template <typename T> void writeToRegion(u8* base, Address offset, Address size, T value)
    {
        if (!base || offset + sizeof(T) > size)
        {
            return;
        }
        std::memcpy(base + offset, &value, sizeof(T));
    }

    template <typename T> T readMmio(Address /*address*/)
    {
        return {};
    }
    template <typename T> void writeMmio(Address /*address*/, T /*value*/) {}
};

} // namespace runtime
} // namespace psxrecomp
