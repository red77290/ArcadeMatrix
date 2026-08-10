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

    idle.rotation = "clock,date,weather,gifs";
    idle.clock_duration_sec = 60;
    idle.date_duration_sec = 10;
    idle.weather_duration_sec = 15;
    idle.gifs_count = 3;
    idle.fighter_enabled = true;
    idle.fighter_interval_sec = 10;
    
    idle.mode = "gifs_then_clock";
    idle.gifs_before_clock = 10;

    weather.api_key = "";
    weather.city = "";
    weather.lang = "";
    weather.weather_offset_x = 0;
    weather.weather_offset_y = 0;

    standby.night_mode_enabled = false;
    standby.turn_off_at = "23:00";
    standby.wake_up_at = "07:00";
    standby.night_brightness = 10;
    standby.matrix_power = true;

    dateSettings.theme = 0;
    dateSettings.background_sprite = "";
    dateSettings.format = "DD/MM";
    dateSettings.date_font = 0;
    dateSettings.date_size = 1;
    dateSettings.date_offset_x = 0;
    dateSettings.date_offset_y = 0;
    dateSettings.date_color_1 = "#ffffff";
    dateSettings.date_color_2 = "#ffffff";
    dateSettings.date_font_path = "";

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
    int commentPos = line.indexOf('#');
    if (commentPos == -1) commentPos = line.indexOf(';');
    if (commentPos != -1) line = line.substring(0, commentPos);
    line.trim();
    
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
        else if (key == "COLOR_DEPTH") matrix.colorDepth = value.toInt();
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
        if (key == "NTP_SERVER") time.ntpServer = value;
        else if (key == "TIMEZONE") time.timezone = value;
        else if (key == "FORMAT_24H") time.format24h = (value == "true" || value == "1");
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
        else if (key == "GIFS_COUNT") idle.gifs_count = value.toInt();
        else if (key == "FIGHTER_ENABLED") idle.fighter_enabled = (value == "true" || value == "1");
        else if (key == "FIGHTER_INTERVAL_SEC") idle.fighter_interval_sec = value.toInt();
        
        else if (key == "MODE") idle.mode = value; // Legacy
        else if (key == "GIFS_BEFORE_CLOCK") idle.gifs_before_clock = value.toInt(); // Legacy
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
            std::vector<String> paths;
            for (JsonVariant v : playlistsArray) {
                paths.push_back(v.as<String>());
            }
            extern GifEngine gifEngine;
            gifEngine.setDefaultPlaylists(paths);
        }
        playlistFile.close();
    }

    return true;
}

bool ConfigLoader::saveToSD(const char* filepath) {
    // The SD card on this project shares timing-sensitive SPI access with a lot of other
    // activity (HUB75 DMA output, GIF/font reads, ...). Transient "Wait Failed"/"Select Failed"
    // glitches from the SD driver are common and usually resolve themselves a few milliseconds
    // later - so a single failed SD.open()/write here shouldn't silently discard the user's
    // settings. Retry a few times before giving up.
    const int MAX_ATTEMPTS = 3;
    for (int attempt = 1; attempt <= MAX_ATTEMPTS; attempt++) {
        if (writeConfigFile(filepath)) {
            return true;
        }
        LOGW("ConfigLoader", "saveToSD: attempt %d/%d failed, retrying...", attempt, MAX_ATTEMPTS);
        delay(50);
    }
    LOGE("ConfigLoader", "saveToSD: all attempts failed - settings were NOT persisted to SD!");
    return false;
}

