#pragma once

#include "psxrecomp/runtime/cdrom.h"
#include "psxrecomp/runtime/debug_overlay.h"
#include "psxrecomp/runtime/dma.h"
#include "psxrecomp/runtime/gpu.h"
#include "psxrecomp/runtime/input.h"
#include "psxrecomp/runtime/interrupt_controller.h"
#include "psxrecomp/runtime/logger.h"
#include "psxrecomp/runtime/memory_map.h"
#include "psxrecomp/runtime/scheduler.h"
#include "psxrecomp/runtime/spu.h"
#include "psxrecomp/runtime/timers.h"
#include "psxrecomp/types.h"

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <optional>
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
            // If a VSync counter address has been registered and this is a
            // 32-bit read of that counter, detect spin-wait polling and
            // advance the frame so the counter increments.
            // PSn00bSDK VSync() pattern:
            //   u32 old = *counter;
            //   while (*counter == old) { /* spin */ }
            // We count consecutive reads that see the same counter value.
            // After VSYNC_POLL_THRESHOLD reads, we know the game is
            // polling and we trigger frame advancement.
            if constexpr (sizeof(T) == 4)
            {
                if (m_vsyncCounterAddress != 0 && !m_inVsyncCounterRead &&
                    physical == normalizeAddress(m_vsyncCounterAddress))
                {
                    // Read current counter value from RAM
                    Address cOff = physical - MemoryMap::RAM_BASE;
                    u32 currentCounter = 0;
                    if (cOff + 4 <= MemoryMap::RAM_SIZE)
                    {
                        std::memcpy(&currentCounter, m_ram.data() + cOff, sizeof(u32));
                    }
                    if (currentCounter == m_lastVsyncCounterValue)
                    {
                        ++m_vsyncPollCount;
                        if (m_vsyncPollCount >= VSYNC_POLL_THRESHOLD)
                        {
                            onVsyncCounterRead();
                            m_vsyncPollCount = 0;
                        }
                    }
                    else
                    {
                        // Counter changed (frame was advanced via another path
                        // such as GPUSTAT read).  Reset poll detector.
                        m_lastVsyncCounterValue = currentCounter;
                        m_vsyncPollCount = 0;
                    }
                }
            }
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
    RuntimeDebugOverlay& debugOverlay();
    TimerController& timers();

    void setDiscSwapInfo(DiscSwapInfo info);
    const DiscSwapInfo& discSwapInfo() const;

    std::vector<u8> dumpRam() const;
    std::vector<u8> dumpVram() const;
    std::vector<u8> dumpSpuRam() const;

    std::vector<u8> serializeState() const;
    bool deserializeState(const std::vector<u8>& state);
    uint64_t stateChecksum() const;

    void callGpuIntrinsic(Address address);
    void callSpuIntrinsic(Address address);
    void callCdromIntrinsic(Address address);

    /**
     * @brief Handle a BIOS vector call (A0h/B0h/C0h).
     * @param vector The BIOS vector address (0xA0, 0xB0, or 0xC0).
     * @param regs Pointer to the register file (32 registers).
     * @param regCount Number of registers in the array.
     */
    void callBiosVector(u32 vector, u32* regs, size_t regCount);

    void setAutoFrameProgressOnInterruptPoll(bool enabled);

    /**
     * @brief Register a RAM address containing the vsync frame counter.
     *
     * When set, runFrame() will auto-increment the 32-bit word at this
     * address, simulating the VBlank IRQ handler that PSn00bSDK relies
     * on to detect completed frames.
     */
    void setVsyncCounterAddress(Address address);

    /**
     * @brief Register the RAM address of PSn00bSDK's "GPU busy" byte.
     *
     * When set, GPU commands processed via callGpuIntrinsic() will
     * automatically clear this byte, preventing DrawSync(0) from
     * spinning for its full 1M-iteration timeout.
     */
    void setDrawSyncBusyAddress(Address address);

    /**
     * @brief Get the monotonic frame counter (incremented each runFrame()).
     */
    u32 frameCount() const;

    /**
     * @brief Advance one frame (calls runFrame()) and return the new frame count.
     *
     * Intended for use by idle-loop / RAM-polling detectors in generated code.
     */
    u32 advanceFrame();

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

    void callBiosSyscall(u32 code, u32* regs, size_t regCount);

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
    RuntimeDebugOverlay m_debugOverlay;
    TimerController m_timers;
    DiscSwapInfo m_discSwapInfo;
    bool m_autoFrameProgressOnInterruptPoll = false;
    u32 m_frameCount = 0;
    u32 m_gpuStatReadCount = 0;        ///< Consecutive GPUSTAT reads within same VBlank phase
    Address m_vsyncCounterAddress = 0; ///< RAM address of PSn00bSDK vsync_counter (0 = disabled)
    Address m_drawSyncBusyAddress = 0; ///< RAM address of PSn00bSDK GPU busy byte (0 = disabled)
    u32 m_lastVsyncCounterValue = 0;   ///< Counter value at last frame progression
    u32 m_vsyncPollCount = 0;          ///< Consecutive reads seeing the same counter value
    bool m_inVsyncCounterRead = false; ///< Re-entrancy guard for onVsyncCounterRead()
    u32 m_criticalSectionDepth = 0;    ///< Tracks nested Enter/ExitCriticalSection syscalls
    // Keep VSync spin-loop detection responsive so frame-poll waits
    // (e.g. while (*counter == old)) don't consume most of the step budget.
    static constexpr u32 VSYNC_POLL_THRESHOLD = 64; ///< Reads before triggering frame advancement

    /**
     * @brief Clear PSn00bSDK's DrawSync busy byte if its address is registered.
     *
     * Called after GPU BIOS calls (GPU_cw, GPU_cwp, send_gpu_linked_list)
     * to signal that the GPU operation completed synchronously.
     */
    void clearDrawSyncBusy();

    /**
     * @brief Called when the VSync counter RAM address is read.
     *
     * Advances the display phase so the counter increments, preventing
     * VSync polling loops from spinning forever.
     */
    void onVsyncCounterRead();

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
