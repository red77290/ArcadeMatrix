🇬🇧 English | 🇫🇷 [Français](CONFIGURATION_FR.md) | 🇪🇸 [Español](CONFIGURATION_ES.md)

# Detailed Configuration (config.json) - ESP32

The ESP32 configuration system uses a `config.json` file stored at the root of the SD card. This file is usually managed automatically via the Web interface, but understanding it is useful for advanced configuration or debugging.

Since the S13 Parity update, the architecture is entirely focused on independent engine "instances".

---

## 1. Global Structure

```json
{
  "matrix": { ... },
  "wifi": { ... },
  "system": { ... },
  "instances": [ ... ],
  "rotation": [ ... ]
}
```

---

## 2. The `"matrix"` Block (HUB75 & DMA Hardware)

This block configures the I2S bus and the matrix driver. Since the ESP32 drives the pins directly without a real-time OS, these settings are critical for stability.

| Key | Type | Description |
| :--- | :--- | :--- |
| `width` | `int` | Width of a single panel (e.g., `64`). |
| `height` | `int` | Height of a single panel (e.g., `32`). |
| `chain_length` | `int` | Number of panels chained horizontally. Warning: ESP32 RAM won't support long chains. |
| `pwm_bits` | `int` | Color depth. Default value `8`. Increasing it above 8 drastically drops FPS on ESP32. |
| `driver_chip` | `String` | Controller chip (`SHIFTREG`, `FM6126A`). |
| `force_single_buffer` | `bool` | If `true`, rendering is less smooth (visible tearing) but it **halves DMA memory usage**. Very useful on 128x64 matrices without PSRAM. |
| `brightness_limit` | `int` | Maximum software brightness limiter (`0` to `100`). Protects your power supply. |

*Note: Any modification of the matrix geometry (`width`, `height`, `driver_chip`, `pwm_bits`) requires a physical reboot of the ESP32.*

---

## 3. The `"system"` Block (Environment and Standby)

| Key | Type | Description |
| :--- | :--- | :--- |
| `timezone` | `String` | POSIX string (e.g., `CET-1CEST,M3.5.0,M10.5.0/3`). |
| `format_24h` | `bool` | Time format. `true` = 23:00, `false` = 11:00 PM. |
| `lang` | `String` | System language (e.g., `en`, `fr`). |
| `night_mode_enabled` | `bool` | Enables automatic turn-off or brightness reduction at night. |
| `turn_off_at` | `String` | Standby start time (e.g., `"23:00"`). |
| `wake_up_at` | `String` | Wake-up time (e.g., `"07:00"`). |
| `night_brightness` | `int` | Standby brightness (`0` = matrix completely off and DMA suspended). |
| `fighter_enabled` | `bool` | Enables MUGEN combat sprites overlay (`.fgt`) on top of other engines. |
| `fighter_interval_sec` | `int` | Delay in seconds between two MUGEN fights. |

---

## 4. The `"wifi"` Block

| Key | Type | Description |
| :--- | :--- | :--- |
| `ssid` | `String` | The name of your 2.4 GHz Wi-Fi network (ESP32 does not support 5 GHz). |
| `password` | `String` | The WPA2 key. |
| `hostname` | `String` | The mDNS name to access the interface via `http://hostname.local`. |

---

## 5. Engines: `"instances"` & `"rotation"`

Thanks to the decoupled "Lazy-Once" architecture, the ESP32 can virtually handle an infinite number of engine configurations without overloading the Heap, as long as these engines are not in the active loop.

### `"instances"`
This is an array containing the configuration of each logical block.

```json
{
  "instance_id": "crypto_main",
  "engine_id": "crypto",
  "config": {
    "symbols": "BTC,ETH,SOL",
    "duration_sec": 10
  }
}
```
* `instance_id`: Unique name of this block (e.g., you can have two different crypto widgets).
* `engine_id`: The internal identifier of the C++ Engine.
* `config`: A dynamic JSON object specific to the engine (its `Capabilities`).

### `"rotation"`
Defines the display order on the screen.

```json
{
  "instance_id": "crypto_main",
  "duration_sec": 30
}
```
The ESP32 will allocate memory (`initialize()`) for `crypto_main` the very first time it encounters it in this rotation loop.

---

## 6. Engine Configurations

Each engine declares its configuration schema dynamically. Below are the parameters and options for all available engines:

