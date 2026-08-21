🇬🇧 [English](ARCHITECTURE.md) | 🇫🇷 [Français](ARCHITECTURE_FR.md) | 🇪🇸 Español

# Resumen de Arquitectura (ESP32 - C++)

Este documento proporciona una visión general detallada de la arquitectura de ArcadeMatrix en ESP32 desarrollada en **C++**. Explica las restricciones de hardware, la estrategia de hilos de doble núcleo, la tubería de renderizado y el ciclo de vida "Lazy-Once" de los motores.

---

## 1. Filosofía Central: Restricciones de Hardware

A diferencia de la versión para Raspberry Pi, la versión para ESP32 está desarrollada en **C++** y construida en torno a severas restricciones de hardware:
- **Límites de RAM y PSRAM:** La arquitectura del núcleo está optimizada para ejecutarse en el mínimo común denominador (un ESP32 estándar con ~320 KB de RAM libre). Sin embargo, ArcadeMatrix soporta plenamente placas avanzadas como el **ESP32-S3** con PSRAM (hasta 16 MB). *Atención:* algunos motores que consumen mucha memoria (como `CryptoEngine` y `StockEngine` que almacenan grandes gráficos históricos y analizan enormes cargas JSON de API) **requieren estrictamente PSRAM** para funcionar. Los motores que caben en los 320 KB la usan, mientras que aquellos que requieren PSRAM fallarán si se activan en un ESP32 estándar. La fragmentación del montón (Heap) sigue siendo nuestro mayor enemigo, de ahí la importancia del ciclo de vida controlado.
- **Restricciones de CPU (240 MHz):** Para mantener 60 FPS en la matriz, el renderizado debe ser extremadamente rápido.
- **Acceso Directo DMA:** Las primitivas de dibujo se escriben directamente en el búfer de hardware DMA I2S sin un sistema operativo intermedio.

---

## 2. El Ciclo de Vida "Lazy-Once" y Estrategia de Memoria

Para evitar pánicos del núcleo (Kernel Panics) causados por la fragmentación del montón con el tiempo, la arquitectura C++ se basa en un modelo estricto de ciclo de vida **Lazy-Once** a través de un patrón de Fábrica (Factory).

```mermaid
graph TD
                 Registry[Engine Registry]
                       │
                 Descriptor[EngineDescriptor]
                       │
                Factory[Lambda Factory]
                       │
                 Instance[IEngine (std::unique_ptr)]
                       │
              ┌────────┴────────┐
              │                 │
       Context[ApplicationContext] Config[DictionaryEngineConfig]
              │                 │
              └────────┬────────┘
                       │
                 Manager[RotationManager]
                       │
          ┌────────────┼────────────┐
          │            │            │
       activate      update       render
          │            │            │
          └────────────┼────────────┘
                       │
                  deactivate
```

### Explicación de las Fases (C++):

1. **`initialize()` (Asignación):**
   * Llamado *exactamente una vez* la primera vez que el motor debe mostrarse.
   * Este es el **ÚNICO** lugar donde deben ocurrir asignaciones de `new`, `std::vector` o `String`. Debe preasignar toda la memoria necesaria aquí.
2. **`activate()` (Preparación Temporal):**
   * Llamado cada vez que el motor se activa. Restablece temporizadores o estados temporales sin asignar memoria.
3. **`update()` & `render()` (Hot Loop - 60 FPS):**
   * **Restricción:** **ESTRICTAMENTE NINGUNA ASIGNACIÓN DINÁMICA INNECESARIA.** No use concatenación de `String`, no llame a `malloc`. Mute arreglos preasignados.
4. **`deactivate()` (Espera):**
   * Libera conexiones de red temporales o detiene la escucha.

### ¿Por qué el Patrón Factory?
Si instanciáramos globalmente todos los motores C++ en el arranque, el montón de 320 KB se agotaría instantáneamente. El Registro almacena objetos `EngineDescriptor` livianos que contienen una función lambda (`factory`). El `RotationManager` solo llama a esta fábrica cuando un motor está programado para aparecer por primera vez, manteniendo los motores inactivos fuera de la RAM.

---

## 3. Arquitectura de Doble Núcleo

El ESP32 es un microcontrolador de doble núcleo. ArcadeMatrix explota esto para separar las responsabilidades:

1. **Núcleo 1 (App Core): Bucle de Renderizado**
   - La función `loop()` se ejecuta aquí.
   - Responsable de llamar a `update()` y `render()` exactamente a 60 FPS.
   - Empuja píxeles a través del DMA I2S. **Nunca debe bloquearse.**

2. **Núcleo 0 (Pro Core): Red y API**
   - Maneja el Servidor Web, API REST, conexiones Wi-Fi y tareas asincrónicas.
   - Modificar la configuración a través de la API (Núcleo 0) actualiza el `DictionaryEngineConfig` de forma segura mientras el Núcleo 1 continúa renderizando.

---

## 4. Hardware HAL (Aislamiento I2C / I2S)

Dado que el ESP32 comparte pines y buses, el `HardwareHAL` abstrae la capa de hardware:
- Gestiona el bus I2C (para RTC, sensores) y garantiza la seguridad de los hilos.
- Previene colisiones entre la tarjeta SD (SPI/SDMMC) y el DMA de la Matriz LED (I2S).
