#pragma once
#include <stdint.h>
#include <stddef.h>
#include <ArduinoJson.h>
#include "GraphPayload.h"

/**
 * The MQTT Data payloads other than the graph (docs/MQTT_DATA_CONTRACT.md):
 *
 *  - value:   {"type":"value","value":"21.7","unit":"°C","label":"OUTSIDE","color":"#40C0FF"}
 *  - table:   {"type":"table","title":"ENERGY","show_title":true,"tiles":[{"label":"HOUSE","label_short":"HSE",
 *              "value":"2.8","unit":"kW","color":"#FFB000","label_color":"#808080"}, ... up to 4]}
 *  - weather: {"type":"weather","units":"imperial","current":{"temp":75,"condition":"clear-night","humidity":47,
 *              "wind":7,"wind_unit":"mph","wind_dir":"NE"},"days":[{"temp_max":89,"temp_min":70,"condition":"sunny",
 *              "precip_prob":0}, ... up to 5]}
 *
 * Plain fixed-size data like GraphData, so they cross the cores without the heap.
 */
namespace feed {

/// None = no payload (or an empty one); Unsupported = `type` missing or unknown, invalid JSON, or a
/// known type whose content cannot be drawn.
enum class Kind : uint8_t { None = 0, Value = 1, Table = 2, Graph = 3, Weather = 4, Unsupported = 5 };

/// The page type named by the payload's required "type": "value", "table", "graph" or "weather".
/// Anything else, or no "type", is Unsupported.
Kind detectKind(const JsonDocument& doc);

/// The payload's "seconds" (time on the page; for weather, on each of its pages): default 10,
/// clamped to 3..3600.
uint16_t pageSeconds(const JsonDocument& doc);

static constexpr uint8_t MAX_TILES = 4;

struct Tile {
    char label[24];
    char labelShort[12];       ///< optional abbreviation, used when the full label does not fit
    char value[16];
    char unit[8];
    graph::Rgb color;          ///< value colour, default white
    graph::Rgb labelColor;     ///< label and unit colour, default #808080
};

struct ValuesData {
    bool valid;
    bool showTitle;            ///< table "show_title" (default true); a value page has no title
    char title[24];
    uint8_t count;
    Tile tiles[MAX_TILES];
};

/// "table": a title and 1-4 tiles.
bool parseTable(const JsonDocument& doc, ValuesData& out);

/// "value": one tile (label above value and unit), drawn like a 1-tile table without a title.
bool parseValue(const JsonDocument& doc, ValuesData& out);

static constexpr uint8_t MAX_DAYS = 5;

struct WeatherDay {
    bool hasTemps;
    float tempMax;
    float tempMin;
    char condition[20];        ///< HA condition id, "" when absent
    int8_t precipProb;         ///< -1 when absent
};

struct WeatherFeed {
    bool valid;
    bool imperial;
    bool hasCurrent;
    float currentTemp;
    char currentCondition[20];
    int8_t humidity;           ///< -1 when absent
    float wind;                ///< NaN when absent
    char windUnit[8];
    char windDir[4];           ///< 8-point compass ("NE"), "" when absent
    uint8_t dayCount;
    WeatherDay days[MAX_DAYS];
};

bool parseWeather(const JsonDocument& doc, WeatherFeed& out);

/// The firmware's OWM-style icon code for a weather condition id of the contract (unknown = "03d").
const char* iconForCondition(const char* haCondition);

/// Index 0..13 of the condition's label group (Clear, Sunny, Partly cloudy, Cloudy, Fog, Unusual,
/// Rain, Heavy rain, Storm, Storm + rain, Snow, Sleet, Hail, Windy); unknown = -1.
int conditionGroup(const char* haCondition);

}  // namespace feed
