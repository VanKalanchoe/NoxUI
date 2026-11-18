#pragma once

#include <cstdint>
#include <functional>
#include <string_view>

namespace NoxUI
{
    enum class LogLevel : uint8_t
    {
        Info = 0,
        Warn,
        Error
    };

    using LogCallback = std::function<void(LogLevel level, std::string_view message)>;

    void SetLogCallback(LogCallback callback);
    void LogMessage(LogLevel level, std::string_view message);
}
