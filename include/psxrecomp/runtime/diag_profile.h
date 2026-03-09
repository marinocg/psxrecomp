#pragma once

#include "psxrecomp/runtime/diag_types.h"

#include <memory>
#include <string>

namespace psxrecomp
{
namespace runtime
{

class RuntimeLogger;

/// Holds a parsed diagnostic profile and provides query helpers.
class DiagProfile
{
  public:
    DiagProfile();
    ~DiagProfile();

    /// Load a profile from a JSON file path. Returns true on success.
    bool loadFromFile(const std::string& path, RuntimeLogger* logger = nullptr);

    /// Load a profile from a JSON string. Returns true on success.
    bool loadFromString(const std::string& json, RuntimeLogger* logger = nullptr);

    /// Whether a profile has been successfully loaded.
    bool isLoaded() const;

    /// Access the parsed profile data.
    const DiagProfileData& data() const;

    /// Profile name (empty if not loaded).
    const std::string& name() const;

    /// Find a validator config by name. Returns nullptr if not found.
    const ValidatorConfig* findValidator(const std::string& name) const;

    /// Find a tracepoint config by name. Returns nullptr if not found.
    const TracepointConfig* findTracepoint(const std::string& name) const;

    /// Find a watchpoint config by name. Returns nullptr if not found.
    const WatchpointConfig* findWatchpoint(const std::string& name) const;

    /// Resolve a profile path from CLI arg, env var, or auto-detect.
    /// Returns the resolved path, or empty string if none found.
    static std::string resolveProfilePath(const std::string& cliArg = "");

  private:
    bool parseProfileData(const std::string& json, RuntimeLogger* logger);

    DiagProfileData m_data;
    bool m_loaded = false;
};

} // namespace runtime
} // namespace psxrecomp
