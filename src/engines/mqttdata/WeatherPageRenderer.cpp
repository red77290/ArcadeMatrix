#include "WeatherPageRenderer.h"
#include <math.h>
#include <string.h>
#include <time.h>
#include "../renderers/WeatherLayout.h"
#include "../../../include/core/I18n.h"

namespace weather_page {

namespace {

constexpr char kDegree = (char)0xF7;   // Adafruit GFX draws the CP437 degree glyph for 0xF7 with CP437 mode off

void conditionText(const char* cond, Lang lang, char* shortOut, size_t shortCap, char* longOut, size_t longCap) {
    const int group = feed::conditionGroup(cond);
    if (group < 0) {   // unknown condition: show it as received
        strlcpy(shortOut, cond, shortCap);
        strlcpy(longOut, cond, longCap);
        return;
    }
    strlcpy(shortOut, I18n::getConditionLabel(group, false, lang), shortCap);
    strlcpy(longOut, I18n::getConditionLabel(group, true, lang), longCap);
}

}  // namespace

void draw(MatrixPanel_I2S_DMA* m, const feed::WeatherFeed& wx, uint8_t page) {
    if (!m) return;
    weather_layout::Page p;
    const Lang lang = I18n::getLang();
    const bool hasNow = wx.hasCurrent;

    if (hasNow && page == 0) {
        // NOW: the live station reading over "<humidity>%  <dir> <speed><unit>" (the direction never
        // stands without a speed; the layout drops it first if the line would reach the label).
        p.isNow = true;
        p.fahrenheit = wx.imperial;
        const char* cond = wx.currentCondition[0] ? wx.currentCondition : (wx.dayCount ? wx.days[0].condition : "");
        p.icon = feed::iconForCondition(cond);
        strlcpy(p.label, I18n::getNowLabel(lang), sizeof(p.label));
        strlcpy(p.labelLong, p.label, sizeof(p.labelLong));
        snprintf(p.top, sizeof(p.top), "%ld%c%c", lroundf(wx.currentTemp), kDegree, wx.imperial ? 'F' : 'C');
        char speed[16] = {0}, wind[24] = {0};
        if (!isnan(wx.wind)) {
            snprintf(speed, sizeof(speed), "%ld%s", lroundf(wx.wind), wx.windUnit);
            if (wx.windDir[0]) snprintf(wind, sizeof(wind), "%s %s", wx.windDir, speed);
        }
        auto build = [&](const char* w, char* out, size_t cap) {
            if (wx.humidity >= 0 && w[0]) snprintf(out, cap, "%d%%  %s", wx.humidity, w);
            else if (wx.humidity >= 0) snprintf(out, cap, "%d%%", wx.humidity);
            else strlcpy(out, w, cap);
        };
        build(wind[0] ? wind : speed, p.bottom, sizeof(p.bottom));
        build(speed, p.bottomShort, sizeof(p.bottomShort));
        if (!p.bottom[0]) {   // neither humidity nor wind: the short condition instead
            char longUnused[32];
            conditionText(cond, lang, p.bottom, sizeof(p.bottom), longUnused, sizeof(longUnused));
            strlcpy(p.bottomShort, p.bottom, sizeof(p.bottomShort));
        }
        weather_layout::draw(m, p);
        return;
    }

    const uint8_t day = (uint8_t)(page - (hasNow ? 1 : 0));
    const feed::WeatherDay* d = day < wx.dayCount ? &wx.days[day] : nullptr;
    const char* cond = (d && d->condition[0]) ? d->condition : (day == 0 ? wx.currentCondition : "");
    p.icon = feed::iconForCondition(cond);
    conditionText(cond, lang, p.desc, sizeof(p.desc), p.descLong, sizeof(p.descLong));
    struct tm t;
    int wday = 0;
    if (getLocalTime(&t, 0)) wday = t.tm_wday;
    wday = (wday + day) % 7;
    strlcpy(p.label, I18n::getWeatherDayLabel(wday, day == 0, day == 1), sizeof(p.label));
    strlcpy(p.labelLong, I18n::getWeatherDayLabelLong(wday, day == 0, day == 1, lang), sizeof(p.labelLong));
    const float lo = (d && d->hasTemps) ? d->tempMin : wx.currentTemp;
    const float hi = (d && d->hasTemps) ? d->tempMax : wx.currentTemp;
    weather_layout::setRange(p, lo, hi, wx.imperial);
    weather_layout::draw(m, p);
}

}  // namespace weather_page
