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
#ifdef HARDWARE_PROFILE_WAVESHARE_S3
    matrix.forceSingleBuffer = false;
#else
    // On standard ESP32 without PSRAM, 128x32 double buffering uses too much DMA memory.
    // The library silently drops color depth to fit it, resulting in wrong/neon colors.
    // Defaulting to single buffer fixes this and preserves true colors.
    matrix.forceSingleBuffer = true; 
#endif
    matrix.colorDepth = 8;
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
        else if (key == "POWER_LIMIT_PERCENT" || key == "BRIGHTNESS_LIMIT") matrix.powerLimitPercent = value.toInt();
        else if (key == "COLOR_DEPTH" || key == "PWM_BITS") matrix.colorDepth = value.toInt();
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
    out += "COLOR_DEPTH=" + String(matrix.colorDepth) + "\n";
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
String ConfigLoader::serializeToJson() const {
    DynamicJsonDocument doc(16384);

    JsonObject matrixObj = doc.createNestedObject("MATRIX");
    matrixObj["width"] = matrix.width;
    matrixObj["height"] = matrix.height;
    matrixObj["panelType"] = matrix.panelType;
    matrixObj["chainLength"] = matrix.chainLength;
    matrixObj["powerLimitPercent"] = matrix.powerLimitPercent;
    matrixObj["colorDepth"] = matrix.colorDepth;
    matrixObj["forceSingleBuffer"] = matrix.forceSingleBuffer;
    matrixObj["rgbSequence"] = matrix.rgbSequence;
    matrixObj["limitRefreshRateHz"] = matrix.limitRefreshRateHz;
    matrixObj["driverChip"] = matrix.driverChip;
    matrixObj["clkPhase"] = matrix.clkPhase;
    matrixObj["latchBlanking"] = matrix.latchBlanking;
    matrixObj["rowAddressMode"] = matrix.rowAddressMode;

    JsonObject wifiObj = doc.createNestedObject("WIFI");
    wifiObj["ssid"] = wifi.ssid;
    wifiObj["password"] = wifi.password;
    wifiObj["hostname"] = wifi.hostname;

    JsonObject mqttObj = doc.createNestedObject("MQTT");
    mqttObj["enabled"] = mqtt.enabled;
    mqttObj["broker"] = mqtt.broker;
    mqttObj["port"] = mqtt.port;
    mqttObj["user"] = mqtt.user;
    mqttObj["pass"] = mqtt.pass;
    mqttObj["deviceName"] = mqtt.deviceName;
    mqttObj["topic_batocera"] = mqtt.topic_batocera;
    mqttObj["topic_recalbox"] = mqtt.topic_recalbox;

    JsonObject timeObj = doc.createNestedObject("TIME");
    timeObj["ntpServer"] = time.ntpServer;
    timeObj["timezone"] = time.timezone;
    timeObj["format24h"] = time.format24h;
    timeObj["clock_font"] = time.clock_font;
    timeObj["clock_size"] = time.clock_size;
    timeObj["clock_theme"] = time.clock_theme;
    timeObj["clock_offset_x"] = time.clock_offset_x;
    timeObj["clock_offset_y"] = time.clock_offset_y;
    timeObj["clock_color_1"] = time.clock_color_1;
    timeObj["clock_color_2"] = time.clock_color_2;
    timeObj["clock_font_path"] = time.clock_font_path;

    JsonObject dateSettingsObj = doc.createNestedObject("DATE");
    dateSettingsObj["theme"] = dateSettings.theme;
    dateSettingsObj["format"] = dateSettings.format;
    dateSettingsObj["date_font"] = dateSettings.date_font;
    dateSettingsObj["date_size"] = dateSettings.date_size;
    dateSettingsObj["date_offset_x"] = dateSettings.date_offset_x;
    dateSettingsObj["date_offset_y"] = dateSettings.date_offset_y;
    dateSettingsObj["background_sprite"] = dateSettings.background_sprite;
    dateSettingsObj["date_color_1"] = dateSettings.date_color_1;
    dateSettingsObj["date_color_2"] = dateSettings.date_color_2;
    dateSettingsObj["date_font_path"] = dateSettings.date_font_path;

    JsonObject idleObj = doc.createNestedObject("IDLE");
    idleObj["rotation"] = idle.rotation;
    idleObj["clock_duration_sec"] = idle.clock_duration_sec;
    idleObj["date_duration_sec"] = idle.date_duration_sec;
    idleObj["weather_duration_sec"] = idle.weather_duration_sec;
    idleObj["temp_duration_sec"] = idle.temp_duration_sec;
    idleObj["decibel_duration_sec"] = idle.decibel_duration_sec;
    idleObj["gifs_count"] = idle.gifs_count;
    idleObj["fighter_enabled"] = idle.fighter_enabled;
    idleObj["fighter_interval_sec"] = idle.fighter_interval_sec;

    JsonObject envObj = doc.createNestedObject("ENVIRONMENT");
    envObj["unit"] = env.unit;
    envObj["temp_offset"] = env.temp_offset;

    JsonObject audioObj = doc.createNestedObject("AUDIO");
    audioObj["visualizer_enabled"] = audio.visualizer_enabled;
    audioObj["visualizer_mode"] = audio.visualizer_mode;
    audioObj["mic_gain"] = audio.mic_gain;
    audioObj["db_calibration"] = audio.db_calibration;

    JsonObject weatherObj = doc.createNestedObject("WEATHER");
    weatherObj["api_key"] = weather.api_key;
    weatherObj["city"] = weather.city;
    weatherObj["lang"] = weather.lang;
    weatherObj["weather_offset_x"] = weather.weather_offset_x;
    weatherObj["weather_offset_y"] = weather.weather_offset_y;

    JsonObject standbyObj = doc.createNestedObject("STANDBY");
    standbyObj["night_mode_enabled"] = standby.night_mode_enabled;
    standbyObj["turn_off_at"] = standby.turn_off_at;
    standbyObj["wake_up_at"] = standby.wake_up_at;
    standbyObj["night_brightness"] = standby.night_brightness;

    JsonObject fontsObj = doc.createNestedObject("FONTS");
    fontsObj["custom_font_path"] = fonts.custom_font_path;

    JsonObject cryptoObj = doc.createNestedObject("CRYPTO");
    cryptoObj["enabled"] = crypto.enabled;
    cryptoObj["symbols"] = crypto.symbols;
    cryptoObj["duration_sec"] = crypto.duration_sec;
    cryptoObj["cache_ttl_min"] = crypto.cache_ttl_min;
    cryptoObj["currency"] = crypto.currency;

    JsonObject stockObj = doc.createNestedObject("STOCK");
    stockObj["enabled"] = stock.enabled;
    stockObj["symbols"] = stock.symbols;
    stockObj["duration_sec"] = stock.duration_sec;
    stockObj["cache_ttl_min"] = stock.cache_ttl_min;

    JsonArray instancesArr = doc.createNestedArray("INSTANCES");
    for (const auto& inst : instances) {
        JsonObject iObj = instancesArr.createNestedObject();
        iObj["instance_id"] = inst.instance_id;
        iObj["engine_id"] = inst.engine_id;
    }

    JsonArray rotArr = doc.createNestedArray("ROTATION_LIST");
    for (const auto& rot : rotation) {
        JsonObject rObj = rotArr.createNestedObject();
        rObj["instance_id"] = rot.instance_id;
        rObj["duration_sec"] = rot.duration_sec;
    }

    String out;
    serializeJson(doc, out);
    return out;
}


