🇬🇧 [English](MQTT_DATA_CONTRACT.md) | 🇫🇷 [Français](MQTT_DATA_CONTRACT_FR.md) | 🇪🇸 Español

# Contrato MQTT Data

El motor `mqttdata` («Datos MQTT») muestra páginas que envía cualquier publicador MQTT: un sistema domótico, un
script, Node-RED. Este documento es el protocolo entre el publicador y el panel. Para Home Assistant, unos blueprints
listos para usar lo implementan: consulte [HOME_ASSISTANT_ES.md](HOME_ASSISTANT_ES.md).

## 1. Cómo usa MQTT el panel

- El broker se configura una vez en *Sistema → MQTT & API → Datos MQTT* (`data_mqtt`: broker, port, user, pass). Es
  independiente del enlace arcade `mqtt`, que puede tomar el control de la pantalla.
- Una pantalla Datos MQTT (una instancia del motor `mqttdata`) lista **topics**, separados por comas, una **página**
  por topic, en ese orden. No se configura nada más en el panel: todo lo demás viene de los mensajes.
- **Nada se ejecuta en segundo plano.** Cuando la pantalla aparece en la rotación, el panel se conecta, se suscribe a
  sus topics, muestra lo que el broker le entrega y mantiene las páginas al día mientras se muestra. Cuando la
  pantalla termina, se desconecta y libera toda su memoria.
- Por eso el publicador **DEBE publicar con `retain: true`**: el panel solo ve el último mensaje retenido de cada
  topic (una instantánea) más lo que llegue mientras la pantalla está visible.
- Un **mensaje retenido vacío** (carga de longitud cero, la forma de MQTT de borrar un mensaje retenido) vacía la
  página: muestra `SIN DATOS`.
- El id de cliente es `<hostname wifi>-data`, así que varios paneles pueden compartir un broker.

## 2. Campos comunes

Cada mensaje es un objeto JSON.

| Campo | Tipo | Obligatorio | Por defecto | Descripción |
| :--- | :--- | :--- | :--- | :--- |
| `type` | cadena | **sí** | — | `value`, `table`, `graph` o `weather`: elige el renderizado. |
| `v` | entero | no | `1` | Versión del contrato que sigue el publicador (ver sección 5). |
| `seconds` | entero | no | `10` | Tiempo en esta página, de 3 a 3600. Para `weather`, se aplica a cada una de sus páginas. |

El texto usa una fuente de píxeles ASCII: `°` está soportado, los demás caracteres no ASCII se muestran como `?`.
Los colores van en formato `"#RRGGBB"`.

### Tiempo en pantalla

La pantalla Datos MQTT fija su propio tiempo: un ciclo completo de sus páginas, es decir la suma de sus `seconds` (un
topic `weather` cuenta una página AHORA más una por día de pronóstico). Una página sin mensaje cuenta 10 s. La
duración del hueco en la rotación se ignora (el motor marca su propio ritmo, como el reproductor GIF). Las páginas
pasan en el orden de los topics y cada activación empieza por la primera.

## 3. Tipos de página

### `value`: un valor

```json
{"type":"value","value":"21.7","unit":"°C","label":"OUTSIDE","color":"#40C0FF"}
```

| Campo | Tipo | Por defecto | Descripción |
| :--- | :--- | :--- | :--- |
| `value` | cadena (o número) | — (obligatorio) | Valor ya formateado; el panel no calcula nada. |
| `unit` | cadena | `""` | Se dibuja tras el valor, en el color de la etiqueta y más pequeña. |
| `label` | cadena | `""` | Se dibuja encima del valor. |
| `label_short` | cadena | `""` | Se usa si `label` no cabe. |
| `color` | color | `#FFFFFF` | Color del valor. |
| `label_color` | color | `#808080` | Color de la etiqueta y la unidad. |

Se dibuja exactamente como una `table` de un mosaico sin título: la fuente más grande que cabe.

### `table`: hasta 4 valores

