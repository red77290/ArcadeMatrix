🇬🇧 English | 🇫🇷 [Français](HOME_ASSISTANT_FR.md) | 🇪🇸 [Español](HOME_ASSISTANT_ES.md)

# Home Assistant integration

Home Assistant can feed the sign's **MQTT Data** screens: values, tables, 24 h graphs and your own weather station.
Home Assistant does the work (history, formatting); the sign just draws what it receives. The protocol is described in
[MQTT_DATA_CONTRACT.md](MQTT_DATA_CONTRACT.md); the blueprints below implement it, so you do not need to write any
templates.

## 1. Broker

1. Install the **Mosquitto broker** add-on (Settings → Add-ons) and the **MQTT** integration if you have not already.
2. Create a Home Assistant user for the sign (Settings → People → Users, "Can only log in from the local network"),
   for example `arcadematrix`. Mosquitto accepts Home Assistant users as MQTT logins.
3. On the sign: *System → MQTT & API → MQTT Data*: broker = your Home Assistant address, port `1883`, that user and
   password. Save. Nothing connects until an MQTT Data screen is shown.

## 2. Import the blueprints

The blueprints are in [`tools/home_assistant/blueprints/`](../tools/home_assistant/blueprints/):

| Blueprint | Kind | Publishes |
| :--- | :--- | :--- |
| `arcadematrix_value.yaml` | automation | one entity as a `value` page |
| `arcadematrix_table.yaml` | automation | up to 4 entities as a `table` page |
| `arcadematrix_weather.yaml` | automation | a weather entity (now + forecast) as a `weather` page |
| `arcadematrix_graph_history.yaml` | **template** | a sensor holding 24 h of history (96 x 15 min) of one or two sensors |
| `arcadematrix_graph.yaml` | automation | that history as a `graph` page |

Import each one with Settings → Automations & Scenes → Blueprints → **Import Blueprint**, pasting the file's
GitHub URL (`https://github.com/red77290/ArcadeMatrix/blob/main/tools/home_assistant/blueprints/<file>`). You can also
copy the automation blueprints to `config/blueprints/automation/arcadematrix/` and the template blueprint to
`config/blueprints/template/arcadematrix/`, then reload.

Every blueprint publishes **retained** messages (the sign only reads what the broker keeps) and republishes when
Home Assistant starts.

## 3. Value, table and weather pages

Create an automation from the blueprint (Settings → Automations & Scenes → Blueprints → the blueprint → Create
automation), pick the entities and choose an **MQTT topic**, for example `arcadematrix/value/outside`. The page is
published right away and again whenever the entities change (weather also every 30 minutes).

The table blueprint has an optional **Short labels** input: comma-separated, in the same order as the entities (for
example `DOWN, UP, OUT`). A small panel uses them when a full label does not fit; larger panels keep the full words.

## 4. Graph pages (two blueprints)

A graph needs 24 hours of samples, which Home Assistant keeps in a sensor made by the **template** blueprint; the
**graph** automation then publishes that sensor.

1. Add the history sensor to `configuration.yaml` (template blueprints are used from YAML) and restart Home Assistant
   or reload template entities:

   ```yaml
   template:
     - use_blueprint:
         path: arcadematrix/arcadematrix_graph_history.yaml   # the path under config/blueprints/template/
         input:
           sensor_1: sensor.outdoor_temperature
           sensor_2: sensor.living_room_temperature            # optional
       name: Outside vs inside history
       unique_id: arcadematrix_history_outside_inside
   ```

   If you imported the blueprint from its URL, check the folder Home Assistant stored it in and use that `path`.
2. Create an automation from **ArcadeMatrix - publish a graph**: pick the history sensor, a topic (for example
   `arcadematrix/graph/inside`), a title, legend texts and colours.
3. The history fills over the **first 24 hours**: one sample every 15 minutes, newest on the right. Until then the
   graph shows what has been collected so far.

## 5. Show it on the sign

Add a screen with the **MQTT Data** engine and list the topics, comma-separated, in the order the pages should
appear, for example:

```
arcadematrix/weather/local, arcadematrix/graph/inside, arcadematrix/value/outside
```

Up to 6 topics with PSRAM, 4 without. The screen stays up for one full cycle of its pages (each page's `seconds`,
10 by default; a weather topic shows a NOW page plus one page per forecast day), so its rotation duration does not
matter. To remove a page for good, delete its retained message, for example with Developer Tools → Actions →
`mqtt.publish` with the topic, an empty payload and `retain: true`.
