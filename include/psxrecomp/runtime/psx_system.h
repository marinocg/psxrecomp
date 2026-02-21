#pragma once

#include "psxrecomp/runtime/cdrom.h"
#include "psxrecomp/runtime/debug_overlay.h"
#include "psxrecomp/runtime/dma.h"
#include "psxrecomp/runtime/gpu.h"
#include "psxrecomp/runtime/input.h"
#include "psxrecomp/runtime/interrupt_controller.h"
#include "psxrecomp/runtime/interrupt_dispatcher.h"
#include "psxrecomp/runtime/kernel_events.h"
#include "psxrecomp/runtime/logger.h"
#include "psxrecomp/runtime/memory_map.h"
#include "psxrecomp/runtime/scheduler.h"
#include "psxrecomp/runtime/spu.h"
#include "psxrecomp/runtime/timers.h"
#include "psxrecomp/types.h"

#include <cstddef>
#include <cstring>
#include <optional>
#include <sstream>
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
     * @brief Advance emulated hardware by CPU cycles.
     *
     * Recompiled code should call this periodically so hardware timing is
     * driven by executed instructions, not by MMIO polling patterns.
     */
    void tickCpuCycles(u32 cpuCycles);

    /**
     * @brief Total emulated CPU cycles since reset.
     */
    uint64_t cpuCyclesElapsed() const;

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

    /**
     * @brief Legacy compatibility switch.
     *
     * Kept for ABI/source compatibility with generated modules; this runtime
     * now uses cycle-driven timing and ignores this toggle.
     */
    void setAutoFrameProgressOnInterruptPoll(bool enabled);

    /**
     * @brief Register a RAM address containing the vsync frame counter.
     *
     * Legacy compatibility entry point. The runtime no longer mutates
     * arbitrary RAM to emulate SDK-specific handlers.
     */
    void setVsyncCounterAddress(Address address);

    /**
     * @brief Register the RAM address of PSn00bSDK's "GPU busy" byte.
     *
     * Legacy compatibility entry point. The runtime no longer patches RAM
     * for SDK-specific DrawSync behavior.
     */
    void setDrawSyncBusyAddress(Address address);

    /**
     * @brief Get the monotonic frame counter (incremented on each VBlank).
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

    /**
     * @brief Access the kernel event table.
     */
    KernelEventTable& events();

    /**
     * @brief Access the interrupt dispatcher.
     */
    InterruptDispatcher& dispatcher();

    /**
     * @brief Install the callback invoker bridge.
     *
     * The generated module calls this during run() so that the interrupt
     * dispatcher can invoke recompiled callback code.
     */
    void setCallbackInvoker(CallbackInvoker invoker);

    /**
     * @brief Service pending interrupts and dispatch kernel events.
     *
     * Should be called from generated code at safe points (after
     * setProgramCounter updates) to allow interrupt-driven callbacks
     * to execute.
     */
    void serviceInterrupts();

    /**
     * @brief Invoke a PSX callback at the given address.
     *
     * Uses the installed callback invoker to call into recompiled code.
     * No-op if no invoker is installed.
     */
    void invokeCallback(u32 address);

    /**
     * @brief Current critical-section nesting depth.
     */
    u32 criticalSectionDepth() const;

  private:
    struct ReturnFromExceptionSignal
    {
    };

    std::vector<u8> m_ram;        // 2MB main RAM
    std::vector<u8> m_scratchpad; // 1KB scratchpad
    std::vector<u8> m_bios;       // 512KB BIOS

    Gpu m_gpu;
    Spu m_spu;
    Cdrom m_cdrom;
    InputController m_input;
    DmaController m_dma;
    InterruptController m_interrupts;
    KernelEventTable m_events;
    InterruptDispatcher m_dispatcher;
    Scheduler m_scheduler;
    uint64_t m_cpuCycles = 0;
    uint64_t m_gpuDrainCarry = 0;
    bool m_videoSchedulePrimed = false;
    RuntimeLogger m_logger;
    RuntimeDebugOverlay m_debugOverlay;
    TimerController m_timers;
    DiscSwapInfo m_discSwapInfo;
    u32 m_frameCount = 0;
    u32 m_criticalSectionDepth = 0;    ///< Tracks nested Enter/ExitCriticalSection syscalls
    u32 m_customExitHandler = 0;       ///< Address set by SetCustomExitFromException (B0 0x19)
    CallbackInvoker m_callbackInvoker; ///< Bridge for direct BIOS callback invocation
    bool m_inCustomExitHandler = false;
    bool m_inCallbackInvocation = false;
    std::optional<Address> m_legacyDrawSyncDispatcher;
    bool m_legacyDrawSyncScanDone = false;

    /**
     * @brief Legacy no-op compatibility hook.
     */
    void clearDrawSyncBusy();

    /**
     * @brief Prime periodic VBlank/display events.
     */
    void primeVideoSchedule();

    void handleVBlankStart();
    void invokeCustomExitHandler();
    u32 resolveCustomExitCallback(u32 address) const;

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

    /// Dispatch helpers for BIOS vector sub-tables.
    bool callBiosVectorA0(u32 functionId, u32* regs);
    bool callBiosVectorB0(u32 functionId, u32* regs);
    bool callBiosVectorC0(u32 functionId, u32* regs);
};

} // namespace runtime
} // namespace psxrecomp
