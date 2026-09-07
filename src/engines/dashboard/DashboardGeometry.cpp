#include "DashboardGeometry.h"
#include <math.h>

DashboardLayout DashboardLayoutCalculator::calculate(const DisplayGeometry& geometry, const DashboardConfigParams& config) {
    DashboardLayout l;
    int w = geometry.width;
    int h = geometry.height;

    bool isTate = (geometry.layoutClass == LayoutClass::PORTRAIT || geometry.layoutClass == LayoutClass::TALL || w < 48 || h > (w * 3) / 2);
    bool isWide = (geometry.layoutClass == LayoutClass::WIDE || w >= 128);
    bool isSquare = (w == h || (w >= 48 && h >= 48 && abs(w - h) <= 16));

    if (isTate) {
        // ====================================================================
        // PORTRAIT TOWER LAYOUT (e.g. 64x256, 32x128, 64x128)
        // ====================================================================
        l.isVerticalTower = true;
        int curY = 0;

        if (h >= 240) {
            // Full 64x256 Tall Tower
            if (config.showClock) {
                l.hasClock = true;
                l.clockRect = Rect(0, (int16_t)curY, (uint16_t)w, 64);
                curY += 65;
            }
            if (config.showWorldClock) {
                l.hasWorldClock = true;
                l.worldClockRect = Rect(0, (int16_t)curY, (uint16_t)w, 44);
                curY += 45;
            }
            if (config.showWeather || config.showIndoorTemp) {
                l.hasClimate = true;
                l.climateRect = Rect(0, (int16_t)curY, (uint16_t)w, 54);
                curY += 55;
            }
            if (config.showMarkets && curY < h) {
                l.hasMarket = true;
                int remH = h - curY;
                l.marketRect = Rect(0, (int16_t)curY, (uint16_t)w, (uint16_t)(config.showSysInfo ? (remH - 16) : remH));
                curY += l.marketRect.height + 1;
            }
            if (config.showSysInfo && curY < h) {
                l.hasSysInfo = true;
                l.sysInfoRect = Rect(0, (int16_t)curY, (uint16_t)w, (uint16_t)(h - curY));
            }
        } else if (h >= 120) {
            // Medium 64x128 / 32x128 Tower
            if (config.showClock) {
                l.hasClock = true;
                l.clockRect = Rect(0, (int16_t)curY, (uint16_t)w, 48);
                curY += 49;
            }
            if (config.showWeather || config.showIndoorTemp) {
                l.hasClimate = true;
                l.climateRect = Rect(0, (int16_t)curY, (uint16_t)w, 36);
                curY += 37;
            }
            if (config.showMarkets && curY < h) {
                l.hasMarket = true;
                l.marketRect = Rect(0, (int16_t)curY, (uint16_t)w, (uint16_t)(h - curY));
            } else if (config.showWorldClock && curY < h) {
                l.hasWorldClock = true;
                l.worldClockRect = Rect(0, (int16_t)curY, (uint16_t)w, (uint16_t)(h - curY));
            } else if (config.showSysInfo && curY < h) {
                l.hasSysInfo = true;
                l.sysInfoRect = Rect(0, (int16_t)curY, (uint16_t)w, (uint16_t)(h - curY));
            }
        } else {
            // Small 32x64 / 64x64 Tower
            if (config.showClock) {
                l.hasClock = true;
                l.clockRect = Rect(0, 0, (uint16_t)w, (uint16_t)(h / 2));
            }
            if (config.showWeather || config.showIndoorTemp) {
                l.hasClimate = true;
                l.climateRect = Rect(0, (int16_t)(h / 2 + 1), (uint16_t)w, (uint16_t)(h - (h / 2) - 1));
            }
        }
    } else if (isWide) {
        // ====================================================================
        // WIDESCREEN DESK CLOCK LAYOUT (e.g. 256x64, 192x64, 128x64, 128x32)
        // ====================================================================
        l.isHorizontalDeck = true;

        bool hasTopWidgets = config.showWorldClock || config.showWeather || config.showIndoorTemp || config.showSysInfo;
        bool hasBotWidgets = config.showMarkets;
        bool hasAnyContent = hasTopWidgets || hasBotWidgets;

        int gap = (h >= 64) ? 2 : 1;
        int contentX = 0;
        int contentW = w;

        // 1. Clock Placement & Auto-Expansion
        if (config.showClock) {
            l.hasClock = true;
            if (!hasAnyContent) {
                // Clock is the only active widget -> takes 100% full screen
                l.clockRect = Rect(0, 0, (uint16_t)w, (uint16_t)h);
                return l;
            } else {
                // Clock occupies left column
                int clockW = min(h, (w >= 200) ? 64 : (w / 3));
                l.clockRect = Rect(0, 0, (uint16_t)clockW, (uint16_t)h);
                contentX = clockW + gap;
                contentW = w - contentX;
            }
        }

        // 2. Right Content Area (Top Row & Bottom Row Auto-Scaling)
        if (contentW > 10) {
            if (hasTopWidgets && hasBotWidgets) {
                // Dual Row Split
                int topH = (h - gap) / 2;
                int botY = topH + gap;
                int botH = h - botY;

                int topCount = (config.showWorldClock ? 1 : 0) + 
                               ((config.showWeather || config.showIndoorTemp) ? 1 : 0) + 
                               (config.showSysInfo ? 1 : 0);
                int totalGaps = (topCount - 1) * gap;
                int availTopW = contentW - totalGaps;
                int remainingW = availTopW;
                int widgetsLeft = topCount;
                int curTopX = contentX;

                if (config.showWorldClock && widgetsLeft > 0) {
                    l.hasWorldClock = true;
                    int slotW = (widgetsLeft == 1) ? remainingW : (availTopW / topCount);
                    l.worldClockRect = Rect((int16_t)curTopX, 0, (uint16_t)slotW, (uint16_t)topH);
                    curTopX += slotW + gap;
                    remainingW -= slotW;
                    widgetsLeft--;
                }
                if ((config.showWeather || config.showIndoorTemp) && widgetsLeft > 0) {
                    l.hasClimate = true;
                    int slotW = (widgetsLeft == 1) ? remainingW : (availTopW / topCount);
                    l.climateRect = Rect((int16_t)curTopX, 0, (uint16_t)slotW, (uint16_t)topH);
                    curTopX += slotW + gap;
                    remainingW -= slotW;
                    widgetsLeft--;
                }
                if (config.showSysInfo && widgetsLeft > 0) {
                    l.hasSysInfo = true;
                    l.sysInfoRect = Rect((int16_t)curTopX, 0, (uint16_t)remainingW, (uint16_t)topH);
                }

                // Bottom Row (Markets)
                l.hasMarket = true;
                l.marketRect = Rect((int16_t)contentX, (int16_t)botY, (uint16_t)contentW, (uint16_t)botH);

            } else if (hasTopWidgets && !hasBotWidgets) {
                // Only Top Widgets -> Expand to Full Height h
                int topCount = (config.showWorldClock ? 1 : 0) + 
                               ((config.showWeather || config.showIndoorTemp) ? 1 : 0) + 
                               (config.showSysInfo ? 1 : 0);
                int totalGaps = (topCount - 1) * gap;
                int availTopW = contentW - totalGaps;
                int remainingW = availTopW;
                int widgetsLeft = topCount;
                int curTopX = contentX;

                if (config.showWorldClock && widgetsLeft > 0) {
                    l.hasWorldClock = true;
                    int slotW = (widgetsLeft == 1) ? remainingW : (availTopW / topCount);
                    l.worldClockRect = Rect((int16_t)curTopX, 0, (uint16_t)slotW, (uint16_t)h);
                    curTopX += slotW + gap;
                    remainingW -= slotW;
                    widgetsLeft--;
                }
                if ((config.showWeather || config.showIndoorTemp) && widgetsLeft > 0) {
                    l.hasClimate = true;
                    int slotW = (widgetsLeft == 1) ? remainingW : (availTopW / topCount);
                    l.climateRect = Rect((int16_t)curTopX, 0, (uint16_t)slotW, (uint16_t)h);
                    curTopX += slotW + gap;
                    remainingW -= slotW;
                    widgetsLeft--;
                }
                if (config.showSysInfo && widgetsLeft > 0) {
                    l.hasSysInfo = true;
                    l.sysInfoRect = Rect((int16_t)curTopX, 0, (uint16_t)remainingW, (uint16_t)h);
                }

            } else if (!hasTopWidgets && hasBotWidgets) {
                // Only Market Ticker -> Expands to Full Height h
                l.hasMarket = true;
                l.marketRect = Rect((int16_t)contentX, 0, (uint16_t)contentW, (uint16_t)h);
            }
        }

    } else if (isSquare) {
        // ====================================================================
        // SQUARE MATRIX (64x64, 32x32)
        // ====================================================================
        bool hasClim = config.showWeather || config.showIndoorTemp;
        bool hasMkt = config.showMarkets;
        bool hasWc = config.showWorldClock;
        bool hasAnyContent = hasClim || hasMkt || hasWc;

        if (config.showClock && !hasAnyContent) {
            l.hasClock = true;
            l.clockRect = Rect(0, 0, (uint16_t)w, (uint16_t)h);
        } else if (config.showClock && hasAnyContent) {
            int topH = h * 55 / 100; // Clock takes upper 55%
            int botY = topH + 1;
            int botH = h - botY;

            l.hasClock = true;
            l.clockRect = Rect(0, 0, (uint16_t)w, (uint16_t)topH);

            if (hasClim && (hasMkt || hasWc)) {
                l.hasClimate = true;
                l.climateRect = Rect(0, (int16_t)botY, (uint16_t)(w / 2 - 1), (uint16_t)botH);
                if (hasMkt) {
                    l.hasMarket = true;
                    l.marketRect = Rect((int16_t)(w / 2), (int16_t)botY, (uint16_t)(w - w / 2), (uint16_t)botH);
                } else {
                    l.hasWorldClock = true;
                    l.worldClockRect = Rect((int16_t)(w / 2), (int16_t)botY, (uint16_t)(w - w / 2), (uint16_t)botH);
                }
            } else if (hasClim) {
                l.hasClimate = true;
                l.climateRect = Rect(0, (int16_t)botY, (uint16_t)w, (uint16_t)botH);
            } else if (hasMkt) {
                l.hasMarket = true;
                l.marketRect = Rect(0, (int16_t)botY, (uint16_t)w, (uint16_t)botH);
            } else if (hasWc) {
                l.hasWorldClock = true;
                l.worldClockRect = Rect(0, (int16_t)botY, (uint16_t)w, (uint16_t)botH);
            }
        } else if (!config.showClock && hasAnyContent) {
            int halfH = h / 2;
            if (hasClim && hasMkt) {
                l.hasClimate = true;
                l.climateRect = Rect(0, 0, (uint16_t)w, (uint16_t)(halfH - 1));
                l.hasMarket = true;
                l.marketRect = Rect(0, (int16_t)(halfH + 1), (uint16_t)w, (uint16_t)(h - halfH - 1));
            } else if (hasClim) {
                l.hasClimate = true;
                l.climateRect = Rect(0, 0, (uint16_t)w, (uint16_t)h);
            } else if (hasMkt) {
                l.hasMarket = true;
                l.marketRect = Rect(0, 0, (uint16_t)w, (uint16_t)h);
            }
        }

    } else {
        // ====================================================================
        // STANDARD COMPACT 64x32
        // ====================================================================
        bool hasClim = config.showWeather || config.showIndoorTemp;
        bool hasMkt = config.showMarkets;
        bool hasAnyContent = hasClim || hasMkt;

        if (config.showClock && !hasAnyContent) {
            l.hasClock = true;
            l.clockRect = Rect(0, 0, (uint16_t)w, (uint16_t)h);
        } else if (!config.showClock && hasAnyContent) {
            if (hasClim) {
                l.hasClimate = true;
                l.climateRect = Rect(0, 0, (uint16_t)w, (uint16_t)h);
            } else if (hasMkt) {
                l.hasMarket = true;
                l.marketRect = Rect(0, 0, (uint16_t)w, (uint16_t)h);
            }
        } else {
            int leftW = w / 2;
            l.hasClock = true;
            l.clockRect = Rect(0, 0, (uint16_t)leftW, (uint16_t)h);
            if (hasClim) {
                l.hasClimate = true;
                l.climateRect = Rect((int16_t)(leftW + 1), 0, (uint16_t)(w - leftW - 1), (uint16_t)h);
            }
        }
    }

    return l;
}