### Engine: `clock`
| Field | Type | Default | Options | Description |
| :--- | :--- | :--- | :--- | :--- |
| `clock_theme` | `ENUM` | `0` | From `/api/themes` | Visual theme / publisher clockface (Nintendo, Capcom, Sega, Arcade, Cyberpunk, Flip, Tetris, etc.). |
| `clock_format` | `String` | `%H:%M:%S` | `%H:%M:%S`, `%H:%M`, `%I:%M:%S %p`, `%I:%M %p` | POSIX strftime format string. |
| `clock_font` | `ENUM` | `PressStart2P.ttf` | From `/api/fonts` | Display typeface (`PressStart2P`, `namco`, `FreeSansBold`, `FreeMonoBold`, `RetroGaming`, or custom `.amf` font). |
| `timezone` | `ENUM` | `Europe/Paris` | From `/api/timezones` | Timezone / region for time calculation. |
| `clock_size` | `int` | `2` | `1` to `5` | Font scale multiplier. |
| `clock_color_1` | `Color` | `#ffffff` | Hex `#RRGGBB` | Primary top color (used with Custom theme 20). |
| `clock_color_2` | `Color` | `#ff00ff` | Hex `#RRGGBB` | Secondary bottom color (used with Custom theme 20). |
| `clock_offset_x` | `int` | `0` | `-64` to `64` | Horizontal pixel shift. |
| `clock_offset_y` | `int` | `0` | `-32` to `32` | Vertical pixel shift. |

### Engine: `date`
| Field | Type | Default | Options | Description |
| :--- | :--- | :--- | :--- | :--- |
| `date_theme` | `ENUM` | `0` | From `/api/themes` | Visual theme for date. |
| `date_format` | `String` | `%d/%m/%Y` | `%d/%m/%Y`, `%Y-%m-%d`, `%d %b %Y`, `%A %d %B` | Format for date display. |
| `date_font` | `ENUM` | `PressStart2P.ttf` | From `/api/fonts` | Display typeface (`PressStart2P`, `namco`, `FreeSansBold`, `FreeMonoBold`, `RetroGaming`, or `.amf`). |
| `timezone` | `ENUM` | `Europe/Paris` | From `/api/timezones` | Timezone / region for date calculation. |
| `date_size` | `int` | `1` | `1` to `3` | Font scale multiplier. |
| `date_color_1` | `Color` | `#ffffff` | Hex `#RRGGBB` | Primary color (used with Custom theme 20). |
| `date_color_2` | `Color` | `#00ffff` | Hex `#RRGGBB` | Secondary color (used with Custom theme 20). |
| `date_offset_x` | `int` | `0` | `-64` to `64` | Horizontal pixel shift. |
| `date_offset_y` | `int` | `0` | `-32` to `32` | Vertical pixel shift. |

