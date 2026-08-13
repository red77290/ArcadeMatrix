#include "ConfigLoader.h"
#include "SDUtils.h"
#include "Logger.h"
#include <ArduinoJson.h>
#include "../engines/GifEngine.h"

ConfigLoader::ConfigLoader() {
    setDefaults();
}

void ConfigLoader::setDefaults() {
    matrix.width = 64;
    matrix.height = 32;
    matrix.panelType = "FM6126A";
    matrix.chainLength = 1;
    matrix.powerLimitPercent = 100;
    matrix.forceSingleBuffer = false;
    matrix.pwmBits = 8;
    matrix.rgbSequence = "RGB";
    matrix.limitRefreshRateHz = 0;
    matrix.driverChip = "SHIFTREG";
    matrix.clkPhase = false;
    matrix.latchBlanking = 8;
    matrix.rowAddressMode = 0;

    wifi.ssid = "";
    wifi.password = "";
    wifi.hostname = "ArcadeMatrix";

    mqtt.enabled = false;
    mqtt.broker = "";
    mqtt.port = 1883;
    mqtt.user = "";
    mqtt.pass = "";
    mqtt.deviceName = "ArcadeMatrix";
    mqtt.topic_batocera = "batocera/system/playing";
    mqtt.topic_recalbox = "recalbox/system/playing";

    time.ntpServer = "pool.ntp.org";
    time.timezone = "CET-1CEST,M3.5.0,M10.5.0/3";
    time.format24h = true;
    time.clock_font = 0;
    time.clock_size = 1;
    time.clock_theme = 0;
    time.clock_offset_x = 0;
    time.clock_offset_y = 0;
    time.clock_color_1 = "#ffffff";
    time.clock_color_2 = "#ffffff";
    time.clock_font_path = "";

    idle.rotation = "clock,date,weather,gifs,temp,decibel";
    idle.clock_duration_sec = 60;
    idle.date_duration_sec = 10;
    idle.weather_duration_sec = 15;
    idle.temp_duration_sec = 8;
    idle.decibel_duration_sec = 10;
    idle.gifs_count = 3;
    idle.fighter_enabled = true;
    idle.fighter_interval_sec = 10;

    env.unit = "C";
    env.temp_offset = 0.0f;

    audio.visualizer_enabled = false;
    audio.visualizer_mode = "spectrum";
    audio.mic_gain = 1.0f;
    audio.db_calibration = 0.0f;

    dateSettings.theme = 0;
    dateSettings.format = "DD/MM";
    dateSettings.date_font = 0;
    dateSettings.date_size = 1;
    dateSettings.date_offset_x = 0;
    dateSettings.date_offset_y = 0;
    dateSettings.background_sprite = "";
    dateSettings.date_color_1 = "#ffffff";
    dateSettings.date_color_2 = "#ffffff";
    dateSettings.date_font_path = "";

    weather.api_key = "";
    weather.city = "";
    weather.lang = "en";
    weather.weather_offset_x = 0;
    weather.weather_offset_y = 0;

    standby.night_mode_enabled = false;
    standby.turn_off_at = "23:00";
    standby.wake_up_at = "07:00";
    standby.night_brightness = 10;

    fonts.custom_font_path = "";

    crypto.enabled = true;
    crypto.symbols = "BTC,ETH,SOL,DOGE";
    crypto.duration_sec = 5;
    crypto.cache_ttl_min = 1;
    crypto.currency = "USD";

    stock.enabled = true;
    stock.symbols = "AAPL,NVDA,TSLA,MSFT";
    stock.duration_sec = 5;
    stock.cache_ttl_min = 1;
}

String ConfigLoader::stripComments(String line) {
    line.trim();
    if (line.startsWith("#") || line.startsWith(";")) {
        return "";
    }

    int semiPos = line.indexOf(';');
    if (semiPos != -1) {
        line = line.substring(0, semiPos);
    }

    int spaceHashPos = line.indexOf(" #");
    if (spaceHashPos != -1) {
        line = line.substring(0, spaceHashPos);
    }

    line.trim();
    return line;
}

String ConfigLoader::extractValue(String line) {
    int equalsPos = line.indexOf('=');
    if (equalsPos == -1) return "";
    
    String val = line.substring(equalsPos + 1);
    val.trim();
    
    if (val.startsWith("\"") && val.endsWith("\"") && val.length() >= 2) {
        val = val.substring(1, val.length() - 1);
    }
    return val;
}

