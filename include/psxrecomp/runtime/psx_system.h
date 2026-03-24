#pragma once

#include "psxrecomp/runtime/bios_file_table.h"

#ifdef __clang__
#pragma clang diagnostic pop
#endif
#include "psxrecomp/runtime/callback_trace.h"
#include "psxrecomp/runtime/cdrom.h"
#include "psxrecomp/runtime/control_plane_timeline.h"
#include "psxrecomp/runtime/cop0.h"
#include "psxrecomp/runtime/debug_overlay.h"
#include "psxrecomp/runtime/diag_boundaries.h"
#include "psxrecomp/runtime/diag_cdrom_bank_tracer.h"
#include "psxrecomp/runtime/diag_cdrom_late_buffer_tracker.h"
#include "psxrecomp/runtime/diag_explainers.h"
#include "psxrecomp/runtime/diag_metadata_watch.h"
#include "psxrecomp/runtime/diag_profile.h"
#include "psxrecomp/runtime/diag_rev2_decoder_handoff_tracker.h"
#include "psxrecomp/runtime/diag_tracepoints.h"
#include "psxrecomp/runtime/diag_validators.h"
#include "psxrecomp/runtime/diag_watchpoints.h"
#include "psxrecomp/runtime/dma.h"
#include "psxrecomp/runtime/gpu.h"
#include "psxrecomp/runtime/gte.h"
#include "psxrecomp/runtime/hook_entry_int_trace.h"
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

/// Runtime environment for recompiled PSX code: memory management and hardware emulation.
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
#endif
class PsxSystem
{
  public:
    enum class CpuExecutionPhase
    {
        Reset,
        BootInitializing,
        AwaitingExecutableEntry,
        Running,
    };

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

    bool initialize();
    void reset();
    void boot();
    void runFrame();

    /// Advance emulated hardware by cpu_cycles; call periodically from recompiled code.
    void tickCpuCycles(u32 cpuCycles);

    /// Total emulated CPU cycles since reset.
    uint64_t cpuCyclesElapsed() const;

    /// Read T from PSX address space (RAM / scratchpad / BIOS / MMIO).
    template <typename T> T read(Address address);

    /// Write T to PSX address space (RAM / scratchpad / MMIO).
    template <typename T> void write(Address address, T value);

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
    CallbackTraceEngine& callbackTrace();
    const CallbackTraceEngine& callbackTrace() const;
    std::string formatHookEntryIntResumeTrace() const;

    bool loadDiagProfile(const std::string& path = "");
    const DiagProfile& diagProfile() const;
    DiagWatchpointEngine& diagWatchpoints();
    DiagTracepointEngine& diagTracepoints();
    DiagValidatorEngine& diagValidators();
    const DiagValidatorEngine& diagValidators() const;
    DiagBoundaryDispatcher& diagBoundaries();
    DiagExplainerEngine& diagExplainers();
    DiagMetadataWatchEngine& diagMetadataWatch();
    DiagCdromBankTracer& diagCdromBankTracer();
    DiagCdromLateBufferTracker& diagCdromLateBufferTracker();
    DiagRev2DecoderHandoffTracker& diagRev2DecoderHandoffTracker();

    /// Set/get the last resume address for diagnostic context.
    void setLastResumeAddress(Address address);
    Address lastResumeAddress() const;

    void observeProgramCounter(Address pc, const u32* regs = nullptr, size_t regCount = 0);
    void observeProgramCounter(Address architecturalPc, Address observedPc, const u32* regs,
                               size_t regCount);
    Address architecturalProgramCounter() const;
    CpuExecutionPhase cpuExecutionPhase() const;

    void validateAllocatorHeapBoundary(const std::string& source, Address relatedAddress = 0);
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

    /// Handle a BIOS vector call (A0h/B0h/C0h); regs is a 32-element register file.
    void callBiosVector(u32 vector, u32* regs, size_t regCount);

    std::string describeBiosCdromState() const;

    /// Monotonic frame counter; incremented on each VBlank.
    u32 frameCount() const;

    /// Advance one frame and return the new frame count (for idle-loop detectors).
    u32 advanceFrame();

