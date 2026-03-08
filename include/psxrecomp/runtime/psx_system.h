#pragma once

#include "psxrecomp/runtime/bios_file_table.h"
#include "psxrecomp/runtime/cdrom.h"
#include "psxrecomp/runtime/cop0.h"
#include "psxrecomp/runtime/debug_overlay.h"
#include "psxrecomp/runtime/dma.h"
#include "psxrecomp/runtime/gpu.h"
#include "psxrecomp/runtime/gte.h"
#include "psxrecomp/runtime/input.h"
#include "psxrecomp/runtime/interrupt_controller.h"
#include "psxrecomp/runtime/interrupt_dispatcher.h"
#include "psxrecomp/runtime/kernel_events.h"
#include "psxrecomp/runtime/logger.h"
#include "psxrecomp/runtime/mdec.h"
#include "psxrecomp/runtime/memory_map.h"
#include "psxrecomp/runtime/scheduler.h"
#include "psxrecomp/runtime/sio0.h"
#include "psxrecomp/runtime/spu.h"
#include "psxrecomp/runtime/stall_classifier.h"
#include "psxrecomp/runtime/timers.h"
#include "psxrecomp/types.h"

#include <array>
#include <cstddef>
#include <cstring>
#include <memory>
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
    enum class CallbackContextDisposition
    {
        RestoreSaved,
        CommitMutated,
    };

    static constexpr u32 BIOS_C0_TABLE_ADDRESS = 0x80000500u;
    static constexpr u32 BIOS_C0_HANDLER_TABLE_ADDRESS = 0x80000540u;
    static constexpr u32 BIOS_B0_TABLE_ADDRESS = 0x80000580u;
    static constexpr u32 BIOS_B0_HANDLER_TABLE_ADDRESS = 0x800005C0u;

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
            const Address offset = physical - MemoryMap::RAM_BASE;
            const u8 writeSize = static_cast<u8>(sizeof(T));
            if (m_stallClassifier.shouldWatchRamWrite(physical, writeSize))
            {
                const T oldValue = readFromRegion<T>(m_ram.data(), offset, MemoryMap::RAM_SIZE);
                writeToRegion<T>(m_ram.data(), offset, MemoryMap::RAM_SIZE, value);
                m_stallClassifier.recordRamWrite(m_debugOverlay.lastProgramCounter(), physical,
                                                 writeSize, static_cast<u32>(oldValue),
                                                 static_cast<u32>(value));
            }
            else
            {
                writeToRegion<T>(m_ram.data(), offset, MemoryMap::RAM_SIZE, value);
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

    /**
     * @brief Get pointer to RAM
     * @return Pointer to 2MB RAM
     */
    u8* getRam();
    const u8* getRam() const;

    Gpu& gpu();
    Spu& spu();
    Cdrom& cdrom();
    Mdec& mdec();
    InputController& input();
    Sio0& sio0();
    DmaController& dma();
    InterruptController& interrupts();
    Scheduler& scheduler();
    RuntimeLogger& logger();
    RuntimeDebugOverlay& debugOverlay();
    TimerController& timers();
    Cop0& cop0();
    Gte& gte();
    StallClassifier& stallClassifier();

    /// Record the current recompiled program counter and run targeted diagnostics.
    void observeProgramCounter(Address pc);

    /// Validate the allocator heap at a risky runtime boundary when enabled.
    void validateAllocatorHeapBoundary(const std::string& source, Address relatedAddress = 0);

    /// Validate the allocator heap after returning from a watched allocator function.
    void validateAllocatorHeapCallBoundary(Address address);

    void setDisc(std::shared_ptr<Disc> disc);

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
     * @brief Invoke a PSX callback and return $v0.
     *
     * Used internally for BIOS IRQ priority-chain emulation.
     * May throw ReturnFromExceptionSignal.
     */
    u32 invokeCallbackRaw(u32 address);

    /**
     * @brief Apply queued HookEntryInt register state to callback registers.
     *
     * HookEntryInt behaves like longjmp(setjmp_buf, 1), so only callee-saved
     * registers plus v0 are restored. Caller-saved registers are left intact.
     *
     * @return true if queued register state was applied.
     */
    CallbackContextDisposition consumePendingCallbackRegisters(std::array<u32, 32>& regsInOut);

    u32 callbackContextCommitGeneration() const;

    /**
     * @brief Current critical-section nesting depth.
     */
    u32 criticalSectionDepth() const;

  private:
    struct ReturnFromExceptionSignal
    {
    };

    void bindGteRuntimeHooks();

    std::vector<u8> m_ram;        // 2MB main RAM
    std::vector<u8> m_scratchpad; // 1KB scratchpad
    std::vector<u8> m_bios;       // 512KB BIOS

    Gpu m_gpu;
    Spu m_spu;
    Cdrom m_cdrom;
    Mdec m_mdec;
    InputController m_input;
    Sio0 m_sio0;
    DmaController m_dma;
    InterruptController m_interrupts;
    KernelEventTable m_events;
    InterruptDispatcher m_dispatcher;
    Scheduler m_scheduler;
    uint64_t m_cpuCycles = 0;
    uint64_t m_gpuDrainCarry = 0;
    bool m_videoSchedulePrimed = false;
    u32 m_videoLineScheduleCarry = 0;
    RuntimeLogger m_logger;
    RuntimeDebugOverlay m_debugOverlay;
    TimerController m_timers;
    Cop0 m_cop0;
    Gte m_gte;
    StallClassifier m_stallClassifier;
    std::shared_ptr<Disc> m_disc;
    DiscSwapInfo m_discSwapInfo;
    bool m_discSwapInfoInitialized = false;
    u32 m_frameCount = 0;
    u32 m_criticalSectionDepth = 0;    ///< Tracks nested Enter/ExitCriticalSection syscalls
    CallbackInvoker m_callbackInvoker; ///< Bridge for direct BIOS callback invocation
    struct HookEntryIntState
    {
        u32 descriptorAddress = 0; ///< B0(19) HookEntryInt setjmp buffer pointer.
    };
    struct BiosCdromState
    {
        bool initialized = false;
        u32 handleStorageAddress = 0;
        std::array<u32, 5> eventHandles{};
        u32 asyncResultPtr = 0;   ///< Destination for CdAsyncGetStatus result.
        u32 asyncReadBuffer = 0;  ///< Destination buffer for sector reads.
        u32 asyncReadCount = 0;   ///< Sectors remaining to read.
        u32 asyncSectorsRead = 0; ///< Sectors copied so far.
    };
    struct GpuPortTraceEntry
    {
        Address pc = 0;
        Address address = 0;
        u32 value = 0;
        uint64_t sequence = 0;
    };
    struct GpuWaitTraceState
    {
        bool configured = false;
        bool enabled = false;
        uint64_t sequence = 0;
        uint64_t helperEntries = 0;
        std::array<GpuPortTraceEntry, 8> recentWrites{};
        size_t recentWriteCount = 0;
        size_t recentWriteHead = 0;
        bool hasInitialStatus = false;
        u32 initialStatus = 0;
        bool hasCompareStatus = false;
        u32 compareStatus = 0;
        bool hasLoopStatus = false;
        u32 loopStatus = 0;
    };
    struct DisplayTimingTraceState
    {
        bool configured = false;
        bool enabled = false;
        uint64_t tickDisplayCalls = 0;
        bool hasGpuStatus = false;
        u32 gpuStatus = 0;
        bool hasTimer1Counter = false;
        u32 timer1Counter = 0;
        bool hasDisplayLine = false;
        u32 displayLine = 0;
        Gpu::DisplayPhase displayPhase = Gpu::DisplayPhase::ActiveDisplay;
        bool oddField = false;
        bool hasHelperLastCounter = false;
        u32 helperLastCounter = 0;
        bool hasHelperLastReturn = false;
        u32 helperLastReturn = 0;
        bool hasHelperCachedReturn = false;
        u32 helperCachedReturn = 0;
        bool hasThreshold = false;
        u32 threshold = 0;
        bool hasThresholdCounter = false;
        u32 thresholdCounter = 0;
        bool hasThresholdMessage = false;
        u32 thresholdMessage = 0;
        bool hasThresholdExceeded = false;
        bool thresholdExceeded = false;
    };
    HookEntryIntState m_hookEntryInt;
    BiosCdromState m_biosCdrom;
    GpuWaitTraceState m_gpuWaitTrace;
    DisplayTimingTraceState m_displayTimingTrace;
    BiosFileTable m_biosFt;
    bool m_inHookEntryIntHandler = false;
    bool m_inCallbackInvocation = false;
    bool m_hasPendingCallbackRegisters = false;
    u32 m_callbackContextCommitGeneration = 0;
    std::array<u32, 32> m_pendingCallbackRegisters{};
    std::array<bool, 32> m_pendingCallbackRegisterMask{};

    /// BIOS IRQ priority chains (C0:02 SysEnqIntRP / C0:03 SysDeqIntRP).
    /// Each head is a PSX pointer to a 16-byte structure in RAM.
    std::array<u32, 4> m_irqChainHeads{};

    /**
     * @brief Run BIOS IRQ priority chains once (ExceptionHandler model).
     * @return true if a handler executed ReturnFromException (abort lower-priority processing).
     */
    bool dispatchIrqChains();

    /**
     * @brief Prime periodic VBlank/display events.
     */
    void primeVideoSchedule();

    void handleDisplayLineTick();
    void syncLevelInterruptSources();
    void syncCop0InterruptPending();
    void invokeHookEntryIntHandler();
    void initializeBiosCdromState(u32 handleStorageAddress);
    void resetBiosCdromState();
    bool serviceBiosCdromInterrupt();
    bool callBiosCdFunction(u32 functionId, u32* regs);

    /**
     * @brief Block until a kernel event is delivered, advancing hardware.
     *
     * Implements the real PSX BIOS WaitEvent semantics for NoCallback
     * events: tick CPU cycles, service interrupts, and pump hardware
     * until the event transitions to Delivered, or until a watchdog
     * fires.
     *
     * @param handle  The kernel event handle to wait on.
     * @return true if the event was delivered, false on watchdog timeout.
     */
    bool waitForEvent(u32 handle);

    struct RamCopyBounds
    {
        bool destinationInRam = false;
        bool destinationOverflow = false;
        Address physicalDestination = 0;
        u32 writableLength = 0;
    };

    RamCopyBounds planRamCopy(Address destination, u32 requestedLength) const;
    u32 copyBufferToRam(Address destination, const u8* source, u32 actualLength,
                        u32 requestedLength, Address writerPc, const std::string& sourceTag,
                        const std::string& detail);
    u32 fillBufferToRam(Address destination, u8 value, u32 requestedLength, Address writerPc,
                        const std::string& sourceTag, const std::string& detail);
    void logRamCopyWarning(const std::string& sourceTag, Address destination,
                           u32 requestedLength, const RamCopyBounds& bounds, u32 actualLength);
    bool gpuWaitTraceEnabled();
    void traceGpuWaitProgramCounter(Address pc);
    void traceGpuWaitStatusRead(Address pc, u32 value);
    void recordGpuPortTrace(Address address, u32 value);
    std::string formatRecentGpuPortWrites() const;
    bool displayTimingTraceEnabled();
    void traceDisplayTimingProgramCounter(Address pc);
    void traceDisplayTimingSnapshot(const char* source, Address pc);
    void traceDisplayLineTick(Address pc, u32 callIndex, u16 previousLine,
                              Gpu::DisplayPhase previousPhase, bool previousOddField);
    static const char* displayPhaseName(Gpu::DisplayPhase phase);

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
