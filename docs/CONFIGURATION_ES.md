🇬🇧 [English](CONFIGURATION.md) | 🇫🇷 [Français](CONFIGURATION_FR.md) | 🇪🇸 Español

# Configuración Detallada (config.json) - ESP32

El sistema de configuración del ESP32 utiliza un archivo `config.json` almacenado en la raíz de la tarjeta SD. Este archivo suele gestionarse automáticamente a través de la interfaz web, pero entenderlo es útil para la configuración avanzada o la depuración.

Desde la actualización de Paridad S13, la arquitectura está completamente enfocada en "instancias" de motores independientes.

---

## 1. Estructura Global

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

## 2. El Bloque `"matrix"` (Hardware HUB75 y DMA)

Este bloque configura el bus I2S y el controlador de la matriz. Dado que el ESP32 controla los pines directamente sin un sistema operativo en tiempo real, estos ajustes son críticos para la estabilidad.

| Clave | Tipo | Descripción |
| :--- | :--- | :--- |
| `width` | `int` | Ancho de un solo panel (ej. `64`). |
| `height` | `int` | Alto de un solo panel (ej. `32`). |
| `chain_length` | `int` | Número de paneles encadenados horizontalmente. Advertencia: La RAM del ESP32 no soportará cadenas largas. |
| `pwm_bits` | `int` | Profundidad de color. Valor por defecto `8`. Aumentarlo por encima de 8 reduce drásticamente los FPS en ESP32. |
| `driver_chip` | `String` | Chip controlador (`SHIFTREG`, `FM6126A`). |
| `force_single_buffer` | `bool` | Si es `true`, el renderizado es menos suave (tearing visible) pero **reduce a la mitad el uso de memoria DMA**. Muy útil en matrices de 128x64 sin PSRAM. |
| `brightness_limit` | `int` | Limitador de brillo máximo por software (`0` a `100`). Protege su fuente de alimentación. |

*Nota: Cualquier modificación de la geometría de la matriz (`width`, `height`, `driver_chip`, `pwm_bits`) requiere un reinicio físico del ESP32.*

---

## 3. El Bloque `"system"` (Entorno y Espera)

| Clave | Tipo | Descripción |
| :--- | :--- | :--- |
| `timezone` | `String` | Cadena POSIX (ej. `CET-1CEST,M3.5.0,M10.5.0/3`). |
| `format_24h` | `bool` | Formato de hora. `true` = 23:00, `false` = 11:00 PM. |
| `lang` | `String` | Idioma del sistema (ej. `en`, `es`). |
| `night_mode_enabled` | `bool` | Activa el apagado automático o la reducción de brillo por la noche. |
| `turn_off_at` | `String` | Hora de inicio de espera (ej. `"23:00"`). |
| `wake_up_at` | `String` | Hora de despertar (ej. `"07:00"`). |
| `night_brightness` | `int` | Brillo de espera (`0` = matriz completamente apagada y DMA suspendido). |
| `fighter_enabled` | `bool` | Activa la superposición de sprites de combate MUGEN (`.fgt`) sobre otros motores. |
| `fighter_interval_sec` | `int` | Retraso en segundos entre dos combates MUGEN. |

---

## 4. El Bloque `"wifi"`

| Clave | Tipo | Descripción |
| :--- | :--- | :--- |
| `ssid` | `String` | El nombre de su red Wi-Fi de 2.4 GHz (ESP32 no soporta 5 GHz). |
| `password` | `String` | La clave WPA2. |
| `hostname` | `String` | El nombre mDNS para acceder a la interfaz vía `http://hostname.local`. |

---

## 5. Motores: `"instances"` & `"rotation"`

Gracias a la arquitectura desacoplada "Lazy-Once", el ESP32 puede manejar virtualmente un número infinito de configuraciones de motores sin sobrecargar el montón (Heap), siempre y cuando estos motores no estén en el bucle activo.

