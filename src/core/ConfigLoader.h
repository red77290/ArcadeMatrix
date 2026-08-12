#pragma once
#include <Arduino.h>

/**
 * @struct MatrixConfig
 * @brief Configuration settings for the LED Matrix panel hardware.
 */
struct MatrixConfig {
    int width;              ///< Matrix panel width in pixels (e.g. 64, 128, 256)
    int height;             ///< Matrix panel height in pixels (e.g. 32, 64)
    String panelType;       ///< Driver chip type ("FM6126A", "ICN2038S", "SHIFTREG")
    int chainLength;        ///< Number of daisy-chained panels (default 1)
    int powerLimitPercent;  ///< Maximum brightness percentage (0-100)
    bool forceSingleBuffer; ///< Force single buffer mode to save RAM
    int pwmBits;         ///< Color depth in bits per channel (e.g. 8 for 24-bit RGB)
    String rgbSequence;     ///< RGB sequence mapping (e.g. "RGB", "RBG", "BGR")
    int limitRefreshRateHz; ///< Optional refresh rate limiter in Hz (0 = unlimited)
    String driverChip;      ///< Driver chip implementation ("SHIFTREG", "FM6126A")
    bool clkPhase;          ///< Clock phase toggle for glitched displays
    int latchBlanking;      ///< Latch blanking pulse width
    int rowAddressMode;     ///< Row address mode for multiplexed panels
};

/**
 * @struct WifiConfig
 * @brief Credentials for local Wi-Fi connectivity.
 */
struct WifiConfig {
    String ssid;            ///< Wi-Fi network SSID
    String password;        ///< Wi-Fi network password
    String hostname;        ///< mDNS hostname (e.g. "arcadematrix")
};

/**
 * @struct MqttConfig
 * @brief MQTT broker integration settings for Batocera and Recalbox.
 */
struct MqttConfig {
    bool enabled;           ///< Toggle MQTT listener active status
    String broker;          ///< Broker IP address or hostname
    int port;               ///< Broker port (default 1883)
    String user;            ///< Optional broker username
    String pass;            ///< Optional broker password
    String deviceName;      ///< Device client ID
    String topic_batocera;  ///< MQTT topic for Batocera system status
    String topic_recalbox;  ///< MQTT topic for Recalbox system status
};

/**
 * @struct TimeConfig
 * @brief Real-time clock display parameters and NTP settings.
 */
struct TimeConfig {
    String ntpServer;       ///< NTP server address (default "pool.ntp.org")
    String timezone;        ///< POSIX timezone string (e.g. "CET-1CEST,M3.5.0,M10.5.0/3")
    bool format24h;         ///< 24-hour format toggle (true = 24h, false = 12h AM/PM)
    int clock_font;         ///< Selected clock font index (0 to 5)
    int clock_size;         ///< Clock font size multiplier
    int clock_theme;        ///< Clock theme index
    int clock_offset_x;     ///< Horizontal position offset
    int clock_offset_y;     ///< Vertical position offset
    String clock_color_1;   ///< Primary gradient color in hex (e.g. "#ffffff")
    String clock_color_2;   ///< Secondary gradient color in hex
    String clock_font_path; ///< Custom SD font file path
};

/**
 * @struct DateSettingsConfig
 * @brief Date display parameters.
 */
struct DateSettingsConfig {
    int theme;              ///< Date theme index
    String format;          ///< Date format string ("DD/MM" or "MM/DD")
    int date_font;          ///< Font index
    int date_size;          ///< Font size multiplier
    int date_offset_x;      ///< Horizontal offset
    int date_offset_y;      ///< Vertical offset
    String background_sprite;///< Background raw image filename
    String date_color_1;    ///< Primary gradient color
    String date_color_2;    ///< Secondary gradient color
    String date_font_path;  ///< Custom font path
};

/**
 * @struct IdleConfig
 * @brief Manages the automatic rotation sequence between modules.
 */
