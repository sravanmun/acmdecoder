// logger.hpp
#pragma once

#include <string>
#include <fstream>
#include <memory>
#include <mutex>

enum class LogLevel {
    DEBUG = 1,
    INFO,
    WARNING,
    FATAL
};

// Parse "DEBUG"/"INFO"/"WARNING"/"FATAL" (case-insensitive). Throws on bad input.
LogLevel logLevelFromString(const std::string& s);

class Logger {
public:
    static Logger& getInstance();

    void log(LogLevel level, const std::string& where, const std::string& message, bool write_to_file);

    // Minimum level for *console* output. File output (when write_to_file=true)
    // continues to record every level regardless of this setting.
    void setPrintLevel(LogLevel level);

private:
    Logger() = default;
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void checkAndRotateLog();
    std::string getTimestamp();
    std::string getCurrentDate();
    int getCurrentDay();
    std::string getLevelString(LogLevel level);
    void setConsoleColor(LogLevel level);
    void resetConsoleColor();

    std::string log_dir_ = "log";
    std::ofstream log_file_;
    int current_day_ = -1;
    std::string previous_date_;
    std::mutex mutex_;
    LogLevel print_level_ = LogLevel::INFO;  // console-only threshold
};
