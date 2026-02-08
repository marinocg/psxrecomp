#include "psxrecomp/runtime/logger.h"

#include <cstdio>

namespace psxrecomp
{
namespace runtime
{

void RuntimeLogger::setCallback(Callback callback)
{
    m_callback = std::move(callback);
}

void RuntimeLogger::log(LogLevel level, const std::string& message) const
{
    if (m_callback)
    {
        m_callback(level, message);
        return;
    }

    const char* label = nullptr;
    switch (level)
    {
    case LogLevel::Debug:
        label = "DEBUG";
        break;
    case LogLevel::Info:
        label = "INFO";
        break;
    case LogLevel::Warn:
        label = "WARN";
        break;
    case LogLevel::Error:
        label = "ERROR";
        break;
    default:
        label = "INFO";
        break;
    }

    std::fprintf(stderr, "[runtime:%s] %s\n", label, message.c_str());
}

} // namespace runtime
} // namespace psxrecomp