```json
{"type":"table","title":"HOME","show_title":true,
 "tiles":[{"label":"DOWNSTAIRS","label_short":"DN","value":"76","unit":"°F","color":"#40C0FF"},
          {"label":"UPSTAIRS","value":"77","unit":"°F"}]}
```

| Campo | Tipo | Por defecto | Descripción |
| :--- | :--- | :--- | :--- |
| `title` | cadena | `""` | Fila de título, mostrada con 1 a 3 mosaicos si hay sitio (nunca con 4). |
| `show_title` | booleano | `true` | `false` oculta el título. |
| `tiles` | array | — (obligatorio, 1 a 4) | Cada mosaico tiene los campos de una página `value` (`value`, `unit`, `label`, `label_short`, `color`, `label_color`). Los mosaicos de más se ignoran. |

1 a 3 mosaicos en columnas, 4 en una cuadrícula 2x2. Cada mosaico se apila (etiqueta arriba, valor y unidad debajo)
y se centra. Se usa la `label` completa si cabe, si no `label_short`, luego una fuente menor y por último se recorta
la etiqueta. Un valor nunca se recorta: primero se quita su unidad y después se dibuja en la fuente más pequeña.

### `graph`: barras y líneas en el tiempo

```json
{"type":"graph","title":"DOWNSTAIRS COOL","title_short":"DN COOL","summary":"76°","summary_color":"#40A0FF",
 "slots":96,"min":68,"max":90,"yaxis":true,
 "series":[{"label":"OUTSIDE","style":"line","color":"#FF4020","data":[75.0,74.8,null,74.1]},
           {"label":"INSIDE","style":"line","color":"#40A0FF","data":[76.0,76.2,76.1,75.9]}],
 "bands":[{"from":40,"to":43,"color":"#183860"}],
 "marks":[{"at":19,"color":"#404040"}],
 "legend":[{"text":"OUTSIDE","color":"#FF4020"},{"text":"INSIDE","color":"#40A0FF"}]}
```

| Campo | Tipo | Por defecto | Descripción |
| :--- | :--- | :--- | :--- |
| `title` / `title_short` | cadena | `""` | Título del encabezado; el corto solo se usa si el completo no cabe. |
| `summary` | cadena | `""` | Parte derecha del encabezado (p. ej. el último valor). |
| `summary_color` | color | color de la 1.ª serie | Color del resumen. |
| `show_header` | booleano | `true` | `false` oculta el encabezado; el gráfico ocupa toda la altura. |
| `series` | array | — (obligatorio) | Hasta 4: `label`, `color`, `style` (`"bar"` por defecto o `"line"`), `data` (números; `null` = hueco en una línea, 0 en una barra). |
| `slots` | entero | serie más larga | Posiciones en x; los datos se **alinean a la derecha** (último punto = el más reciente = a la derecha). |
| `min` / `max` | número | automático | Rango y; `max` ausente o `<= min` = automático (líneas: rango de los datos ampliado un 5 %, al menos 0,5). |
| `stack` | booleano | `true` | Barras apiladas (pilas positivas y negativas por separado) o superpuestas. |
| `yaxis` | booleano | `false` | Extremos del rango a la izquierda (paneles de al menos 64 px de alto). |
| `bands` | array | `[]` | Rangos de fondo `{from, to, color}` en índices de slot `[from, to)`; hasta 96, se conservan los más recientes. |
| `marks` | array | `[]` | Líneas verticales punteadas `{at, color}`; hasta 8. |
| `legend` | array | `[]` | Hasta 4 `{text, color}` bajo el gráfico en paneles de al menos 64 px de alto; los que no caben se quitan por el final. |
| `unit` | cadena | `""` | Informativo. |

Las barras parten de la línea cero (los valores negativos bajan); aparece una línea cero tenue cuando 0 está dentro
del rango. Las líneas son de 1 px con uniones verticales y se dibujan después de las barras.

### `weather`: ahora y pronóstico

