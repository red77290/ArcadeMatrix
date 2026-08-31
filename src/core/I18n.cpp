#include "core/I18n.h"
#include "core/ConfigLoader.h"

extern ConfigLoader config;

Lang I18n::parseLang(const String& code) {
    String c = code;
    c.trim();
    c.toLowerCase();
    if (c == "en") return Lang::EN;
    if (c == "es") return Lang::ES;
    return Lang::FR;
}

Lang I18n::getLang() {
    return parseLang(config.system.lang);
}

const char* I18n::getLangCode(Lang l) {
    switch (l) {
        case Lang::EN: return "en";
        case Lang::ES: return "es";
        default: return "fr";
    }
}

const char* I18n::getWeatherDayLabel(int dayOfWeek, bool isToday, bool isTomorrow) {
    return getWeatherDayLabel(dayOfWeek, isToday, isTomorrow, getLang());
}

const char* I18n::getWeatherDayLabel(int dayOfWeek, bool isToday, bool isTomorrow, Lang l) {
    if (isToday) {
        switch (l) {
            case Lang::EN: return "TODAY";
            case Lang::ES: return "HOY";
            default: return "AUJ.";
        }
    }
    if (isTomorrow) {
        switch (l) {
            case Lang::EN: return "TOM.";
            case Lang::ES: return "MAÑ.";
            default: return "DEM.";
        }
    }
    
    switch (l) {
        case Lang::EN: {
            static const char* enDays[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
            return enDays[dayOfWeek % 7];
        }
        case Lang::ES: {
            static const char* esDays[] = {"DOM", "LUN", "MAR", "MIÉ", "JUE", "VIE", "SÁB"};
            return esDays[dayOfWeek % 7];
        }
        default: {
            static const char* frDays[] = {"DIM", "LUN", "MAR", "MER", "JEU", "VEN", "SAM"};
            return frDays[dayOfWeek % 7];
        }
    }
}

String I18n::getWeatherCondition(const String& raw) {
    return getWeatherCondition(raw, getLang());
}

String I18n::getWeatherCondition(const String& raw, Lang l) {
    String lower = raw;
    lower.toLowerCase();
    
    switch (l) {
        case Lang::EN: {
            if (lower.indexOf("clear") >= 0 || lower.indexOf("sun") >= 0) return "Clear";
            if (lower.indexOf("few clouds") >= 0 || lower.indexOf("scattered") >= 0) return "P.Cloudy";
            if (lower.indexOf("overcast") >= 0) return "Overcast";
            if (lower.indexOf("cloud") >= 0) return "Clouds";
            if (lower.indexOf("thunder") >= 0 || lower.indexOf("storm") >= 0) return "Storm";
            if (lower.indexOf("drizzle") >= 0) return "Drizzle";
            if (lower.indexOf("rain") >= 0) return "Rain";
            if (lower.indexOf("snow") >= 0) return "Snow";
            if (lower.indexOf("mist") >= 0) return "Mist";
            if (lower.indexOf("fog") >= 0) return "Fog";
            return "Clear";
        }
        case Lang::ES: {
            if (lower.indexOf("clear") >= 0 || lower.indexOf("sun") >= 0) return "Soleado";
            if (lower.indexOf("few clouds") >= 0 || lower.indexOf("scattered") >= 0) return "Parcial";
            if (lower.indexOf("overcast") >= 0) return "Cubierto";
            if (lower.indexOf("cloud") >= 0) return "Nubes";
            if (lower.indexOf("thunder") >= 0 || lower.indexOf("storm") >= 0) return "Torm.";
            if (lower.indexOf("drizzle") >= 0) return "Lloviz.";
            if (lower.indexOf("rain") >= 0) return "Lluvia";
            if (lower.indexOf("snow") >= 0) return "Nieve";
            if (lower.indexOf("mist") >= 0) return "Bruma";
            if (lower.indexOf("fog") >= 0) return "Niebla";
            return "Variable";
        }
        default: { // FR
            if (lower.indexOf("clear") >= 0 || lower.indexOf("sun") >= 0) return "Soleil";
            if (lower.indexOf("few clouds") >= 0 || lower.indexOf("scattered") >= 0) return "Eclairc.";
            if (lower.indexOf("overcast") >= 0) return "Couvert";
            if (lower.indexOf("cloud") >= 0) return "Nuages";
            if (lower.indexOf("thunder") >= 0 || lower.indexOf("storm") >= 0) return "Orage";
            if (lower.indexOf("drizzle") >= 0) return "Bruine";
            if (lower.indexOf("rain") >= 0) return "Pluie";
            if (lower.indexOf("snow") >= 0) return "Neige";
            if (lower.indexOf("mist") >= 0) return "Brume";
            if (lower.indexOf("fog") >= 0) return "Brouill.";
            return "Variable";
        }
    }
}

const char* I18n::getOutdoorLabel(Lang l) {
    switch (l) {
        case Lang::EN: return "OUTDOOR";
        case Lang::ES: return "EXTERIOR";
        default: return "EXTERIEUR";
    }
}

const char* I18n::getIndoorLabel(Lang l) {
    switch (l) {
        case Lang::EN: return "IN:";
        default: return "INT:";
    }
}

const char* I18n::getClimateLabel(Lang l) {
    switch (l) {
        case Lang::EN: return "CLIMATE";
        case Lang::ES: return "CLIMA";
        default: return "METEO";
    }
}

std::vector<String> I18n::getWordClockLines(int hours, int minutes) {
    int roundedM = (minutes / 5) * 5;
    bool pastHalf = minutes > 30;
    int displayH = (pastHalf && roundedM != 0) ? (hours + 1) % 24 : hours;
    int readH = displayH % 12;
    Lang l = getLang();
    
    switch (l) {
        case Lang::EN: {
            String strH;
            if (displayH == 0) strH = "MIDNIGHT";
            else if (displayH == 12) strH = "NOON";
            else {
                switch (readH) {
                    case 1: strH = "ONE"; break;
                    case 2: strH = "TWO"; break;
                    case 3: strH = "THREE"; break;
                    case 4: strH = "FOUR"; break;
                    case 5: strH = "FIVE"; break;
                    case 6: strH = "SIX"; break;
                    case 7: strH = "SEVEN"; break;
                    case 8: strH = "EIGHT"; break;
                    case 9: strH = "NINE"; break;
                    case 10: strH = "TEN"; break;
                    case 11: strH = "ELEVEN"; break;
                    default: strH = "?"; break;
                }
            }
            
            String strM;
            if (roundedM == 0 || roundedM == 60) strM = "O'CLOCK";
            else if (roundedM == 5 && !pastHalf) strM = "FIVE";
            else if (roundedM == 10 && !pastHalf) strM = "TEN";
            else if (roundedM == 15) strM = "A QUARTER";
            else if (roundedM == 20 && !pastHalf) strM = "TWENTY";
            else if (roundedM == 25 && !pastHalf) strM = "TWENTY-FIVE";
            else if (roundedM == 30) strM = "HALF";
            else if (pastHalf) {
                int diff = 60 - roundedM;
                if (diff == 5) strM = "FIVE";
                else if (diff == 10) strM = "TEN";
                else if (diff == 15) strM = "A QUARTER";
                else if (diff == 20) strM = "TWENTY";
                else if (diff == 25) strM = "TWENTY-FIVE";
                else strM = "FIVE";
            } else {
                strM = "O'CLOCK";
            }
            
            String strConn = "";
            if (roundedM != 0 && roundedM != 60) {
                strConn = pastHalf ? "TO" : "PAST";
            }
            
            std::vector<String> lines;
            lines.push_back("IT IS");
            if (strConn == "") {
                if (displayH == 0 || displayH == 12) {
                    lines.push_back(strH);
                } else {
                    lines.push_back(strH);
                    lines.push_back(strM);
                }
            } else {
                lines.push_back(strM);
                lines.push_back(strConn);
                lines.push_back(strH);
            }
            return lines;
        }
        case Lang::ES: {
            String strH;
            if (displayH == 0) strH = "MEDIANOCHE";
            else if (displayH == 12) strH = "MEDIODIA";
            else {
                switch (readH) {
                    case 1: strH = "LA UNA"; break;
                    case 2: strH = "LAS DOS"; break;
                    case 3: strH = "LAS TRES"; break;
                    case 4: strH = "LAS CUATRO"; break;
                    case 5: strH = "LAS CINCO"; break;
                    case 6: strH = "LAS SEIS"; break;
                    case 7: strH = "LAS SIETE"; break;
                    case 8: strH = "LAS OCHO"; break;
                    case 9: strH = "LAS NUEVE"; break;
                    case 10: strH = "LAS DIEZ"; break;
                    case 11: strH = "LAS ONCE"; break;
                    default: strH = "?"; break;
                }
            }
            
            String strM;
            if (roundedM == 0 || roundedM == 60) strM = "EN PUNTO";
            else if (roundedM == 5 && !pastHalf) strM = "Y CINCO";
            else if (roundedM == 10 && !pastHalf) strM = "Y DIEZ";
            else if (roundedM == 15 && !pastHalf) strM = "Y CUARTO";
            else if (roundedM == 20 && !pastHalf) strM = "Y VEINTE";
            else if (roundedM == 25 && !pastHalf) strM = "Y VEINTICINCO";
            else if (roundedM == 30) strM = "Y MEDIA";
            else if (pastHalf) {
                int diff = 60 - roundedM;
                if (diff == 5) strM = "MENOS CINCO";
                else if (diff == 10) strM = "MENOS DIEZ";
                else if (diff == 15) strM = "MENOS CUARTO";
                else if (diff == 20) strM = "MENOS VEINTE";
                else if (diff == 25) strM = "MENOS VEINTICINCO";
                else strM = "MENOS CINCO";
            } else {
                strM = "EN PUNTO";
            }
            
            std::vector<String> lines;
            if (displayH == 0 || displayH == 12) {
                if (roundedM == 0 || roundedM == 60) {
                    lines.push_back("ES LA");
                    lines.push_back(strH);
                } else {
                    lines.push_back("ES LA");
                    lines.push_back(strH);
                    lines.push_back(strM);
                }
            } else {
                String prefix = (readH == 1 && displayH != 0 && displayH != 12) ? "ES LA" : "SON LAS";
                lines.push_back(prefix);
                lines.push_back(strH);
                lines.push_back(strM);
            }
            return lines;
        }
        default: { // FR
            String strH;
            if (displayH == 0) strH = "MINUIT";
            else if (displayH == 12) strH = "MIDI";
            else {
                switch (readH) {
                    case 1: strH = "UNE"; break;
                    case 2: strH = "DEUX"; break;
                    case 3: strH = "TROIS"; break;
                    case 4: strH = "QUATRE"; break;
                    case 5: strH = "CINQ"; break;
                    case 6: strH = "SIX"; break;
                    case 7: strH = "SEPT"; break;
                    case 8: strH = "HUIT"; break;
                    case 9: strH = "NEUF"; break;
                    case 10: strH = "DIX"; break;
                    case 11: strH = "ONZE"; break;
                    default: strH = "?"; break;
                }
            }
            
            String strHSuffix = "";
            if (displayH != 0 && displayH != 12) {
                strHSuffix = (readH == 1) ? " HEURE" : " HEURES";
            }
            
            String strM;
            if (roundedM == 0 || roundedM == 60) strM = "PILE";
            else if (roundedM == 5 && !pastHalf) strM = "CINQ";
            else if (roundedM == 10 && !pastHalf) strM = "DIX";
            else if (roundedM == 15 && !pastHalf) strM = "ET QUART";
            else if (roundedM == 20 && !pastHalf) strM = "VINGT";
            else if (roundedM == 25 && !pastHalf) strM = "VINGT-CINQ";
            else if (roundedM == 30) strM = "ET DEMIE";
            else if (pastHalf) {
                int diff = 60 - roundedM;
                if (diff == 5) strM = "MOINS CINQ";
                else if (diff == 10) strM = "MOINS DIX";
                else if (diff == 15) strM = "MOINS LE QUART";
                else if (diff == 20) strM = "MOINS VINGT";
                else if (diff == 25) strM = "MOINS VINGT-CINQ";
                else strM = "MOINS CINQ";
            } else {
                strM = "PILE";
            }
            
            return {
                "IL EST",
                strH + strHSuffix,
                strM
            };
        }
    }
}

const char* I18n::getNoiseLevelLabel(int level) {
    Lang l = getLang();
    switch (l) {
        case Lang::EN: {
            switch (level) {
                case 0: return "SILENCE";
                case 1: return "PEACEFUL";
                case 2: return "MODERATE";
                case 3: return "HIGH";
                case 4: return "LOUD";
                default: return "ALERT";
            }
        }
        case Lang::ES: {
            switch (level) {
                case 0: return "SILENCIO";
                case 1: return "TRANQUILO";
                case 2: return "MODERADO";
                case 3: return "ELEVADO";
                case 4: return "RUIDOSO";
                default: return "ALERTA";
            }
        }
        default: { // FR
            switch (level) {
                case 0: return "SILENCE";
                case 1: return "PAISIBLE";
                case 2: return "MODERE";
                case 3: return "ELEVE";
                case 4: return "BRUYANT";
                default: return "ALERTE";
            }
        }
    }
}