### Engine: `weather`
| Field | Type | Default | Options | Description |
| :--- | :--- | :--- | :--- | :--- |
| `api_key` | `String` | `""` | Free API key | Your OpenWeatherMap API Key (free tier at [openweathermap.org](https://home.openweathermap.org/users/sign_up)). |
| `city` | `String` | `Paris,FR` | Text | City location (see formatting guide below). |
| `units` | `ENUM` | `metric` | `metric`, `imperial` | Temperature unit: `metric` for Celsius (°C) or `imperial` for Fahrenheit (°F). |
| `lang` | `ENUM` | `fr` | `fr`, `en`, `es`, `de`, `it` | Language code for day labels (TODAY / AUJ. / HOY). |
| `weather_offset_x` | `int` | `0` | `-64` to `64` | Horizontal pixel shift. |
| `weather_offset_y` | `int` | `0` | `-32` to `32` | Vertical pixel shift. |

#### How to Format the `city` Field on OpenWeatherMap
OpenWeatherMap uses the ISO 3166 country code (and 2-letter state code for the US) to disambiguate locations:
* **International Locations:** Use `City,CountryCode` (e.g. `Paris,FR`, `London,GB`, `Tokyo,JP`, `Montreal,CA`).
* **United States Locations:** Use `City,StateCode,CountryCode` (e.g. `Tucson,AZ,US`, `Miami,FL,US`, `Dallas,TX,US`). Specifying only the city or omitting the country may return an incorrect city with the same name.
* **Where to Look:** Go to [openweathermap.org](https://openweathermap.org), search for your city. The top search result header and URL show the exact `City,State,Country` string recognized by the API.

### Engine: `gifs`
| Field | Type | Default | Options | Description |
| :--- | :--- | :--- | :--- | :--- |
| `folder` | `LIST` | `all` | From `/api/playlists` | Active GIF playlists / folders (e.g. `Arcade`, `Consoles`, `Fighters`). |
| `speed_multiplier` | `Float` | `1.0` | `0.25` to `3.0` | Playback speed factor (`1.0` = normal speed). |
| `shuffle` | `Boolean` | `true` | `true`, `false` | Randomize playback order. |
| `duration_sec` | `int` | `10` | `2` to `120` | Dwell duration in seconds per GIF. |

### Engine: `crypto` (Requires PSRAM)
| Field | Type | Default | Options | Description |
| :--- | :--- | :--- | :--- | :--- |
| `symbols` | `String` | `BTC,ETH,SOL` | Comma-separated | Crypto asset ticker symbols to monitor. |
| `show_chart` | `Boolean` | `true` | `true`, `false` | Display historical price sparkline chart. |
| `chart_timeframe` | `ENUM` | `daily` | `hourly`, `daily`, `weekly`, `monthly` | Historical sparkline chart timeframe. |
| `duration_sec` | `int` | `5` | `3` to `30` | Seconds to display each crypto asset page. |
| `currency` | `ENUM` | `USD` | `USD`, `EUR`, `GBP`, `JPY` | Fiat currency for quote valuations. |
| `provider` | `ENUM` | `coingecko` | `coingecko`, `binance` | Live market data provider. |
| `cache_ttl_min` | `int` | `5` | `1` to `60` | Minutes between fresh API quote updates. |
| `crypto_offset_x` | `int` | `0` | `-64` to `64` | Horizontal pixel shift. |
| `crypto_offset_y` | `int` | `0` | `-32` to `32` | Vertical pixel shift. |

### Engine: `stock` (Requires PSRAM)
| Field | Type | Default | Options | Description |
| :--- | :--- | :--- | :--- | :--- |
| `symbols` | `String` | `AAPL,TSLA,NVDA` | Comma-separated | Stock ticker symbols to monitor. |
| `show_chart` | `Boolean` | `true` | `true`, `false` | Display historical price sparkline chart. |
| `chart_timeframe` | `ENUM` | `daily` | `hourly`, `daily`, `weekly`, `monthly` | Historical sparkline chart timeframe. |
| `duration_sec` | `int` | `5` | `3` to `30` | Seconds to display each stock page. |
| `provider` | `ENUM` | `yahoo` | `yahoo` | Market data provider. |
| `cache_ttl_min` | `int` | `5` | `1` to `60` | Minutes between fresh API updates. |
| `stock_offset_x` | `int` | `0` | `-64` to `64` | Horizontal pixel shift. |
| `stock_offset_y` | `int` | `0` | `-32` to `32` | Vertical pixel shift. |

### Engine: `audiovisualizer` (Requires Microphone)
| Field | Type | Default | Options | Description |
| :--- | :--- | :--- | :--- | :--- |
| `enabled` | `Boolean` | `false` | `true`, `false` | Enable real-time FFT spectrum overlay. |
| `style` | `ENUM` | `spectrum` | `spectrum`, `waveform`, `radial`, `neon_fire` | FFT audio visualization style. |
| `sensitivity` | `int` | `5` | `1` to `10` | FFT microphone response sensitivity. |
| `gain` | `Float` | `1.0` | `0.5` to `5.0` | Hardware microphone gain scaling multiplier. |

### Engine: `decibelMeter` (Requires Microphone)
| Field | Type | Default | Options | Description |
| :--- | :--- | :--- | :--- | :--- |
| `threshold` | `int` | `80` | `40` to `120` | Noise alert warning threshold in decibels (dB). |

### Engine: `temp` (Requires Sensor)
| Field | Type | Default | Options | Description |
| :--- | :--- | :--- | :--- | :--- |
| `units` | `ENUM` | `C` | `C`, `F` | Onboard temperature sensor measurement unit. |
| `temp_offset_x` | `int` | `0` | `-64` to `64` | Horizontal pixel shift. |
| `temp_offset_y` | `int` | `0` | `-32` to `32` | Vertical pixel shift. |

### Engine: `message`
| Field | Type | Default | Options | Description |
| :--- | :--- | :--- | :--- | :--- |
| `text` | `String` | `ArcadeMatrix` | Text | Text banner or message to display. |
| `color` | `Color` | `#ffffff` | Hex `#RRGGBB` | Text color. |
| `size` | `int` | `1` | `1` to `4` | Font scale multiplier. |
| `direction` | `ENUM` | `rtl` | `rtl`, `ltr`, `ttb`, `btt`, `static` | Scroll direction (`rtl` = right to left, `static` = centered non-scrolling). |
| `speed` | `int` | `50` | `10` to `200` | Delay in milliseconds per scroll step (lower is faster). |
| `font` | `ENUM` | `Default` | From `/api/fonts` | Display typeface (`PressStart2P`, `namco`, `FreeSansBold`, `FreeMonoBold`, `RetroGaming`, `.amf`). |

---

*Note: You can always query the live schema and registered engines from the ESP32 via `GET /api/engines`.*


