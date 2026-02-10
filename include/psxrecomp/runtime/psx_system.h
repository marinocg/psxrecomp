#pragma once

#include "psxrecomp/runtime/cdrom.h"
#include "psxrecomp/runtime/dma.h"
#include "psxrecomp/runtime/gpu.h"
#include "psxrecomp/runtime/input.h"
#include "psxrecomp/runtime/interrupt_controller.h"
#include "psxrecomp/runtime/logger.h"
#include "psxrecomp/runtime/memory_map.h"
#include "psxrecomp/runtime/scheduler.h"
#include "psxrecomp/runtime/spu.h"
#include "psxrecomp/types.h"

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

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
    struct DiscSwapInfo
    {
        struct DiscEntry
        {
            u32 index = 0;
            std::string label;
            std::string path;
        };
        std::string setName;
        u32 activeDiscIndex = 0;
        std::vector<DiscEntry> discs;
    };

    PsxSystem();
    ~PsxSystem();

    PsxSystem(const PsxSystem&) = delete;
    PsxSystem& operator=(const PsxSystem&) = delete;
    PsxSystem(PsxSystem&&) = delete;
    PsxSystem& operator=(PsxSystem&&) = delete;

    /**
     * @brief Initialize the PSX system
     * @return true if successful
     */
    bool initialize();

    /**
     * @brief Reset system state and hardware
     */
    void reset();

    /**
     * @brief Perform a boot routine
     */
    void boot();

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
        if (isInRange(physical, MemoryMap::RAM_BASE, MemoryMap::RAM_SIZE))
        {
            return readFromRegion<T>(m_ram.data(), physical - MemoryMap::RAM_BASE,
                                     MemoryMap::RAM_SIZE);
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

    /**
     * @brief Write to PSX memory
     * @param address Memory address
     * @param value Value to write
     */
    template <typename T> void write(Address address, T value)
    {
        Address physical = normalizeAddress(address);
        if (isInRange(physical, MemoryMap::RAM_BASE, MemoryMap::RAM_SIZE))
        {
            writeToRegion<T>(m_ram.data(), physical - MemoryMap::RAM_BASE, MemoryMap::RAM_SIZE,
                             value);
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

    /**
     * @brief Get pointer to RAM
     * @return Pointer to 2MB RAM
     */
    u8* getRam();
    const u8* getRam() const;

    Gpu& gpu();
    Spu& spu();
    Cdrom& cdrom();
    InputController& input();
    DmaController& dma();
    InterruptController& interrupts();
    Scheduler& scheduler();
    RuntimeLogger& logger();

    void setDiscSwapInfo(DiscSwapInfo info);
    const DiscSwapInfo& discSwapInfo() const;

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

    template <typename T> T readMmioExplicit(Address address)
    {
        Address physical = normalizeAddress(address);
        return readMmio<T>(physical);
    }

    template <typename T> void writeMmioExplicit(Address address, T value)
    {
        Address physical = normalizeAddress(address);
        writeMmio<T>(physical, value);
    }

    void callBiosSyscall(u32 code, const u32* regs, size_t regCount);

  private:
    std::vector<u8> m_ram;        // 2MB main RAM
    std::vector<u8> m_scratchpad; // 1KB scratchpad
    std::vector<u8> m_bios;       // 512KB BIOS

    Gpu m_gpu;
    Spu m_spu;
    Cdrom m_cdrom;
    InputController m_input;
    DmaController m_dma;
    InterruptController m_interrupts;
    Scheduler m_scheduler;
    RuntimeLogger m_logger;
    DiscSwapInfo m_discSwapInfo;

    static Address normalizeAddress(Address address)
    {
        return address & 0x1FFFFFFF;
    }

    static bool isInRange(Address address, Address base, Address size)
    {
        return address >= base && (address - base) < size;
    }

    template <typename T> T readFromRegion(const u8* base, Address offset, Address size) const
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

    template <typename T> void writeToRegion(u8* base, Address offset, Address size, T value)
    {
        static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4,
                      "Unsupported write size for runtime MMIO");
        if (!base || size < sizeof(T) || offset > (size - sizeof(T)))
        {
            return;
        }
        std::memcpy(base + offset, &value, sizeof(T));
    }

    template <typename T> T readMmio(Address address)
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

    template <typename T> void writeMmio(Address address, T value)
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

    u32 readMmio32(Address address);
    u16 readMmio16(Address address);
    u8 readMmio8(Address address);
    void writeMmio32(Address address, u32 value);
    void writeMmio16(Address address, u16 value);
    void writeMmio8(Address address, u8 value);

    void handleDmaTransfer(DmaPort port);
};

} // namespace runtime
} // namespace psxrecomp
