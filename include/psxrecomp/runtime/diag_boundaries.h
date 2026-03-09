#pragma once

#include "psxrecomp/runtime/diag_types.h"

#include <string>
#include <vector>

namespace psxrecomp
{
namespace runtime
{

class DiagValidatorEngine;
class DiagExplainerEngine;
class RuntimeLogger;

/// Profile-driven boundary hook dispatcher.
/// Maps boundary events to diagnostic actions defined in the profile.
class DiagBoundaryDispatcher
{
  public:
    DiagBoundaryDispatcher();

    /// Configure from profile boundary definitions and link engines.
    void configure(const std::vector<BoundaryConfig>& configs, DiagValidatorEngine* validators,
                   DiagExplainerEngine* explainers);

    /// Fire a boundary event, running all configured diagnostics.
    /// Returns true if all validators passed.
    bool onBoundary(BoundaryKind kind, const u8* ram, size_t ramSize,
                    RuntimeLogger* logger) const;

    /// Check if any boundary is configured for the given kind.
    bool hasBoundary(BoundaryKind kind) const;

    /// Number of configured boundaries.
    size_t boundaryCount() const;

  private:
    std::vector<BoundaryConfig> m_configs;
    DiagValidatorEngine* m_validators = nullptr;
    DiagExplainerEngine* m_explainers = nullptr;
};

} // namespace runtime
} // namespace psxrecomp
