🇬🇧 [English](DEVELOPER.md) | 🇫🇷 [Français](DEVELOPER_FR.md) | 🇪🇸 Español

# Guía del Desarrollador (ESP32 - C++)

Bienvenido a la guía de desarrollo de ArcadeMatrix para ESP32. Este documento explica cómo extender la arquitectura y crear nuevos Motores en C++.

---

## 1. Entendiendo la Arquitectura: Motores, Registro y Ciclo de Vida

ArcadeMatrix ya no tiene una lista codificada de sus características. El sistema se basa en un **Registro** que descubre los motores en el arranque.

### 1.1 El Ciclo de Vida Estricto (Lazy-Once)

El ESP32 tiene un montón (Heap) extremadamente limitado (aprox. 320 KB). Para evitar bloqueos (Kernel Panics) debido a la fragmentación de la memoria, ArcadeMatrix impone un estricto ciclo de vida para cada `IEngine`.

```text
initialize()
    │
    ├── asignación vía 'new' o 'std::vector'
    ├── carga de activos (imágenes de la SD)
    ├── preparación de la caché
    └── inicialización pesada
          ↓
activate()
    │
    └── preparación del estado temporal (reinicio del cronómetro, etc.)
          ↓
update()
    │
    └── lógica en tiempo real (60 FPS) - **SIN ASIGNACIONES DINÁMICAS INNECESARIAS**
          ↓
render()
    │
    └── renderizado en tiempo real (60 FPS) - **SIN ASIGNACIONES DINÁMICAS INNECESARIAS**
          ↓
deactivate()
    │
    └── liberación de recursos externos o detención de escuchas
```

- **Regla de oro:** Nunca cree nuevos `String`, `std::vector`, o use `malloc`/`new` dentro de `update()` o `render()`. Preasigne sus búferes en `initialize()` y mutelos en el lugar.
- **`onConfigChanged()`:** Permite al motor actualizar su estado interno cuando el usuario cambia la configuración a través de la interfaz web (asíncrono en el Núcleo 0).
- **`isFinished()`:** Útil para indicar al `RotationManager` que un motor ha terminado su tarea para forzar el paso al siguiente motor sin esperar el tiempo de espera.

---

## 2. Tutorial: Creación de un Nuevo Motor

Para crear un nuevo motor, debe implementar la interfaz `IEngine` y proporcionar un `EngineDescriptor` a través del `EngineRegistry`.

### Paso 1: Crear el encabezado (`src/engines/MyEngine.h`)

```cpp
#pragma once
#include "core/engine_contract.h"
#include <Arduino.h>

class MyEngine : public IEngine {
public:
    MyEngine();
    virtual ~MyEngine() = default;

    void initialize(ApplicationContext* context, DictionaryEngineConfig* config) override;
    void activate() override;
    void update(ApplicationContext* context) override;
    void render(ApplicationContext* context) override;
    void deactivate() override;
    void onConfigChanged(DictionaryEngineConfig* config) override;
    bool isFinished() const override;

private:
    String mySetting;
    int counter;
};
```

### Paso 2: Implementar el Ciclo de Vida (`src/engines/MyEngine.cpp`)

Implemente el comportamiento de su motor, respetando la restricción de asignación.

```cpp
#include "MyEngine.h"
#include "core/EngineRegistry.h"

MyEngine::MyEngine() : counter(0) {}

void MyEngine::initialize(ApplicationContext* context, DictionaryEngineConfig* config) {
    // Asignación de memoria, lectura de ajustes
    mySetting = config->getString("my_setting", "default");
    Serial.println("¡MyEngine inicializado!");
}

void MyEngine::activate() {
    counter = 0; // Reinicio rápido
}

void MyEngine::update(ApplicationContext* context) {
    // Lógica de negocio rápida, SIN asignaciones
    counter++;
}

void MyEngine::render(ApplicationContext* context) {
    // Renderizado por hardware vía context->display
    context->display->fillScreen(0);
    context->display->setCursor(0, 0);
    context->display->print(mySetting.c_str()); // ¡No hay construcción de String aquí!
}

void MyEngine::deactivate() {
}

void MyEngine::onConfigChanged(DictionaryEngineConfig* config) {
    mySetting = config->getString("my_setting", "default");
}

bool MyEngine::isFinished() const {
    return false;
}
```

### Paso 3: Registrar el Motor en el Arranque

Abra el archivo `src/main.cpp` (o la ubicación centralizada de inicialización del Registro) y añada su descriptor para exponer los campos de configuración a la API Web:

