# Build & Utility Scripts (ESP32)

## 1. `scripts/build_webui.py`
Automates two vital tasks prior to firmware compilation:
1. **Minification & Header Embedding**: Bundles `data/index.html` into a compressed C byte-array header `src/api/WebUI.h` served by the onboard async web server.
2. **Build Information Injection**: Extracts the current Git commit hash (`git rev-parse --short HEAD`) and UTC build timestamp, generating `src/core/BuildInfo.h` exposed via `/api/version`.

### Running manually:
```bash
python3 scripts/build_webui.py
```
*(Automatically invoked via PlatformIO `extra_scripts` during build).*
