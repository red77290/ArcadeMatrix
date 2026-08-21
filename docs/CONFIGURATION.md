# Configuration Guide (`config.json` & API)

🇬🇧 English | 🇫🇷 [Français](CONFIGURATION_FR.md) | 🇪🇸 [Español](CONFIGURATION_ES.md)

ArcadeMatrix is fully configurable without recompiling C++ code. All parameters can be managed via the Web UI (at `http://arcadematrix.local`), via the REST API, or directly in the `config.json` file on the SD card root.

---

## 📊 Comprehensive Parameter Reference Matrix

| Section | `config.json` Key | REST API Key | Type | Default | Example | Runtime Effect | Persisted? | Reboot Required? | Description |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **[WIFI]** | `SSID` | `wifi_ssid` | string | `""` | `"HomeWiFi"` | Boot connection | Yes | **Yes** | 2.4GHz Wi-Fi Network Name |
| | `PASSWORD` | `wifi_password` | string | `""` | `"secret"` | Boot connection | Yes | **Yes** | WPA2 Password |
| | `HOSTNAME` | `hostname` | string | `"ArcadeMatrix"` | `"my-matrix"` | mDNS | Yes | **Yes** | Network hostname (`http://my-matrix.local`) |
| **[MATRIX]**| `WIDTH` | `matrix_cols` | int | `64` | `256` | DMA Matrix | Yes | **Yes** | Total matrix width in pixels |
| | `HEIGHT` | `matrix_rows` | int | `32` | `64` | DMA Matrix | Yes | **Yes** | Total matrix height in pixels |
| | `CHAIN` | `matrix_chain` | int | `1` | `2` | Panels | Yes | **Yes** | Number of chained panels |
| | `BRIGHTNESS_LIMIT`|`brightness_limit`| int | `100` | `80` | **Live** | Yes | No | Brightness limit (0-100%) |
| | `PWM_BITS` | `pwm_bits` | int | `8` | `8` | Depth | Yes | **Yes** | Color channel bit depth (8 default) |
| | `DRIVER_CHIP` | `matrix_driver_chip`| string | `"SHIFTREG"` | `"FM6126A"` | Driver IC | Yes | **Yes** | Driver IC chip (`SHIFTREG`, `FM6126A`, `ICN2038S`, `SM16208`) |
| | `FORCE_SINGLE_BUFFER`| `force_single_buffer`| bool | `false` | `true` | RAM Memory | Yes | **Yes** | Force single buffering to save RAM |
| **[TIME]** | `NTP_SERVER` | `ntp_server` | string | `"pool.ntp.org"` | `"time.google.com"` | NTP Sync | Yes | No | NTP Time Server URL |
| | `TIMEZONE` | `timezone` | string | `"CET-1CEST..."` | `"EST5EDT"` | POSIX Timezone | Yes | No | POSIX Timezone string |
| | `FORMAT_24H` | `format_24h` | bool | `true` | `false` | Display | Yes | No | 24-hour format (`true`) or 12-hour AM/PM (`false`) |
| | `CLOCK_THEME` | `clock_theme` | int | `0` | `20` | **Live** | Yes | No | Clock Theme ID (0-29, 20=Custom Gradient) |
| | `CLOCK_COLOR_1` | `clock_color_1` | string | `"#ffffff"` | `"#FF0000"` | **Live** | Yes | No | Hex color string for gradient start |
| | `CLOCK_COLOR_2` | `clock_color_2` | string | `"#ffffff"` | `"#00FF00"` | **Live** | Yes | No | Hex color string for gradient end |
| | `CLOCK_FONT_PATH`| `clock_font_path`| string | `""` | `"/fonts/my.amf"`| **Live** | Yes | No | Path to custom `.amf` font on SD |
| **[IDLE]** | `ROTATION` | `rotation` | string | `"clock,date,weather,gifs,temp,decibel"` | `"clock,gifs"` | **Live** | Yes | No | Active modules (`clock`, `date`, `weather`, `gifs`, `crypto`, `stocks`, `temp`, `decibel`) |
| | `CLOCK_DURATION_SEC`|`clock_duration_sec`| int | `60` | `30` | **Live** | Yes | No | Clock display duration in seconds |
| | `GIFS_COUNT` | `gifs_count` | int | `3` | `5` | **Live** | Yes | No | Number of GIFs played per cycle |
| | `FIGHTER_ENABLED`| `fighter_enabled` | bool | `true` | `false` | **Live** | Yes | No | Enable MUGEN battle overlay |
| | `FIGHTER_INTERVAL_SEC`|`fighter_interval_sec`| int | `10` | `20` | **Live** | Yes | No | Delay in seconds between MUGEN fights |
| **[STANDBY]**| `NIGHT_MODE_ENABLED`|`night_mode_enabled`| bool | `false` | `true` | **Live** | Yes | No | Enable automatic night sleep mode |
| | `TURN_OFF_AT` | `turn_off_at` | string | `"23:00"` | `"22:30"` | **Live** | Yes | No | Turn off time (HH:MM) |
| | `WAKE_UP_AT` | `wake_up_at` | string | `"07:00"` | `"08:00"` | **Live** | Yes | No | Wake up time (HH:MM) |
| | `NIGHT_BRIGHTNESS`|`night_brightness`| int | `10` | `0` | **Live** | Yes | No | Night brightness (0 = matrix completely off) |
| **[CRYPTO]** | `ENABLED` | `crypto_enabled` | bool | `true` | `false` | **Live** | Yes | No | Enable crypto ticker |
| | `SYMBOLS` | `crypto_symbols` | string | `"BTC,ETH,SOL,DOGE"` | `"BTC,ETH"` | **Live** | Yes | No | Comma-separated crypto tickers |
| | `DURATION_SEC` | `crypto_duration_sec`| int | `5` | `10` | **Live** | Yes | No | Display duration per token in seconds |
| **[STOCK]** | `ENABLED` | `stock_enabled` | bool | `true` | `false` | **Live** | Yes | No | Enable stock ticker |
| | `SYMBOLS` | `stock_symbols` | string | `"AAPL,NVDA,TSLA,MSFT"` | `"AAPL"` | **Live** | Yes | No | Comma-separated stock tickers |
| | `DURATION_SEC` | `stock_duration_sec` | int | `5` | `10` | **Live** | Yes | No | Display duration per stock in seconds |
| **[AUDIO]** | `VISUALIZER_ENABLED` | `visualizer_enabled` | bool | `false` | `true` | **Live** | Yes | No | Enable audio visualizer mode |
| | `VISUALIZER_MODE` | `visualizer_mode` | string | `"spectrum"` | `"waveform"` | **Live** | Yes | No | Visualizer pattern (`spectrum`, `waveform`, `radial`, `neon_fire`) |
| | `MIC_GAIN` | `mic_gain` | float | `1.0` | `1.5` | **Live** | Yes | No | Microphone pre-gain multiplier |
| | `DB_CALIBRATION` | `db_calibration` | float | `0.0` | `2.5` | **Live** | Yes | No | Relative sound-level offset calibration |

---

## 📌 Important Notes

- **`ROTATION`**: The `ROTATION` string controls autonomous display modules (`clock`, `date`, `weather`, `gifs`, `crypto`, `stocks`, `temp`, `decibel`). **Note: `sprites` is NOT a rotation module**. MUGEN fighters are rendered dynamically as an overlay by `FighterEngine` via `FIGHTER_ENABLED`.
- **Audio Visualizer & Sound Level Precision**:
  - `VisualizerEngine` processes audio samples into an amplitude/energy band approximation (**pseudo-spectrum**) optimized for high-FPS LED matrix rendering rather than a physical DSP FFT analyzer.
  - `DecibelEngine` computes a **calibratable relative sound-level indicator** derived from microphone RMS energy.
- **Reboot Requirement**: Hardware geometry changes (`WIDTH`, `HEIGHT`, `DRIVER_CHIP`, `CHAIN`, `PWM_BITS`) and Wi-Fi credentials require a system reboot (`POST /api/system/reboot` or the *Reboot System* button in the Web UI). All visual parameters (themes, colors, durations, brightness) update live on the fly.
