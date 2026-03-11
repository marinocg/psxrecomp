#pragma once

#include "psxrecomp/runtime/diag_types.h"

#include <unordered_map>
#include <string>
#include <vector>

namespace psxrecomp
{
namespace runtime
{

class RuntimeLogger;

/// Record of a tracepoint hit.
struct TracepointHit
{
    const TracepointConfig* config = nullptr;
    Address pc = 0;
    Address callerPc = 0;
    Address returnPc = 0;
    Address resumeAddress = 0;
    u32 callbackCommitGeneration = 0;
    u32 irqStatus = 0;
    u32 irqMask = 0;
    u32 irqPendingMasked = 0;
    bool isEntry = false; ///< true if this is entry into the PC range
    bool isExit = false;  ///< true if this is exit from the PC range
};

/// Profile-driven PC-range tracepoint engine.
class DiagTracepointEngine
{
  public:
    DiagTracepointEngine();

    /// Configure from profile tracepoint definitions.
    void configure(const std::vector<TracepointConfig>& configs);

    /// Observe a program counter and emit trace events if inside a traced range.
    void observePc(Address pc, Address resumeAddress, u32 callbackCommitGeneration, u32 irqStatus,
                   u32 irqMask, RuntimeLogger* logger);

    /// Record an MMIO read and log if it falls within a traced address set.
    void recordMmioRead(Address mmioAddress, u32 value, Address pc, RuntimeLogger* logger);

    /// Whether any tracepoint is configured.
    bool hasTracepoints() const;

    /// Check if a given PC is within any configured tracepoint range.
    bool isInTracedRange(Address pc) const;

    /// Format a summary of recent tracepoint activity.
    std::string formatRecentTraces() const;

    /// Number of configured tracepoints.
    size_t tracepointCount() const;

  private:
    struct ActiveRange
    {
        const TracepointConfig* config = nullptr;
        bool inside = false;
        Address entryPc = 0;
        Address entryCallerPc = 0;
        Address entryResumeAddress = 0;
        u32 entryCallbackCommitGeneration = 0;
        u32 entryIrqStatus = 0;
        u32 entryIrqMask = 0;
        u32 entryIrqPendingMasked = 0;
        Address lastRepeatedCallerPc = 0;
        Address lastRepeatedResumeAddress = 0;
        u32 lastRepeatedCallbackCommitGeneration = 0;
        u32 lastRepeatedIrqPendingMasked = 0;
        u32 repeatCount = 0;
        bool repeatLogged = false;
        std::unordered_map<Address, u32> callerHistogram;
    };

    std::vector<TracepointConfig> m_configs;
    std::vector<ActiveRange> m_ranges;
    std::vector<TracepointHit> m_recentHits;
    Address m_previousPc = 0;
    static constexpr size_t MAX_RECENT_HITS = 32;
};

} // namespace runtime
} // namespace psxrecomp
