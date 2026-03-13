#pragma once

#include "psxrecomp/runtime/diag_types.h"

#include <string>
#include <vector>

namespace psxrecomp
{
namespace runtime
{

class RuntimeLogger;

/// Result of running a single validator.
struct ValidationResult
{
    bool passed = true;
    std::string report;
    std::string validatorName;
};

/// Profile-driven structure validator framework.
class DiagValidatorEngine
{
  public:
    DiagValidatorEngine();

    /// Configure from profile validator definitions.
    void configure(const std::vector<ValidatorConfig>& configs);

    /// Run a named validator against the given RAM snapshot.
    ValidationResult runValidator(const std::string& name, const u8* ram, size_t ramSize) const;

    /// Run all configured validators.
    std::vector<ValidationResult> runAll(const u8* ram, size_t ramSize) const;

    /// Number of configured validators.
    size_t validatorCount() const;

    /// Get all configured validator names.
    std::vector<std::string> validatorNames() const;

    /// Find a validator config by name. Returns nullptr if not found.
    const ValidatorConfig* findConfig(const std::string& name) const;

  private:
    ValidationResult runSentinelBlockChain(const ValidatorConfig& config, const u8* ram,
                                           size_t ramSize) const;
    ValidationResult runPointerCell(const ValidatorConfig& config, const u8* ram,
                                    size_t ramSize) const;
    ValidationResult runLinkedList(const ValidatorConfig& config, const u8* ram,
                                   size_t ramSize) const;
    ValidationResult runBoundedWalk(const ValidatorConfig& config, const u8* ram,
                                    size_t ramSize) const;

    static u32 readWord(const u8* ram, Address physicalAddress, size_t ramSize);
    static bool isPhysicalRamAddress(Address address, size_t ramSize);
    static Address toPhysical(Address address);
    static bool isAligned(Address address, u32 alignmentMask);

    std::string validateChain(const ValidatorConfig& config, Address start, const char* label,
                              const u8* ram, size_t ramSize) const;
    std::string describeChain(const ValidatorConfig& config, Address start, const char* label,
                              const u8* ram, size_t ramSize) const;

    std::vector<ValidatorConfig> m_configs;
};

} // namespace runtime
} // namespace psxrecomp
