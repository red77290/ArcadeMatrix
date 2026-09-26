🇬🇧 English | 🇫🇷 [Français](CONFIGURATION_FR.md) | 🇪🇸 [Español](CONFIGURATION_ES.md)

# Detailed Configuration (config.json) — ESP32

The configuration system relies exclusively on a single `config.json` file located on LittleFS / SD storage. It handles the matrix DMA driver, network, MQTT integration, system behaviour, display orientation, API security, and the decoupled logic of each engine ("instances").

> `config.json` is the **single source of truth**. On boot the file is validated and self-healed by `ConfigSanitizer`, so missing keys are automatically re-created with safe defaults.

---

## 1. Global Structure

```json
{
  "matrix": { ... },
  "wifi": { ... },
  "mqtt": { ... },
  "system": { ... },
  "display": { ... },
  "audio": { ... },
  "instances": [ ... ],
  "rotation": [ ... ],
  "api_auth_enabled": false,
  "api_token": ""
}
```

---

## 2. The `"matrix"` Block (Hardware Driver)

This block configures the DMA parameters for the `ESP32-HUB75-MatrixPanel-I2S-DMA` driver. Changing critical hardware values triggers an automatic driver reload.

| Key | Type | Description |
| :--- | :--- | :--- |
| `width` | `int` | Width of a single panel (e.g., `64` or `256`). |
| `height` | `int` | Height of a single panel (e.g., `32` or `64`). |
| `chain_length` | `int` | Number of panels chained horizontally. |
| `driver_chip` | `String` | Controller chip (`SHIFTREG`, `FM6126A`, `ICN2038S`, `MBI5124`, `SM16208`). |
| `rgb_sequence` | `String` | Color order (`RGB`, `RBG`, `BGR`, ...). Fix swapped colors here. |
| `color_depth` | `int` | Color depth (`1`–`8` bits). Default `8`. Lower to save DMA RAM. |
| `limit_refresh_rate_hz` | `int` | Cap the refresh rate (`0` = uncapped). |
| `row_address_mode` | `int` | Row addressing mode (`0`: Direct Binary, `1`: ShiftReg, `2`: Direct 16, `3`: Direct 32, `4`: Direct 64). |
| `clk_phase` | `bool` | Invert CLK clock phase (`false` default; set `true` if panel requires inverted clock latching). |
| `latch_blanking` | `int` | Latch blanking cycles (`0`–`8`) for ghosting/phantom line reduction. |
| `render_pipeline` | `String` | Drawing and presentation pipeline (`auto`, `canvas_single`, `canvas_double`, `direct_double`, `direct_single`). Default `auto`. Controls intermediate canvas allocation and DMA synchronization. |
| `force_single_buffer` | `bool` | Force single DMA buffer to save internal SRAM (`false` default; legacy shim mapping to `canvas_single`). |
| `rotation_offset` | `int` | Mounting orientation offset (`0`=0°, `1`=90°, `2`=180°, `3`=270°). |
| `auto_rotate` | `bool` | Enable automatic display orientation via onboard Gyroscope/IMU (`true` default). |
| `rotation_transition` | `String` | Visual transition effect (`vortex`, `glitch`, `slide`, `zoom`, `matrix`, `random`, `none`). |
| `rotation_transition_duration_ms` | `int` | Transition effect duration in milliseconds (default `400`). |

### 2.1 Rendering & Buffering Pipeline Options

ArcadeMatrix v4 introduces the Hardware-Agnostic Drawing SPI (`IDrawingSurface`), decoupling engine pixel rasterization from physical DMA controllers. You can configure the pipeline directly in `config.json` or interactively from the **System Settings → Hardware** tab in the Web UI:

- **`auto`** *(Recommended)*: Automatically resolves the optimal pipeline based on your hardware profile and memory tier:
  - **ESP32-S3 / PSRAM Boards**: Allocates an intermediate 16-bit RGB565 canvas in external PSRAM with Double DMA buffering (`canvas_double`) for maximal throughput and tear-free 60 FPS animation.
  - **Classic ESP32 (No PSRAM)**: Allocates an intermediate canvas in internal SRAM with Single DMA buffering (`canvas_single`). This frees ~16–20 KB of scarce DMA memory, preventing Wi-Fi init failures (`esp_wifi_init 4353`) while eliminating screen tearing via synchronized bulk burst encoding.
- **`canvas_single`**: 16-bit RGB565 canvas buffer + single DMA back-buffer. Halves DMA RAM requirements while using `Hub75BulkEncoder` sequential burst packing to prevent scanline tearing.
- **`canvas_double`**: 16-bit RGB565 canvas buffer + double DMA buffers. Best for multi-panel displays (e.g. 128×64, 256×64) with PSRAM.
- **`direct_double`**: Legacy direct rendering into HUB75 DMA double-buffers without an intermediate canvas.
- **`direct_single`**: Legacy direct rendering into a single DMA buffer (minimal memory footprint; may cause visible scanline tearing during redraws).

