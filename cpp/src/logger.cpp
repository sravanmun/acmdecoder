// Log.cpp
#include "logger.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <ctime>
#include <sys/stat.h>
#include <cstdlib>


Logger& Logger::getInstance() {
    static Logger instance;
    return instance;
}


Logger::~Logger() {
    if (log_file_.is_open()) {
        log_file_.close();
    }
}

std::string Logger::getTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now;
    localtime_r(&time_t_now, &tm_now);
    
    std::ostringstream oss;
    oss << std::put_time(&tm_now, "%F %T") << ',' 
        << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}


std::string Logger::getCurrentDate() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now;
    localtime_r(&time_t_now, &tm_now);
    
    std::ostringstream oss;
    oss << std::put_time(&tm_now, "%F");
    return oss.str();
}


int Logger::getCurrentDay() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now;
    localtime_r(&time_t_now, &tm_now);
    return tm_now.tm_yday;
}


void Logger::checkAndRotateLog() {
    int current_day = getCurrentDay();
    std::string log_filename = log_dir_ + "/decoder.log";
                                   
    if (!log_file_.is_open()) {
        // Create directory if it doesn't exist
        mkdir(log_dir_.c_str(), 0755);
        
        log_file_.open(log_filename, std::ios::app);
        current_day_ = current_day;
        previous_date_ = getCurrentDate();
    }
    
    if (current_day != current_day_) {
        log_file_.close();
        
        // Rotate old log file
        std::string old_filename = log_filename + "." + previous_date_;
        std::rename(log_filename.c_str(), old_filename.c_str());
        
        // Open new log file
        log_file_.open(log_filename, std::ios::app);
        current_day_ = current_day;
        previous_date_ = getCurrentDate();
    }
}

std::string Logger::getLevelString(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO: return "INFO";
        case LogLevel::WARNING: return "WARNING";
        case LogLevel::FATAL: return "CRITICAL";
        default: return "UNKNOWN";
    }
}

void Logger::setConsoleColor(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG:
            break; // default color
        case LogLevel::INFO:
            std::cout << "\033[1;32m"; // green
            break;
        case LogLevel::WARNING:
            std::cout << "\033[0;33m"; // orange
            break;
        case LogLevel::FATAL:
            std::cout << "\033[1;31m"; // red
            break;
    }
}

void Logger::resetConsoleColor() {
    std::cout << "\033[0m";
}

void Logger::log(LogLevel level, const std::string& where, const std::string& message, bool write_to_file) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (write_to_file){
        checkAndRotateLog();
    }
    
    std::string timestamp = getTimestamp();
    std::string level_str = getLevelString(level);
    
    // Console output with color (only for INFO, WARNING, FATAL)
    if (level >= LogLevel::INFO) {
        setConsoleColor(level);
        std::cout << timestamp << " - " << where << " - " << level_str << " - " << message << std::endl;
        resetConsoleColor();
        std::cout << std::flush; // flush after resetting console color
    }
    
    // File output
    if (write_to_file && (log_file_.is_open())) {
        log_file_ << timestamp << " - " << where << " - " << level_str << " - " << message << std::endl;
        log_file_.flush();
    }
    
    // Exit on FATAL
    // if (level == LogLevel::FATAL) {
    //    std::exit(1);
    // }
}