struct IdleConfig {
    String rotation;        ///< Comma-separated list of active modules (e.g. "clock,date,gifs,temp,decibel")
    int clock_duration_sec; ///< Seconds to display the Clock
    int date_duration_sec;  ///< Seconds to display the Date
    int weather_duration_sec;///< Seconds to display the Weather
    int temp_duration_sec;   ///< Seconds to display Indoor Temperature & Humidity
    int decibel_duration_sec;///< Seconds to display Decibel Meter
    int gifs_count;         ///< Number of GIFs to play before rotating
    bool fighter_enabled;   ///< Whether MUGEN fighters are enabled
    int fighter_interval_sec; ///< Seconds to wait between MUGEN fights
    
    // Legacy support for backwards compatibility
    String mode;
    int gifs_before_clock;
};

/**
 * @struct EnvironmentConfig
 * @brief Indoor Temperature & Humidity sensor settings.
 */
struct EnvironmentConfig {
    String unit;            ///< Temperature unit ("C" or "F")
    float temp_offset;      ///< Temperature offset adjustment in C
};

/**
 * @struct AudioConfig
 * @brief Microphone, Rhythmic Visualizer, and Decibel Meter settings.
 */
struct AudioConfig {
    bool visualizer_enabled;///< Priority visualizer toggle
    String visualizer_mode; ///< Visualizer display mode ("spectrum", "waveform", "radial", "neon_fire")
    float mic_gain;         ///< Microphone gain multiplier (default 1.0)
    float db_calibration;   ///< Decibel calibration offset in dB
};

/**
 * @struct WeatherConfig
 * @brief OpenWeatherMap service parameters.
 */
struct WeatherConfig {
    String api_key;         ///< OpenWeatherMap API key
    String city;            ///< Target city string (e.g. "Paris,FR")
    String lang;            ///< Language code (e.g. "fr", "en")
    int weather_offset_x;   ///< Position X offset
    int weather_offset_y;   ///< Position Y offset
};

/**
 * @struct StandbyConfig
 * @brief Night mode and automatic power schedule settings.
 */
struct StandbyConfig {
    bool night_mode_enabled;///< Night mode toggle
    String turn_off_at;     ///< Standby start time ("HH:MM")
    String wake_up_at;      ///< Standby end time ("HH:MM")
    int night_brightness;   ///< Standby brightness level (0-100)
    
    // Runtime state flag
    bool matrix_power = true;
};

/**
 * @struct FontConfig
 * @brief SD-loadable custom font configuration.
 */
struct FontConfig {
    String custom_font_path;
};

/**
 * @struct CryptoConfig
 * @brief Real-time crypto ticker settings.
 */
struct CryptoConfig {
    bool enabled;
    String symbols;         ///< Comma-separated list of crypto symbols (e.g. "BTC,ETH,SOL")
    int duration_sec;       ///< Display duration per crypto symbol
    int cache_ttl_min;      ///< Market data cache TTL in minutes
    String currency;        ///< Currency string (e.g. "USD", "EUR")
};

/**
 * @struct StockConfig
 * @brief Real-time stock market quote settings.
 */
struct StockConfig {
    bool enabled;
    String symbols;         ///< Comma-separated list of stock tickers (e.g. "AAPL,NVDA,TSLA,MSFT")
    int duration_sec;       ///< Display duration per stock ticker
    int cache_ttl_min;      ///< Quote cache TTL in minutes (refresh rate)
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
     * @brief Save the current active configuration back to the SD Card file.
     * @param filepath Path to the target file.
     * @return true on success.
     */
    bool saveToSD(const char* filepath);

    /**
     * @brief Cleanly strip comments from an INI line without destroying hex colors (#FF0000).
     */
    static String stripComments(String line);

    MatrixConfig matrix;
    WifiConfig wifi;
    MqttConfig mqtt;
    TimeConfig time;
    DateSettingsConfig dateSettings;
    IdleConfig idle;
    EnvironmentConfig env;
    AudioConfig audio;
    WeatherConfig weather;
    StandbyConfig standby;
    FontConfig fonts;
    CryptoConfig crypto;
    StockConfig stock;

private:
    void parseLine(String line, String& currentSection);
    String extractValue(String line);
};
