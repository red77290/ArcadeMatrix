#include "Logger.h"

#ifdef CORE_DEBUG_LEVEL
LogLevel Logger::currentLevel = static_cast<LogLevel>(CORE_DEBUG_LEVEL);
#else
LogLevel Logger::currentLevel = LOG_LEVEL_INFO;
#endif
