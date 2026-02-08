#pragma once

#include <functional>
#include <string>

namespace psxrecomp
{
namespace runtime
{

enum class LogLevel
{
    Debug,
    Info,
    Warn,
    Error
};

class RuntimeLogger
{
  public:
    using Callback = std::function<void(LogLevel, const std::string&)>;

    void setCallback(Callback callback);
    void log(LogLevel level, const std::string& message) const;

  private:
    Callback m_callback;
};

} // namespace runtime
} // namespace psxrecomp
