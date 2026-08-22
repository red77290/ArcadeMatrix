#pragma once
#include <Arduino.h>

enum class Timeframe {
    Hourly,
    Daily,
    Weekly,
    Monthly
};

inline Timeframe timeframeFromString(const String& str) {
    String s = str;
    s.toLowerCase();
    if (s == "hourly" || s == "1h" || s == "60m") return Timeframe::Hourly;
    if (s == "weekly" || s == "7d" || s == "1w") return Timeframe::Weekly;
    if (s == "monthly" || s == "30d" || s == "1m" || s == "1mo") return Timeframe::Monthly;
    return Timeframe::Daily;
}

inline const char* timeframeToString(Timeframe tf) {
    switch (tf) {
        case Timeframe::Hourly: return "hourly";
        case Timeframe::Daily: return "daily";
        case Timeframe::Weekly: return "weekly";
        case Timeframe::Monthly: return "monthly";
    }
    return "daily";
}

inline const char* timeframeLabel(Timeframe tf) {
    switch (tf) {
        case Timeframe::Hourly: return "1H";
        case Timeframe::Daily: return "1D";
        case Timeframe::Weekly: return "7D";
        case Timeframe::Monthly: return "1M";
    }
    return "1D";
}
