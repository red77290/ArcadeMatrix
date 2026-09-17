#include "ClockFaceFont.h"
#include "../fonts/ArcadeFonts.h"

void ClockFaceFont::load(const EngineConfig* cfg) {
    active = nullptr;
    String setting = cfg ? cfg->getString("clock_font", "") : "";
    if (setting.isEmpty() && cfg) setting = cfg->getString("font", "");
    if (setting.isEmpty() && cfg) setting = cfg->getString("clock_font_path", "");
    if (setting.isEmpty() && cfg) setting = cfg->getString("font_path", "");
    if (setting.isEmpty() || setting.equalsIgnoreCase("Default")) return;

    if (setting.equalsIgnoreCase("PressStart2P") || setting.equalsIgnoreCase("PressStart2P.ttf")) { active = &PressStart2P9pt7b; return; }
    if (setting.equalsIgnoreCase("namco") || setting.equalsIgnoreCase("namco.ttf")) { active = &namco__9pt7b; return; }
    if (setting.equalsIgnoreCase("FreeSansBold") || setting.equalsIgnoreCase("FreeSansBold.ttf")) { active = &FreeSansBold9pt7b; return; }
    if (setting.equalsIgnoreCase("FreeMonoBold") || setting.equalsIgnoreCase("FreeMonoBold.ttf")) { active = &FreeMonoBold9pt7b; return; }
    if (setting.equalsIgnoreCase("RetroGaming") || setting.equalsIgnoreCase("Retro_Gaming") || setting.equalsIgnoreCase("RetroGaming.ttf")) { active = &Retro_Gaming9pt7b; return; }

    if (setting.endsWith(".amf") || setting.endsWith(".AMF") || setting.startsWith("/")) {
        if (!loader.loadFromSD(setting.c_str())) {
            String alt = setting.startsWith("/") ? setting : ("/fonts/" + setting);
            if (!loader.loadFromSD(alt.c_str())) {
                Serial.printf("ClockFaceFont: clock_font '%s' failed to load from SD, using the built-in font.\n", setting.c_str());
                return;
            }
        }
        active = loader.getFont();
    }
}
