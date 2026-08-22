# Contributing to ArcadeMatrix (ESP32)

🇬🇧 English | 🇫🇷 [Français](CONTRIBUTING_FR.md) | 🇪🇸 [Español](CONTRIBUTING_ES.md)

Thank you for your interest in contributing to ArcadeMatrix!

## 1. Development Principles
- **Respect Hardware Constraints**: All code running in the display loop (`update` and `render`) must have zero dynamic memory allocations.
- **Hardware Isolation**: Do not scatter `#ifdef` or `psramFound()` checks. Hardware detection belongs strictly in `HardwareHAL`, and requirement gating belongs in `EngineRegistrar`.
- **Wiring Freeze**: Pin definitions in `include/HardwareProfile.h` are tested and frozen. Do not modify pin maps.
- **Trilingual Documentation**: When modifying user-facing documentation or adding architecture decisions, keep EN, FR, and ES versions synchronized.

## 2. Running Tests
```bash
pio test -e esp32dev
```

## 3. Pull Request Process
1. Fork the repo and create your branch from `main`.
2. Ensure your changes compile cleanly on both `esp32dev` and `esp32s3_waveshare`:
   ```bash
   pio run -e esp32dev
   pio run -e esp32s3_waveshare
   ```
3. Submit your PR with a clear summary of changes and tests performed.
