#pragma once
#include <functional>
#include <string_view>

namespace atmera {
enum class LogLevel { Info = 1, Warning = 2, Error = 3 };
using LogSink = std::function<void(LogLevel, std::string_view)>;

class Logger {
public:
    explicit Logger(LogSink sink = {});
    void log(LogLevel level, std::string_view message) const;
private:
    LogSink sink_;
};
}