```json
{"type":"weather","units":"imperial","seconds":8,
 "current":{"temp":75,"condition":"clear-night","humidity":47,"wind":7,"wind_unit":"mph","wind_dir":"NE"},
 "days":[{"temp_max":89,"temp_min":70,"condition":"sunny","precip_prob":0},
         {"temp_max":90,"temp_min":71,"condition":"partlycloudy","precip_prob":5}]}
```

| Campo | Tipo | Por defecto | Descripción |
| :--- | :--- | :--- | :--- |
| `units` | cadena | `"metric"` | `"imperial"` = °F, `"metric"` = °C. Los números se muestran tal cual, nunca se convierten. |
| `current.temp` | número | — | Lectura en vivo; crea la página AHORA. |
| `current.condition` | cadena | `""` | Id de condición (lista abajo). |
| `current.humidity` | entero | — | Porcentaje. |
| `current.wind` / `wind_unit` / `wind_dir` | número / cadena / cadena | — | Velocidad, unidad, dirección de 8 puntos (`"NE"`); se muestra `"47%  NE 9mph"`. |
| `days[]` | array | `[]` | Hasta 5: `temp_max`, `temp_min`, `condition`, `precip_prob`. El día 0 es hoy. |

Páginas: AHORA primero (si hay `current.temp`), luego una por día, cada una durante `seconds`. En °F la máxima se
muestra encima de la mínima (convención de EE. UU.). Ids de condición: `sunny`, `clear-night`, `partlycloudy`,
`cloudy`, `fog`, `rainy`, `pouring`, `lightning`, `lightning-rainy`, `snowy`, `snowy-rainy`, `hail`, `windy`,
`windy-variant`, `exceptional`; un id desconocido se muestra tal cual.

## 4. Límites

| | ESP32-S3 con PSRAM (p. ej. Waveshare ESP32-S3 Matrix) | ESP32 sin PSRAM (`esp32dev`) |
| :--- | :--- | :--- |
| Mensaje más grande | 8192 bytes | 4096 bytes |
| Puntos conservados por serie de gráfico (los más recientes) | 288 | 96 |
| Topics (páginas) por pantalla Datos MQTT | 6 | 4 |
| Series / bandas / marcas / elementos de leyenda | 4 / 96 / 8 / 4 | 4 / 96 / 8 / 4 |
| Mosaicos de tabla / días de clima | 4 / 5 | 4 / 5 |

Los campos de texto se recortan al tamaño de su búfer: títulos 23 caracteres, etiquetas de mosaico 23, valores 15,
unidades 7. Un mensaje por encima del límite no se dibuja: la página muestra `NO COMPATIBLE`, o `SIN DATOS` si es tan grande que el cliente MQTT no llega a recibirlo.

## 5. Versiones y compatibilidad futura

- `v` es la versión del contrato que sigue el publicador; este firmware implementa la versión 1.
- Los campos desconocidos se ignoran. Un mensaje con un `v` mayor se dibuja igualmente con los campos que este
  firmware conoce.
- Los nuevos tipos de página tendrán nuevos nombres de `type`; un panel que no conoce un tipo muestra `NO COMPATIBLE`
  solo para esa página.

## 6. Avisos

| Aviso | Cuándo |
| :--- | :--- |
| `CONECTANDO` | La pantalla está visible, el broker aún no está conectado y no ha llegado ninguna página. |
| `SIN CONEXION` | No se puede contactar con el broker (dirección errónea, acceso rechazado) y no ha llegado ninguna página. |
| `SIN DATOS` | Ningún mensaje retenido en ese topic 3 s después de conectar, o un mensaje retenido vacío. |
| `NO COMPATIBLE` | `type` ausente o desconocido, JSON inválido, mensaje por encima del límite o contenido que el tipo no puede dibujar. Se muestra durante los `seconds` de la página (10 por defecto); las demás páginas no se ven afectadas. |

Los avisos siguen el idioma del panel (EN / FR / ES).