### `"instances"`
Esta es una matriz que contiene la configuración de cada bloque lógico.

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
* `instance_id`: Nombre único de este bloque (ej. puedes tener dos widgets de criptomonedas diferentes).
* `engine_id`: El identificador interno del Motor C++.
* `config`: Un objeto JSON dinámico específico para el motor (sus `Capabilities`).

### `"rotation"`
Define el orden de visualización en la pantalla.

```json
{
  "instance_id": "crypto_main",
  "duration_sec": 30,
  "overlays": {
    "fighter": true
  }
}
```
* `instance_id`: Identificador único de la instancia de motor a ejecutar.
* `duration_sec`: Duración de visualización en segundos.
* `overlays.fighter`: (Booleano opcional, por defecto `false`) Activa la superposición animada de combate MUGEN Fighter sobre este slot. Las superposiciones son transversales y pueden activarse en CUALQUIER motor (Clock, Date, Weather, GIFs, Crypto, etc.).

El ESP32 asignará memoria (`initialize()`) para `crypto_main` la primera vez que lo encuentre en este bucle de rotación.

---

## 6. Configuración de todos los Motores

Cada motor declara su esquema de configuración de forma dinámica. A continuación se detallan los parámetros de todos los motores disponibles:

### Motor: `clock`
| Campo | Tipo | Predeterminado | Opciones | Descripción |
| :--- | :--- | :--- | :--- | :--- |
| `clock_theme` | `ENUM` | `0` | Desde `/api/themes` | Tema visual / esfera de reloj (Nintendo, Capcom, Sega, Arcade, Cyberpunk, Flip, Tetris, etc.). |
| `clock_format` | `String` | `%H:%M:%S` | `%H:%M:%S`, `%H:%M`, `%I:%M:%S %p`, `%I:%M %p` | Cadena de formato POSIX strftime. |
| `clock_font` | `ENUM` | `PressStart2P.ttf` | Desde `/api/fonts` | Tipo de fuente (`PressStart2P`, `namco`, `FreeSansBold`, `FreeMonoBold`, `RetroGaming`, o `.amf` SD). |
| `timezone` | `ENUM` | `Europe/Paris` | Desde `/api/timezones` | Zona horaria / región. |
| `clock_size` | `int` | `2` | `1` a `5` | Multiplicador de escala de la fuente. |
| `clock_color_1` | `Color` | `#ffffff` | Hex `#RRGGBB` | Color primario superior (usado con el tema Personalizado 20). |
| `clock_color_2` | `Color` | `#ff00ff` | Hex `#RRGGBB` | Color secundario inferior (usado con el tema Personalizado 20). |
| `clock_offset_x` | `int` | `0` | `-64` a `64` | Desplazamiento horizontal de píxeles. |
| `clock_offset_y` | `int` | `0` | `-32` a `32` | Desplazamiento vertical de píxeles. |

### Motor: `date`
| Campo | Tipo | Predeterminado | Opciones | Descripción |
| :--- | :--- | :--- | :--- | :--- |
| `date_theme` | `ENUM` | `0` | Desde `/api/themes` | Tema visual para la fecha. |
| `date_format` | `String` | `%d/%m/%Y` | `%d/%m/%Y`, `%Y-%m-%d`, `%d %b %Y`, `%A %d %B` | Formato para mostrar la fecha. |
| `date_font` | `ENUM` | `PressStart2P.ttf` | Desde `/api/fonts` | Tipo de fuente (`PressStart2P`, `namco`, `FreeSansBold`, `FreeMonoBold`, `RetroGaming`, o `.amf`). |
| `timezone` | `ENUM` | `Europe/Paris` | Desde `/api/timezones` | Zona horaria para la fecha. |
| `date_size` | `int` | `1` | `1` a `3` | Multiplicador de escala de la fuente. |
| `date_color_1` | `Color` | `#ffffff` | Hex `#RRGGBB` | Color primario (usado con el tema Personalizado 20). |
| `date_color_2` | `Color` | `#00ffff` | Hex `#RRGGBB` | Color secundario (usado con el tema Personalizado 20). |
| `date_offset_x` | `int` | `0` | `-64` a `64` | Desplazamiento horizontal de píxeles. |
| `date_offset_y` | `int` | `0` | `-32` a `32` | Desplazamiento vertical de píxeles. |