> [!NOTE]
> For backward compatibility, setting `force_single_buffer: true` automatically maps to `canvas_single` when `render_pipeline` is `auto` or unspecified.

### 2.2 Web UI Hardware Tab Integration

The Web UI (System Settings → Hardware) directly controls these parameters:
1. **Rendering & Buffering Pipeline Dropdown (`hw-render-pipeline`)**: Select between `auto`, `canvas_single`, `canvas_double`, `direct_double`, or `direct_single`.
2. **Force Single Buffer Switch (`hw-force-single-buffer`)**: Backward-compatible toggle for low-SRAM operation.
3. **Saving**: Clicking **Save Hardware Settings** (`btn-save-hw`) posts the parameters to `POST /api/system` and `POST /api/settings`, then cleanly restarts the display panel with the new pipeline.

> Live daytime brightness is **not** stored in this block; it is controlled at runtime from the Web UI (Dashboard slider → `POST /api/system { "brightness_limit": 0-100 }`). Night brightness lives in the `system` block (§4).

---

## 3. The `"wifi"` Block

| Key | Type | Description |
| :--- | :--- | :--- |
| `ssid` | `String` | The name of your Wi-Fi network. |
| `password` | `String` | The WPA2 key. |
| `hostname` | `String` | Device hostname advertised on the network. |
| `configured` | `bool` | Set to `false` to force a (re)connection attempt on next boot. Set back to `true` automatically on success. |
| `disable_internal` | `bool` | If using an external USB dongle, disable the Pi's internal Wi-Fi (changing this triggers a restart). |

You can also push credentials at runtime with `POST /api/wifi { "ssid": "...", "password": "..." }`, which sets `configured=false` and restarts the network provisioning.

---

## 4. The `"system"` Block (Environment & Standby)

| Key | Type | Description |
| :--- | :--- | :--- |
| `timezone` | `String` | POSIX string (e.g., `CET-1CEST,M3.5.0,M10.5.0/3`). |
| `format_24h` | `bool` | Time format. `true` = 23:00, `false` = 11:00 PM. |
| `lang` | `String` | System language (e.g., `en`, `fr`, `es`). |
| `temp_unit` | `String` | Temperature unit preference (`C` for Celsius, `F` for Fahrenheit). |
| `temp_offset` | `float` | Offset calibration applied to onboard/environmental sensor (in selected temperature unit). |
| `night_mode_enabled` | `bool` | Enables automatic turn-off / brightness reduction at night. |
| `turn_off_at` | `String` | Standby start time (e.g., `"23:00"`). |
| `wake_up_at` | `String` | Wake-up time (e.g., `"07:00"`). |
| `night_brightness` | `int` | Standby brightness (`0` = matrix completely off). |
| `day_brightness` | `int` | Live daytime brightness (`0`–`100`). Set from the dashboard slider and persisted across restarts. |
| `idle_fighter_enabled` | `bool` | Master switch for the decorative Fighter overlay composited on top of idle rotation screens (per-screen opt-in via each rotation entry). |
| `idle_fighter_interval` | `int` | Seconds between two fight animations (minimum `1`). |

---

## 5. The `"mqtt"` Block (Recalbox / Batocera Marquees)

| Key | Type | Description |
| :--- | :--- | :--- |
| `enabled` | `bool` | Enable the MQTT listener for Pixelcade-style marquees. |
| `broker` | `String` | Broker IP/host (usually the Pi itself). |
| `port` | `int` | Broker port (default `1883`). |
| `user` | `String` | Broker username (optional). |
| `pass` | `String` | Broker password (optional). |
| `device_name` | `String` | Identifier published by this device. |
| `allow_overlay` | `bool` | Allow decorative overlays (e.g. Street Fighter) on top of MQTT/marquee screens (default `false`). |
| *(auto-subscription)* | `system/playing/#` | Subscribes automatically to all supported retro gaming systems: `system/playing/recalbox`, `system/playing/batocera`, `system/playing/retropie`. |

The sync daemon can be installed on the console (Recalbox, Batocera, RetroPie) over SSH from the Web UI (`POST /api/mqtt/install`) with target OS selection or auto-detection, and its logs fetched with `POST /api/mqtt/logs`.

> [!NOTE]
> For Batocera, version **v33 or newer** is required for dynamic marquee browsing (`game-selected` and `system-selected` hooks). Batocera v32 and earlier only trigger game launch/stop events. Recalbox is supported on all versions.

---

## 6. API Security (`api_auth_enabled` / `api_token`)

These settings control whether write/mutating endpoints (reboots, brightness adjustments, configuration saves, engine modifications, OTA updates, and GIF uploads) require a valid secret authentication token.

