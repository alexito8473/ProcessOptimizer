# ⚡ ProcessOptimizer

<div align="center">

### Una herramienta moderna de monitorización y optimización para Windows.

**Monitoriza tu hardware. Comprende tu sistema. Controla su rendimiento.**

![C++20](https://img.shields.io/badge/C++-20-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)
![Windows 11](https://img.shields.io/badge/Plataforma-Windows%2011-0078D6?style=for-the-badge&logo=windows&logoColor=white)
![DirectX 11](https://img.shields.io/badge/Gráficos-DirectX%2011-0078D6?style=for-the-badge)
![Visual Studio 2022](https://img.shields.io/badge/IDE-Visual%20Studio%202022-5C2D91?style=for-the-badge&logo=visualstudio&logoColor=white)
![Versión](https://img.shields.io/badge/Versión-1.3.0-blue?style=for-the-badge)
![Protocolo](https://img.shields.io/badge/Protocolo-10-informational?style=for-the-badge)

</div>

---

## 🖥️ Sobre el proyecto

**ProcessOptimizer** es una aplicación nativa para Windows desarrollada en C++20, diseñada para proporcionar información detallada sobre el hardware, los recursos y el rendimiento del sistema, y para optimizar el frame time de forma segura y reversible.

El proyecto se compone de **tres ejecutables independientes** que comparten un núcleo común (`Shared/`):

| Ejecutable | Función |
| --- | --- |
| `ProcessOptimizer.Service.exe` | Servicio de Windows (`LocalSystem`, arranque automático). Ejecuta `SystemMonitor`, es dueño de los históricos y expone un named pipe `\\.\pipe\ProcessOptimizer.Monitor`. |
| `ProcessOptimizer.UI.exe` | Interfaz completa. Consume el pipe; si el servicio no está disponible sigue monitorizando en local. Incluye el indicador de modo y los controles de servicio en Settings. |
| `ProcessOptimizer.Tray.exe` | Mini UI de bandeja. Tooltip con CPU/RAM/GPU, popup con el uso actual y el proceso con más carga, y menú contextual. |

El objetivo es conseguir una aplicación moderna, eficiente, modular, con consumo reducido de recursos y con optimización reversible que nunca toque el juego.

## 📝 Últimos cambios (1.3.0)

- **Modo único Balanceado**: se eliminan `Gaming` y `Multitasking`. Un solo interruptor: `Off = 0`, `Balanced = 1`.
- **Juego intacto por defecto**: sin afinidad dura, sin `SetPriorityClass` sobre el juego, sin `timeBeginPeriod`, sin CPU Sets sobre el juego (salvo perfil experimental opt-in).
- **Auto-calibración silenciosa**: al activar Balanceado el servicio corre A/B automáticamente (20 s + 20 s). La UI muestra progreso y veredicto; el A/B manual queda en la sección Avanzado.
- **Modo seguro para juegos sin DXGI** (Minecraft/OpenGL): si no hay muestras DXGI, la contención actúa con evidencia PDH, sin plan de energía ni GPU, máximo 1 proceso.
- **Fix Discord/micrófono**: los clientes de voz (Discord, Zoom, Teams, Slack, Skype, OBS, VoiceMeeter) están excluidos de la contención; se quita `IGNORE_TIMER_RESOLUTION` de EcoQoS; se añade histeresis; la auditoría se persiste en disco.
- **Fix ETW**: `StutterCorrelator::EtwLoop` usaba `logfile.LogFileName`; corregido a `logfile.LoggerName`.
- **Fix contención siempre 0**: refresco de primer plano cada ciclo, identidad válida con primer plano **o** juego, `Contain()` ya no aborta si falla power throttling/memoria.
- **ProtocolVersion = 10**, versiones UI/Servicio/Tray = `1.3.0`.
- **Reorganización**: `Shared/`, `ProcessOptimizer.Service/`, `ProcessOptimizer.Tray/` independientes.
- Instalador `Installer/Output/ProcessOptimizer-Setup.exe` regenerado.

## 🎯 Objetivos

- 📊 Monitorización del hardware en tiempo real.
- 🧠 Información detallada del procesador y sus núcleos lógicos.
- 💾 Seguimiento del consumo y estado de la memoria RAM.
- 🎮 Monitorización de la actividad de la GPU.
- ⚙️ Información y seguimiento de los procesos del sistema.
- 📈 Gráficas históricas de rendimiento.
- 🖥️ Interfaz modular con paneles independientes.
- 🚀 Arquitectura eficiente y escalable.
- 🔧 Optimización de frame time segura, reversible y medible.

---

## ✨ Funcionalidades

### 🧠 Monitorización de CPU
- Uso total e individual de procesadores lógicos en tiempo real.
- Topología, frecuencia, cachés, virtualización por hardware.
- Historial de utilización y gráficas por procesador lógico.

### 💾 Monitorización de memoria RAM
- Memoria física total/usada/porcentaje, comprometida, caché, pools.
- Información de módulos (capacidad, fabricante, localizador, velocidad).
- Historial y representación gráfica.

### 🎮 Monitorización de GPU
- Utilización, actividad, historial y dashboard independiente.
- Contadores PDH `GPU Engine` + detalles DXGI.

### ⚙️ Monitorización de procesos
- Enumeración, PID, uso de CPU/memoria, actividad GPU, motor gráfico.

### 🎨 Interfaz gráfica
- Dear ImGui + ImPlot, DirectX 11, modos `System/Light/Dark`, seis diseños, español/inglés.
- Renderizado bajo demanda (hash del draw data, `Present` solo si cambia).
- Componentes reutilizables (`Widgets.h`).

### 🛡️ Optimización (modo Balanceado)
- Un solo interruptor para el usuario normal.
- Auto-calibración silenciosa al activar.
- Modo seguro para juegos sin DXGI.
- Contención reversible de segundo plano (ProBalance-style).
- Auditoría en memoria y en disco (`containment-audit.log`).

---

## 🏗️ Arquitectura del proyecto

### Estructura de carpetas

```
ProcessOptimizer/                (raíz)
├── Shared/                     # Código compartido por los 3 ejecutables
│   ├── Core/Events/            # EventSystem, IEvent, Event
│   ├── IPC/                    # Protocol.h, PipeTransport
│   ├── Monitoring/             # Cpu, Gpu, Memory, Process, System monitors
│   ├── Optimization/           # CpuOrchestrator, BackgroundContainment, AbController, ...
│   └── Service/                # ServiceControl, MonitorState
├── ProcessOptimizer/           # Proyecto UI
│   ├── main.cpp
│   ├── src/UI/                 # Windows, Screens, Rendering, Components, ...
│   ├── src/UI/Localization/
│   ├── Assets/
│   └── ProcessOptimizer.vcxproj
├── ProcessOptimizer.Service/   # Proyecto servicio
│   ├── src/Service/            # ServiceApp, ServiceHost, ServiceControl (via Shared)
│   └── ProcessOptimizer.Service.vcxproj
├── ProcessOptimizer.Tray/      # Proyecto bandeja
│   ├── src/Tray/               # TrayApp, tray_main
│   └── ProcessOptimizer.Tray.vcxproj
├── Installer/
│   └── ProcessOptimizer.iss
└── ProcessOptimizer.slnx
```

### Componentes principales

1. **Core y eventos**: `EventSystem` publica snapshots (`CPU`, `MEMORY`, `GPU`, `PROCESS`) sin acoplar módulos.
2. **SystemMonitor**: coordina monitores con intervalos independientes (CPU 1 s, RAM/GPU 2 s, procesos 3 s).
3. **SharedState**: almacena snapshots e historiales (buffers circulares de 300 muestras) con mutex.
4. **Renderizado**: `ImGuiRenderer` (D3D11 + ImGui) e `ImGuiRegionRenderer` (regiones independientes).
5. **Servicio/UI/Bandeja**: comunicación por named pipe versionado; reconexión automática; modo local de respaldo.

---

## 🔄 Flujo de datos

```text
                WINDOWS API
                     |
                     v
              SYSTEM MONITORS
                     |
         +-----------+-----------+
         |           |           |
         v           v           v
        CPU        MEMORY       GPU
         |           |           |
         +-----------+-----------+
                     |
                     v
               PROCESS MONITOR
                     |
                     v
                 EVENT SYSTEM
                     |
                     v
                   WINDOW
                     |
                     v
                 SHARED STATE
                     |
           +---------+---------+
           |         |         |
           v         v         v
         CPU       MEMORY     GPU
       DASHBOARD  DASHBOARD DASHBOARD
           |         |         |
           +---------+---------+
                     |
                     v
              IMGUI RENDERER
                     |
                     v
                DIRECTX 11
                     |
                     v
                  DISPLAY
```

---

## 🛠️ Tecnologías utilizadas

| Tecnología | Utilización |
| --- | --- |
| **C++20** | Desarrollo principal |
| **Windows API / Win32** | Información del sistema y ventanas |
| **DirectX 11** | Renderizado mediante GPU |
| **Dear ImGui** | Interfaz gráfica |
| **ImPlot** | Gráficas y visualización |
| **Windows PDH** | Contadores de rendimiento |
| **DXGI** | Infraestructura gráfica |
| **vcpkg** | Gestión de dependencias |
| **Visual Studio 2022** | Entorno de desarrollo |
| **Inno Setup 7** | Instalador |

---

## ⚡ Renderizado bajo demanda

La interfaz no repinta en reposo. Cada snapshot invalida solo los componentes que dependen de él mediante `UI::Core::UiScheduler`:

- `CPU` → página CPU y sección CPU del dashboard.
- `MEMORY` → resumen, gráfico y módulos de RAM.
- `GPU` → página GPU y sección GPU del dashboard.
- `PROCESS` → tabla de procesos y destacados del dashboard.

El hover se atiende con `WM_MOUSEMOVE` sin realimentar `needsRedraw_`. `ImGuiRenderer::EndFrame()` calcula un hash del draw data y solo llama a `Present` cuando el frame cambió.

---

## 🛡️ Optimización: modo Balanceado

### Principios
- **Juego intacto por defecto**: sin afinidad dura, sin `SetPriorityClass` sobre el juego, sin `timeBeginPeriod`, sin CPU Sets sobre el juego (salvo perfil experimental opt-in).
- **Un solo modo activo**: `Off = 0`, `Balanced = 1`.
- **Windows decide la planificación**; `CpuOrchestrator` es pasivo en Balanceado.
- **Contención reversible** (`BackgroundContainment`) es el único actuador de segundo plano: EcoQoS, `MEMORY_PRIORITY_LOW`, CPU Sets fríos y prioridad temporal `BELOW_NORMAL` estilo ProBalance.
- **Clave de reversibilidad**: `PID + startTime`. Backup con `Get*`, restauración al perder foco/salir/desactivar y evidencia A/B o modo seguro previo.
- **Telemetría agregada**, sin muestra por frame por IPC. Servicio optimizado para usar poca CPU.

### Auto-calibración
Al activar Balanceado, el servicio corre A/B automáticamente:
- 20 s línea base + 20 s candidato.
- Conserva el candidato solo si p95 baja ≥ 2 %, p99 no empeora, frames > 25 ms no empeoran y CPU por frame no sube.
- La UI muestra progreso y veredicto. El A/B manual queda en la sección Avanzado.

### Modo seguro (juegos sin DXGI)
Para juegos OpenGL/Vulkan sin DXGI (Minecraft):
- Si no hay ≥ 10 muestras DXGI y el progreso ≥ 40 %, se activa modo seguro.
- Contención con evidencia PDH (context switches, page faults), máximo 1 proceso.
- Sin plan de energía ni GPU.
- Se resetea al desactivar o al completar un A/B positivo.

### Protección de voz/chat (fix Discord)
- `IsProtected()` excluye Discord (todas las ediciones), Zoom, Teams, Slack, Skype, OBS y VoiceMeeter.
- Se quita `IGNORE_TIMER_RESOLUTION` de EcoQoS para no romper captura WASAPI.
- Histeresis para evitar el vaivén contain→restore→contain.
- Auditoría persistida en `C:\ProgramData\ProcessOptimizer\containment-audit.log`.

---

## 🎨 Componentes, temas e idiomas

La UI se compone con widgets reutilizables de `ProcessOptimizer/src/UI/Components/Widgets/Widgets.h`. Configuración en `%APPDATA%\ProcessOptimizer\settings.json`:

```json
{
  "mode": "system",
  "design": "default",
  "language": "es"
}
```

- `mode`: `system`, `light` o `dark`.
- `design`: `default`, `ocean`, `forest`, `sunset`, `graphite`, `highcontrast`.
- `language`: `es` o `en`.

Los textos están centralizados en `ProcessOptimizer/src/UI/Localization/Localization.cpp`.

---

## 📁 Organización lógica del proyecto

Esquema conceptual (no sustituye el árbol real):

```text
ProcessOptimizer/
├── Shared/          (Core, IPC, Monitoring, Optimization, Service)
├── ProcessOptimizer/ (UI, main.cpp, Assets)
├── ProcessOptimizer.Service/ (servicio)
├── ProcessOptimizer.Tray/ (bandeja)
├── Installer/
└── ProcessOptimizer.slnx
```

---

## 🚀 Instalación y compilación

### Requisitos
- Windows 11 de 64 bits.
- Visual Studio 2022.
- Compilador compatible con C++20.
- Windows SDK.
- vcpkg.
- DirectX 11 compatible.
- Inno Setup 7 (para el instalador).

### Dependencias
```powershell
vcpkg install "imgui[win32-binding,dx11-binding]:x64-windows"
vcpkg install "implot:x64-windows"
```

### Compilar
```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" "ProcessOptimizer.slnx" /p:Configuration=Release /p:Platform=x64 /m:1 /v:minimal
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" "ProcessOptimizer.slnx" /p:Configuration=Debug /p:Platform=x64 /m:1 /v:minimal
```

### Instalador
```powershell
& "C:\Program Files\Inno Setup 7\ISCC.exe" "Installer\ProcessOptimizer.iss"
```

---

## 🔌 Protocolo y comunicación

`Shared/IPC/Protocol.h` define un mensaje binario versionado (`magic`, `versión`, `tipo`, `tamaño`). `PipeTransport.cpp` implementa servidor y cliente con mensajes `Ping` y `GetState`. `ProtocolVersion = 10`.

### Modo local y reconexión
La UI intenta conectar al iniciar; si no hay servicio, arranca el monitor local. Un hilo cliente sigue sondeando el pipe: cuando el servicio aparece, la UI deja de monitorizar en local y pasa a modo servicio; si el servicio cae, vuelve al modo local automáticamente.

### Bandeja
El tray usa un mutex nominal para no duplicar iconos. El popup se muestra con `WM_MOUSEMOVE` del icono y se oculta cuando el cursor sale del icono y del popup. El menú permite abrir la UI, arrancar/parar el servicio y salir.

### Permisos del servicio
El servicio concede `SERVICE_START | SERVICE_STOP | SERVICE_QUERY_STATUS` a usuarios autenticados e interactivos, para que la UI y la bandeja puedan arrancarlo/pararlo sin UAC. El named pipe tiene DACL explícita para permitir conexión desde la sesión 0.

### Logo e iconos
`ProcessOptimizer/Assets/logo.ico` está embebido mediante `App.rc` en los tres ejecutables. `Assets/logo.png` se distribuye como recurso gráfico y el instalador usa el `.ico`.

---

## 📄 Licencia / estado

Proyecto en desarrollo. Todos los cambios de optimización son reversibles y se validan con A/B o modo seguro antes de aplicarse.