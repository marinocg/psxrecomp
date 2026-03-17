#pragma once

#include "psxrecomp/runtime/diag_cdrom_bank_tracer.h"
#include "psxrecomp/runtime/diag_types.h"

#include <string>
#include <vector>

namespace psxrecomp
{
namespace runtime
{

/// Generic device/register explainer that decodes hardware state.
class DiagExplainerEngine
{
  public:
    DiagExplainerEngine();

    /// Configure from profile explainer definitions.
    void configure(const std::vector<ExplainerConfig>& configs);

    /// Check if an explainer of the given kind is enabled.
    bool isEnabled(ExplainerKind kind) const;

    /// Decode a GPUSTAT register value into a human-readable string.
    static std::string explainGpustat(u32 value);

    /// Decode CD-ROM IRQ flags and enable mask.
    static std::string explainCdromIrq(u8 flags, u8 enable);

    /// Decode the IRQ controller status and mask registers.
    static std::string explainIrqController(u16 status, u16 mask);

    /// Decode a DMA channel's control register.
    static std::string explainDmaChannel(u8 port, u32 control);

    /// Return an end-of-run bank-aware CDROM host-interface summary.
    /// Delegates to the provided tracer's formatSummary().
    static std::string explainCdromBankSummary(const DiagCdromBankTracer& tracer);

    /// Number of configured explainers.
    size_t explainerCount() const;

  private:
    std::vector<ExplainerConfig> m_configs;
};

} // namespace runtime
} // namespace psxrecomp
