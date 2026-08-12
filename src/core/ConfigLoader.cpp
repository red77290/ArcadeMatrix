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

bool ConfigLoader::saveToSD(const char* filepath) {
    if (sd.exists(filepath)) {
        sd.remove(filepath);
    }

    FsFile file = sd.open(filepath, FILE_OPEN_WRITE);
    if (!file) {
        LOGE("ConfigLoader", "Failed to open %s for writing", filepath);
        return false;
    }

    file.println("[WIFI]");
    file.printf("SSID=%s\n", wifi.ssid.c_str());
    file.printf("PASSWORD=%s\n", wifi.password.c_str());
    file.printf("HOSTNAME=%s\n\n", wifi.hostname.c_str());

    file.println("[MATRIX]");
    file.printf("WIDTH=%d\n", matrix.width);
    file.printf("HEIGHT=%d\n", matrix.height);
    file.printf("PANEL_TYPE=%s\n", matrix.panelType.c_str());
    file.printf("CHAIN=%d\n", matrix.chainLength);
    file.printf("BRIGHTNESS_LIMIT=%d\n", matrix.powerLimitPercent);
    file.printf("PWM_BITS=%d\n", matrix.pwmBits);
    file.printf("FORCE_SINGLE_BUFFER=%s\n", matrix.forceSingleBuffer ? "true" : "false");
    file.printf("RGB_SEQUENCE=%s\n", matrix.rgbSequence.c_str());
    file.printf("LIMIT_REFRESH_RATE_HZ=%d\n", matrix.limitRefreshRateHz);
    file.printf("DRIVER_CHIP=%s\n\n", matrix.driverChip.c_str());

    file.println("[MQTT]");
    file.printf("ENABLED=%s\n", mqtt.enabled ? "true" : "false");
    file.printf("BROKER=%s\n", mqtt.broker.c_str());
    file.printf("PORT=%d\n", mqtt.port);
    file.printf("USER=%s\n", mqtt.user.c_str());
    file.printf("PASS=%s\n", mqtt.pass.c_str());
    file.printf("DEVICE_NAME=%s\n", mqtt.deviceName.c_str());
    file.printf("TOPIC_BATOCERA=%s\n", mqtt.topic_batocera.c_str());
    file.printf("TOPIC_RECALBOX=%s\n\n", mqtt.topic_recalbox.c_str());

    file.println("[TIME]");
    file.printf("NTP_SERVER=%s\n", time.ntpServer.c_str());
    file.printf("TIMEZONE=%s\n", time.timezone.c_str());
    file.printf("FORMAT_24H=%s\n", time.format24h ? "true" : "false");
    file.printf("CLOCK_FONT=%d\n", time.clock_font);
    file.printf("CLOCK_SIZE=%d\n", time.clock_size);
    file.printf("CLOCK_THEME=%d\n", time.clock_theme);
    file.printf("CLOCK_OFFSET_X=%d\n", time.clock_offset_x);
    file.printf("CLOCK_OFFSET_Y=%d\n", time.clock_offset_y);
    file.printf("CLOCK_COLOR_1=%s\n", time.clock_color_1.c_str());
    file.printf("CLOCK_COLOR_2=%s\n", time.clock_color_2.c_str());
    file.printf("CLOCK_FONT_PATH=%s\n\n", time.clock_font_path.c_str());

    file.println("[IDLE]");
    file.printf("ROTATION=%s\n", idle.rotation.c_str());
    file.printf("CLOCK_DURATION_SEC=%d\n", idle.clock_duration_sec);
    file.printf("DATE_DURATION_SEC=%d\n", idle.date_duration_sec);
    file.printf("WEATHER_DURATION_SEC=%d\n", idle.weather_duration_sec);
    file.printf("TEMP_DURATION_SEC=%d\n", idle.temp_duration_sec);
    file.printf("DECIBEL_DURATION_SEC=%d\n", idle.decibel_duration_sec);
    file.printf("GIFS_COUNT=%d\n", idle.gifs_count);
    file.printf("FIGHTER_ENABLED=%s\n", idle.fighter_enabled ? "true" : "false");
    file.printf("FIGHTER_INTERVAL_SEC=%d\n\n", idle.fighter_interval_sec);

    file.println("[ENVIRONMENT]");
    file.printf("UNIT=%s\n", env.unit.c_str());
    file.printf("TEMP_OFFSET=%.2f\n\n", env.temp_offset);

    file.println("[AUDIO]");
    file.printf("VISUALIZER_ENABLED=%s\n", audio.visualizer_enabled ? "true" : "false");
    file.printf("VISUALIZER_MODE=%s\n", audio.visualizer_mode.c_str());
    file.printf("MIC_GAIN=%.2f\n", audio.mic_gain);
    file.printf("DB_CALIBRATION=%.2f\n\n", audio.db_calibration);

    file.println("[DATE]");
    file.printf("THEME=%d\n", dateSettings.theme);
    file.printf("BACKGROUND_SPRITE=%s\n", dateSettings.background_sprite.c_str());
    file.printf("FORMAT=%s\n", dateSettings.format.c_str());
    file.printf("DATE_FONT=%d\n", dateSettings.date_font);
    file.printf("DATE_SIZE=%d\n", dateSettings.date_size);
    file.printf("DATE_OFFSET_X=%d\n", dateSettings.date_offset_x);
    file.printf("DATE_OFFSET_Y=%d\n", dateSettings.date_offset_y);
    file.printf("DATE_COLOR_1=%s\n", dateSettings.date_color_1.c_str());
    file.printf("DATE_COLOR_2=%s\n", dateSettings.date_color_2.c_str());
    file.printf("DATE_FONT_PATH=%s\n\n", dateSettings.date_font_path.c_str());

    file.println("[WEATHER]");
    file.printf("API_KEY=%s\n", weather.api_key.c_str());
    file.printf("CITY=%s\n", weather.city.c_str());
    file.printf("LANG=%s\n", weather.lang.c_str());
    file.printf("WEATHER_OFFSET_X=%d\n", weather.weather_offset_x);
    file.printf("WEATHER_OFFSET_Y=%d\n\n", weather.weather_offset_y);

    file.println("[STANDBY]");
    file.printf("NIGHT_MODE_ENABLED=%s\n", standby.night_mode_enabled ? "true" : "false");
    file.printf("TURN_OFF_AT=%s\n", standby.turn_off_at.c_str());
    file.printf("WAKE_UP_AT=%s\n", standby.wake_up_at.c_str());
    file.printf("NIGHT_BRIGHTNESS=%d\n\n", standby.night_brightness);

    file.println("[FONTS]");
    file.printf("CUSTOM_FONT_PATH=%s\n\n", fonts.custom_font_path.c_str());

    file.println("[CRYPTO]");
    file.printf("ENABLED=%s\n", crypto.enabled ? "true" : "false");
    file.printf("SYMBOLS=%s\n", crypto.symbols.c_str());
    file.printf("DURATION_SEC=%d\n", crypto.duration_sec);
    file.printf("CACHE_TTL_MIN=%d\n", crypto.cache_ttl_min);
    file.printf("CURRENCY=%s\n\n", crypto.currency.c_str());

    file.println("[STOCK]");
    file.printf("ENABLED=%s\n", stock.enabled ? "true" : "false");
    file.printf("SYMBOLS=%s\n", stock.symbols.c_str());
    file.printf("DURATION_SEC=%d\n", stock.duration_sec);
    file.printf("CACHE_TTL_MIN=%d\n\n", stock.cache_ttl_min);

    file.close();
    return true;
}
