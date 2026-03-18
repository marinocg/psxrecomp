#pragma once

#include "psxrecomp/types.h"

#include <string>
#include <vector>

namespace psxrecomp
{
namespace runtime
{

/// Memory map boundaries used by diagnostic checks.
struct DiagMemoryMapConfig
{
    Address ramBase = 0x80000000u;
    Address ramEnd = 0x80200000u;
};

/// Watchpoint observation kinds.
enum class WatchpointKind : u8
{
    RamRead,
    RamWrite,
    MmioRead,
    MmioWrite
};

/// Action when a watchpoint predicate fires.
enum class WatchpointAction : u8
{
    Log,
    Summarize,
    Trap,
    TrapOnFirstViolation
};

/// Predicate that decides whether a watched value is noteworthy.
struct WatchpointPredicate
{
    std::string type; ///< "aligned_pointer_in_region", "value_in_range", "nonzero",
                      ///< "monotonic_increasing", "monotonic_decreasing",
                      ///< "bitmask_invariant", "region_membership"
    Address regionStart = 0;
    Address regionEnd = 0;
    u32 valueMin = 0;
    u32 valueMax = 0;
    u32 bitmask = 0;
};

/// A single watchpoint definition from a profile.
struct WatchpointConfig
{
    std::string name;
    WatchpointKind kind = WatchpointKind::RamWrite;
    Address rangeStart = 0;
    Address rangeEnd = 0;
    WatchpointPredicate predicate;
    WatchpointAction action = WatchpointAction::Log;
};

/// A single PC-range tracepoint definition from a profile.
struct TracepointConfig
{
    std::string name;
    Address pcRangeStart = 0;
    Address pcRangeEnd = 0;
    std::vector<std::string> registers;
    bool logBranches = false;
    std::vector<Address> mmioReads;
    bool captureContext = false;
    bool callerHistogram = false;
    u32 repeatThreshold = 0;
};

/// Validator type enumeration.
enum class ValidatorType : u8
{
    PointerCell,
    LinkedList,
    SentinelBlockChain,
    BoundedStructureWalk
};

/// Condition that must hold before a validator runs.
struct ValidatorEnableCondition
{
    std::string type; ///< "nonzero_u32", "always"
    Address address = 0;
};

/// Header layout description for heap-style validators.
struct ValidatorHeaderConfig
{
    u32 sizeMask = 0xFFFFFFFCu;
    u32 freeBit = 0x1u;
    u32 sentinel = 0xFFFFFFFEu;
};

/// A single structure validator definition from a profile.
struct ValidatorConfig
{
    std::string name;
    ValidatorType type = ValidatorType::SentinelBlockChain;
    ValidatorEnableCondition enabledWhen;
    Address currentRoot = 0;
    Address backupRoot = 0;
    Address regionStart = 0;
    Address regionEnd = 0;
    ValidatorHeaderConfig header;
    size_t maxNodes = 64;
};

/// Boundary event kinds that can trigger diagnostics.
enum class BoundaryKind : u8
{
    IrqCallbackReturn,
    BiosCallReturn,
    BiosMemoryOp,
    DmaCompletion,
    MmioPollSuspicion,
    FunctionReturn,
    StallDetected
};

/// Mapping from a boundary event to diagnostics that should run.
struct BoundaryConfig
{
    BoundaryKind kind = BoundaryKind::IrqCallbackReturn;
    std::vector<std::string> runValidators;
    std::vector<std::string> runExplainers;
    bool trapOnFailure = false;
};

/// Explainer type enumeration.
enum class ExplainerKind : u8
{
    Gpustat,
    CdromIrq,
    IrqController,
    DmaChannel,
    /// End-of-run bank-aware CDROM host-interface summary.
    CdromBankSummary,
    /// End-of-run rolling sector-phase transition trace (PR-RV21c).
    CdromPhaseSummary,
    /// End-of-run XA sector classification and delivery summary (PR-RV23).
    CdromXaClassification,
    /// Post-ReadS/ReadN XA stream summary scoped to the most recent XA-enabled stream (PR-RV27).
    CdromPostStreamValidator,
    /// Per-sector CPU payload breakdown for the first 32 post-stream sectors (PR-RV28).
    CdromCpuPayloadSummary
};

/// A single device/register explainer from a profile.
struct ExplainerConfig
{
    ExplainerKind kind = ExplainerKind::Gpustat;
    Address address = 0;
};

/// Metadata integrity watch for structured memory regions.
struct MetadataWatchConfig
{
    std::string name;
    Address regionStart = 0;
    Address regionEnd = 0;
    u32 alignmentMask = 3u;
    u32 sentinelValue = 0xFFFFFFFEu;
    bool checkAlignment = true;
    bool checkNonzeroSize = true;
    bool checkNextInRegion = true;
    u32 sizeMask = 0xFFFFFFFCu;
};

/// Suspect function range for semantic audit.
struct SuspectFunctionConfig
{
    std::string name;
    Address pcStart = 0;
    Address pcEnd = 0;
    std::vector<Address> watchedCells;
};

/// Top-level diagnostic profile data loaded from a JSON file.
struct DiagProfileData
{
    std::string name;
    DiagMemoryMapConfig memoryMap;
    std::vector<WatchpointConfig> watchpoints;
    std::vector<TracepointConfig> tracepoints;
    std::vector<ValidatorConfig> validators;
    std::vector<BoundaryConfig> boundaries;
    std::vector<ExplainerConfig> explainers;
    std::vector<MetadataWatchConfig> metadataWatches;
    std::vector<SuspectFunctionConfig> suspectFunctions;
};

} // namespace runtime
} // namespace psxrecomp