bool ConfigLoader::parseFromJson(const char* jsonContent) {
    DynamicJsonDocument doc(16384);
    DeserializationError error = deserializeJson(doc, jsonContent);
    if (error) {
        LOGE("ConfigLoader", "JSON parse failed: %s", error.c_str());
        return false;
    }

    if (doc.containsKey("MATRIX")) {
        JsonObject matrixObj = doc["MATRIX"];
        if (matrixObj.containsKey("width")) matrix.width = matrixObj["width"].as<int>();
        if (matrixObj.containsKey("height")) matrix.height = matrixObj["height"].as<int>();
        if (matrixObj.containsKey("panelType")) matrix.panelType = matrixObj["panelType"].as<const char*>();
        if (matrixObj.containsKey("chainLength")) matrix.chainLength = matrixObj["chainLength"].as<int>();
        if (matrixObj.containsKey("powerLimitPercent")) matrix.powerLimitPercent = matrixObj["powerLimitPercent"].as<int>();
        if (matrixObj.containsKey("colorDepth")) matrix.colorDepth = matrixObj["colorDepth"].as<int>();
        if (matrixObj.containsKey("forceSingleBuffer")) matrix.forceSingleBuffer = matrixObj["forceSingleBuffer"].as<bool>();
        if (matrixObj.containsKey("rgbSequence")) matrix.rgbSequence = matrixObj["rgbSequence"].as<const char*>();
        if (matrixObj.containsKey("limitRefreshRateHz")) matrix.limitRefreshRateHz = matrixObj["limitRefreshRateHz"].as<int>();
        if (matrixObj.containsKey("driverChip")) matrix.driverChip = matrixObj["driverChip"].as<const char*>();
        if (matrixObj.containsKey("clkPhase")) matrix.clkPhase = matrixObj["clkPhase"].as<bool>();
        if (matrixObj.containsKey("latchBlanking")) matrix.latchBlanking = matrixObj["latchBlanking"].as<int>();
        if (matrixObj.containsKey("rowAddressMode")) matrix.rowAddressMode = matrixObj["rowAddressMode"].as<int>();
    }

    if (doc.containsKey("WIFI")) {
        JsonObject wifiObj = doc["WIFI"];
        if (wifiObj.containsKey("ssid")) wifi.ssid = wifiObj["ssid"].as<const char*>();
        if (wifiObj.containsKey("password")) wifi.password = wifiObj["password"].as<const char*>();
        if (wifiObj.containsKey("hostname")) wifi.hostname = wifiObj["hostname"].as<const char*>();
    }

    if (doc.containsKey("MQTT")) {
        JsonObject mqttObj = doc["MQTT"];
        if (mqttObj.containsKey("enabled")) mqtt.enabled = mqttObj["enabled"].as<bool>();
        if (mqttObj.containsKey("broker")) mqtt.broker = mqttObj["broker"].as<const char*>();
        if (mqttObj.containsKey("port")) mqtt.port = mqttObj["port"].as<int>();
        if (mqttObj.containsKey("user")) mqtt.user = mqttObj["user"].as<const char*>();
        if (mqttObj.containsKey("pass")) mqtt.pass = mqttObj["pass"].as<const char*>();
        if (mqttObj.containsKey("deviceName")) mqtt.deviceName = mqttObj["deviceName"].as<const char*>();
        if (mqttObj.containsKey("topic_batocera")) mqtt.topic_batocera = mqttObj["topic_batocera"].as<const char*>();
        if (mqttObj.containsKey("topic_recalbox")) mqtt.topic_recalbox = mqttObj["topic_recalbox"].as<const char*>();
    }

    if (doc.containsKey("TIME")) {
        JsonObject timeObj = doc["TIME"];
        if (timeObj.containsKey("ntpServer")) time.ntpServer = timeObj["ntpServer"].as<const char*>();
        if (timeObj.containsKey("timezone")) time.timezone = timeObj["timezone"].as<const char*>();
        if (timeObj.containsKey("format24h")) time.format24h = timeObj["format24h"].as<bool>();
        if (timeObj.containsKey("clock_font")) time.clock_font = timeObj["clock_font"].as<int>();
        if (timeObj.containsKey("clock_size")) time.clock_size = timeObj["clock_size"].as<int>();
        if (timeObj.containsKey("clock_theme")) time.clock_theme = timeObj["clock_theme"].as<int>();
        if (timeObj.containsKey("clock_offset_x")) time.clock_offset_x = timeObj["clock_offset_x"].as<int>();
        if (timeObj.containsKey("clock_offset_y")) time.clock_offset_y = timeObj["clock_offset_y"].as<int>();
        if (timeObj.containsKey("clock_color_1")) time.clock_color_1 = timeObj["clock_color_1"].as<const char*>();
        if (timeObj.containsKey("clock_color_2")) time.clock_color_2 = timeObj["clock_color_2"].as<const char*>();
        if (timeObj.containsKey("clock_font_path")) time.clock_font_path = timeObj["clock_font_path"].as<const char*>();
    }

    if (doc.containsKey("DATE")) {
        JsonObject dateSettingsObj = doc["DATE"];
        if (dateSettingsObj.containsKey("theme")) dateSettings.theme = dateSettingsObj["theme"].as<int>();
        if (dateSettingsObj.containsKey("format")) dateSettings.format = dateSettingsObj["format"].as<const char*>();
        if (dateSettingsObj.containsKey("date_font")) dateSettings.date_font = dateSettingsObj["date_font"].as<int>();
        if (dateSettingsObj.containsKey("date_size")) dateSettings.date_size = dateSettingsObj["date_size"].as<int>();
        if (dateSettingsObj.containsKey("date_offset_x")) dateSettings.date_offset_x = dateSettingsObj["date_offset_x"].as<int>();
        if (dateSettingsObj.containsKey("date_offset_y")) dateSettings.date_offset_y = dateSettingsObj["date_offset_y"].as<int>();
        if (dateSettingsObj.containsKey("background_sprite")) dateSettings.background_sprite = dateSettingsObj["background_sprite"].as<const char*>();
        if (dateSettingsObj.containsKey("date_color_1")) dateSettings.date_color_1 = dateSettingsObj["date_color_1"].as<const char*>();
        if (dateSettingsObj.containsKey("date_color_2")) dateSettings.date_color_2 = dateSettingsObj["date_color_2"].as<const char*>();
        if (dateSettingsObj.containsKey("date_font_path")) dateSettings.date_font_path = dateSettingsObj["date_font_path"].as<const char*>();
    }

    if (doc.containsKey("IDLE")) {
        JsonObject idleObj = doc["IDLE"];
        if (idleObj.containsKey("rotation")) idle.rotation = idleObj["rotation"].as<const char*>();
        if (idleObj.containsKey("clock_duration_sec")) idle.clock_duration_sec = idleObj["clock_duration_sec"].as<int>();
        if (idleObj.containsKey("date_duration_sec")) idle.date_duration_sec = idleObj["date_duration_sec"].as<int>();
        if (idleObj.containsKey("weather_duration_sec")) idle.weather_duration_sec = idleObj["weather_duration_sec"].as<int>();
        if (idleObj.containsKey("temp_duration_sec")) idle.temp_duration_sec = idleObj["temp_duration_sec"].as<int>();
        if (idleObj.containsKey("decibel_duration_sec")) idle.decibel_duration_sec = idleObj["decibel_duration_sec"].as<int>();
        if (idleObj.containsKey("gifs_count")) idle.gifs_count = idleObj["gifs_count"].as<int>();
        if (idleObj.containsKey("fighter_enabled")) idle.fighter_enabled = idleObj["fighter_enabled"].as<bool>();
        if (idleObj.containsKey("fighter_interval_sec")) idle.fighter_interval_sec = idleObj["fighter_interval_sec"].as<int>();
    }

    if (doc.containsKey("ENVIRONMENT")) {
        JsonObject envObj = doc["ENVIRONMENT"];
        if (envObj.containsKey("unit")) env.unit = envObj["unit"].as<const char*>();
        if (envObj.containsKey("temp_offset")) env.temp_offset = envObj["temp_offset"].as<float>();
    }

    if (doc.containsKey("AUDIO")) {
        JsonObject audioObj = doc["AUDIO"];
        if (audioObj.containsKey("visualizer_enabled")) audio.visualizer_enabled = audioObj["visualizer_enabled"].as<bool>();
        if (audioObj.containsKey("visualizer_mode")) audio.visualizer_mode = audioObj["visualizer_mode"].as<const char*>();
        if (audioObj.containsKey("mic_gain")) audio.mic_gain = audioObj["mic_gain"].as<float>();
        if (audioObj.containsKey("db_calibration")) audio.db_calibration = audioObj["db_calibration"].as<float>();
    }

    if (doc.containsKey("WEATHER")) {
        JsonObject weatherObj = doc["WEATHER"];
        if (weatherObj.containsKey("api_key")) weather.api_key = weatherObj["api_key"].as<const char*>();
        if (weatherObj.containsKey("city")) weather.city = weatherObj["city"].as<const char*>();
        if (weatherObj.containsKey("lang")) weather.lang = weatherObj["lang"].as<const char*>();
        if (weatherObj.containsKey("weather_offset_x")) weather.weather_offset_x = weatherObj["weather_offset_x"].as<int>();
        if (weatherObj.containsKey("weather_offset_y")) weather.weather_offset_y = weatherObj["weather_offset_y"].as<int>();
    }

    if (doc.containsKey("STANDBY")) {
        JsonObject standbyObj = doc["STANDBY"];
        if (standbyObj.containsKey("night_mode_enabled")) standby.night_mode_enabled = standbyObj["night_mode_enabled"].as<bool>();
        if (standbyObj.containsKey("turn_off_at")) standby.turn_off_at = standbyObj["turn_off_at"].as<const char*>();
        if (standbyObj.containsKey("wake_up_at")) standby.wake_up_at = standbyObj["wake_up_at"].as<const char*>();
        if (standbyObj.containsKey("night_brightness")) standby.night_brightness = standbyObj["night_brightness"].as<int>();
    }

    if (doc.containsKey("FONTS")) {
        JsonObject fontsObj = doc["FONTS"];
        if (fontsObj.containsKey("custom_font_path")) fonts.custom_font_path = fontsObj["custom_font_path"].as<const char*>();
    }

    if (doc.containsKey("CRYPTO")) {
        JsonObject cryptoObj = doc["CRYPTO"];
        if (cryptoObj.containsKey("enabled")) crypto.enabled = cryptoObj["enabled"].as<bool>();
        if (cryptoObj.containsKey("symbols")) crypto.symbols = cryptoObj["symbols"].as<const char*>();
        if (cryptoObj.containsKey("duration_sec")) crypto.duration_sec = cryptoObj["duration_sec"].as<int>();
        if (cryptoObj.containsKey("cache_ttl_min")) crypto.cache_ttl_min = cryptoObj["cache_ttl_min"].as<int>();
        if (cryptoObj.containsKey("currency")) crypto.currency = cryptoObj["currency"].as<const char*>();
    }

    if (doc.containsKey("STOCK")) {
        JsonObject stockObj = doc["STOCK"];
        if (stockObj.containsKey("enabled")) stock.enabled = stockObj["enabled"].as<bool>();
        if (stockObj.containsKey("symbols")) stock.symbols = stockObj["symbols"].as<const char*>();
        if (stockObj.containsKey("duration_sec")) stock.duration_sec = stockObj["duration_sec"].as<int>();
        if (stockObj.containsKey("cache_ttl_min")) stock.cache_ttl_min = stockObj["cache_ttl_min"].as<int>();
    }

    if (doc.containsKey("INSTANCES")) {
        instances.clear();
        JsonArray arr = doc["INSTANCES"].as<JsonArray>();
        for (JsonVariant v : arr) {
            JsonObject o = v.as<JsonObject>();
            EngineInstance i;
            i.instance_id = o["instance_id"].as<const char*>();
            i.engine_id = o["engine_id"].as<const char*>();
            instances.push_back(i);
        }
    }

    if (doc.containsKey("ROTATION_LIST")) {
        rotation.clear();
        JsonArray arr = doc["ROTATION_LIST"].as<JsonArray>();
        for (JsonVariant v : arr) {
            JsonObject o = v.as<JsonObject>();
            RotationEntry r;
            r.instance_id = o["instance_id"].as<const char*>();
            r.duration_sec = o["duration_sec"].as<int>();
            rotation.push_back(r);
        }
    }

    return true;
}


