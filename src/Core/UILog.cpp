#include "NoxUI/Core/UILog.h"

#include <cstdio>
#include <mutex>

namespace NoxUI
{
    namespace
    {
        std::mutex g_LogMutex;
        LogCallback g_LogCallback;
    }

    void SetLogCallback(LogCallback callback)
    {
        std::scoped_lock lock(g_LogMutex);
        g_LogCallback = std::move(callback);
    }

    void LogMessage(LogLevel level, std::string_view message)
    {
        LogCallback cb;
        {
            std::scoped_lock lock(g_LogMutex);
            cb = g_LogCallback;
        }
        if (cb)
        {
            cb(level, message);
            return;
        }

        const char* tag = level == LogLevel::Error ? "ERROR" : level == LogLevel::Warn ? "WARN" : "INFO";
        std::fprintf(stderr, "[NoxUI][%s] %.*s\n", tag, static_cast<int>(message.size()), message.data());
    }
}
