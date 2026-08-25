# Contribuir a ArcadeMatrix (ESP32)

[English](CONTRIBUTING.md) | 🇫🇷 [Français](CONTRIBUTING_FR.md) | 🇪🇸 Español

¡Gracias por su interés en contribuir a ArcadeMatrix!

## 1. Principios de Desarrollo
- **Respeto a las restricciones de hardware**: Todo código en el bucle de visualización (`update` y `render`) debe tener cero asignaciones dinámicas de memoria.
- **Aislamiento de hardware**: Sin llamadas `psramFound()` ni `#ifdef` dispersos. La detección pertenece estrictamente a `HardwareHAL` y el control de requisitos a `EngineRegistrar`.
- **Cableado congelado**: Las definiciones de pines en `include/HardwareProfile.h` están probadas y congeladas. No modifique los pines.
- **Documentación trilingüe**: Al modificar documentación o decisiones de diseño, mantenga sincronizadas las versiones EN, FR y ES.

## 2. Ejecutar Pruebas
```bash
pio test -e esp32dev
```

## 3. Proceso de Pull Request
1. Cree un fork del repositorio y su rama de trabajo.
2. Compruebe que el código compila limpiamente en ambos entornos:
   ```bash
   pio run -e esp32dev
   pio run -e esp32s3_waveshare
   ```
3. Envíe su PR con una descripción clara de los cambios realizados.
