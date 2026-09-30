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
    /// Unabbreviated day label ("TODAY", "TOMORROW", "WEDNESDAY") for panels wide enough to show it.
    static const char* getWeatherDayLabelLong(int dayOfWeek, bool isToday, bool isTomorrow, Lang l);
    static String getWeatherCondition(const String& raw);
    static String getWeatherCondition(const String& raw, Lang l);
    /// Unabbreviated condition ("Partly Cloudy", "Thunderstorm") for panels wide enough to show it.
    static String getWeatherConditionLong(const String& raw, Lang l);
    static const char* getOutdoorLabel(Lang l);
    static const char* getIndoorLabel(Lang l);
    static const char* getClimateLabel(Lang l);
    
    // WordClock
    static std::vector<String> getWordClockLines(int hours, int minutes);

    // Dates
    /// Three-letter month label ("SEP", "SEPT", "SEP"), month0 is 0-11.
    static const char* getMonthLabel(int month0, Lang l);
    /// A date line in the order the language uses: "MON SEP 28", "LUN 28 SEPT", "LUN 28 SEP".
    /// Written into the caller's buffer so a clock face can build it without touching the heap.
    static void getDateLine(int weekday, int month0, int day, Lang l, char* out, size_t outSize);
    /// The time spoken as words, split into the hour line and the minute line beneath it. Both are
    /// written into caller-owned buffers for the same reason.
    static void getSpokenTime(int hours, int minutes, Lang l,
                              char* hourWords, size_t hourSize,
                              char* minuteWords, size_t minuteSize);
    
    // Noise / Decibel
    static const char* getNoiseLevelLabel(int level);

    // GNews Status
    static const char* getGNewsStatusLabel(uint8_t status);
    static const char* getGNewsStatusLabel(uint8_t status, Lang l);
};

#endif // I18N_H
