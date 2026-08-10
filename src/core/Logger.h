#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>

/**
 * @brief Structured Logger for ArcadeMatrix
 * Supports log levels (ERROR, WARN, INFO, DEBUG, VERBOSE), timestamps, and tags.
 */

enum LogLevel {
    LOG_LEVEL_NONE = 0,
    LOG_LEVEL_ERROR = 1,
    LOG_LEVEL_WARN = 2,
    LOG_LEVEL_INFO = 3,
    LOG_LEVEL_DEBUG = 4,
    LOG_LEVEL_VERBOSE = 5
};

class Logger {
public:
    static void setLevel(LogLevel level) { currentLevel = level; }
    static LogLevel getLevel() { return currentLevel; }

    static void error(const char* tag, const char* format, ...) {
        if (currentLevel < LOG_LEVEL_ERROR) return;
        va_list args;
        va_start(args, format);
        logFormatted("ERROR", tag, format, args);
        va_end(args);
    }

    static void warn(const char* tag, const char* format, ...) {
        if (currentLevel < LOG_LEVEL_WARN) return;
        va_list args;
        va_start(args, format);
        logFormatted("WARN", tag, format, args);
        va_end(args);
    }

    static void info(const char* tag, const char* format, ...) {
        if (currentLevel < LOG_LEVEL_INFO) return;
        va_list args;
        va_start(args, format);
        logFormatted("INFO", tag, format, args);
        va_end(args);
    }

    static void debug(const char* tag, const char* format, ...) {
        if (currentLevel < LOG_LEVEL_DEBUG) return;
        va_list args;
        va_start(args, format);
        logFormatted("DEBUG", tag, format, args);
        va_end(args);
    }

    static void verbose(const char* tag, const char* format, ...) {
        if (currentLevel < LOG_LEVEL_VERBOSE) return;
        va_list args;
        va_start(args, format);
        logFormatted("VERB", tag, format, args);
        va_end(args);
    }

private:
    static LogLevel currentLevel;

    static void logFormatted(const char* levelStr, const char* tag, const char* format, va_list args) {
        char buf[256];
        vsnprintf(buf, sizeof(buf), format, args);
        
        unsigned long ms = millis();
        unsigned long secs = ms / 1000;
        unsigned long mins = secs / 60;
        unsigned long hours = mins / 60;
        
        Serial.printf("[%02lu:%02lu:%02lu.%03lu] [%s] [%s] %s\n",
                      hours % 24, mins % 60, secs % 60, ms % 1000,
                      levelStr, tag, buf);
    }
};

#define LOGE(tag, fmt, ...) Logger::error(tag, fmt, ##__VA_ARGS__)
#define LOGW(tag, fmt, ...) Logger::warn(tag, fmt, ##__VA_ARGS__)
#define LOGI(tag, fmt, ...) Logger::info(tag, fmt, ##__VA_ARGS__)
#define LOGD(tag, fmt, ...) Logger::debug(tag, fmt, ##__VA_ARGS__)
#define LOGV(tag, fmt, ...) Logger::verbose(tag, fmt, ##__VA_ARGS__)

#endif // LOGGER_H
