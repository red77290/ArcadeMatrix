# ArcadeMatrix - Ejemplo de tarjeta SD

🇬🇧 [English](README.md) | 🇫🇷 [Français](README_FR.md) | 🇪🇸 Español

Esta carpeta es un punto de partida listo para usar para tu tarjeta SD: copia su contenido a la
raíz de una tarjeta SD formateada en **FAT32**, edita `conf.ini` con tu Wi-Fi/hardware, y ya
está listo para arrancar.

```
sdCard/
  ├─ conf.ini            <- tu configuración, ver docs/CONFIGURATION_ES.md para la referencia completa
  ├─ gifs/                <- manifiesto de playlist GIF de ejemplo (ver gif_indexation/ más abajo)
  ├─ fighters_32/         <- exportación de sprites MUGEN de ejemplo para matrices de 32px de alto
  └─ gif_indexation/      <- herramienta del lado del PC, NO necesaria en la tarjeta SD misma (ver más abajo)
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