void ConfigLoader::migrateLegacyRotation() {
    if (!instances.empty() || !rotation.empty()) return;
    
    // Add default instances for engines used in conf.ini
    instances.push_back({"default_clock", "clock"});
    instances.push_back({"default_date", "date"});
    instances.push_back({"default_weather", "weather"});
    instances.push_back({"gifs", "gifs"});
    instances.push_back({"default_temp", "temp"});
    instances.push_back({"default_decibel", "decibel"});
    instances.push_back({"default_network", "network"});
    
    String legacy = idle.rotation;
    int start = 0;
    while (start < legacy.length()) {
        int end = legacy.indexOf(',', start);
        if (end == -1) end = legacy.length();
        String t = legacy.substring(start, end);
        t.trim();
        if (t.length() > 0) {
            RotationEntry r;
            if (t == "clock") { r.instance_id = "default_clock"; r.duration_sec = idle.clock_duration_sec; }
            else if (t == "date") { r.instance_id = "default_date"; r.duration_sec = idle.date_duration_sec; }
            else if (t == "weather") { r.instance_id = "default_weather"; r.duration_sec = idle.weather_duration_sec; }
            else if (t == "temp") { r.instance_id = "default_temp"; r.duration_sec = idle.temp_duration_sec; }
            else if (t == "decibel") { r.instance_id = "default_decibel"; r.duration_sec = idle.decibel_duration_sec; }
            else if (t == "gifs") { r.instance_id = "gifs"; r.duration_sec = 0; } // Gifs rely on loops/counts normally
            else if (t == "network") { r.instance_id = "default_network"; r.duration_sec = 10; }
            else { r.instance_id = t; r.duration_sec = 10; }
            rotation.push_back(r);
        }
        start = end + 1;
    }
}
