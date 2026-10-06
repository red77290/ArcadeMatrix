🇬🇧 English | 🇫🇷 [Français](MQTT_DATA_CONTRACT_FR.md) | 🇪🇸 [Español](MQTT_DATA_CONTRACT_ES.md)

# MQTT Data contract

The `mqttdata` engine ("MQTT Data") shows pages that any MQTT publisher sends: a home automation system, a script,
Node-RED. This document is the protocol between the publisher and the sign. For Home Assistant, ready-made
blueprints implement it: see [HOME_ASSISTANT.md](HOME_ASSISTANT.md).

## 1. How the sign uses MQTT

- The broker is set once in *System → MQTT & API → MQTT Data* (`data_mqtt`: broker, port, user, pass). It is
  separate from the arcade `mqtt` link, which can take over the display.
- An MQTT Data screen (an instance of the `mqttdata` engine) lists **topics**, comma-separated, one **page** per
  topic, shown in that order. Nothing else is configured on the sign: everything else comes from the payloads.
- **Nothing runs in the background.** When the screen comes up in the rotation, the sign connects, subscribes to
  its topics, draws what the broker hands over, and keeps the pages live while it is on screen. When the screen
  ends, it disconnects and releases all its memory.
- So the publisher **MUST publish with `retain: true`**: the sign only sees the last retained message of each topic
  (a snapshot) plus whatever arrives while the screen is up.
