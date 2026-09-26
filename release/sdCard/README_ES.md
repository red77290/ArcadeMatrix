# ArcadeMatrix - Ejemplo de tarjeta SD

🇬🇧 [English](README.md) | 🇫🇷 [Français](README_FR.md) | 🇪🇸 Español

Esta carpeta es un punto de partida listo para usar para tu tarjeta SD: copia su contenido a la
raíz de una tarjeta SD formateada en **FAT32**, edita los archivos de configuración modulares en `config/` (o el archivo heredado `config.json`), y ya está listo para arrancar.

```
sdCard/
  ├─ config/                <- configuración modular por dominio (v4.0+)
  │    ├─ hardware.json     <- dimensiones de matriz, profundidad de color, controlador, pines
  │    ├─ system.json       <- zona horaria, modo noche, brillo, idioma
  │    ├─ network.json      <- credenciales Wi-Fi y ajustes MQTT
  │    ├─ playlist.json     <- secuencia de rotación de motores e intervalos
  │    └─ instances/        <- configuraciones individuales de instancias (*.json)
  ├─ config.json            <- fallback monolítico heredado (migrado automáticamente a config/ si existe)
  ├─ gifs/                  <- manifiesto de playlist GIF de ejemplo (ver gif_indexation/ más abajo)
  ├─ gifs_tate/             <- biblioteca vertical (Tate) (ej. muestra 32x128 para paneles rotados)
  ├─ fighters_32/           <- exportación de sprites MUGEN de ejemplo para matrices de 32px de alto
  ├─ fighters_64/           <- exportación de sprites MUGEN de ejemplo para matrices de 64px de alto
  ├─ fonts/                 <- fuentes de mapa de bits personalizadas
  └─ gif_indexation/        <- herramienta del lado del PC, NO necesaria en la tarjeta SD misma (ver más abajo)
```

## Sobre `gif_indexation/`
Esta subcarpeta es una copia de conveniencia de `tools/gif_indexation/` del repositorio
principal - los scripts que regeneran `gifs/playlists.json` después de agregar/eliminar
carpetas de GIF. **Se ejecutan en tu computadora (macOS/Linux/Windows), no en el ESP32**, así
que no necesitas estrictamente copiar esta subcarpeta en la tarjeta SD - se incluye aquí
únicamente para que tengas todo en una sola descarga sin necesidad de clonar el repositorio
completo. Consulta `gif_indexation/README_ES.md` para el uso.

---
*Para la guía de configuración completa, consulta `docs/GETTING_STARTED_ES.md` del repositorio principal.*
