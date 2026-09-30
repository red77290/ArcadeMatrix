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

// Long forms for wide panels. Accented capitals are avoided because the built-in 5x7 font has none.
const char* I18n::getWeatherDayLabelLong(int dayOfWeek, bool isToday, bool isTomorrow, Lang l) {
    if (isToday) {
        switch (l) {
            case Lang::EN: return "TODAY";
            case Lang::ES: return "HOY";
            default: return "AUJOURD'HUI";
        }
    }
    if (isTomorrow) {
        switch (l) {
            case Lang::EN: return "TOMORROW";
            case Lang::ES: return "MANANA";
            default: return "DEMAIN";
        }
    }
    switch (l) {
        case Lang::EN: {
            static const char* enDays[] = {"SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY"};
            return enDays[dayOfWeek % 7];
        }
        case Lang::ES: {
            static const char* esDays[] = {"DOMINGO", "LUNES", "MARTES", "MIERCOLES", "JUEVES", "VIERNES", "SABADO"};
            return esDays[dayOfWeek % 7];
        }
        default: {
            static const char* frDays[] = {"DIMANCHE", "LUNDI", "MARDI", "MERCREDI", "JEUDI", "VENDREDI", "SAMEDI"};
            return frDays[dayOfWeek % 7];
        }
    }
}

