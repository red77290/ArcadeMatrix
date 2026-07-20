#include "ConfigLoader.h"
#include <SD.h>
#include <ArduinoJson.h>
#include "../engines/GifEngine.h"

ConfigLoader::ConfigLoader() {
    setDefaults();
}

void ConfigLoader::setDefaults() {
    matrix.width = 64;
    matrix.height = 32;
    matrix.panelType = "P3";
    matrix.chainLength = 2;
    matrix.powerLimitPercent = 50;
    matrix.forceSingleBuffer = false;
    matrix.colorDepth = 24;

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

    idle.rotation = "clock,date,weather,gifs";
    idle.clock_duration_sec = 60;
    idle.date_duration_sec = 10;
    idle.weather_duration_sec = 15;
    idle.gifs_count = 3;
    idle.sprite_count = 1;
    idle.fighter_interval_sec = 10;
    
    idle.mode = "gifs_then_clock";
    idle.gifs_before_clock = 10;

    weather.api_key = "";
    weather.city = "";
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

    fonts.custom_font_path = "";
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
        else if (key == "CHAIN") matrix.chainLength = value.toInt();
        else if (key == "BRIGHTNESS_LIMIT") matrix.powerLimitPercent = value.toInt();
        else if (key == "COLOR_DEPTH") matrix.colorDepth = value.toInt();
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
    }
    else if (currentSection == "IDLE") {
        if (key == "ROTATION") idle.rotation = value;
        else if (key == "CLOCK_DURATION_SEC") idle.clock_duration_sec = value.toInt();
        else if (key == "DATE_DURATION_SEC") idle.date_duration_sec = value.toInt();
        else if (key == "WEATHER_DURATION_SEC") idle.weather_duration_sec = value.toInt();
        else if (key == "GIFS_COUNT") idle.gifs_count = value.toInt();
        else if (key == "SPRITE_COUNT") idle.sprite_count = value.toInt();
        else if (key == "FIGHTER_INTERVAL_SEC") idle.fighter_interval_sec = value.toInt();
        
        else if (key == "MODE") idle.mode = value; // Legacy
        else if (key == "GIFS_BEFORE_CLOCK") idle.gifs_before_clock = value.toInt(); // Legacy
    }
    else if (currentSection == "WEATHER") {
        if (key == "API_KEY") weather.api_key = value;
        else if (key == "CITY") weather.city = value;
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
    }
    else if (currentSection == "FONTS") {
        if (key == "CUSTOM_FONT_PATH") fonts.custom_font_path = value;
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
    File file = SD.open(filepath, FILE_READ);
    if (!file) {
        Serial.printf("Failed to open %s\n", filepath);
        return false;
    }

    String currentSection = "";
    while (file.available()) {
        String line = file.readStringUntil('\n');
        parseLine(line, currentSection);
    }
    
    file.close();

    // Load saved playlists for default rotation
    File playlistFile = SD.open("/playlists_selected.json", FILE_READ);
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
    // Some ESP32 cores append to the file if it exists, so we must remove it first to overwrite cleanly
    if (SD.exists(filepath)) {
        SD.remove(filepath);
    }
    
    File file = SD.open(filepath, FILE_WRITE);
    if (!file) {
        Serial.println("Failed to open config file for writing");
        return false;
    }
    
    file.println("# ==========================================");
    file.println("# ArcadeMatrix - Configuration File");
    file.println("# ==========================================\n");

    file.println("[wifi]");
    file.println("ssid=" + wifi.ssid);
    file.println("password=" + wifi.password);
    file.println("hostname=" + wifi.hostname);
    file.println();

    file.println("[time]");
    file.println("ntpServer=" + time.ntpServer);
    file.println("timezone=" + time.timezone);
    file.println("format24h=" + String(time.format24h ? "1" : "0"));
    file.println("clock_font=" + String(time.clock_font));
    file.println("clock_size=" + String(time.clock_size));
    file.println("clock_offset_x=" + String(time.clock_offset_x));
    file.println("clock_offset_y=" + String(time.clock_offset_y));
    file.println("# Theme/Character: 0=Nintendo, 1=Capcom, 2=Taito, 3=Sega, 4=Cave, 5=Konami, 6=SNK, 7=Technos, 8=IGS, 9=Hudson, 10=Banpresto, 11=Namco, 12=Ryu, 13=Mario, 14=Marco, 15=Megaman, 16=Bub, 17=SpaceInvader, 18=Cyberpunk, 19=FlipClock");
    file.println("clock_theme=" + String(time.clock_theme));
    file.println("clock_color_1=" + time.clock_color_1);
    file.println("clock_color_2=" + time.clock_color_2);
    file.println();

    file.println("[matrix]");
    file.println("width=" + String(matrix.width));
    file.println("height=" + String(matrix.height));
    file.println("chain=" + String(matrix.chainLength));
    file.println("brightness_limit=" + String(matrix.powerLimitPercent));
    file.println("color_depth=" + String(matrix.colorDepth));
    file.println();

    file.println("[mqtt]");
    file.println("enabled=" + String(mqtt.enabled ? "1" : "0"));
    file.println("broker=" + mqtt.broker);
    file.println("port=" + String(mqtt.port));
    file.println("user=" + mqtt.user);
    file.println("pass=" + mqtt.pass);
    file.println("topic_batocera=" + mqtt.topic_batocera);
    file.println("topic_recalbox=" + mqtt.topic_recalbox);
    file.println();

    file.println("[idle]");
    file.println("rotation=" + idle.rotation);
    file.println("clock_duration_sec=" + String(idle.clock_duration_sec));
    file.println("date_duration_sec=" + String(idle.date_duration_sec));
    file.println("weather_duration_sec=" + String(idle.weather_duration_sec));
    file.println("gifs_count=" + String(idle.gifs_count));
    file.println("sprite_count=" + String(idle.sprite_count));
    file.println("fighter_interval_sec=" + String(idle.fighter_interval_sec));
    file.println("mode=" + idle.mode);
    file.println("gifs_before_clock=" + String(idle.gifs_before_clock));
    file.println();
    
    file.println("[weather]");
    file.println("api_key=" + weather.api_key);
    file.println("city=" + weather.city);
    file.println("weather_offset_x=" + String(weather.weather_offset_x));
    file.println("weather_offset_y=" + String(weather.weather_offset_y));
    file.println();

    file.println("[standby]");
    file.println("night_mode_enabled=" + String(standby.night_mode_enabled ? "1" : "0"));
    file.println("turn_off_at=" + standby.turn_off_at);
    file.println("wake_up_at=" + standby.wake_up_at);
    file.println("night_brightness=" + String(standby.night_brightness));
    file.println();

    file.println("[date]");
    file.println("# Theme colors for the Date display");
    file.println("# -1=None, 0=Nintendo, 1=Capcom, 2=Taito, 3=Sega, 4=Cave, 5=Konami, 6=SNK, 7=Technos, 8=IGS, 9=Hudson, 10=Banpresto, 11=Namco");
    file.println("theme=" + String(dateSettings.theme));
    file.println("background_sprite=" + dateSettings.background_sprite);
    file.println("format=" + dateSettings.format);
    file.println("date_font=" + String(dateSettings.date_font));
    file.println("date_size=" + String(dateSettings.date_size));
    file.println("date_offset_x=" + String(dateSettings.date_offset_x));
    file.println("date_offset_y=" + String(dateSettings.date_offset_y));
    file.println("date_color_1=" + dateSettings.date_color_1);
    file.println("date_color_2=" + dateSettings.date_color_2);
    file.println();

    file.println("[fonts]");
    file.println("# Optional SD-loadable custom bitmap font (.amf format, see tools/bdf_to_amfont)");
    file.println("# Leave empty to use the compiled-in fonts only.");
    file.println("custom_font_path=" + fonts.custom_font_path);
    file.println();
    
    file.close();
    return true;
}