void ConfigLoader::parseLine(String line, String& currentSection) {
    line = stripComments(line);
    if (line.length() == 0) return;

    if (line.startsWith("[") && line.endsWith("]")) {
        currentSection = line.substring(1, line.length() - 1);
        currentSection.toUpperCase();
        return;
    }

    String value = extractValue(line);
    String key = line.substring(0, line.indexOf('='));
    key.trim();
    key.toUpperCase();

    if (currentSection == "WIFI") {
        if (key == "SSID") wifi.ssid = value;
        else if (key == "PASSWORD") wifi.password = value;
        else if (key == "HOSTNAME") wifi.hostname = value;
    } 
    else if (currentSection == "MATRIX") {
        if (key == "WIDTH") matrix.width = value.toInt();
        else if (key == "HEIGHT") matrix.height = value.toInt();
        else if (key == "PANEL_TYPE") matrix.panelType = value;
        else if (key == "CHAIN") matrix.chainLength = value.toInt();
        else if (key == "BRIGHTNESS_LIMIT" || key == "BRIGHTNESS") matrix.powerLimitPercent = value.toInt();
        else if (key == "PWM_BITS" || key == "COLOR_DEPTH") matrix.pwmBits = value.toInt();
        else if (key == "FORCE_SINGLE_BUFFER") matrix.forceSingleBuffer = (value == "true" || value == "1");
        else if (key == "RGB_SEQUENCE") matrix.rgbSequence = value;
        else if (key == "LIMIT_REFRESH_RATE_HZ") matrix.limitRefreshRateHz = value.toInt();
        else if (key == "DRIVER_CHIP") matrix.driverChip = value;
        else if (key == "CLK_PHASE") matrix.clkPhase = (value == "true" || value == "1");
        else if (key == "LATCH_BLANKING") matrix.latchBlanking = value.toInt();
        else if (key == "ROW_ADDRESS_MODE") matrix.rowAddressMode = value.toInt();
    }
    else if (currentSection == "MQTT") {
        if (key == "ENABLED") mqtt.enabled = (value == "true" || value == "1");
        else if (key == "BROKER") mqtt.broker = value;
        else if (key == "PORT") mqtt.port = value.toInt();
        else if (key == "USER") mqtt.user = value;
        else if (key == "PASS" || key == "PASSWORD") mqtt.pass = value;
        else if (key == "TOPIC_BATOCERA") mqtt.topic_batocera = value;
        else if (key == "TOPIC_RECALBOX") mqtt.topic_recalbox = value;
        else if (key == "DEVICE_NAME" || key == "DEVICENAME") mqtt.deviceName = value;
    }
    else if (currentSection == "TIME") {
        if (key == "NTP_SERVER" || key == "NTPSERVER") time.ntpServer = value;
        else if (key == "TIMEZONE") time.timezone = value;
        else if (key == "FORMAT_24H" || key == "FORMAT24H") time.format24h = (value == "true" || value == "1");
        else if (key == "CLOCK_FONT") time.clock_font = value.toInt();
        else if (key == "CLOCK_SIZE") time.clock_size = value.toInt();
        else if (key == "CLOCK_THEME") time.clock_theme = value.toInt();
        else if (key == "CLOCK_OFFSET_X") time.clock_offset_x = value.toInt();
        else if (key == "CLOCK_OFFSET_Y") time.clock_offset_y = value.toInt();
        else if (key == "CLOCK_COLOR_1") time.clock_color_1 = value;
        else if (key == "CLOCK_COLOR_2") time.clock_color_2 = value;
        else if (key == "CLOCK_FONT_PATH") time.clock_font_path = value;
    }
    else if (currentSection == "IDLE") {
        if (key == "ROTATION") idle.rotation = value;
        else if (key == "CLOCK_DURATION_SEC") idle.clock_duration_sec = value.toInt();
        else if (key == "DATE_DURATION_SEC") idle.date_duration_sec = value.toInt();
        else if (key == "WEATHER_DURATION_SEC") idle.weather_duration_sec = value.toInt();
        else if (key == "TEMP_DURATION_SEC") idle.temp_duration_sec = value.toInt();
        else if (key == "DECIBEL_DURATION_SEC" || key == "DB_DURATION_SEC") idle.decibel_duration_sec = value.toInt();
        else if (key == "GIFS_COUNT") idle.gifs_count = value.toInt();
        else if (key == "FIGHTER_ENABLED") idle.fighter_enabled = (value == "true" || value == "1");
        else if (key == "FIGHTER_INTERVAL_SEC") idle.fighter_interval_sec = value.toInt();
        
        else if (key == "MODE") idle.mode = value; // Legacy
        else if (key == "GIFS_BEFORE_CLOCK") idle.gifs_before_clock = value.toInt(); // Legacy
    }
    else if (currentSection == "ENVIRONMENT" || currentSection == "TEMP") {
        if (key == "UNIT" || key == "TEMP_UNIT") env.unit = value;
        else if (key == "TEMP_OFFSET") env.temp_offset = value.toFloat();
    }
    else if (currentSection == "AUDIO" || currentSection == "SOUND") {
        if (key == "VISUALIZER_ENABLED") audio.visualizer_enabled = (value == "true" || value == "1");
        else if (key == "VISUALIZER_MODE") audio.visualizer_mode = value;
        else if (key == "MIC_GAIN") audio.mic_gain = value.toFloat();
        else if (key == "DB_CALIBRATION") audio.db_calibration = value.toFloat();
    }
    else if (currentSection == "WEATHER") {
        if (key == "API_KEY") weather.api_key = value;
        else if (key == "CITY") weather.city = value;
        else if (key == "LANG") weather.lang = value;
        else if (key == "WEATHER_OFFSET_X") weather.weather_offset_x = value.toInt();
        else if (key == "WEATHER_OFFSET_Y") weather.weather_offset_y = value.toInt();
    }
    else if (currentSection == "STANDBY") {
        if (key == "NIGHT_MODE" || key == "NIGHT_MODE_ENABLED") standby.night_mode_enabled = (value == "true" || value == "1");
        else if (key == "TURN_OFF_AT") standby.turn_off_at = value;
        else if (key == "WAKE_UP_AT") standby.wake_up_at = value;
        else if (key == "NIGHT_BRIGHTNESS") standby.night_brightness = value.toInt();
    }
    else if (currentSection == "DATE") {
        if (key == "THEME") dateSettings.theme = value.toInt();
        else if (key == "FORMAT") dateSettings.format = value;
        else if (key == "DATE_FONT") dateSettings.date_font = value.toInt();
        else if (key == "DATE_SIZE") dateSettings.date_size = value.toInt();
        else if (key == "DATE_OFFSET_X") dateSettings.date_offset_x = value.toInt();
        else if (key == "DATE_OFFSET_Y") dateSettings.date_offset_y = value.toInt();
        else if (key == "BACKGROUND_SPRITE") dateSettings.background_sprite = value;
        else if (key == "DATE_COLOR_1") dateSettings.date_color_1 = value;
        else if (key == "DATE_COLOR_2") dateSettings.date_color_2 = value;
        else if (key == "DATE_FONT_PATH") dateSettings.date_font_path = value;
    }
    else if (currentSection == "FONTS") {
        if (key == "CUSTOM_FONT_PATH") fonts.custom_font_path = value;
    }
    else if (currentSection == "CRYPTO") {
        if (key == "ENABLED") crypto.enabled = (value == "true" || value == "1");
        else if (key == "SYMBOLS") crypto.symbols = value;
        else if (key == "DURATION_SEC") crypto.duration_sec = value.toInt();
        else if (key == "CACHE_TTL_MIN" || key == "CACHE_TTL") crypto.cache_ttl_min = value.toInt();
        else if (key == "CURRENCY") crypto.currency = value;
    }
    else if (currentSection == "STOCK" || currentSection == "STOCKS") {
        if (key == "ENABLED") stock.enabled = (value == "true" || value == "1");
        else if (key == "SYMBOLS") stock.symbols = value;
        else if (key == "DURATION_SEC") stock.duration_sec = value.toInt();
        else if (key == "CACHE_TTL_MIN" || key == "CACHE_TTL") stock.cache_ttl_min = value.toInt();
    }
}