String I18n::getWeatherConditionLong(const String& raw, Lang l) {
    String lower = raw;
    lower.toLowerCase();
    switch (l) {
        case Lang::EN: {
            if (lower.indexOf("clear") >= 0 || lower.indexOf("sun") >= 0) return "Clear";
            if (lower.indexOf("few clouds") >= 0 || lower.indexOf("scattered") >= 0) return "Partly Cloudy";
            if (lower.indexOf("overcast") >= 0) return "Overcast";
            if (lower.indexOf("cloud") >= 0) return "Cloudy";
            if (lower.indexOf("thunder") >= 0 || lower.indexOf("storm") >= 0) return "Thunderstorm";
            if (lower.indexOf("drizzle") >= 0) return "Drizzle";
            if (lower.indexOf("rain") >= 0) return "Rain";
            if (lower.indexOf("snow") >= 0) return "Snow";
            if (lower.indexOf("mist") >= 0) return "Mist";
            if (lower.indexOf("fog") >= 0) return "Fog";
            return "Clear";
        }
        case Lang::ES: {
            if (lower.indexOf("clear") >= 0 || lower.indexOf("sun") >= 0) return "Soleado";
            if (lower.indexOf("few clouds") >= 0 || lower.indexOf("scattered") >= 0) return "Parcialmente Nublado";
            if (lower.indexOf("overcast") >= 0) return "Cubierto";
            if (lower.indexOf("cloud") >= 0) return "Nublado";
            if (lower.indexOf("thunder") >= 0 || lower.indexOf("storm") >= 0) return "Tormenta";
            if (lower.indexOf("drizzle") >= 0) return "Llovizna";
            if (lower.indexOf("rain") >= 0) return "Lluvia";
            if (lower.indexOf("snow") >= 0) return "Nieve";
            if (lower.indexOf("mist") >= 0) return "Bruma";
            if (lower.indexOf("fog") >= 0) return "Niebla";
            return "Variable";
        }
        default: { // FR
            if (lower.indexOf("clear") >= 0 || lower.indexOf("sun") >= 0) return "Soleil";
            if (lower.indexOf("few clouds") >= 0 || lower.indexOf("scattered") >= 0) return "Eclaircies";
            if (lower.indexOf("overcast") >= 0) return "Couvert";
            if (lower.indexOf("cloud") >= 0) return "Nuageux";
            if (lower.indexOf("thunder") >= 0 || lower.indexOf("storm") >= 0) return "Orage";
            if (lower.indexOf("drizzle") >= 0) return "Bruine";
            if (lower.indexOf("rain") >= 0) return "Pluie";
            if (lower.indexOf("snow") >= 0) return "Neige";
            if (lower.indexOf("mist") >= 0) return "Brume";
            if (lower.indexOf("fog") >= 0) return "Brouillard";
            return "Variable";
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

const char* I18n::getGNewsStatusLabel(uint8_t status) {
    return getGNewsStatusLabel(status, getLang());
}

const char* I18n::getGNewsStatusLabel(uint8_t status, Lang l) {
    switch (status) {
        case 1: // EMPTY_KEY
            switch (l) {
                case Lang::EN: return "API KEY REQUIRED";
                case Lang::ES: return "CLAVE API REQUERIDA";
                default: return "CLE API REQUISE";
            }
        case 2: // INVALID_KEY
            switch (l) {
                case Lang::EN: return "INVALID API KEY";
                case Lang::ES: return "CLAVE API INVALIDA";
                default: return "CLE API INVALIDE";
            }
        case 3: // RATE_LIMITED
            switch (l) {
                case Lang::EN: return "RATE LIMITED";
                case Lang::ES: return "LIMITE SUPERADO";
                default: return "LIMITE ATTEINTE";
            }
        case 4: // NETWORK_ERROR
            switch (l) {
                case Lang::EN: return "NETWORK ERROR";
                case Lang::ES: return "ERROR DE RED";
                default: return "ERREUR RESEAU";
            }
        case 5: // LOADING
            switch (l) {
                case Lang::EN: return "LOADING...";
                case Lang::ES: return "CARGANDO...";
                default: return "CHARGEMENT...";
            }
        default:
            return "GNEWS LIVE";
    }
}


// ---------------------------------------------------------------------------
// Dates and spoken time
// ---------------------------------------------------------------------------

const char* I18n::getMonthLabel(int month0, Lang l) {
    static const char* en[] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };
    static const char* fr[] = { "JANV", "FEVR", "MARS", "AVR", "MAI", "JUIN",
                                "JUIL", "AOUT", "SEPT", "OCT", "NOV", "DEC" };
    static const char* es[] = { "ENE", "FEB", "MAR", "ABR", "MAY", "JUN",
                                "JUL", "AGO", "SEP", "OCT", "NOV", "DIC" };
    int i = ((month0 % 12) + 12) % 12;
    switch (l) {
        case Lang::FR: return fr[i];
        case Lang::ES: return es[i];
        default: return en[i];
    }
}

void I18n::getDateLine(int weekday, int month0, int day, Lang l, char* out, size_t outSize) {
    if (!out || outSize == 0) return;
    static const char* enDays[] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };
    static const char* frDays[] = { "DIM", "LUN", "MAR", "MER", "JEU", "VEN", "SAM" };
    static const char* esDays[] = { "DOM", "LUN", "MAR", "MIE", "JUE", "VIE", "SAB" };
    int wd = ((weekday % 7) + 7) % 7;
    const char* dayName = (l == Lang::FR) ? frDays[wd] : (l == Lang::ES) ? esDays[wd] : enDays[wd];
    const char* monthName = getMonthLabel(month0, l);
    if (l == Lang::EN) {
        snprintf(out, outSize, "%s %s %d", dayName, monthName, day);   // month before the day
    } else {
        snprintf(out, outSize, "%s %d %s", dayName, day, monthName);   // day before the month
    }
}

namespace {

const char* const EN_HOURS[13] = { "twelve", "one", "two", "three", "four", "five", "six",
                                   "seven", "eight", "nine", "ten", "eleven", "twelve" };
const char* const EN_UNITS[20] = { "zero", "one", "two", "three", "four", "five", "six", "seven",
                                   "eight", "nine", "ten", "eleven", "twelve", "thirteen",
                                   "fourteen", "fifteen", "sixteen", "seventeen", "eighteen",
                                   "nineteen" };
const char* const EN_TENS[6] = { "", "ten", "twenty", "thirty", "forty", "fifty" };

const char* const FR_HOURS[13] = { "minuit", "une", "deux", "trois", "quatre", "cinq", "six",
                                   "sept", "huit", "neuf", "dix", "onze", "midi" };
const char* const FR_UNITS[20] = { "zero", "une", "deux", "trois", "quatre", "cinq", "six", "sept",
                                   "huit", "neuf", "dix", "onze", "douze", "treize", "quatorze",
                                   "quinze", "seize", "dix-sept", "dix-huit", "dix-neuf" };
const char* const FR_TENS[6] = { "", "dix", "vingt", "trente", "quarante", "cinquante" };

const char* const ES_HOURS[13] = { "doce", "una", "dos", "tres", "cuatro", "cinco", "seis",
                                   "siete", "ocho", "nueve", "diez", "once", "doce" };
const char* const ES_UNITS[20] = { "cero", "uno", "dos", "tres", "cuatro", "cinco", "seis", "siete",
                                   "ocho", "nueve", "diez", "once", "doce", "trece", "catorce",
                                   "quince", "dieciseis", "diecisiete", "dieciocho", "diecinueve" };
const char* const ES_TENS[6] = { "", "diez", "veinte", "treinta", "cuarenta", "cincuenta" };

/// Spell a minute count into `out`. Everything here is snprintf into the caller's buffer, so the
/// face can redraw without a heap allocation on the render core.
void spellMinutes(int m, Lang l, char* out, size_t n) {
    if (!out || n == 0) return;
    if (m < 20) {
        snprintf(out, n, "%s", (l == Lang::FR) ? FR_UNITS[m] : (l == Lang::ES) ? ES_UNITS[m] : EN_UNITS[m]);
        return;
    }
    int tens = m / 10, units = m % 10;
    const char* tensWord = (l == Lang::FR) ? FR_TENS[tens] : (l == Lang::ES) ? ES_TENS[tens] : EN_TENS[tens];
    if (units == 0) {
        snprintf(out, n, "%s", tensWord);
        return;
    }
    const char* unitWord = (l == Lang::FR) ? FR_UNITS[units] : (l == Lang::ES) ? ES_UNITS[units] : EN_UNITS[units];
    if (l == Lang::FR) {
        // vingt et une, trente-deux
        snprintf(out, n, (units == 1) ? "%s et %s" : "%s-%s", tensWord, unitWord);
        return;
    }
    if (l == Lang::ES) {
        // veintiuno is one word; from thirty on it is "treinta y uno"
        if (tens == 2) snprintf(out, n, "veinti%s", unitWord);
        else           snprintf(out, n, "%s y %s", tensWord, unitWord);
        return;
    }
    snprintf(out, n, "%s %s", tensWord, unitWord);
}

}  // namespace

void I18n::getSpokenTime(int hours, int minutes, Lang l,
                         char* hourWords, size_t hourSize,
                         char* minuteWords, size_t minuteSize) {
    if (!hourWords || hourSize == 0 || !minuteWords || minuteSize == 0) return;
    hourWords[0] = '\0';
    minuteWords[0] = '\0';

    int h24 = ((hours % 24) + 24) % 24;
    int m = ((minutes % 60) + 60) % 60;
    int h12 = h24 % 12;
    char mins[24];

    if (l == Lang::FR) {
        // "huit" / "heures cinq", with midi and minuit on the hour.
        if (m == 0 && (h24 == 0 || h24 == 12)) {
            snprintf(hourWords, hourSize, "%s", (h24 == 0) ? "minuit" : "midi");
            return;
        }
        snprintf(hourWords, hourSize, "%s", FR_HOURS[(h12 == 0) ? ((h24 == 0) ? 0 : 12) : h12]);
        if (m == 0)       snprintf(minuteWords, minuteSize, "heures");
        else if (m == 15) snprintf(minuteWords, minuteSize, "heures et quart");
        else if (m == 30) snprintf(minuteWords, minuteSize, "heures et demie");
        else if (m == 45) snprintf(minuteWords, minuteSize, "heures quarante-cinq");
        else {
            spellMinutes(m, l, mins, sizeof(mins));
            snprintf(minuteWords, minuteSize, "heures %s", mins);
        }
        return;
    }

    if (l == Lang::ES) {
        // "ocho" / "y cinco", with mediodia and medianoche on the hour.
        if (m == 0 && (h24 == 0 || h24 == 12)) {
            snprintf(hourWords, hourSize, "%s", (h24 == 0) ? "medianoche" : "mediodia");
            return;
        }
        snprintf(hourWords, hourSize, "%s", ES_HOURS[(h12 == 0) ? 0 : h12]);
        if (m == 0)       snprintf(minuteWords, minuteSize, "en punto");
        else if (m == 15) snprintf(minuteWords, minuteSize, "y cuarto");
        else if (m == 30) snprintf(minuteWords, minuteSize, "y media");
        else {
            spellMinutes(m, l, mins, sizeof(mins));
            snprintf(minuteWords, minuteSize, "y %s", mins);
        }
        return;
    }

    // English, the wording of the source face.
    if (m == 0 && h24 == 0)  { snprintf(hourWords, hourSize, "midnight"); return; }
    if (m == 0 && h24 == 12) { snprintf(hourWords, hourSize, "noon"); return; }
    snprintf(hourWords, hourSize, "%s", EN_HOURS[(h12 == 0) ? 0 : h12]);
    if (m == 0)       snprintf(minuteWords, minuteSize, "o'clock");
    else if (m == 30) snprintf(minuteWords, minuteSize, "%s", (h24 == 0 || h24 == 12) ? "thirty" : "a half");
    else if (m < 10)  snprintf(minuteWords, minuteSize, "oh %s", EN_UNITS[m]);
    else {
        spellMinutes(m, l, mins, sizeof(mins));
        snprintf(minuteWords, minuteSize, "%s", mins);
    }
}
