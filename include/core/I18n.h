#ifndef I18N_H
#define I18N_H

#include <Arduino.h>
#include <vector>

enum class Lang {
    FR,
    EN,
    ES
};

class I18n {
public:
    static Lang getLang();
    static Lang parseLang(const String& code);
    static const char* getLangCode(Lang l);
    
    // Weather & Climate
    static const char* getWeatherDayLabel(int dayOfWeek, bool isToday, bool isTomorrow);
    static const char* getWeatherDayLabel(int dayOfWeek, bool isToday, bool isTomorrow, Lang l);
    static String getWeatherCondition(const String& raw);
    static String getWeatherCondition(const String& raw, Lang l);
    static const char* getOutdoorLabel(Lang l);
    static const char* getIndoorLabel(Lang l);
    static const char* getClimateLabel(Lang l);
    
    // WordClock
    static std::vector<String> getWordClockLines(int hours, int minutes);
    
    // Noise / Decibel
    static const char* getNoiseLevelLabel(int level);
};

#endif // I18N_H
