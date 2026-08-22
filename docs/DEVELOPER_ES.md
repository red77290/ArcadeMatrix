[English](DEVELOPER.md) | 🇫🇷 [Français](DEVELOPER_FR.md) | 🇪🇸 Español

# Guía para Desarrolladores (ESP32 — C++)

Bienvenido a la guía de desarrollo de ArcadeMatrix para ESP32. Este documento explica cómo crear nuevos motores, declarar esquemas de configuración, definir requisitos de hardware e integrarse de manera limpia con la interfaz web dinámica.

---

## 1. Arquitectura de Motores y Ciclo de Vida Estricto

ArcadeMatrix se basa en una arquitectura desacoplada:
1. **`IEngine`**: Contrato de interfaz abstracto para todos los módulos de visualización.
2. **`EngineRegistry`**: Registro centralizado que almacena descriptores y fábricas.
3. **`EngineRegistrar`**: Punto único de control (gating) que evalúa las capacidades de `HardwareHAL`.
4. **`ConfigSanitizer`**: Validación declarativa e inyección de valores predeterminados.
5. **`RotationManager`**: Instanciación bajo demanda (lazy) y gestión del bucle de rotación.

```text
initialize() [Configuración inicial y asignación de memoria]
      ↓
activate() [Reinicio de temporizadores / estado al cambiar]
      ↓
update() [Cálculo lógico - 60 FPS - CERO asignaciones en heap]
      ↓
render() [Dibujo de píxeles en MatrixPanel_I2S_DMA]
      ↓
deactivate() [Pausa / liberar archivos / detener audio]
```

### Reglas Críticas
- **Cero Asignaciones en el Bucle Activo**: Nunca instancie `String`, `std::vector` ni use `malloc`/`new` en `update()` o `render()`. Preasigne sus búferes en `initialize()`.
- **Recarga en Caliente (Hot Reload)**: Implemente `onConfigChanged(const EngineConfig* config)` para aplicar cambios de usuario en vivo sin reiniciar.
- **Aislamiento de Hardware**: Nunca use `psramFound()` ni `#ifdef BOARD_HAS_PSRAM` dentro del motor. Declare sus necesidades en `requirements.needsPsram`.

---

## 2. Paso a Paso: Creación de un Nuevo Motor

### Paso 1: Declarar la Clase (`src/engines/MyEngine.h`)

```cpp
#pragma once
#include "../../include/core/EngineContract.h"
#include <Arduino.h>

class MyEngine : public IEngine {
public:
    MyEngine();
    ~MyEngine() override = default;

    EngineError initialize(EngineContext* context, const EngineConfig* config) override;
    void activate() override;
    void update(EngineContext* context) override;
    void render(EngineContext* context) override;
    void deactivate() override;
    void onConfigChanged(const EngineConfig* config) override;
    
    // Métodos opcionales (con comportamientos predeterminados seguros):
    bool isFinished() const override { return false; }
    bool isRealtime() const override { return true; }
    bool selfPaced() const override { return false; }
    bool allowsOverlay() const override { return true; }

private:
    MatrixPanel_I2S_DMA* matrix = nullptr;
    int speed = 1;
    String text = "Hola";
    int posX = 0;
};
```

### Paso 2: Implementar el Comportamiento (`src/engines/MyEngine.cpp`)

```cpp
#include "MyEngine.h"
#include "../core/Logger.h"

MyEngine::MyEngine() {}

EngineError MyEngine::initialize(EngineContext* context, const EngineConfig* config) {
    if (!context || !context->getMatrix()) return EngineError::InitializationFailed;
    matrix = context->getMatrix();

    if (config) {
        speed = config->getInt("speed", 1);
        text = config->getString("text", "Hola");
    }
    LOGI("MyEngine", "Inicializado con éxito");
    return EngineError::OK;
}

void MyEngine::activate() {
    posX = 0;
}

void MyEngine::update(EngineContext* context) {
    posX += speed;
    if (matrix && posX > matrix->width()) {
        posX = -50;
    }
}

void MyEngine::render(EngineContext* context) {
    if (!matrix) return;
    matrix->fillScreen(0);
    matrix->setCursor(posX, 10);
    matrix->print(text);
}

void MyEngine::deactivate() {
    // Limpieza de recursos temporales
}

void MyEngine::onConfigChanged(const EngineConfig* config) {
    if (config) {
        speed = config->getInt("speed", 1);
        text = config->getString("text", "Hola");
    }
}
```

### Paso 3: Registrar en `src/engines/EngineRegistrar.cpp`

Añada su descriptor en `EngineRegistrar::registerAll()`:

```cpp
#include "MyEngine.h"

void EngineRegistrar::registerAll() {
    // ...
    EngineDescriptor desc;
    desc.metadata = {
        .id = "my_engine",
        .name = "Mi Motor Personalizado",
        .category = "custom",
        .version = "1.0.0"
    };
    desc.capabilities = {
        .supports_128x32 = true,
        .supports_256x64 = true,
        .realtime = true,
        .interruptible = true,
        .allowsOverlay = true,
        .selfPaced = false
    };
    desc.requirements = {
        .needsPsram = false,
        .needsAudio = false,
        .needsTempSensor = false,
        .needsGyroscope = false,
        .needsNetwork = false,
        .needsSd = false
    };
    desc.schema.fields = {
        {
            .id = "speed",
            .type = ConfigType::INTEGER,
            .label = "Velocidad de desplazamiento",
            .description = "Píxeles por fotograma",
            .default_value = "1",
            .required = false,
            .min_val = "1",
            .max_val = "10",
            .step = "1",
            .validation_policy = ValidationPolicy::Clamp
        },
        {
            .id = "text",
            .type = ConfigType::STRING,
            .label = "Texto mostrado",
            .description = "Mensaje a desplazar",
            .default_value = "Hola Mundo",
            .required = false
        }
    };
    desc.factory = []() {
        return std::unique_ptr<IEngine>(new MyEngine());
    };

    tryRegister(desc);
}
```

---

## 3. Tipos de Datos del Esquema y Opciones Dinámicas

| `ConfigType` | Componente de la UI Web | Atributos Compatibles |
|---|---|---|
| `BOOLEAN` | Desplegable (Activado / Desactivado) | `default_value` |
| `INTEGER` | Campo numérico con límites | `min_val`, `max_val`, `step`, `validation_policy` |
| `FLOAT` | Campo decimal | `min_val`, `max_val`, `step`, `validation_policy` |
| `STRING` | Campo de texto | `default_value` |
| `ENUM` | Menú desplegable | `options="opt1,opt2"`, `options_endpoint` |
| `COLOR` | Selector de color HTML5 | `default_value="#ffffff"` |
| `LIST` | Selección múltiple | `options_endpoint="/api/playlists"`, `multiple=true` |

### Ejemplo con Endpoint de Opciones Dinámicas
```cpp
{
    .id = "theme",
    .type = ConfigType::ENUM,
    .label = "Tema del reloj",
    .default_value = "0",
    .options_endpoint = "/api/themes"
}
```

---

## 4. Control de Requisitos de Hardware (Gating)

Si su motor requiere periféricos específicos (ej: micrófono o PSRAM):
```cpp
desc.requirements.needsPsram = true;
desc.requirements.needsAudio = true;
```

`EngineRegistrar` evalúa automáticamente `HardwareHAL::capabilities()`. Si la placa conectada no dispone del hardware requerido, el motor se omite de forma segura y la interfaz web muestra una advertencia explicativa.
