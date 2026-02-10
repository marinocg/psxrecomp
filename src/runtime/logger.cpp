#include "psxrecomp/runtime/logger.h"

#include <cstdio>
#include <utility>

namespace psxrecomp
{
namespace runtime
{

void RuntimeLogger::setCallback(Callback callback)
{
    m_callback = std::move(callback);
}

void RuntimeLogger::setMinLevel(LogLevel level)
{
    m_minLevel = level;
}

LogLevel RuntimeLogger::minLevel() const
{
    return m_minLevel;
}

void RuntimeLogger::log(LogLevel level, const std::string& message)
{
    log(level, "runtime", message);
}

void RuntimeLogger::log(LogLevel level, std::string category, std::string message)
{
    if (levelRank(level) < levelRank(m_minLevel))
    {
        return;
    }

    LogEvent event;
    event.level = level;
    event.category = std::move(category);
    event.message = std::move(message);
    event.sequence = ++m_sequence;
    m_lastEvent = event;

    if (m_callback)
    {
        m_callback(event);
        return;
    }

    std::fprintf(stderr, "[runtime:%s:%s:#%llu] %s\n", levelLabel(event.level),
                 event.category.c_str(), static_cast<unsigned long long>(event.sequence),
                 event.message.c_str());
}

std::optional<LogEvent> RuntimeLogger::lastEvent() const
{
    return m_lastEvent;
}

const char* RuntimeLogger::levelLabel(LogLevel level)
{
    switch (level)
    {
    case LogLevel::Debug:
        return "DEBUG";
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Warn:
        return "WARN";
    case LogLevel::Error:
        return "ERROR";
    default:
        return "INFO";
    }
}

int RuntimeLogger::levelRank(LogLevel level)
{
    switch (level)
    {
    case LogLevel::Debug:
        return 0;
    case LogLevel::Info:
        return 1;
    case LogLevel::Warn:
        return 2;
    case LogLevel::Error:
        return 3;
    default:
        return 1;
    }
}

} // namespace runtime
} // namespace psxrecomp
