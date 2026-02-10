#pragma once

#include <cstdint>
#include <functional>
#include <optional>
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

struct LogEvent
{
    LogLevel level = LogLevel::Info;
    std::string category;
    std::string message;
    uint64_t sequence = 0;
};

class RuntimeLogger
{
  public:
    using Callback = std::function<void(const LogEvent&)>;

    void setCallback(Callback callback);
    void setMinLevel(LogLevel level);
    LogLevel minLevel() const;

    void log(LogLevel level, const std::string& message);
    void log(LogLevel level, std::string category, std::string message);

    std::optional<LogEvent> lastEvent() const;

  private:
    static const char* levelLabel(LogLevel level);
    static int levelRank(LogLevel level);

    Callback m_callback;
    LogLevel m_minLevel = LogLevel::Info;
    uint64_t m_sequence = 0;
    std::optional<LogEvent> m_lastEvent;
};

} // namespace runtime
} // namespace psxrecomp