    template <typename T> T readMmioExplicit(Address address);
    template <typename T> void writeMmioExplicit(Address address, T value);

    void callBiosSyscall(u32 code, u32* regs, size_t regCount);

    KernelEventTable& events();
    InterruptDispatcher& dispatcher();

    /// Install the callback invoker bridge used by the interrupt dispatcher.
    void setCallbackInvoker(CallbackInvoker invoker);

    /// Service pending interrupts and dispatch kernel events; call at safe recompiled-code points.
    void serviceInterrupts();

    /// Invoke a PSX callback at address; no-op if no invoker is installed.
    void invokeCallback(u32 address, u32 descriptorAddress = 0);

    /// Invoke a PSX callback and return $v0; may throw ReturnFromExceptionSignal.
    u32 invokeCallbackRaw(u32 address, u32 descriptorAddress = 0);

    /// Apply queued HookEntryInt register state; returns CommitMutated if state was applied.
    CallbackContextDisposition consumePendingCallbackRegisters(std::array<u32, 32>& regsInOut);

    u32 callbackContextCommitGeneration() const;

    /// Current critical-section nesting depth.
    u32 criticalSectionDepth() const;

    /// Read-only access to the control-plane timeline ring buffer.
    const RuntimeControlPlaneTimeline& cpTimeline() const;

  private:
    struct ReturnFromExceptionSignal
    {
    };

    void bindGteRuntimeHooks();
    void applyCpuBootState(const CpuBootState& state);
    void noteExecutableEntry(Address pc = 0);
    void setCpuExecutionPhase(CpuExecutionPhase phase);

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
    CpuExecutionPhase m_cpuExecutionPhase = CpuExecutionPhase::Reset;
    TimerController m_timers;
    Cop0 m_cop0;
    Gte m_gte;
    StallClassifier m_stallClassifier;
    CallbackTraceEngine m_callbackTrace;
    HookEntryIntTraceEngine m_hookEntryIntTrace;
    std::shared_ptr<Disc> m_disc;
    DiscSwapInfo m_discSwapInfo;
    bool m_discSwapInfoInitialized = false;
    u32 m_frameCount = 0;
    u32 m_pendingSpuDmaCompletionCycles = 0;
    bool m_pendingSpuDmaCompletion = false;
    bool m_inDmaTransfer = false;
    u32 m_criticalSectionDepth = 0;
    CallbackInvoker m_callbackInvoker;

    struct HookEntryIntState
    {
        u32 descriptorAddress = 0; ///< B0(19) HookEntryInt setjmp buffer pointer.
    };

    struct BiosCdromState
    {
        bool initialized = false;
        u32 handleStorageAddress = 0;
        std::array<u32, 5> eventHandles{};
        bool asyncReadActive = false;
        bool asyncCommandDoneOnAck = false;
        u32 asyncResultPtr = 0;
        u32 asyncReadBuffer = 0;
        u32 asyncReadCount = 0;
        u32 asyncSectorsRead = 0;
        u32 asyncReadMode = 0;
        u32 asyncReadSectorBytes = 0;
        u8 lastDeliveredIrqType = 0;
        u32 lastDeliveredIrqGeneration = 0;
    };

    struct GpuPortTraceEntry
    {
        Address pc = 0;
        Address address = 0;
        u32 value = 0;
        uint64_t sequence = 0;
    };

    HookEntryIntState m_hookEntryInt;
    BiosCdromState m_biosCdrom;
    BiosFileTable m_biosFt;
    bool m_inHookEntryIntHandler = false;
    bool m_inCallbackInvocation = false;
    bool m_hasPendingCallbackRegisters = false;
    u32 m_callbackContextCommitGeneration = 0;
    std::array<u32, 32> m_pendingCallbackRegisters{};
    std::array<bool, 32> m_pendingCallbackRegisterMask{};

    // PSX-SPX: ChangeClearRCnt(t, flag) auto-clear policy (index 0=T0, 1=T1, 2=T2, 3=VBlank).
    std::array<bool, 4> m_changeClearRCntPolicy{};

    // Deferred CDROM DMA: stays busy (bit24 set) until DRQSTS=1.
    bool m_pendingCdromDmaDeferred = false;

