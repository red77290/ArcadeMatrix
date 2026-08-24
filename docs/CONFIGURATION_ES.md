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
  "duration_sec": 30
}
```
El ESP32 asignará memoria (`initialize()`) para `crypto_main` la primera vez que lo encuentre en este bucle de rotación.

---

## 6. Configuración de motores financieros (`crypto` y `stock`)

Los motores `crypto` y `stock` admiten visualización multipágina interactiva con cotizaciones en tiempo real y gráficos sparklines históricos:

| Clave | Tipo | Predeterminado | Opciones | Descripción |
| :--- | :--- | :--- | :--- | :--- |
| `symbols` | `String` | `"BTC,ETH"` / `"AAPL,NVDA"` | Separados por comas | Activos financieros a monitorear. |
| `show_chart` | `bool` | `true` | `true`, `false` | Activa la pantalla con el gráfico sparkline histórico. |
| `chart_timeframe` | `String` | `"daily"` | `"hourly"`, `"daily"`, `"weekly"`, `"monthly"` | Intervalo de tiempo para el gráfico de precios. |
| `duration_sec` / `page_seconds` | `int` | `5` | `3` a `30` | Segundos para mostrar cada vista antes de alternar. |
| `cache_ttl_min` | `int` | `5` | `1` a `60` | Minutos de retención de caché antes de consultar las API. |

---

## 7. Configuración del motor del Clima (`weather`)

El motor `weather` obtiene el pronóstico del tiempo para 3 días desde [OpenWeatherMap](https://openweathermap.org) con una memoria caché automática de 15 minutos:

| Clave | Tipo | Predeterminado | Opciones | Descripción |
| :--- | :--- | :--- | :--- | :--- |
| `api_key` | `String` | `""` | Clave API gratuita | Su clave API de OpenWeatherMap (gratuita en [openweathermap.org](https://home.openweathermap.org/users/sign_up)). |
| `city` | `String` | `"Paris,FR"` | Texto | Ubicación de la ciudad (ver formato a continuación). |
| `units` | `String` | `"metric"` | `"metric"`, `"imperial"` | Unidad de temperatura: `metric` para Celsius (°C) o `imperial` para Fahrenheit (°F). |
| `lang` | `String` | `"es"` | `"en"`, `"fr"`, `"es"`, `"de"`, `"it"` | Idioma de las etiquetas de días (HOY / TODAY / AUJ.). |
| `weather_offset_x` | `int` | `0` | `-64` a `64` | Desplazamiento horizontal en píxeles. |
| `weather_offset_y` | `int` | `0` | `-32` a `32` | Desplazamiento vertical en píxeles. |

### Cómo formatear el campo `city` para OpenWeatherMap
OpenWeatherMap utiliza el código de país ISO 3166 (y el código de estado de 2 letras para EE. UU.) para identificar la localidad sin ambigüedades:
* **Ubicaciones Internacionales:** Use `Ciudad,CodigoPais` (ej. `Madrid,ES`, `BuenosAires,AR`, `Mexico,MX`, `Paris,FR`).
* **Ubicaciones en Estados Unidos:** Use `Ciudad,CodigoEstado,CodigoPais` (ej. `Tucson,AZ,US`, `Miami,FL,US`, `Dallas,TX,US`). Si se omite el estado o el país, la API puede devolver una ciudad homónima en otro lugar.
* **Dónde verificar:** Visite [openweathermap.org](https://openweathermap.org) y busque su ciudad. El encabezado del resultado y la URL muestran exactamente el formato reconocido por la API.


