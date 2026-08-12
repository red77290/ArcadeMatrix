# Guía de Configuración (`conf.ini` y API)

🇬🇧 [English](CONFIGURATION.md) | 🇫🇷 [Français](CONFIGURATION_FR.md) | 🇪🇸 Español

ArcadeMatrix es totalmente configurable sin recompilar el código C++. Todos los parámetros se gestionan a través de la interfaz web (en `http://arcadematrix.local`), mediante la API REST o directamente en el archivo `conf.ini` en la raíz de la tarjeta SD.

---

## 📊 Matriz de Referencia Exhaustiva de Parámetros

| Sección | Clave `conf.ini` | Clave API JSON | Tipo | Por defecto | Ejemplo | Efecto Runtime | ¿Persistido? | ¿Reinicio Requerido? | Descripción |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **[WIFI]** | `SSID` | `wifi_ssid` | string | `""` | `"HomeWiFi"` | Conexión al arrancar | Sí | **Sí** | Nombre de red Wi-Fi 2.4GHz |
| | `PASSWORD` | `wifi_password` | string | `""` | `"secret"` | Conexión al arrancar | Sí | **Sí** | Contraseña WPA2 |
| | `HOSTNAME` | `hostname` | string | `"ArcadeMatrix"` | `"my-matrix"` | mDNS | Sí | **Sí** | Nombre de red (`http://my-matrix.local`) |
| **[MATRIX]**| `WIDTH` | `matrix_cols` | int | `64` | `256` | DMA Matrix | Sí | **Sí** | Ancho total de la matriz en píxeles |
| | `HEIGHT` | `matrix_rows` | int | `32` | `64` | DMA Matrix | Sí | **Sí** | Alto total de la matriz en píxeles |
| | `CHAIN` | `matrix_chain` | int | `1` | `2` | Paneles | Sí | **Sí** | Número de paneles encadenados |
| | `BRIGHTNESS_LIMIT`|`brightness_limit`| int | `100` | `80` | **En vivo** | Sí | No | Límite de brillo (0-100%) |
| | `PWM_BITS` | `pwm_bits` | int | `8` | `8` | Profundidad | Sí | **Sí** | Bits por canal de color (8 por defecto) |
| | `DRIVER_CHIP` | `matrix_driver_chip`| string | `"SHIFTREG"` | `"FM6126A"` | Chip Driver | Sí | **Sí** | Chip driver (`SHIFTREG`, `FM6126A`, `ICN2038S`, `SM16208`) |
| | `FORCE_SINGLE_BUFFER`| `force_single_buffer`| bool | `false` | `true` | Memoria RAM | Sí | **Sí** | Fuerza búfer único para ahorrar RAM |
| **[TIME]** | `NTP_SERVER` | `ntp_server` | string | `"pool.ntp.org"` | `"time.google.com"` | Sincro NTP | Sí | No | Servidor de hora NTP |
| | `TIMEZONE` | `timezone` | string | `"CET-1CEST..."` | `"EST5EDT"` | Zona POSIX | Sí | No | Cadena de zona horaria POSIX |
| | `FORMAT_24H` | `format_24h` | bool | `true` | `false` | Pantalla | Sí | No | Formato 24h (`true`) o 12h AM/PM (`false`) |
| | `CLOCK_THEME` | `clock_theme` | int | `0` | `20` | **En vivo** | Sí | No | ID de Tema de Reloj (0-29, 20=Custom Gradient) |
| | `CLOCK_COLOR_1` | `clock_color_1` | string | `"#ffffff"` | `"#FF0000"` | **En vivo** | Sí | No | Color Hex de inicio de degradado |
| | `CLOCK_COLOR_2` | `clock_color_2` | string | `"#ffffff"` | `"#00FF00"` | **En vivo** | Sí | No | Color Hex de fin de degradado |
| | `CLOCK_FONT_PATH`| `clock_font_path`| string | `""` | `"/fonts/my.amf"`| **En vivo** | Sí | No | Ruta a fuente `.amf` en SD |
| **[IDLE]** | `ROTATION` | `rotation` | string | `"clock,date,weather,gifs"` | `"clock,gifs"` | **En vivo** | Sí | No | Módulos activos (`clock`, `date`, `weather`, `gifs`, `crypto`, `stocks`) |
| | `CLOCK_DURATION_SEC`|`clock_duration_sec`| int | `60` | `30` | **En vivo** | Sí | No | Duración de pantalla de reloj en segundos |
| | `GIFS_COUNT` | `gifs_count` | int | `3` | `5` | **En vivo** | Sí | No | Número de GIFs reproducidos por ciclo |
| | `FIGHTER_ENABLED`| `fighter_enabled` | bool | `true` | `false` | **En vivo** | Sí | No | Activa la superposición de combate MUGEN |
| | `FIGHTER_INTERVAL_SEC`|`fighter_interval_sec`| int | `10` | `20` | **En vivo** | Sí | No | Intervalo en segundos entre combates MUGEN |
| **[STANDBY]**| `NIGHT_MODE_ENABLED`|`night_mode_enabled`| bool | `false` | `true` | **En vivo** | Sí | No | Activa el modo de suspensión nocturna |
| | `TURN_OFF_AT` | `turn_off_at` | string | `"23:00"` | `"22:30"` | **En vivo** | Sí | No | Hora de apagado (HH:MM) |
| | `WAKE_UP_AT` | `wake_up_at` | string | `"07:00"` | `"08:00"` | **En vivo** | Sí | No | Hora de encendido (HH:MM) |
| | `NIGHT_BRIGHTNESS`|`night_brightness`| int | `10` | `0` | **En vivo** | Sí | No | Brillo nocturno (0 = pantalla totalmente apagada) |
| **[CRYPTO]** | `ENABLED` | `crypto_enabled` | bool | `true` | `false` | **En vivo** | Sí | No | Activa el ticker crypto |
| | `SYMBOLS` | `crypto_symbols` | string | `"BTC,ETH,SOL,DOGE"` | `"BTC,ETH"` | **En vivo** | Sí | No | Símbolos crypto (separados por comas) |
| | `DURATION_SEC` | `crypto_duration_sec`| int | `5` | `10` | **En vivo** | Sí | No | Duración por símbolo en segundos |
| **[STOCK]** | `ENABLED` | `stock_enabled` | bool | `true` | `false` | **En vivo** | Sí | No | Activa el ticker bursátil |
| | `SYMBOLS` | `stock_symbols` | string | `"AAPL,NVDA,TSLA,MSFT"` | `"AAPL"` | **En vivo** | Sí | No | Símbolos bursátiles (separados por comas) |
| | `DURATION_SEC` | `stock_duration_sec` | int | `5` | `10` | **En vivo** | Sí | No | Duración por símbolo en segundos |

---

## 📌 Notas Importantes

- **`ROTATION`**: La cadena `ROTATION` controla los módulos de visualización autónomos (`clock`, `date`, `weather`, `gifs`, `crypto`, `stocks`). **Nota: `sprites` NO es un módulo de rotación**. Los luchadores MUGEN se renderizan dinámicamente como una superposición mediante `FighterEngine` a través de `FIGHTER_ENABLED`.
- **Requisito de Reinicio**: Los cambios en la geometría del hardware (`WIDTH`, `HEIGHT`, `DRIVER_CHIP`, `CHAIN`, `PWM_BITS`) y las credenciales Wi-Fi requieren un reinicio del sistema (`POST /api/system/reboot` o el botón *Reboot System* en la Web UI). Todas las demás opciones se aplican inmediatamente en directo.