```cpp
#include "engines/MyEngine.h"

// En setup()
EngineDescriptor myDesc;
myDesc.id = "my_engine";
myDesc.name = "Mi Motor Personalizado";
myDesc.category = "misc";
myDesc.version = "1.0";

ConfigField field;
field.id = "my_setting";
field.type = ConfigType::String;
field.label = "Mi Ajuste";
field.description = "Introduzca una palabra a mostrar";
myDesc.schema.fields.push_back(field);

myDesc.factory = []() -> std::unique_ptr<IEngine> {
    return std::unique_ptr<IEngine>(new MyEngine());
};

EngineRegistry::registerEngine(myDesc);
```

¡Eso es todo! **No se necesita modificar ningún código del `RotationManager`**. El motor se listará automáticamente en la API Web, y su configuración `config.json` se gestionará de forma aislada vía el `ConfigSchema`.

---

## 3. Limitaciones Conocidas y Seguridad

### Seguridad
- **API no autenticada:** La API REST HTTP se ejecuta sin autenticación en la red local. No exponga el puerto 80 directamente a Internet.

### Limitaciones Conocidas
- **Sin rollback automático OTA:** En caso de flashear un firmware con un bucle de arranque (boot-loop), la restauración requiere un reflasheo físico.
- **Red Síncrona en los Proveedores:** Los clientes HTTP externos pueden ser bloqueantes. Aunque se gestionan, es aconsejable limitar las llamadas de red agresivas.
- **Tarjeta SD requerida para activos pesados:** Las animaciones `.fgt` y las fuentes `.amf` requieren estrictamente una tarjeta SD instalada y formateada.

---

## 4. Añadir un nuevo Perfil de Hardware

Aunque ArcadeMatrix está diseñado para optimizar el rendimiento de las placas ESP32 estándar, el proyecto soporta nativamente placas más potentes (ej. **ESP32-S3** con 32 MB de Flash y 16 MB de PSRAM).

Si desea adaptar ArcadeMatrix a una nueva placa (con una asignación de pines diferente u otro tipo de memoria), debe crear un nuevo perfil de hardware. La inyección estática a través de flags de compilación es el método preferido.

### Paso 1: Definir el perfil (`include/HardwareProfile.h`)
Añada un nuevo bloque para definir los pines de su matriz HUB75 y de su tarjeta SD.
```cpp
#elif defined(HARDWARE_PROFILE_MI_ESP_S3)
    // Perfil: ESP32-S3 con PSRAM
    #define MATRIX_R1_PIN 10
    #define MATRIX_G1_PIN 11
    // ... defina todos los pines de la matriz ...
    
    // Tarjeta SD
    #define USE_SD_MMC 1
    #define SD_MMC_D0_PIN 12
    #define SD_MMC_CMD_PIN 13
    #define SD_MMC_CLK_PIN 14
```

### Paso 2: Crear el entorno (`platformio.ini`)
Añada un nuevo entorno para habilitar la PSRAM e inyectar su flag:
```ini
[env:mi_esp_s3]
board = esp32-s3-devkitc-1
build_flags = 
    -D HARDWARE_PROFILE_MI_ESP_S3
    -D BOARD_HAS_PSRAM
    -mfix-esp32-psram-cache-issue
```
*Nota: El código C++ asignará automáticamente las fuentes (AMF) o los búferes de memoria extendidos en la PSRAM vía `ps_malloc` cuando esté disponible.*

### Tabla de Compatibilidad de Hardware de Motores

No todos los motores pueden ejecutarse en un ESP32 estándar debido a la falta de memoria RAM. Asegúrese de documentar estos requisitos para los usuarios.

| Motor (`Engine`) | ESP32 (320 KB) | ESP32-S3 (+PSRAM) | Notas |
| :--- | :---: | :---: | :--- |
| `ClockEngine` | ✅ Sí | ✅ Sí | Lógica ligera, muy pocas asignaciones. |
| `MessageEngine` | ✅ Sí | ✅ Sí | Desplazamiento de texto. |
| `CryptoEngine` | ❌ No | ✅ Sí | Almacena gráficos históricos (arreglos de `float`) y analiza enormes cargas JSON de la API que requieren `ps_malloc`. |
| `StockEngine` | ❌ No | ✅ Sí | Igual que `CryptoEngine`. |
| `FighterEngine` | ✅ Sí | ✅ Sí | Lee la tarjeta SD directamente en un flujo binario `.fgt` sin cargar la imagen completa en la RAM. |