String ConfigLoader::serializeToString() {
    String out = "";
    out += "# ==========================================\n";
    out += "# ArcadeMatrix - Configuration File\n";
    out += "# ==========================================\n\n";

    out += "[wifi]\n";
    out += "ssid=" + wifi.ssid + "\n";
    out += "password=" + wifi.password + "\n";
    out += "hostname=" + wifi.hostname + "\n\n";

    out += "[time]\n";
    out += "ntp_server=" + time.ntpServer + "\n";
    out += "timezone=" + time.timezone + "\n";
    out += "format_24h=" + String(time.format24h ? "1" : "0") + "\n";
    out += "clock_font=" + String(time.clock_font) + "\n";
    out += "clock_size=" + String(time.clock_size) + "\n";
    out += "clock_offset_x=" + String(time.clock_offset_x) + "\n";
    out += "clock_offset_y=" + String(time.clock_offset_y) + "\n";
    out += "clock_theme=" + String(time.clock_theme) + "\n";
    out += "clock_color_1=" + time.clock_color_1 + "\n";
    out += "clock_color_2=" + time.clock_color_2 + "\n";
    out += "clock_font_path=" + time.clock_font_path + "\n\n";

    out += "[matrix]\n";
    out += "width=" + String(matrix.width) + "\n";
    out += "height=" + String(matrix.height) + "\n";
    out += "panel_type=" + matrix.panelType + "\n";
    out += "chain=" + String(matrix.chainLength) + "\n";
    out += "brightness_limit=" + String(matrix.powerLimitPercent) + "\n";
    out += "color_depth=" + String(matrix.colorDepth) + "\n";
    out += "force_single_buffer=" + String(matrix.forceSingleBuffer ? "1" : "0") + "\n";
    out += "rgb_sequence=" + matrix.rgbSequence + "\n";
    out += "limit_refresh_rate_hz=" + String(matrix.limitRefreshRateHz) + "\n";
    out += "driver_chip=" + matrix.driverChip + "\n";
    out += "clk_phase=" + String(matrix.clkPhase ? "1" : "0") + "\n";
    out += "latch_blanking=" + String(matrix.latchBlanking) + "\n";
    out += "row_address_mode=" + String(matrix.rowAddressMode) + "\n\n";

    out += "[mqtt]\n";
    out += "enabled=" + String(mqtt.enabled ? "1" : "0") + "\n";
    out += "broker=" + mqtt.broker + "\n";
    out += "port=" + String(mqtt.port) + "\n";
    out += "user=" + mqtt.user + "\n";
    out += "pass=" + mqtt.pass + "\n";
    out += "topic_batocera=" + mqtt.topic_batocera + "\n";
    out += "topic_recalbox=" + mqtt.topic_recalbox + "\n";
    out += "device_name=" + mqtt.deviceName + "\n\n";

    out += "[idle]\n";
    out += "rotation=" + idle.rotation + "\n";
    out += "clock_duration_sec=" + String(idle.clock_duration_sec) + "\n";
    out += "date_duration_sec=" + String(idle.date_duration_sec) + "\n";
    out += "weather_duration_sec=" + String(idle.weather_duration_sec) + "\n";
    out += "gifs_count=" + String(idle.gifs_count) + "\n";
    out += "fighter_enabled=" + String(idle.fighter_enabled ? "true" : "false") + "\n";
    out += "fighter_interval_sec=" + String(idle.fighter_interval_sec) + "\n";
    out += "mode=" + idle.mode + "\n";
    out += "gifs_before_clock=" + String(idle.gifs_before_clock) + "\n\n";

    out += "[weather]\n";
    out += "api_key=" + weather.api_key + "\n";
    out += "city=" + weather.city + "\n";
    out += "lang=" + weather.lang + "\n";
    out += "weather_offset_x=" + String(weather.weather_offset_x) + "\n";
    out += "weather_offset_y=" + String(weather.weather_offset_y) + "\n\n";

    out += "[standby]\n";
    out += "night_mode_enabled=" + String(standby.night_mode_enabled ? "1" : "0") + "\n";
    out += "turn_off_at=" + standby.turn_off_at + "\n";
    out += "wake_up_at=" + standby.wake_up_at + "\n";
    out += "night_brightness=" + String(standby.night_brightness) + "\n\n";

    out += "[date]\n";
    out += "theme=" + String(dateSettings.theme) + "\n";
    out += "background_sprite=" + dateSettings.background_sprite + "\n";
    out += "format=" + dateSettings.format + "\n";
    out += "date_font=" + String(dateSettings.date_font) + "\n";
    out += "date_size=" + String(dateSettings.date_size) + "\n";
    out += "date_offset_x=" + String(dateSettings.date_offset_x) + "\n";
    out += "date_offset_y=" + String(dateSettings.date_offset_y) + "\n";
    out += "date_color_1=" + dateSettings.date_color_1 + "\n";
    out += "date_color_2=" + dateSettings.date_color_2 + "\n";
    out += "date_font_path=" + dateSettings.date_font_path + "\n\n";

    out += "[fonts]\n";
    out += "custom_font_path=" + fonts.custom_font_path + "\n\n";

    out += "[crypto]\n";
    out += "enabled=" + String(crypto.enabled ? "1" : "0") + "\n";
    out += "symbols=" + crypto.symbols + "\n";
    out += "duration_sec=" + String(crypto.duration_sec) + "\n";
    out += "cache_ttl_min=" + String(crypto.cache_ttl_min) + "\n";
    out += "currency=" + crypto.currency + "\n\n";

    out += "[stock]\n";
    out += "enabled=" + String(stock.enabled ? "1" : "0") + "\n";
    out += "symbols=" + stock.symbols + "\n";
    out += "duration_sec=" + String(stock.duration_sec) + "\n";
    out += "cache_ttl_min=" + String(stock.cache_ttl_min) + "\n\n";

    return out;
}

bool ConfigLoader::writeConfigFile(const char* filepath) {
    // Some ESP32 cores append to the file if it exists, so we must remove it first to overwrite cleanly
    if (sd.exists(filepath)) {
        sd.remove(filepath);
    }
    
    FsFile file = sd.open(filepath, FILE_OPEN_WRITE);
    if (!file) {
        LOGE("ConfigLoader", "Failed to open config file for writing");
        return false;
    }
    
    String content = serializeToString();
    file.print(content);
    file.close();

    // Sanity check: an SD glitch mid-write can leave a truncated/empty file even though close()
    // didn't report an error. A full conf.ini is always several hundred bytes, so treat anything
    // implausibly small as a failed write (triggers a retry in saveToSD()).
    FsFile check = sd.open(filepath, FILE_OPEN_READ);
    if (!check) {
        LOGE("ConfigLoader", "conf.ini could not be reopened after writing - treating as failed write.");
        return false;
    }
    size_t writtenSize = check.size();
    check.close();
    if (writtenSize < 100) {
        LOGE("ConfigLoader", "conf.ini looks truncated after writing (%u bytes) - treating as failed write.", (unsigned)writtenSize);
        return false;
    }

    return true;
}
