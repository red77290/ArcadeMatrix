/**
 * @file ConfigLoader.h
 * @brief Parses and saves the main configuration file (`conf.ini`).
 * 
 * Defines all configuration structures (WiFi, Matrix, MQTT, etc.) and provides 
 * an INI parser to read from and write to the SD card.
 */
#pragma once
#include <Arduino.h>
#include <SD.h>
#include <vector>

/**
 * @struct MatrixConfig
 * @brief Hardware configuration for the HUB75 DMA LED Matrix.
 */
struct MatrixConfig {
    int width;              ///< Total pixel width of the display
    int height;             ///< Total pixel height of the display
    String panelType;       ///< Hardware type (e.g., FM6126A, ICN2038S)
    int chainLength;        ///< Number of chained panels
    int powerLimitPercent;  ///< Brightness capping to prevent power supply issues
    bool forceSingleBuffer; ///< Force single buffering to save RAM on large displays
    int colorDepth;         ///< Number of bits per color channel (e.g. 8 for 16M colors, 3 for 512)
};

/**
 * @struct WiFiConfig
 * @brief Network connectivity settings.
 */
struct WiFiConfig {
    String ssid;            ///< Network SSID
    String password;        ///< WPA2 Password
    String hostname;        ///< mDNS hostname (e.g., "arcadematrix")
};

/**
 * @struct MqttConfig
 * @brief Settings for the MQTT client connecting to Batocera/Recalbox.
 */
struct MqttConfig {
    bool enabled;           ///< Should MQTT listener be active?
    String broker;          ///< IP address of the MQTT Broker
    int port;               ///< MQTT Port (default 1883)
    String user;            ///< MQTT Username (optional)
    String pass;            ///< MQTT Password (optional)
    String deviceName;      ///< Client ID presented to the broker
    String topic_batocera;  ///< Subscribed topic for Batocera events
    String topic_recalbox;  ///< Subscribed topic for Recalbox events
};

/**
 * @struct TimeConfig
 * @brief NTP Time and Clock display settings.
 */
struct TimeConfig {
    String ntpServer;       ///< NTP Server URL (e.g. "pool.ntp.org")
    String timezone;        ///< POSIX Timezone string (e.g. "CET-1CEST")
    bool format24h;         ///< Use 24-hour format instead of 12-hour AM/PM
    int clock_font;         ///< Font ID for the Arcade Clock
    int clock_size;         ///< Scale multiplier (1, 2, or 3)
    int clock_theme;        ///< PublisherTheme ID (Nintendo, Capcom, etc.)
    int clock_offset_x;     ///< Manual X axis pixel offset
    int clock_offset_y;     ///< Manual Y axis pixel offset
    String clock_color_1;   ///< Hex color string for gradient start
    String clock_color_2;   ///< Hex color string for gradient end
};

/**
 * @struct IdleConfig
 * @brief Manages the automatic rotation sequence between modules.
 */
struct IdleConfig {
    String rotation;        ///< Comma-separated list of active modules (e.g. "clock,date,gifs")
    int clock_duration_sec; ///< Seconds to display the Clock
    int date_duration_sec;  ///< Seconds to display the Date
    int weather_duration_sec;///< Seconds to display the Weather
    int gifs_count;         ///< Number of GIFs to play before rotating
    int sprite_count;       ///< Number of MUGEN fights to play before rotating
    int fighter_interval_sec; ///< Seconds to wait between MUGEN fights
    
    // Legacy support for backwards compatibility
    String mode;
    int gifs_before_clock;
};

/**
 * @struct WeatherConfig
 * @brief OpenWeatherMap integration settings.
 */
struct WeatherConfig {
    String api_key;         ///< OpenWeather API key
    String city;            ///< Target city for weather data
    int weather_offset_x;   ///< Manual X axis pixel offset
    int weather_offset_y;   ///< Manual Y axis pixel offset
};

/**
 * @struct StandbyConfig
 * @brief Power saving and sleep scheduling.
 */
struct StandbyConfig {
    bool night_mode_enabled;///< Enable automatic matrix sleep
    String turn_off_at;     ///< Time string (HH:MM) to power down the LEDs
    String wake_up_at;      ///< Time string (HH:MM) to power up the LEDs
    int night_brightness;   ///< Night time brightness
    bool matrix_power;      ///< Runtime toggle for panel power
};

/**
 * @struct DateConfig
 * @brief DateEngine visual settings.
 */
struct DateConfig {
    int theme;                  ///< PublisherTheme ID
    String background_sprite;   ///< Optional static background for date view
    String format;              ///< Date string format (e.g. "DD/MM")
    int date_font;              ///< Font ID for the Date
    int date_size;              ///< Scale multiplier
    int date_offset_x;          ///< Manual X axis pixel offset
    int date_offset_y;          ///< Manual Y axis pixel offset
    String date_color_1;        ///< Hex color string for gradient start
    String date_color_2;        ///< Hex color string for gradient end
};

/**
 * @class ConfigLoader
 * @brief Main engine configuration parser.
 */
class ConfigLoader {
public:
    ConfigLoader();
    
    /**
     * @brief Populate all structs with safe default values.
     */
    void setDefaults();
    
    /**
     * @brief Parse INI formatted data from a raw string in memory.
     * @param iniContent The string content.
     * @return true on success.
     */
    bool parseFromString(const char* iniContent);
    
    /**
     * @brief Read and parse the `conf.ini` file from the SD Card.
     * @param filepath Path to the config file (e.g. "/conf.ini").
     * @return true on success.
     */
    bool parseFromSD(const char* filepath);
    
    /**
     * @brief Serialize the current configuration back to the SD Card.
     * @param filepath Path to save the config file to.
     * @return true on success.
     */
    bool saveToSD(const char* filepath);
    
    // Publicly accessible configurations
    MatrixConfig matrix;
    WiFiConfig wifi;
    MqttConfig mqtt;
    TimeConfig time;
    IdleConfig idle;
    WeatherConfig weather;
    StandbyConfig standby;
    DateConfig dateSettings;

private:
    void parseLine(String line, String& currentSection);
    String extractValue(String line);
};
