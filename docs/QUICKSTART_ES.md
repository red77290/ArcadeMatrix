# Guía de Inicio Rápido (ESP32)

[English](QUICKSTART.md) | 🇫🇷 [Français](QUICKSTART_FR.md) | 🇪🇸 Español

## 1. Requisitos Previos
- [VSCode](https://code.visualstudio.com/) + [Extensión PlatformIO IDE](https://platformio.org/).
- Tarjeta microSD formateada en FAT32 (1 GB a 32 GB).
- Cable USB conectado a su placa ESP32.

---

## 2. Preparación de la Tarjeta SD
1. Formatee la tarjeta microSD en **FAT32**.
2. Copie el contenido de la carpeta `sdcard_assets/` en la raíz de la tarjeta microSD.
3. Inserte la tarjeta en la ranura de su placa.

---

## 3. Compilación y Grabación del Firmware

### Para ESP32 Estándar (`esp32dev`):
```bash
pio run -e esp32dev -t upload
```

### Para Waveshare ESP32-S3 (`esp32s3_waveshare`):
```bash
pio run -e esp32s3_waveshare -t upload
```

---

## 4. Conexión al Panel de Control Web
1. En el primer inicio sin Wi-Fi configurado, ArcadeMatrix inicia un punto de acceso:
   - **SSID**: `ArcadeMatrix-Setup`
   - **Contraseña**: `matrix123`
2. Abra su navegador en `http://192.168.4.1` (o `http://ArcadeMatrix.local` una vez conectado a su red local).
3. ¡Configure sus motores, temas y rotación!
