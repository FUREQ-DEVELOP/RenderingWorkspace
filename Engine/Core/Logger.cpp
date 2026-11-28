#include <ATMERA/Logger.hpp>
#include <iostream>

namespace atmera {
Logger::Logger(LogSink sink) : sink_(std::move(sink)) {}
void Logger::log(LogLevel level, std::string_view message) const {
    if (sink_) { sink_(level, message); return; }
    std::clog << "[ATMERA " << static_cast<int>(level) << "] " << message << '\n';
}
}