| Key | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `api_auth_enabled` | `bool` | `false` | When `false`, all endpoints are accessible without authentication. When `true`, mutating endpoints require a matching token. |
| `api_token` | `String` | `""` | The secret authentication token. Write-only: never exposed by `GET` requests (which return `api_token_configured: true`). |

> [!NOTE]
> Public read endpoints (`/api/status`, `/api/metrics`, `/api/health`, `/api/i18n/current`) remain unblocked regardless of authentication state so local dashboards and monitoring tools continue to operate smoothly.

### 6.1 How to Enable or Disable Authentication

#### Option A: Via the Web Interface (Recommended)
1. Open the Web UI in your browser (`http://arcadematrix.local` or your device's IP address).
2. Navigate to the **System** tab (`sys-tab-hardware`) and locate the **API Authentication** card.
3. Set **Require Auth for API** to **Enabled**.
4. Enter your desired secret token into the **API Token** field (see below for suggestions).
5. Click **Save API Auth**. The token is saved immediately to `/config.json` on the SD card, and automatically cached in your browser's `localStorage`.
6. To **disable** authentication at any time, simply switch the selector to **Disabled** and click **Save API Auth**.

#### Option B: Via `config.json` on the SD card
Edit `/config.json` directly and configure the `system` block:
```json
{
  "system": {
    "api_auth_enabled": true,
    "api_token": "my_super_secret_token_123"
  }
}
```

### 6.2 How to Create a Secure Token
You can use any alphanumeric string as your token. For maximum security, generate a random 32-character hex token in your terminal:
```bash
# On Linux / macOS:
openssl rand -hex 16
# Example output: a3f89e2b109c4d5e7812bc34567890ef
```
Alternatively, choose any strong passphrase you can remember.

### 6.3 How to Connect to the Web UI as a New User

When API security is enabled (`api_auth_enabled: true`), a new user or a fresh browser connects seamlessly and intuitively:

1. **Free Initial Web UI Loading**:
   The web page (`GET /`, `GET /index.html`) and public read-only telemetry endpoints (clock display, hardware status, version, metrics) are always accessible without authentication. The page loads normally on any new device.

2. **Three Ways to Authenticate a New Browser**:

   * **Option 1: Automatic on First Action (Interactive Dialog)**
     As soon as the new user triggers a mutating action (adjusting brightness, switching clocks, modifying colors, saving settings), the ESP32 returns `401 Unauthorized`.
     The Web UI intercepts this 401 response and immediately presents an input prompt:
     > `API Token required or invalid. Enter your API Token:`
     The user pastes the secret token. The Web UI persists it in browser storage (`localStorage.getItem('api_token')`). All subsequent actions are automatically authorized without prompting again.

   * **Option 2: Proactive Entry in System Settings**
     Without waiting for an action to fail, the user can navigate to:
     **System** ➔ **Settings** tab ➔ **API Authentication** card.
     Enter the token into the **API Token** field and click **Save API Auth**. The token is saved in the browser and verified against the device.

   * **Option 3: Direct Link / Bookmark (*Magic Link*)**
     An administrator can share or bookmark a direct link containing the token:
     `http://arcadematrix.local/?token=my_super_secret_token_123`
     Upon loading, the Web UI automatically extracts the `token` parameter and saves it to `localStorage`. The user is immediately authenticated with zero popup dialogs.

* **Write-Only Security**: The ESP32 never transmits the secret token back over `GET /api/system` or `GET /api/settings` (it returns `"api_token_configured": true`), completely protecting the secret from local network inspection.
* **Updating or Clearing the Token**: Entering a new valid token in the prompt or Settings tab overrides the old value. Clearing the token and saving turns off authentication.

### 6.4 Calling the REST API from External Scripts & Home Assistant
When `api_auth_enabled` is `true`, external callers can authenticate using either standard headers or a URL query parameter:

1. **Recommended: `X-API-Token` Header**
   ```bash
   curl -X POST http://arcadematrix.local/api/system/brightness \
     -H "Content-Type: application/json" \
     -H "X-API-Token: my_super_secret_token_123" \
     -d '{"brightness": 80}'
   ```

2. **Standard Bearer Header (`Authorization`)**
   ```bash
   curl -X POST http://arcadematrix.local/api/system/restart \
     -H "Authorization: Bearer my_super_secret_token_123"
   ```

3. **Query Parameter (Quick Testing / Automation Shortcuts)**
   ```bash
   curl -X POST "http://arcadematrix.local/api/system/restart?token=my_super_secret_token_123"
   ```

### 6.5 Anti-Lockout Protection
To prevent users from being permanently locked out of their devices:
* If `api_auth_enabled` is set to `true` but `api_token` is empty (`""`), the firmware treats authentication as inactive and allows access.
* Timing attacks against token verification are prevented using constant-time comparison (`TimingSafe::compare`).

---


## 7. Engines: `"instances"` & `"rotation"`

The decoupled architecture lets you create multiple independent, differently-configured copies of the same Engine.

### `"instances"`
An array holding the configuration of each logical block.

```json
{
  "instance_id": "crypto_main",
  "engine_id": "crypto",
  "config": {
    "symbols": "BTC,ETH,SOL"
  }
}
```
* `instance_id`: Unique name of this block.
* `engine_id`: The internal identifier of the Rust Engine (must be a registered engine — see §9).
* `config`: A flat map of `String` values specific to the engine, validated against its `ConfigSchema`.

Editing an instance through the Web UI (`POST /api/instances`) is applied **live, without a restart**: the runtime calls the engine's `on_config_changed()` on the next frame (Lazy-Once hot-reload). Adding or removing an instance resets the rotation cleanly.

### `"rotation"`
Defines the display order, per-slot duration, and transverse overlay toggles.

```json
{
  "instance_id": "crypto_main",
  "duration_sec": 30,
  "overlays": {
    "fighter": true
  }
}
```
* `instance_id`: Target engine instance.
* `duration_sec`: Dwell duration in seconds (or playback budget for self-paced engines like GIF).
* `overlays.fighter`: (`bool`) Granular toggle for the M.U.G.E.N Fighter decorative overlay on this specific screen.

Only instances listed here are ever initialized, saving memory for unused features. The rotation is editable from the Web UI **Rotation** panel (`GET`/`POST /api/rotation`).

> **Human-Readable Persistence**: When saved to disk (`config.json`), the file is always written formatted and indented (`to_string_pretty`) so it can be cleanly inspected and edited by hand without breaking.

---

## 8. Self-Healing Validation

On every boot **and** on every write via `POST /api/instances`, the `ConfigSanitizer` reconciles each instance against its engine `ConfigSchema`:

* **Missing key** → the schema `default_value` is injected.
* **Integer / Float** → parsed and, if out of `min`/`max`, clamped or reset to default (per the field's `validation_policy`).
* **Boolean** → normalized (`true/1/yes/on` → `true`, `false/0/no/off` → `false`); an unparseable value falls back to the default.
* **Options** → the value must be one of the declared options (comma-separated list for multi-select); otherwise it falls back to the default.
* **Obsolete keys** → keys no longer present in the schema (e.g. after an OTA that renamed a field) are pruned.

The result is saved atomically, so an OTA that adds a new field self-populates it without any user intervention.

---

## 9. Engine Configurations

Each engine advertises its own fields through its `ConfigSchema` (discoverable at `GET /api/engines`, which is what powers the dynamic Web UI). The most common engines:

### Engine: `clock`
| Field | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `theme` | `int` | `0` | Animated clock theme index: `0` = Digital Standard, `1` = Flip Clock, `2` = Cyberpunk, `3` = Word Clock, `4` = Binary Clock, `5` = Pac-Man, `6` = Tetris, `7` = Slot Machine, `8` = Versus (M.U.G.E.N), `9` = Pong, `10` = Matrix Rain (Katakana). |
| `format` | `String` | `%H:%M:%S` | strftime time format. |
| `font` | `String` | `PressStart2P.ttf` | Font file from `/fonts/`. |
| `size` | `int` | `2` | Font scaling factor. |
| `color_1` | `String` | `#FFFFFF` | Primary hex color (gradient start on Custom theme). |
| `color_2` | `String` | `#FFFFFF` | Secondary hex color (gradient end on Custom theme). |
| `offset_x` | `int` | `0` | Horizontal pixel offset. |
| `offset_y` | `int` | `0` | Vertical pixel offset. |

### Engine: `date`
| Field | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `theme` | `int` | `0` | Date theme index. |
| `format` | `String` | `%d/%m` | strftime date format. |
| `font` | `String` | `PressStart2P.ttf` | Font file from `/fonts/`. |
| `size` | `int` | `2` | Font scaling factor. |
| `color_1` | `String` | `#FFFFFF` | Primary hex color. |
| `color_2` | `String` | `#FFFFFF` | Secondary hex color. |
| `offset_x` | `int` | `0` | Horizontal pixel offset. |
| `offset_y` | `int` | `0` | Vertical pixel offset. |

### Engine: `crypto`
| Field | Type | Default | Options | Description |
| :--- | :--- | :--- | :--- | :--- |
| `symbols` | `String` | `BTC,ETH` | Comma-separated | Crypto symbols to monitor (CoinGecko / Binance). |
| `show_chart` | `bool` | `true` | `true`, `false` | Display historical price sparkline chart. |
| `chart_timeframe` | `Options` | `daily` | `hourly`, `daily`, `weekly`, `monthly` | Timeframe for historical price series. |
| `page_seconds` | `int` | `5` | `3` to `30` | Seconds to dwell on each page. |
| `cache_ttl_min` | `int` | `1` | `1` to `60` | Minutes to cache quote price. |

### Engine: `stock`
| Field | Type | Default | Options | Description |
| :--- | :--- | :--- | :--- | :--- |
| `symbols` | `String` | `AAPL,NVDA,TSLA` | Comma-separated | Stock ticker symbols to monitor (Yahoo Finance). |
| `show_chart` | `bool` | `true` | `true`, `false` | Display historical price sparkline chart. |
| `chart_timeframe` | `Options` | `daily` | `hourly`, `daily`, `weekly`, `monthly` | Timeframe for historical price series. |
| `page_seconds` | `int` | `5` | `3` to `30` | Seconds to dwell on each page. |
| `cache_ttl_min` | `int` | `1` | `1` to `60` | Minutes to cache quote price. |

### Engine: `weather`
| Field | Type | Default | Options | Description |
| :--- | :--- | :--- | :--- | :--- |
| `api_key` | `String` | `""` | Free API key | Your OpenWeatherMap API Key (free tier at [openweathermap.org](https://home.openweathermap.org/users/sign_up)). |
| `city` | `String` | `""` | Text | City location (see formatting guide below). |
| `units` | `Options` | `metric` | `metric`, `imperial` | Temperature unit: `metric` for Celsius (°C) or `imperial` for Fahrenheit (°F). |
| `lang` | `Options` | `en` | `en`, `fr`, `es` | Language code for day labels (TODAY / AUJ. / HOY). |
| `offset_x` | `int` | `0` | `-64` to `64` | Horizontal pixel shift. |
| `offset_y` | `int` | `0` | `-32` to `32` | Vertical pixel shift. |

#### How to Format the `city` Field on OpenWeatherMap
OpenWeatherMap uses the ISO 3166 country code (and 2-letter state code for the US) to disambiguate locations:
* **International Locations:** Use `City,CountryCode` (e.g. `Paris,FR`, `London,GB`, `Tokyo,JP`, `Montreal,CA`).
* **United States Locations:** Use `City,StateCode,CountryCode` (e.g. `Tucson,AZ,US`, `Miami,FL,US`, `Dallas,TX,US`). Specifying only the city or omitting the country may return an incorrect city with the same name.
* **Where to Look:** Go to [openweathermap.org](https://openweathermap.org), search for your city. The top search result header and URL show the exact `City,State,Country` string recognized by the API.

### Engine: `sysinfo` (System Monitor)
| Field | Type | Default | Options | Description |
| :--- | :--- | :--- | :--- | :--- |
| `theme` | `int` | `0` | `0` to `2` | Visual theme: `0` = Color Gauge Bars, `1` = Compact 4-Block Grid, `2` = Retro Terminal. |
| `show_cpu` | `bool` | `true` | `true`, `false` | Display real-time CPU usage percentage (CPU %). |
| `show_ram` | `bool` | `true` | `true`, `false` | Display real-time RAM usage percentage (RAM %). |
| `show_temp` | `bool` | `true` | `true`, `false` | Display hardware SoC temperature (dynamic green/amber/red color scale). |
| `show_uptime` | `bool` | `true` | `true`, `false` | Display system Uptime in hours/days. |
| `temp_unit` | `Options` | `C` | `C`, `F` | Temperature unit: Celsius (`C`) or Fahrenheit (`F`). |
| `offset_x` | `int` | `0` | `-64` to `64` | Horizontal pixel offset. |
| `offset_y` | `int` | `0` | `-32` to `32` | Vertical pixel offset. |

### Engine: `gnews` (GNews Live Feed & Breaking News Ticker)

The `gnews` engine provides a real-time live news ticker and breaking news bulletin powered by the [GNews.io](https://gnews.io) API. It features multi-account API pooling, automated failover, persistent SD/disk caching, and smart quota budgeting to maximize free-tier usage.

| Field | Type | Default | Options | Description |
| :--- | :--- | :--- | :--- | :--- |
| `api_key` | `String` | `""` | Comma-separated keys | GNews.io API keys (supports multiple keys: `key1,key2,key3` for multi-account pool with automatic failover). |
| `category` | `Options` | `technology` | `general`, `world`, `nation`, `business`, `technology`, `entertainment`, `sports`, `science`, `health` | Primary news category or comma-separated list for round-robin rotation. |
| `keywords` | `String` | `""` | Text / Query | Custom search query or filter tags (e.g. `ai OR arcade`). |
| `lang` | `Options` | `auto` | `auto`, `en`, `fr`, `es`, `de`, `it`, `pt`, `nl`, `ru`, `zh`, `ja` | Article language (`auto` syncs with system language). |
| `country` | `Options` | `auto` | `auto`, `us`, `fr`, `gb`, `es`, `de`, `ca`, `it`, `jp`, `au`, `br`, `in` | Country edition (`auto` uses local region). |
| `max_articles` | `int` | `5` | `3` to `15` | Maximum number of headlines cached and rotated per cycle. |
| `requests_per_day` | `int` | `10` | `1` to `100` | Total API requests allocated per 24 hours (Free tier limit: 100/day per API key). |
| `force_refresh` | `bool` | `false` | `true`, `false` | Action trigger: immediately purges obsolete language cache and queries the API without resetting daily quota counters. |
| `cache_ttl_min` | `int` | `30` | `5` to `120` | Minimum minutes between API queries when request budgeting is relaxed. |
| `display_mode` | `Options` | `smooth_scroll` | `smooth_scroll`, `vertical_crawl`, `static_paged`, `serpentine` | Animation style: smooth horizontal ticker, vertical crawl, multi-line static paged, or alternating serpentine flow. |
| `scroll_speed` | `int` | `3` | `1` to `10` | Ticker scrolling speed (1: Slow crawl to 10: Turbo speed). |
| `scroll_pause_start_ms` | `int` | `1200` | `0` to `4000` | Initial pause dwell time (ms) at headline start before scrolling. |
| `scroll_pause_end_ms` | `int` | `1000` | `0` to `4000` | Pause dwell time (ms) at end of headline before transitioning. |
| `article_duration_sec` | `int` | `12` | `5` to `60` | Display duration per article in seconds. |
| `theme` | `Options` | `category_dynamic` | `category_dynamic`, `breaking_crimson`, `cyberpunk`, `monochrome_paper` | Color palette scheme. |
| `show_category_badge` | `bool` | `true` | `true`, `false` | Display color-coded category pill (`[TECH]`, `[WORLD]`, etc.). |
| `show_source` | `bool` | `true` | `true`, `false` | Display news source name badge (`BBC News`, `Reuters`, etc.). |
| `show_time_ago` | `bool` | `true` | `true`, `false` | Display relative time badge (`5m ago`, `2h ago`). |
| `show_beacon` | `bool` | `true` | `true`, `false` | Display pulsing live broadcast beacon dot. |
| `show_progress_dots` | `bool` | `true` | `true`, `false` | Display headline index dots (`● ○ ○ ○ ○`). |

#### GNews Architecture & Quota Optimization
1. **Multi-API Key Pooling & Automatic Failover:**
   - You can enter multiple GNews API keys separated by commas (`api_key: "key1,key2,key3"`).
   - If an active key is invalid (`HTTP 401/403`) or exhausts its 100 requests/day quota (`HTTP 429/403`), the engine automatically fails over to the next key in the pool and immediately retries.
   - 2 accounts = 200 requests/day; 3 accounts = 300 requests/day.
2. **Persistent File Caching (`/gnews_cache.json` on ESP32 SD, `gnews_cache.json` on RPi):**
   - Articles and request telemetry are persisted to storage. On reboot, headlines display immediately without burning API quota or stalling for network.
   - If offline or when the daily quota is reached, cached articles are preserved indefinitely and continue scrolling 24/7.
3. **Daily Quota Budgeting (Default: 10 reqs/day) & Shared-Key Protection:**
   - While GNews.io free accounts permit up to 100 requests/day per key, users frequently share their API key across other external services or home automations.
   - To prevent ArcadeMatrix from monopolizing or exhausting the account's external quota, the engine defaults to a conservative **10 requests per day** (`requests_per_day: 10`, spaced evenly every 2 hours 24 minutes: $\Delta t = \frac{86400}{10} = 8640\text{ s}$).
   - Users can freely customize `requests_per_day` (from `1` to `100`). The Web UI quota telemetry dynamically reports consumption against this configured budget (e.g. `Key 1 (..abcd): 4/10 reqs [Active]`).
   - If multiple categories are specified (e.g. `technology,world`), queries alternate in round-robin across categories ($\frac{\text{requests\_per\_day}}{N}$ per topic).
4. **Deferred Updates & Force Refresh:**
   - Changing parameters in the Web UI takes effect at the next scheduled query cycle to protect the quota.
   - Setting `force_refresh: true` purges stale articles of previous languages and triggers an immediate fetch while maintaining daily usage counters.
5. **Midnight UTC Rollover:**
   - At 00:00 UTC, daily quota consumption counters reset to 0 and rate-limited status flags are automatically cleared.

### Engine: `fighter` (M.U.G.E.N Combat)
| Field | Type | Default | Options | Description |
| :--- | :--- | :--- | :--- | :--- |
| `mode` | `Options` | `match` | `match`, `showcase` | Battle mode: `match` (full battle with K.O. & victory) or `showcase` (continuous demonstration). |
| `fighter_1` | `String` | `""` | Character folder name | P1 Fighter (leave blank for random selection). |
| `fighter_2` | `String` | `""` | Character folder name | P2 Fighter (leave blank for random selection). |
| `show_hud` | `bool` | `true` | `true`, `false` | Display retro HP health bars, Super gauges, and fighter names. |
| `match_duration` | `int` | `30` | `10` to `120` | Maximum round duration in seconds before timeout. |

### Engine: `google_cast` (Google Home / Nest Audio)
| Field | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `device_ip` | `String` | `""` | Static IP of your Google Home / Nest Audio speaker. Leave blank for automatic mDNS local discovery. |
| `device_name` | `String` | `""` | Device name filter (e.g. `Living Room`) when automatically scanning your LAN. |
| `show_album_art` | `bool` | `true` | Downloads and displays album artwork on the left of the LED matrix. |
| `show_progress` | `bool` | `true` | Displays real-time playback progress bar at the bottom. |
| `show_visualizer` | `bool` | `true` | Displays animated audio frequency equalizer during playback. |
| `show_volume` | `bool` | `true` | Displays current Google Nest speaker volume level. |

### Engine: `spotify` (Spotify Official Player)
| Field | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `client_id` | `String` | `""` | Your Spotify Developer API Client ID. |
| `client_secret` | `String` | `""` | Your Spotify Developer API Client Secret (optional for PKCE). |
| `refresh_token` | `String` | `""` | Your Spotify OAuth2 Refresh Token for seamless ongoing playback sync. |
| `show_album_art` | `bool` | `true` | Downloads and displays full-color Spotify album artwork. |
| `show_progress` | `bool` | `true` | Displays track playback time progress bar at the bottom. |
| `show_visualizer` | `bool` | `true` | Displays animated audio equalizer during playback. |
| `show_volume` | `bool` | `true` | Displays active Spotify volume percentage. |

### Engine: `gifs`
| Field | Type | Default | Options | Description |
| :--- | :--- | :--- | :--- | :--- |
| `playlists` | `String` (Multi) | `""` | Options from `/api/playlists` | Active GIF playlists or folders to cycle through (comma-separated). |

### Engine: `message`
| Field | Type | Default | Options | Description |
| :--- | :--- | :--- | :--- | :--- |
| `text` | `String` | `Hello` | Text | The text banner or message to display. |
| `color` | `String` | `#ffffff` | Hex Color | Text color in `#RRGGBB` format. |
| `size` | `int` | `1` | `1` to `4` | Font scaling multiplier. |
| `direction` | `Options` | `left` | `left`, `none` | Scroll direction (`left` for leftward scrolling, `none` for centered static text). |
| `speed` | `int` | `50` | `10` to `200` | Milliseconds per scroll step (lower is faster; ignored when static). |
| `font` | `String` | `Default` | Dynamic | Font file from `/fonts/`. |

### Engine: `dashboard` (Master Desk Deck & Multi-Widget Hub)
| Field | Type | Default | Options | Description |
| :--- | :--- | :--- | :--- | :--- |
| `clock_mode` | `Options` | `1` | `0:Digital Modern, 1:Pixel-Art Watch Dial, 2:Minimal` | Clock display style. |
| `theme` | `Options` | `0` | `0:Cyberpunk Neon, 1:Arcade Amber HUD, 2:Minimalist Luxury, 3:Matrix Phosphor` | Color palette for widgets and watch bezels. |
| `show_clock` | `bool` | `true` | `true`, `false` | Display main clock widget. |
| `show_world_clock` | `bool` | `true` | `true`, `false` | Display world clocks badge. |
| `world_clocks` | `String` (Multi) | `NYC,TYO,LON` | Preset tags / Free text | Comma-separated list of airport/city codes (`NYC`, `TYO`, `LON`, `PAR`, `LAX`, `SFO`, `DXB`, `SIN`, `HKG`, `SYD`, `BER`, `ROM`, `MAD`, `AMS`, `YUL`, `UTC`) or custom offsets (`REU:+4`, `NYC:-4`). |
| `show_weather` | `bool` | `true` | `true`, `false` | Display outdoor weather and temperature. |
| `weather_city` | `String` | `Paris` | Text | City for weather forecasts. |
| `weather_api_key` | `String` | `""` | Optional API Key | OpenWeatherMap API key (leave blank to automatically use free Open-Meteo service without API key). |
| `show_indoor_temp` | `bool` | `true` | `true`, `false` | Display onboard SHTC3 indoor room temperature and humidity. |
| `temp_unit` | `Options` | `system` | `system:System (General), C:Celsius (°C), F:Fahrenheit (°F)` | Temperature display unit. |
| `temp_offset` | `float` | `""` | `-30.0` to `30.0` | Calibration offset to compensate for CPU heat dissipation (leave empty for General System setting). |
| `refresh_interval` | `Options` | `10` | `1`, `5`, `10`, `15`, `30`, `60` min | Data refresh frequency for weather and markets. |
| `format_24h` | `Options` | `system` | `system:System (General), 24h:24 Hours, 12h:12 Hours` | 24-hour vs 12-hour AM/PM time display. |
| `lang` | `Options` | `system` | `system:System (General), fr:Français, en:English, es:Español` | Language for weather descriptions and widget labels (`system` syncs with general system language). |
| `show_markets` | `bool` | `true` | `true`, `false` | Display crypto and stock market ticker badges. |
| `tracked_markets` | `String` (Multi) | `BTC,ETH,SOL,NVDA` | Top 20 tags / Free text | Cryptos via Binance API (`BTC`, `ETH`, `SOL`, `DOGE`, `XRP`, `PEPE`, `KAS`, `TAO`, `SUI`...) and Stocks/ETFs via Yahoo Finance (`NVDA`, `AAPL`, `TSLA`, `MSFT`, `GOOG`, `AMZN`, `SPY`, `QQQ`, `PLTR`, `MSTR`...). |
| `show_sysinfo` | `bool` | `true` | `true`, `false` | Display RAM, CPU & WiFi vitals gauge. |
| `show_date` | `bool` | `true` | `true`, `false` | Display day and date badge. |
| `show_seconds` | `bool` | `true` | `true`, `false` | Display sweeping second hand or seconds digits. |
| `smooth_seconds` | `bool` | `true` | `true`, `false` | Continuous sweeping second hand vs crisp 1s ticks. |
| `offset_x` | `int` | `0` | `-64` to `64` | Horizontal pixel offset. |
| `offset_y` | `int` | `0` | `-32` to `32` | Vertical pixel offset. |

> **Adaptive Auto-Scaling**: When widgets are disabled, remaining widgets dynamically expand horizontally and vertically to take 100% of the display without dead space or black borders.

### Engine: `visualizer` (Live Audio Equalizer)
| Field | Type | Default | Options | Description |
| :--- | :--- | :--- | :--- | :--- |
| `style` | `Options` | `spectrum` | `spectrum`, `waveform`, `radial`, `neon_fire` | Visualizer rendering mode: Spectrum Bars, Oscilloscope Waveform, Radial Circular, or Neon Fire. |
| `gain` | `int` | `24` | `0` to `30` | Microphone hardware gain in dB (ES7210 codec). |
| `color_theme` | `Options` | `rainbow` | `rainbow`, `neon`, `fire`, `matrix` | Color gradient for audio bars and waves. |

### Engine: `decibel` (Sound Level Meter)
| Field | Type | Default | Options | Description |
| :--- | :--- | :--- | :--- | :--- |
| `alert_threshold_db` | `int` | `85` | `40` to `120` | Sound pressure level threshold for visual warning. |
| `show_peak` | `bool` | `true` | `true`, `false` | Display peak hold indicator. |

### Engine: `temp` (Indoor Climate Monitor)
| Field | Type | Default | Options | Description |
| :--- | :--- | :--- | :--- | :--- |
| `show_humidity` | `bool` | `true` | `true`, `false` | Display relative humidity percentage. |
| `temp_unit` | `Options` | `C` | `C`, `F` | Celsius or Fahrenheit display. |
| `temp_offset` | `float` | `-3.5` | `-30.0` to `30.0` | Calibration offset (in chosen temperature unit). |

### Engine: `marquee`
| Field | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| *(auto)* | `None` | — | Internal Pixelcade/Recalbox/Batocera marquee sync engine. Displays scraped game box-art and marquees received via MQTT / Webhook. |

---

## 10. Modular Storage Architecture & Working-Set Cache

ArcadeMatrix v4 decouples configuration persistence from physical SD card hardware through the `IConfigStorage` abstraction layer:

```text
 ┌─────────────────────────────────────────────────────────────┐
 │                      ConfigLoader                           │
 └──────────────┬───────────────────────────────┬──────────────┘
                │                               │
                ▼                               ▼
 ┌─────────────────────────────┐ ┌─────────────────────────────┐
 │      WorkingSetCache        │ │       IConfigStorage        │
 │                             │ │                             │
 │ • Dirty bit tracking        │ │ • SdConfigStorage (Hardware)│
 │ • In-RAM atomic mutations   │ │ • MemoryConfigStorage (Mock)│
 │ • Zero Core 1 FS blocking   │ │ • Atomic rename semantics   │
 └─────────────────────────────┘ └─────────────────────────────┘
```

1. **`IConfigStorage` Interface**: Abstract filesystem backend supporting atomic string write (`writeStringAtomic`), streaming read, file existence checks, and recursive directory listing.
2. **`SdConfigStorage`**: Production hardware backend managing SdFat with hardware SPI locking (`SdLockGuard`), writing to temporary files (`.tmp`) followed by atomic rename to eliminate file corruption during abrupt power cuts.
3. **`MemoryConfigStorage`**: In-memory heap/RAM backend used for hermetic off-target unit testing (`test_core`), simulation, and diskless operation.
4. **`WorkingSetCache`**: Core 0 memory-backed dirty state tracker. Mutations (such as Web UI saves or MQTT updates) immediately update the working-set cache in RAM and publish atomic snapshots to Core 1 without waiting on slow SD card I/O. Asynchronous sync flushes dirty files to permanent storage in the background.

---

*Note: All schemas can also be queried dynamically in JSON format from the running system at `GET /api/engines`.*
