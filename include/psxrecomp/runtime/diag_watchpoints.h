#pragma once

#include "psxrecomp/runtime/diag_types.h"

#include <string>
#include <vector>

namespace psxrecomp
{
namespace runtime
{

class RuntimeLogger;

/// Record of a single watchpoint violation or observation.
struct WatchpointEvent
{
    const WatchpointConfig* config = nullptr;
    Address writerPc = 0;
    Address address = 0;
    u8 size = 0;
    u32 oldValue = 0;
    u32 newValue = 0;
    bool predicateViolated = false;
    /// Non-zero when the writer was executing inside a resumed function.
    Address resumeAddress = 0;
};

/// Profile-driven watchpoint engine that replaces ad-hoc watched-write logic.
class DiagWatchpointEngine
{
  public:
    static constexpr size_t EVENT_RING_SIZE = 64;

    DiagWatchpointEngine();

    /// Configure from profile watchpoint definitions and memory map.
    void configure(const std::vector<WatchpointConfig>& configs,
                   const DiagMemoryMapConfig& memMap);

    /// Merge additional watched ranges from PSXRECOMP_WATCH_WRITE env var.
    void mergeEnvWatchedRanges();

    /// Check whether a RAM write at the given address should be intercepted.
    bool shouldWatchRamWrite(Address address, u8 size) const;

    /// Record a RAM write and evaluate watchpoint predicates.
    /// @param resumeAddress Non-zero when the writer is inside a resumed function entry.
    void recordRamWrite(Address writerPc, Address address, u8 size, u32 oldValue, u32 newValue,
                        RuntimeLogger* logger, Address resumeAddress = 0);

    /// Check whether any watchpoint has fired with a trap action.
    bool hasTrapViolation() const;

    /// Format a summary of recent watchpoint events.
    std::string formatSummary() const;

    /// Number of configured watchpoints.
    size_t watchpointCount() const;

    /// Number of recorded events.
    size_t eventCount() const;

    /// Clear all events (but keep configuration).
    void clearEvents();

  private:
    struct EnvRange
    {
        Address start = 0;
        Address end = 0;
    };

    bool evaluatePredicate(const WatchpointPredicate& pred, u32 value) const;
    Address normalizeAddress(Address address) const;
    bool isAlignedPointerInRegion(u32 value, Address regionStart, Address regionEnd) const;

    std::vector<WatchpointConfig> m_configs;
    DiagMemoryMapConfig m_memMap;
    std::vector<EnvRange> m_envRanges;
    std::vector<WatchpointEvent> m_events;
    bool m_trapViolation = false;
};

} // namespace runtime
} // namespace psxrecomp