bool ConfigLoader::parseFromString(const char* iniContent) {
    if (!iniContent) return false;
    
    String content = String(iniContent);
    String currentSection = "";
    int lineStart = 0;
    int lineEnd = 0;

    while (lineEnd != -1) {
        lineEnd = content.indexOf('\n', lineStart);
        String line;
        if (lineEnd == -1) {
            line = content.substring(lineStart);
        } else {
            line = content.substring(lineStart, lineEnd);
            lineStart = lineEnd + 1;
        }
        
        parseLine(line, currentSection);
    }
    return true;
}

bool ConfigLoader::parseFromSD(const char* filepath) {
    if (!sd.exists(filepath)) return false;
    
    FsFile file = sd.open(filepath, FILE_OPEN_READ);
    if (!file) {
        LOGE("ConfigLoader", "Failed to open %s", filepath);
        return false;
    }

    String currentSection = "";
    while (file.available()) {
        String line = file.readStringUntil('\n');
        parseLine(line, currentSection);
    }
    
    file.close();

    // Load saved playlists for default rotation
    FsFile playlistFile;
    if (sd.exists("/playlists_selected.json")) {
        playlistFile = sd.open("/playlists_selected.json", FILE_OPEN_READ);
    }
    if (playlistFile) {
        DynamicJsonDocument doc(4096);
        DeserializationError error = deserializeJson(doc, playlistFile);
        if (!error) {
            JsonArray playlistsArray = doc["playlists"].as<JsonArray>();
            if (playlistsArray.size() > 0) {
                // Playlists are handled by GifEngine dynamically
            }
        }
        playlistFile.close();
    }

    return true;
}

