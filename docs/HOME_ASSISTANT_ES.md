🇬🇧 [English](HOME_ASSISTANT.md) | 🇫🇷 [Français](HOME_ASSISTANT_FR.md) | 🇪🇸 Español

# Integración con Home Assistant

Home Assistant puede alimentar las pantallas **Datos MQTT** del panel: valores, tablas, gráficos de 24 h y su propia
estación meteorológica. Home Assistant hace el trabajo (historial, formato); el panel dibuja lo que recibe. El
protocolo se describe en [MQTT_DATA_CONTRACT_ES.md](MQTT_DATA_CONTRACT_ES.md); los blueprints de abajo lo
implementan, así que no hace falta escribir ningún template.

## 1. Broker

1. Instale el complemento **Mosquitto broker** (Ajustes → Complementos) y la integración **MQTT** si aún no lo ha
   hecho.
2. Cree un usuario de Home Assistant para el panel (Ajustes → Personas → Usuarios, «Solo puede iniciar sesión desde la
   red local»), por ejemplo `arcadematrix`. Mosquitto acepta los usuarios de Home Assistant como credenciales MQTT.
3. En el panel: *Sistema → MQTT & API → Datos MQTT*: broker = la dirección de Home Assistant, puerto `1883`, ese
   usuario y su contraseña. Guarde. Nada se conecta hasta que se muestra una pantalla Datos MQTT.

## 2. Importar los blueprints

Los blueprints están en [`tools/home_assistant/blueprints/`](../tools/home_assistant/blueprints/):

| Blueprint | Tipo | Publica |
| :--- | :--- | :--- |
| `arcadematrix_value.yaml` | automatización | una entidad como página `value` |
| `arcadematrix_table.yaml` | automatización | hasta 4 entidades como página `table` |
| `arcadematrix_weather.yaml` | automatización | una entidad de clima (actual + pronóstico) como página `weather` |
| `arcadematrix_graph_history.yaml` | **template** | un sensor con 24 h de historial (96 x 15 min) de uno o dos sensores |
| `arcadematrix_graph.yaml` | automatización | ese historial como página `graph` |

Importe cada uno en Ajustes → Automatizaciones y escenas → Blueprints → **Importar blueprint**, pegando la URL de
GitHub del archivo (`https://github.com/red77290/ArcadeMatrix/blob/main/tools/home_assistant/blueprints/<archivo>`).
También puede copiar los blueprints de automatización en `config/blueprints/automation/arcadematrix/` y el blueprint
template en `config/blueprints/template/arcadematrix/`, y recargar.

Todos los blueprints publican mensajes **retenidos** (el panel solo lee lo que el broker conserva) y vuelven a
publicar al arrancar Home Assistant.

## 3. Páginas de valor, tabla y clima

Cree una automatización desde el blueprint (Ajustes → Automatizaciones y escenas → Blueprints → el blueprint → Crear
automatización), elija las entidades y un **topic MQTT**, por ejemplo `arcadematrix/value/outside`. La página se
publica en el acto y de nuevo cada vez que cambian las entidades (el clima también cada 30 minutos).

El blueprint de tabla tiene una entrada opcional **Short labels** (etiquetas cortas): separadas por comas, en el mismo
orden que las entidades (por ejemplo `ABAJO, ARRIBA, FUERA`). Un panel pequeño las usa cuando una etiqueta completa no
cabe; los paneles más grandes conservan las palabras enteras.

## 4. Páginas de gráfico (dos blueprints)

Un gráfico necesita 24 horas de muestras, que Home Assistant guarda en un sensor creado por el blueprint **template**;
la automatización **graph** publica después ese sensor.

1. Añada el sensor de historial a `configuration.yaml` (los blueprints template se usan desde YAML) y reinicie Home
   Assistant o recargue las entidades template:

   ```yaml
   template:
     - use_blueprint:
         path: arcadematrix/arcadematrix_graph_history.yaml   # ruta bajo config/blueprints/template/
         input:
           sensor_1: sensor.outdoor_temperature
           sensor_2: sensor.living_room_temperature            # opcional
       name: Outside vs inside history
       unique_id: arcadematrix_history_outside_inside
   ```

   Si importó el blueprint desde su URL, compruebe en qué carpeta lo guardó Home Assistant y use esa `path`.
2. Cree una automatización desde **ArcadeMatrix - publish a graph**: elija el sensor de historial, un topic (por
   ejemplo `arcadematrix/graph/inside`), un título, los textos de leyenda y los colores.
3. El historial se llena durante las **primeras 24 horas**: una muestra cada 15 minutos, la más reciente a la
   derecha. Hasta entonces el gráfico muestra lo que se ha recogido.

## 5. Mostrarlo en el panel

Añada una pantalla con el motor **Datos MQTT** y liste los topics, separados por comas, en el orden de las páginas,
por ejemplo:

```
arcadematrix/weather/local, arcadematrix/graph/inside, arcadematrix/value/outside
```

Hasta 6 topics con PSRAM, 4 sin ella. La pantalla se queda un ciclo completo de sus páginas (los `seconds` de cada
página, 10 por defecto; un topic de clima muestra una página AHORA más una por día de pronóstico), así que su
duración en la rotación no importa. Para quitar una página definitivamente, borre su mensaje retenido, por ejemplo
desde Herramientas para desarrolladores → Acciones → `mqtt.publish` con el topic, una carga vacía y `retain: true`.