### Motor: `weather`
| Campo | Tipo | Predeterminado | Opciones | Descripción |
| :--- | :--- | :--- | :--- | :--- |
| `api_key` | `String` | `""` | Clave API gratuita | Su clave API de OpenWeatherMap (gratis en [openweathermap.org](https://home.openweathermap.org/users/sign_up)). |
| `city` | `String` | `Paris,FR` | Texto | Ubicación de la ciudad (ver formato abajo). |
| `units` | `ENUM` | `metric` | `metric`, `imperial` | Unidad de temperatura: `metric` para Celsius (°C) o `imperial` para Fahrenheit (°F). |
| `lang` | `ENUM` | `es` | `es`, `en`, `fr`, `de`, `it` | Idioma de las etiquetas de días (HOY / TODAY / AUJ.). |
| `weather_offset_x` | `int` | `0` | `-64` a `64` | Desplazamiento horizontal de píxeles. |
| `weather_offset_y` | `int` | `0` | `-32` a `32` | Desplazamiento vertical de píxeles. |

#### Cómo formatear el campo `city` para OpenWeatherMap
OpenWeatherMap utiliza el código de país ISO 3166 (y el código de estado de 2 letras para EE. UU.):
* **Ubicaciones Internacionales:** Use `Ciudad,CodigoPais` (ej. `Madrid,ES`, `BuenosAires,AR`, `Mexico,MX`, `Paris,FR`).
* **Ubicaciones en Estados Unidos:** Use `Ciudad,CodigoEstado,CodigoPais` (ej. `Tucson,AZ,US`, `Miami,FL,US`, `Dallas,TX,US`).
* **Dónde verificar:** Visite [openweathermap.org](https://openweathermap.org) y busque su ciudad.

### Motor: `gifs`
| Campo | Tipo | Predeterminado | Opciones | Descripción |
| :--- | :--- | :--- | :--- | :--- |
| `folder` | `LIST` | `all` | Desde `/api/playlists` | Carpetas / Listas de reproducción de GIFs activas (ej. `Arcade`, `Consoles`, `Fighters`). |
| `speed_multiplier` | `Float` | `1.0` | `0.25` a `3.0` | Factor de velocidad de reproducción (`1.0` = velocidad normal). |
| `shuffle` | `Boolean` | `true` | `true`, `false` | Orden aleatorio de reproducción de las animaciones. |
| `duration_sec` | `int` | `10` | `2` a `120` | Duración en segundos por cada GIF. |

### Motor: `crypto` (Requiere PSRAM)
| Campo | Tipo | Predeterminado | Opciones | Descripción |
| :--- | :--- | :--- | :--- | :--- |
| `symbols` | `String` | `BTC,ETH,SOL` | Separados por comas | Símbolos de criptomonedas a monitorear. |
| `show_chart` | `Boolean` | `true` | `true`, `false` | Muestra el gráfico sparkline histórico. |
| `chart_timeframe` | `ENUM` | `daily` | `hourly`, `daily`, `weekly`, `monthly` | Intervalo temporal del gráfico de precios. |
| `duration_sec` | `int` | `5` | `3` a `30` | Segundos para mostrar cada activo. |
| `currency` | `ENUM` | `USD` | `USD`, `EUR`, `GBP`, `JPY` | Moneda fiduciaria de referencia. |
| `provider` | `ENUM` | `coingecko` | `coingecko`, `binance` | Proveedor de cotizaciones en tiempo real. |
| `cache_ttl_min` | `int` | `5` | `1` a `60` | Minutos entre actualizaciones de API. |
| `crypto_offset_x` | `int` | `0` | `-64` a `64` | Desplazamiento horizontal de píxeles. |
| `crypto_offset_y` | `int` | `0` | `-32` a `32` | Desplazamiento vertical de píxeles. |

### Motor: `stock` (Requiere PSRAM)
| Campo | Tipo | Predeterminado | Opciones | Descripción |
| :--- | :--- | :--- | :--- | :--- |
| `symbols` | `String` | `AAPL,TSLA,NVDA` | Separados por comas | Símbolos bursátiles a monitorear. |
| `show_chart` | `Boolean` | `true` | `true`, `false` | Muestra el gráfico sparkline histórico. |
| `chart_timeframe` | `ENUM` | `daily` | `hourly`, `daily`, `weekly`, `monthly` | Intervalo temporal del gráfico de acciones. |
| `duration_sec` | `int` | `5` | `3` a `30` | Segundos para mostrar cada acción. |
| `provider` | `ENUM` | `yahoo` | `yahoo` | Proveedor de cotizaciones de mercado. |
| `cache_ttl_min` | `int` | `5` | `1` a `60` | Minutos entre actualizaciones de API. |
| `stock_offset_x` | `int` | `0` | `-64` a `64` | Desplazamiento horizontal de píxeles. |
| `stock_offset_y` | `int` | `0` | `-32` a `32` | Desplazamiento vertical de píxeles. |

### Motor: `audiovisualizer` (Requiere Micrófono)
| Campo | Tipo | Predeterminado | Opciones | Descripción |
| :--- | :--- | :--- | :--- | :--- |
| `enabled` | `Boolean` | `false` | `true`, `false` | Activa la superposición del visualizador de audio FFT en vivo. |
| `style` | `ENUM` | `spectrum` | `spectrum`, `waveform`, `radial`, `neon_fire` | Estilo de renderizado de la visualización de audio. |
| `sensitivity` | `int` | `5` | `1` a `10` | Sensibilidad de respuesta del micrófono. |
| `gain` | `Float` | `1.0` | `0.5` a `5.0` | Factor de ganancia del micrófono por hardware. |

### Motor: `decibelMeter` (Requiere Micrófono)
| Campo | Tipo | Predeterminado | Opciones | Descripción |
| :--- | :--- | :--- | :--- | :--- |
| `threshold` | `int` | `80` | `40` a `120` | Umbral de advertencia sonora en decibelios (dB). |

### Motor: `temp` (Requiere Sensor)
| Campo | Tipo | Predeterminado | Opciones | Descripción |
| :--- | :--- | :--- | :--- | :--- |
| `units` | `ENUM` | `C` | `C`, `F` | Unidad de medida del sensor de temperatura integrado. |
| `temp_offset_x` | `int` | `0` | `-64` a `64` | Desplazamiento horizontal de píxeles. |
| `temp_offset_y` | `int` | `0` | `-32` a `32` | Desplazamiento vertical de píxeles. |

### Motor: `message`
| Campo | Tipo | Predeterminado | Opciones | Descripción |
| :--- | :--- | :--- | :--- | :--- |
| `text` | `String` | `ArcadeMatrix` | Texto | Texto del mensaje o cartel a mostrar. |
| `color` | `Color` | `#ffffff` | Hex `#RRGGBB` | Color del texto. |
| `size` | `int` | `1` | `1` a `4` | Multiplicador de escala de la fuente. |
| `direction` | `ENUM` | `rtl` | `rtl`, `ltr`, `ttb`, `btt`, `static` | Dirección de desplazamiento (`rtl` = derecha a izquierda, `static` = texto centrado fijo). |
| `speed` | `int` | `50` | `10` a `200` | Demora en milisegundos por paso de desplazamiento (menor = más rápido). |
| `font` | `ENUM` | `Default` | Desde `/api/fonts` | Tipo de fuente (`PressStart2P`, `namco`, `FreeSansBold`, `FreeMonoBold`, `RetroGaming`, `.amf`). |

---

*Nota: Los esquemas de configuración se pueden consultar en vivo desde el ESP32 mediante `GET /api/engines`.*