String ConfigLoader::serializeToString() const {
    String out = "";

    out += "[WIFI]\n";
    out += "SSID=" + wifi.ssid + "\n";
    out += "PASSWORD=" + wifi.password + "\n";
    out += "HOSTNAME=" + wifi.hostname + "\n\n";

    out += "[MATRIX]\n";
    out += "WIDTH=" + String(matrix.width) + "\n";
    out += "HEIGHT=" + String(matrix.height) + "\n";
    out += "PANEL_TYPE=" + matrix.panelType + "\n";
    out += "CHAIN=" + String(matrix.chainLength) + "\n";
    out += "BRIGHTNESS_LIMIT=" + String(matrix.powerLimitPercent) + "\n";
    out += "PWM_BITS=" + String(matrix.pwmBits) + "\n";
    out += "FORCE_SINGLE_BUFFER=" + String(matrix.forceSingleBuffer ? "true" : "false") + "\n";
    out += "RGB_SEQUENCE=" + matrix.rgbSequence + "\n";
    out += "LIMIT_REFRESH_RATE_HZ=" + String(matrix.limitRefreshRateHz) + "\n";
    out += "DRIVER_CHIP=" + matrix.driverChip + "\n\n";

    out += "[MQTT]\n";
    out += "ENABLED=" + String(mqtt.enabled ? "true" : "false") + "\n";
    out += "BROKER=" + mqtt.broker + "\n";
    out += "PORT=" + String(mqtt.port) + "\n";
    out += "USER=" + mqtt.user + "\n";
    out += "PASS=" + mqtt.pass + "\n";
    out += "DEVICE_NAME=" + mqtt.deviceName + "\n";
    out += "TOPIC_BATOCERA=" + mqtt.topic_batocera + "\n";
    out += "TOPIC_RECALBOX=" + mqtt.topic_recalbox + "\n\n";

    out += "[TIME]\n";
    out += "NTP_SERVER=" + time.ntpServer + "\n";
    out += "TIMEZONE=" + time.timezone + "\n";
    out += "FORMAT_24H=" + String(time.format24h ? "true" : "false") + "\n";
    out += "CLOCK_FONT=" + String(time.clock_font) + "\n";
    out += "CLOCK_SIZE=" + String(time.clock_size) + "\n";
    out += "CLOCK_THEME=" + String(time.clock_theme) + "\n";
    out += "CLOCK_OFFSET_X=" + String(time.clock_offset_x) + "\n";
    out += "CLOCK_OFFSET_Y=" + String(time.clock_offset_y) + "\n";
    out += "CLOCK_COLOR_1=" + time.clock_color_1 + "\n";
    out += "CLOCK_COLOR_2=" + time.clock_color_2 + "\n";
    out += "CLOCK_FONT_PATH=" + time.clock_font_path + "\n\n";

    out += "[IDLE]\n";
    out += "ROTATION=" + idle.rotation + "\n";
    out += "CLOCK_DURATION_SEC=" + String(idle.clock_duration_sec) + "\n";
    out += "DATE_DURATION_SEC=" + String(idle.date_duration_sec) + "\n";
    out += "WEATHER_DURATION_SEC=" + String(idle.weather_duration_sec) + "\n";
    out += "TEMP_DURATION_SEC=" + String(idle.temp_duration_sec) + "\n";
    out += "DECIBEL_DURATION_SEC=" + String(idle.decibel_duration_sec) + "\n";
    out += "GIFS_COUNT=" + String(idle.gifs_count) + "\n";
    out += "FIGHTER_ENABLED=" + String(idle.fighter_enabled ? "true" : "false") + "\n";
    out += "FIGHTER_INTERVAL_SEC=" + String(idle.fighter_interval_sec) + "\n\n";

    out += "[ENVIRONMENT]\n";
    out += "UNIT=" + env.unit + "\n";
    out += "TEMP_OFFSET=" + String(env.temp_offset, 2) + "\n\n";

    out += "[AUDIO]\n";
    out += "VISUALIZER_ENABLED=" + String(audio.visualizer_enabled ? "true" : "false") + "\n";
    out += "VISUALIZER_MODE=" + audio.visualizer_mode + "\n";
    out += "MIC_GAIN=" + String(audio.mic_gain, 2) + "\n";
    out += "DB_CALIBRATION=" + String(audio.db_calibration, 2) + "\n\n";

    out += "[DATE]\n";
    out += "THEME=" + String(dateSettings.theme) + "\n";
    out += "BACKGROUND_SPRITE=" + dateSettings.background_sprite + "\n";
    out += "FORMAT=" + dateSettings.format + "\n";
    out += "DATE_FONT=" + String(dateSettings.date_font) + "\n";
    out += "DATE_SIZE=" + String(dateSettings.date_size) + "\n";
    out += "DATE_OFFSET_X=" + String(dateSettings.date_offset_x) + "\n";
    out += "DATE_OFFSET_Y=" + String(dateSettings.date_offset_y) + "\n";
    out += "DATE_COLOR_1=" + dateSettings.date_color_1 + "\n";
    out += "DATE_COLOR_2=" + dateSettings.date_color_2 + "\n";
    out += "DATE_FONT_PATH=" + dateSettings.date_font_path + "\n\n";

    out += "[WEATHER]\n";
    out += "API_KEY=" + weather.api_key + "\n";
    out += "CITY=" + weather.city + "\n";
    out += "LANG=" + weather.lang + "\n";
    out += "WEATHER_OFFSET_X=" + String(weather.weather_offset_x) + "\n";
    out += "WEATHER_OFFSET_Y=" + String(weather.weather_offset_y) + "\n\n";

    out += "[STANDBY]\n";
    out += "NIGHT_MODE_ENABLED=" + String(standby.night_mode_enabled ? "true" : "false") + "\n";
    out += "TURN_OFF_AT=" + standby.turn_off_at + "\n";
    out += "WAKE_UP_AT=" + standby.wake_up_at + "\n";
    out += "NIGHT_BRIGHTNESS=" + String(standby.night_brightness) + "\n\n";

    out += "[FONTS]\n";
    out += "CUSTOM_FONT_PATH=" + fonts.custom_font_path + "\n\n";

    out += "[CRYPTO]\n";
    out += "ENABLED=" + String(crypto.enabled ? "true" : "false") + "\n";
    out += "SYMBOLS=" + crypto.symbols + "\n";
    out += "DURATION_SEC=" + String(crypto.duration_sec) + "\n";
    out += "CACHE_TTL_MIN=" + String(crypto.cache_ttl_min) + "\n";
    out += "CURRENCY=" + crypto.currency + "\n\n";

    out += "[STOCK]\n";
    out += "ENABLED=" + String(stock.enabled ? "true" : "false") + "\n";
    out += "SYMBOLS=" + stock.symbols + "\n";
    out += "DURATION_SEC=" + String(stock.duration_sec) + "\n";
    out += "CACHE_TTL_MIN=" + String(stock.cache_ttl_min) + "\n\n";

    return out;
}

bool ConfigLoader::saveToSD(const char* filepath) {
    if (sd.exists(filepath)) {
        sd.remove(filepath);
    }

    FsFile file = sd.open(filepath, FILE_OPEN_WRITE);
    if (!file) {
        LOGE("ConfigLoader", "Failed to open %s for writing", filepath);
        return false;
    }

    String data = serializeToString();
    file.print(data);
    file.close();
    return true;
}