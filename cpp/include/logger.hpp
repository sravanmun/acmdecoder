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

class Logger {
public:
    static Logger& getInstance();
    
    void log(LogLevel level, const std::string& where, const std::string& message, bool write_to_file);
    
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
};