    // PSX-SPX edge tracking: I_STAT latches only on false→true transitions.
    bool m_prevGpuIrq = false;
    bool m_prevCdromIrq = false;
    bool m_prevSpuIrq = false;
    bool m_prevDmaIrq = false;
    /// Cdrom::irqEdgeGeneration() snapshot; reset m_prevCdromIrq on advance (same-call pulses).
    u32 m_cdromIrqEdgeGeneration = 0u;

    /// Post-mortem diagnostic ring buffer for control-plane transitions.
    RuntimeControlPlaneTimeline m_cpTimeline;

    /// BIOS IRQ priority chains (C0:02 SysEnqIntRP / C0:03 SysDeqIntRP).
    std::array<u32, 4> m_irqChainHeads{};

    static constexpr u32 IRQ_CHAIN_DATA_SNAPSHOT_WORDS = 16;
    struct IrqChainSnapshot
    {
        u32 baseAddress = 0;
        std::array<u32, IRQ_CHAIN_DATA_SNAPSHOT_WORDS> words{};
    };
    std::array<IrqChainSnapshot, 4> m_irqChainSnapshots{};
    void saveIrqChainSnapshot(u32 priority, u32 structAddress);
    void restoreIrqChainSnapshot(u32 priority);

    u32 m_biosHeapBase = 0;
    u32 m_biosHeapSize = 0;
    u32 m_biosHeapCursor = 0;

    DiagProfile m_diagProfile;
    DiagWatchpointEngine m_diagWatchpoints;
    DiagTracepointEngine m_diagTracepoints;
    DiagExplainerEngine m_diagExplainers;
    DiagValidatorEngine m_diagValidators;
    DiagBoundaryDispatcher m_diagBoundaries;
    DiagMetadataWatchEngine m_diagMetadataWatch;
    DiagCdromBankTracer m_diagCdromBankTracer;
    DiagCdromLateBufferTracker m_diagCdromLateBufferTracker;
    DiagRev2DecoderHandoffTracker m_diagRev2DecoderHandoffTracker;

    Address m_lastResumeAddress = 0;

    /// Run BIOS IRQ priority chains; returns true if a handler issued ReturnFromException.
    bool dispatchIrqChains();

    /// Inner IRQ work: chains, CD-ROM, HookEntryInt, kernel events (called from serviceInterrupts).
    void serviceIrqWork(u32 pendingMasked);

    void primeVideoSchedule();
    void handleDisplayLineTick();
    void syncLevelInterruptSources();
    void syncCop0InterruptPending();
    void invokeHookEntryIntHandler();
    void initializeBiosCdromState(u32 handleStorageAddress);
    void resetBiosCdromState();
    bool serviceBiosCdromInterrupt();
    bool callBiosCdFunction(u32 functionId, u32* regs);

    /// Block until kernel event handle is delivered, ticking hardware; false on watchdog timeout.
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
    void logRamCopyWarning(const std::string& sourceTag, Address destination, u32 requestedLength,
                           const RamCopyBounds& bounds, u32 actualLength);

    static Address normalizeAddress(Address address) { return address & 0x1FFFFFFF; }
    static bool isInRange(Address address, Address base, Address size)
    {
        return address >= base && (address - base) < size;
    }

    template <typename T> T readFromRegion(const u8* base, Address offset, Address size) const;
    template <typename T> void writeToRegion(u8* base, Address offset, Address size, T value);
    template <typename T> T readMmio(Address address);
    template <typename T> void writeMmio(Address address, T value);

    u32 readMmio32(Address address);
    u16 readMmio16(Address address);
    u8 readMmio8(Address address);
    void writeMmio32(Address address, u32 value);
    void writeMmio16(Address address, u16 value);
    void writeMmio8(Address address, u8 value);

    void handleDmaTransfer(DmaPort port);

    bool callBiosVectorA0(u32 functionId, u32* regs);
    bool callBiosVectorB0(u32 functionId, u32* regs);
    bool callBiosVectorC0(u32 functionId, u32* regs);
};

} // namespace runtime
} // namespace psxrecomp

#include "psx_system_inl.h"
