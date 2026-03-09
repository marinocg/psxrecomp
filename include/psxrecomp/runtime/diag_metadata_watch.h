#pragma once

#include "psxrecomp/runtime/diag_types.h"

#include <string>
#include <vector>

namespace psxrecomp
{
namespace runtime
{

class RuntimeLogger;

/// Record of a metadata integrity violation.
struct MetadataViolation
{
    std::string watchName;
    Address address = 0;
    u32 value = 0;
    Address writerPc = 0;
    std::string reason;
};

/// Profile-driven metadata/header integrity watch engine.
class DiagMetadataWatchEngine
{
  public:
    DiagMetadataWatchEngine();

    /// Configure from profile metadata watch definitions.
    void configure(const std::vector<MetadataWatchConfig>& configs);

    /// Record a RAM write and check if it violates metadata integrity.
    void recordWrite(Address address, u32 newValue, Address writerPc, RuntimeLogger* logger);

    /// Check integrity of a named metadata region against current RAM state.
    std::string checkIntegrity(const std::string& name, const u8* ram, size_t ramSize) const;

    /// Format all recorded violations.
    std::string formatViolations() const;

    /// Number of configured metadata watches.
    size_t watchCount() const;

    /// Number of recorded violations.
    size_t violationCount() const;

    /// Clear recorded violations.
    void clearViolations();

  private:
    bool isInWatchedRegion(Address address) const;

    std::vector<MetadataWatchConfig> m_configs;
    std::vector<MetadataViolation> m_violations;
    static constexpr size_t MAX_VIOLATIONS = 64;
};

} // namespace runtime
} // namespace psxrecomp
