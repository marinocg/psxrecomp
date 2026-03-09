#pragma once

#include "psxrecomp/runtime/diag_types.h"

#include <string>
#include <vector>

namespace psxrecomp
{
namespace runtime
{

/// Audit report for a suspect function.
struct SemanticAuditReport
{
    std::string functionName;
    Address pcStart = 0;
    Address pcEnd = 0;
    std::vector<Address> watchedCells;
    std::string disassembly;
    std::string accessAnnotations;
    std::string summary;
};

/// Reusable tool for answering "runtime corruption or codegen bug?"
/// for suspect function ranges defined in a diagnostic profile.
class SemanticAuditTool
{
  public:
    SemanticAuditTool();

    /// Configure from suspect function definitions.
    void configure(const std::vector<SuspectFunctionConfig>& configs);

    /// Generate an audit report for a named function by inspecting RAM content.
    SemanticAuditReport audit(const std::string& name, const u8* ram, size_t ramSize) const;

    /// Generate reports for all configured suspect functions.
    std::vector<SemanticAuditReport> auditAll(const u8* ram, size_t ramSize) const;

    /// Format an audit report as a human-readable string.
    static std::string formatReport(const SemanticAuditReport& report);

    /// Number of configured suspect functions.
    size_t functionCount() const;

  private:
    std::string disassembleRange(Address start, Address end, const u8* ram,
                                size_t ramSize) const;
    std::string annotateAccesses(Address start, Address end,
                                 const std::vector<Address>& cells, const u8* ram,
                                 size_t ramSize) const;

    std::vector<SuspectFunctionConfig> m_configs;
};

} // namespace runtime
} // namespace psxrecomp
