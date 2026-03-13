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
    WatchpointKind kind = WatchpointKind::RamWrite;
    Address accessPc = 0;
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
    void configure(const std::vector<WatchpointConfig>& configs, const DiagMemoryMapConfig& memMap);

    /// Merge additional watched ranges from PSXRECOMP_WATCH_WRITE env var.
    void mergeEnvWatchedRanges();

    /// Check whether a RAM write at the given address should be intercepted.
    bool shouldWatchRamWrite(Address address, u8 size) const;

    /// Check whether a RAM read at the given address should be intercepted.
    bool shouldWatchRamRead(Address address, u8 size) const;

    /// Record a RAM write and evaluate watchpoint predicates.
    /// @param resumeAddress Non-zero when the writer is inside a resumed function entry.
    void recordRamWrite(Address writerPc, Address address, u8 size, u32 oldValue, u32 newValue,
                        RuntimeLogger* logger, Address resumeAddress = 0);

    /// Record a RAM read and evaluate watchpoint predicates.
    /// @param resumeAddress Non-zero when the reader is inside a resumed function entry.
    void recordRamRead(Address readerPc, Address address, u8 size, u32 value,
                       RuntimeLogger* logger, Address resumeAddress = 0);

    /// Check whether an MMIO write at the given physical address should be intercepted.
    bool shouldWatchMmioWrite(Address address, u8 size) const;

    /// Check whether an MMIO read at the given physical address should be intercepted.
    bool shouldWatchMmioRead(Address address, u8 size) const;

    /// Record an MMIO write observation.
    void recordMmioWrite(Address writerPc, Address address, u8 size, u32 value,
                         RuntimeLogger* logger, Address resumeAddress = 0);

    /// Record an MMIO read observation.
    void recordMmioRead(Address readerPc, Address address, u8 size, u32 value,
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

    bool shouldWatchRamAccess(WatchpointKind kind, Address address, u8 size) const;
    bool shouldWatchMmioAccess(WatchpointKind kind, Address address, u8 size) const;
    void recordRamAccess(WatchpointKind kind, Address accessPc, Address address, u8 size,
                         u32 oldValue, u32 newValue, RuntimeLogger* logger,
                         Address resumeAddress);
    void recordMmioAccess(WatchpointKind kind, Address accessPc, Address address, u8 size,
                          u32 value, RuntimeLogger* logger, Address resumeAddress);
    bool evaluatePredicate(const WatchpointPredicate& pred, u32 value) const;
    Address normalizeAddress(Address address) const;
    bool isAlignedPointerInRegion(u32 value, Address regionStart, Address regionEnd) const;

    std::vector<WatchpointConfig> m_configs;
    DiagMemoryMapConfig m_memMap;
    std::vector<EnvRange> m_envWriteRanges;
    std::vector<WatchpointEvent> m_events;
    bool m_trapViolation = false;
};

} // namespace runtime
} // namespace psxrecomp