- An **empty retained message** (zero-length payload, MQTT's way of deleting a retained message) clears the page:
  it shows `NO DATA`.
- The client id is `<wifi hostname>-data`, so several signs can share one broker.

## 2. Common fields

Every payload is one JSON object.

| Field | Type | Required | Default | Description |
| :--- | :--- | :--- | :--- | :--- |
| `type` | string | **yes** | — | `value`, `table`, `graph` or `weather`: selects the renderer. |
| `v` | integer | no | `1` | Contract version the publisher follows (see section 5). |
| `seconds` | integer | no | `10` | Time on this page, 3 to 3600. For `weather`, applies to each of its pages. |

Text is drawn in an ASCII pixel font: `°` is supported, other non-ASCII characters show as `?`.
Colours are `"#RRGGBB"`.

### Time on screen

The MQTT Data screen sets its own time on screen: one full cycle of its pages, i.e. the sum of their `seconds`
(a `weather` topic counts one page for NOW plus one per forecast day). A page without a payload yet counts 10 s.
The rotation slot's own duration is ignored (the engine is self-paced, like the GIF player). Pages cycle in topic
order and every activation starts at the first page.

## 3. Page types

### `value`: one value

```json
{"type":"value","value":"21.7","unit":"°C","label":"OUTSIDE","color":"#40C0FF"}
```

| Field | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `value` | string (or number) | — (required) | Preformatted value; the sign never does arithmetic. |
| `unit` | string | `""` | Drawn after the value in the label colour, smaller. |
| `label` | string | `""` | Drawn above the value. |
| `label_short` | string | `""` | Used when `label` does not fit. |
| `color` | colour | `#FFFFFF` | Value colour. |
| `label_color` | colour | `#808080` | Label and unit colour. |

Drawn exactly like a one-tile `table` without a title: the largest font that fits.

### `table`: up to 4 values

```json
{"type":"table","title":"HOME","show_title":true,
 "tiles":[{"label":"DOWNSTAIRS","label_short":"DN","value":"76","unit":"°F","color":"#40C0FF"},
          {"label":"UPSTAIRS","value":"77","unit":"°F"}]}
```

| Field | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `title` | string | `""` | Title row, shown with 1 to 3 tiles when there is room (never with 4). |
| `show_title` | boolean | `true` | `false` hides the title. |
| `tiles` | array | — (required, 1 to 4) | Each tile has the fields of a `value` page (`value`, `unit`, `label`, `label_short`, `color`, `label_color`). Extra tiles are ignored. |

1 to 3 tiles sit in columns, 4 in a 2x2 grid. Each tile is stacked (label on top, value and unit under it) and
centred. The full `label` is used when it fits, else `label_short`, then a smaller font, then the label is cut. A
value is never cut: its unit is dropped first, then it is drawn in the smallest font.

### `graph`: bars and lines over time

```json
{"type":"graph","title":"DOWNSTAIRS COOL","title_short":"DN COOL","summary":"76°","summary_color":"#40A0FF",
 "slots":96,"min":68,"max":90,"yaxis":true,
 "series":[{"label":"OUTSIDE","style":"line","color":"#FF4020","data":[75.0,74.8,null,74.1]},
           {"label":"INSIDE","style":"line","color":"#40A0FF","data":[76.0,76.2,76.1,75.9]}],
 "bands":[{"from":40,"to":43,"color":"#183860"}],
 "marks":[{"at":19,"color":"#404040"}],
 "legend":[{"text":"OUTSIDE","color":"#FF4020"},{"text":"INSIDE","color":"#40A0FF"}]}
```

| Field | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `title` / `title_short` | string | `""` | Header title; the short one is used only when the full one does not fit. |
| `summary` | string | `""` | Right side of the header (e.g. the latest value). |
| `summary_color` | colour | first series' colour | Summary colour. |
| `show_header` | boolean | `true` | `false` hides the header; the plot uses the whole height. |
| `series` | array | — (required) | Up to 4: `label`, `color`, `style` (`"bar"` default or `"line"`), `data` (numbers; `null` = gap in a line, 0 in a bar). |
| `slots` | integer | longest series | X positions; data is **right-aligned** (last point = newest = rightmost). |
| `min` / `max` | number | automatic | Y range; `max` absent or `<= min` = automatic (lines: data range padded by 5%, at least 0.5). |
| `stack` | boolean | `true` | Bar series stacked (positive and negative stacks apart) or overlaid. |
| `yaxis` | boolean | `false` | Range ends printed at the left (panels at least 64 px tall). |
| `bands` | array | `[]` | Background ranges `{from, to, color}` in slot indices `[from, to)`; up to 96, newest kept. |
| `marks` | array | `[]` | Dotted vertical lines `{at, color}`; up to 8. |
| `legend` | array | `[]` | Up to 4 `{text, color}` drawn under the plot on panels at least 64 px tall; items that do not fit are dropped from the end. |
| `unit` | string | `""` | Informational. |

Bars grow from the zero line (negative values go down); a dim zero line shows when 0 is inside the range. Lines are
1 px with vertical joins and are drawn after the bars.

### `weather`: now and forecast

```json
{"type":"weather","units":"imperial","seconds":8,
 "current":{"temp":75,"condition":"clear-night","humidity":47,"wind":7,"wind_unit":"mph","wind_dir":"NE"},
 "days":[{"temp_max":89,"temp_min":70,"condition":"sunny","precip_prob":0},
         {"temp_max":90,"temp_min":71,"condition":"partlycloudy","precip_prob":5}]}
```

| Field | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `units` | string | `"metric"` | `"imperial"` = °F, `"metric"` = °C. Numbers are shown as sent, never converted. |
| `current.temp` | number | — | Live reading; makes the NOW page. |
| `current.condition` | string | `""` | Condition id (list below). |
| `current.humidity` | integer | — | Percent. |
| `current.wind` / `wind_unit` / `wind_dir` | number / string / string | — | Speed, unit, 8-point direction (`"NE"`); drawn as `"47%  NE 9mph"`. |
| `days[]` | array | `[]` | Up to 5: `temp_max`, `temp_min`, `condition`, `precip_prob`. Day 0 is today. |

Pages: NOW first (when `current.temp` is present), then one page per day, each for `seconds`. In °F the high is
shown above the low (US convention). Condition ids: `sunny`, `clear-night`, `partlycloudy`, `cloudy`, `fog`,
`rainy`, `pouring`, `lightning`, `lightning-rainy`, `snowy`, `snowy-rainy`, `hail`, `windy`, `windy-variant`,
`exceptional`; an unknown id is shown as received.

## 4. Limits

| | ESP32-S3 with PSRAM (e.g. Waveshare ESP32-S3 Matrix) | ESP32 without PSRAM (`esp32dev`) |
| :--- | :--- | :--- |
| Largest payload | 8192 bytes | 4096 bytes |
| Graph points kept per series (newest win) | 288 | 96 |
| Topics (pages) per MQTT Data screen | 6 | 4 |
| Series / bands / marks / legend items | 4 / 96 / 8 / 4 | 4 / 96 / 8 / 4 |
| Table tiles / weather days | 4 / 5 | 4 / 5 |

Text fields are cut to fit their buffers: titles 23 characters, tile labels 23, values 15, units 7.
A payload over the limit is not drawn: the page shows `UNSUPPORTED`, or `NO DATA` when it is too large for the MQTT client to receive at all.

## 5. Versioning and forward compatibility

- `v` is the contract version the publisher follows; this firmware implements version 1.
- Unknown fields are ignored. A payload with a higher `v` is still drawn with the fields this firmware knows.
- New page types will get new `type` names; a sign that does not know a type shows `UNSUPPORTED` for that page only.

## 6. Notices

| Notice | When |
| :--- | :--- |
| `CONNECTING` | The screen is up, the broker is not connected yet and no page has arrived. |
| `NO CONNECTION` | The broker cannot be reached (wrong address, refused login) and no page has arrived. |
| `NO DATA` | No retained message on that topic 3 s after connecting, or an empty retained message. |
| `UNSUPPORTED` | `type` missing or unknown, invalid JSON, a payload over the limit, or content the type cannot draw. Shown for that page's `seconds` (default 10); other pages are unaffected. |

Notices are localized (EN / FR / ES) with the sign's language.
