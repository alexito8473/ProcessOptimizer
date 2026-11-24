# ⚡ ProcessOptimizer

<div align="center">

### Una herramienta moderna de monitorización y optimización para Windows.

**Monitoriza tu hardware. Comprende tu sistema. Controla su rendimiento.**

![C++20](https://img.shields.io/badge/C++-20-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)
![Windows 11](https://img.shields.io/badge/Plataforma-Windows%2011-0078D6?style=for-the-badge&logo=windows&logoColor=white)
![DirectX 11](https://img.shields.io/badge/Gráficos-DirectX%2011-0078D6?style=for-the-badge)
![Visual Studio 2022](https://img.shields.io/badge/IDE-Visual%20Studio%202022-5C2D91?style=for-the-badge&logo=visualstudio&logoColor=white)
![Estado](https://img.shields.io/badge/Estado-En%20desarrollo-orange?style=for-the-badge)

</div>

---

## 🖥️ Sobre el proyecto

**ProcessOptimizer** es una aplicación nativa para Windows desarrollada en C++20, diseñada para proporcionar información detallada sobre el hardware, los recursos y el rendimiento del sistema.

El proyecto está evolucionando hacia una herramienta de monitorización integral que centraliza CPU, memoria RAM, GPU y procesos en una única interfaz.

Su arquitectura está organizada en módulos independientes para separar la recopilación de datos, la comunicación entre componentes, el almacenamiento de estadísticas y la representación gráfica.

El objetivo es conseguir una aplicación moderna, eficiente, modular y con un consumo reducido de recursos.

## 📝 Últimos cambios

- **Dashboard inicial** con tarjetas de CPU, RAM, GPU y procesos, y gráfico de
  líneas continuas con una serie por adaptador gráfico.
- **Interfaz fluida a 60 FPS**: el ratón y el scroll no se limitan a Hz fijos y
  el hover no fuerza repintados.
- **Menor consumo de GPU**: `Present` solo se llama cuando el frame cambia
  realmente (hash del draw data), con un consumo observado de ~2 % GPU.
- **Corrección de los minicharts de CPU**: cada procesador lógico recibe ahora
  sus muestras correctas en lugar de mostrar `Collecting data...`.
- **Componentes reutilizables**: títulos, tarjetas, badges, botones, barras de
  uso, controles segmentados y muestras de color.
- **Temas e idiomas**: modos `System / Light / Dark`, seis diseños e inglés y
  español, configurables desde Settings o desde `settings.json`.
- **Textos en UTF-8 correcto**: tildes y `ñ` se renderizan bien en español.

## 🎯 Objetivos

- 📊 Monitorización del hardware en tiempo real.
- 🧠 Información detallada del procesador y sus núcleos lógicos.
- 💾 Seguimiento del consumo y estado de la memoria RAM.
- 🎮 Monitorización de la actividad de la GPU.
- ⚙️ Información y seguimiento de los procesos del sistema.
- 📈 Gráficas históricas de rendimiento.
- 🖥️ Interfaz modular con paneles independientes.
- 🚀 Arquitectura eficiente y escalable.
- 🔧 Incorporación progresiva de herramientas de optimización.

---

## ✨ Funcionalidades

### 🧠 Monitorización de CPU

El módulo de CPU proporciona información sobre el procesador y su actividad.

- Uso total del procesador en tiempo real.
- Uso individual de los procesadores lógicos.
- Información sobre núcleos físicos y procesadores lógicos.
- Detección de la topología del procesador.
- Información de frecuencia del procesador.
- Información de los distintos niveles de caché.
- Detección de virtualización por hardware.
- Estadísticas de actividad.
- Historial de utilización.
- Gráficas individuales de los procesadores lógicos.

El dashboard de CPU permite visualizar la actividad general y analizar el comportamiento de los procesadores lógicos mediante gráficas independientes.

### 💾 Monitorización de memoria RAM

El módulo de memoria proporciona una visión general del estado de la memoria física y sus principales métricas.

- Memoria física total.
- Memoria utilizada.
- Porcentaje de utilización.
- Información de los módulos de memoria detectados.
- Capacidad de cada módulo.
- Fabricante y localizador del dispositivo.
- Velocidad configurada y velocidad detectada, cuando está disponible.
- Número de ranuras ocupadas y totales.
- Memoria comprometida y límite de confirmación.
- Memoria en caché.
- Información de los pools paginado y no paginado.
- Historial de utilización de memoria.
- Representación gráfica de las estadísticas.

La información de los módulos depende de los datos que proporcione el sistema y de la disponibilidad de la información del hardware.

### 🎮 Monitorización de GPU

El módulo gráfico está diseñado para mostrar la actividad de la tarjeta gráfica.

- Seguimiento de la utilización de GPU.
- Información de actividad gráfica.
- Visualización de estadísticas mediante gráficas.
- Historial de rendimiento.
- Dashboard independiente para la monitorización gráfica.

La obtención de métricas depende de las capacidades del hardware y de los mecanismos de monitorización disponibles en Windows.

### ⚙️ Monitorización de procesos

El módulo de procesos amplía la monitorización desde el hardware hasta las aplicaciones que se ejecutan en el sistema.

La arquitectura contempla:

- Enumeración de procesos.
- Identificación mediante PID.
- Seguimiento de utilización de CPU.
- Seguimiento del consumo de memoria.
- Asociación de actividad GPU con los procesos.
- Identificación del motor gráfico utilizado.
- Organización de la información por proceso.

La integración de las métricas de procesos y GPU continúa en desarrollo.

### 🎨 Interfaz gráfica

La aplicación utiliza Dear ImGui e ImPlot para construir una interfaz de escritorio personalizada.

Características:

- Interfaz nativa para Windows.
- Modos de tema `System / Light / Dark` y seis diseños de paleta.
- Español e inglés, seleccionables en Settings o en `settings.json`.
- Paneles independientes.
- Navegación entre secciones.
- Gráficas de rendimiento.
- Visualización de estadísticas en tiempo real.
- Componentes visuales modulares reutilizables.
- Renderizado mediante DirectX 11.

Secciones disponibles en la interfaz:

| Sección   | Contenido                             |
| :-------- | :------------------------------------ |
| Dashboard | Vista general del sistema             |
| CPU       | Información y gráficas del procesador |
| RAM       | Memoria física y estadísticas         |
| GPU       | Actividad y estadísticas gráficas     |
| Processes | Información de los procesos           |
| Settings  | Configuración de la aplicación        |

---

## 🏗️ Arquitectura del proyecto

ProcessOptimizer utiliza una arquitectura modular que separa la monitorización del sistema y la interfaz gráfica.

### 1. Core y sistema de eventos

El núcleo contiene el sistema de eventos utilizado para comunicar los diferentes módulos.

Los componentes pueden publicar y recibir actualizaciones sin depender directamente de la implementación interna de otros módulos.

Los eventos principales son:

| Evento    | Información                 |
| :-------- | :-------------------------- |
| `CPU`     | Estadísticas del procesador |
| `MEMORY`  | Estadísticas de memoria RAM |
| `GPU`     | Estadísticas de la GPU      |
| `PROCESS` | Información de procesos     |

El sistema utiliza plantillas para trabajar con distintos tipos de snapshots.

### 2. SystemMonitor

`SystemMonitor` coordina los módulos encargados de recopilar información del sistema.

Sus responsabilidades incluyen:

- Iniciar y detener la monitorización.
- Coordinar los monitores individuales.
- Recopilar los snapshots.
- Publicar los datos mediante `EventSystem`.
- Mantener independientes los ciclos de actualización.

Los intervalos de actualización configurados actualmente son:

| Módulo      |  Intervalo |
| :---------- | ---------: |
| CPU         |  1 segundo |
| Memoria RAM | 2 segundos |
| GPU         | 2 segundos |
| Procesos    | 3 segundos |

Estos intervalos permiten actualizar cada módulo de forma independiente sin obligar a todos los componentes a trabajar con la misma frecuencia.

### 3. SharedState y comunicación con la interfaz

La ventana principal recibe los eventos publicados por `SystemMonitor` y actualiza un estado compartido.

`SharedState` almacena los snapshots y los historiales utilizados por los dashboards.

Entre sus responsabilidades están:

- Mantener los últimos datos recibidos.
- Conservar los historiales de rendimiento.
- Sincronizar el acceso a los datos mediante mutex.
- Facilitar el acceso de la interfaz a las estadísticas.
- Evitar que los dashboards dependan directamente de los monitores.

Los historiales de CPU y memoria utilizan actualmente buffers circulares de 300 muestras.

### 4. Renderizado gráfico

La representación de la interfaz se divide entre el renderizador principal y el renderizador de regiones.

**ImGuiRenderer**

Se encarga de:

- Crear el dispositivo Direct3D 11.
- Gestionar el Swap Chain.
- Crear y administrar el render target principal.
- Inicializar el contexto de Dear ImGui.
- Inicializar los backends Win32 y DirectX 11.
- Gestionar el ciclo de renderizado.
- Presentar los fotogramas en pantalla.

**ImGuiRegionRenderer**

Permite renderizar paneles en regiones independientes.

Su implementación contempla:

- Contextos independientes de ImGui.
- Contextos independientes de ImPlot.
- Texturas Direct3D 11 por región.
- Render targets individuales.
- Shader Resource Views para mostrar las regiones.
- Redimensionamiento de las superficies de renderizado.
- Transferencia de información de entrada desde la ventana principal.

Esta arquitectura permite separar la representación gráfica de cada panel y facilita su evolución independiente.

---

## 🔄 Flujo de datos

El funcionamiento general sigue este esquema:

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

La comunicación mediante eventos permite mantener la recopilación de información separada de la representación visual.

---

## 🛠️ Tecnologías utilizadas

| Tecnología             | Utilización                              |
| :--------------------- | :--------------------------------------- |
| **C++20**              | Desarrollo principal                     |
| **Windows API**        | Obtención de información del sistema     |
| **Win32 API**          | Gestión de ventanas y mensajes           |
| **DirectX 11**         | Renderizado mediante GPU                 |
| **Dear ImGui**         | Interfaz gráfica                         |
| **ImPlot**             | Gráficas y visualización de estadísticas |
| **Windows PDH**        | Contadores de rendimiento                |
| **DXGI**               | Infraestructura gráfica y dispositivos   |
| **vcpkg**              | Gestión de dependencias                  |
| **Visual Studio 2022** | Entorno de desarrollo                    |

---

## 📁 Organización lógica del proyecto

La estructura modular se organiza alrededor de las siguientes responsabilidades:

```text
ProcessOptimizer/
│
├── Core/
│   └── Events/
│       ├── Event.h
│       └── EventSystem.h
│
├── Monitoring/
│   ├── CPU/
│   │   └── CpuMonitor/
│   │
│   ├── Memory/
│   │   └── MemoryMonitor/
│   │
│   ├── GPU/
│   │   └── GpuMonitor/
│   │
│   ├── Process/
│   │   ├── ProcessMonitor/
│   │   ├── ProcessEnumerator/
│   │   ├── ProcessPerformance/
│   │   └── ProcessGpu/
│   │
│   └── SystemMonitor/
│
├── UI/
│   ├── Windows/
│   │
│   ├── Dashboard/
│   │   ├── CPU/
│   │   ├── Memory/
│   │   ├── GPU/
│   │   └── Processes/
│   │
│   ├── Components/
│   │   └── Charts/
│   │
│   └── Rendering/
│       ├── ImGuiRenderer/
│       ├── ImGuiRegionRenderer/
│       └── DirectX11/
│
└── ProcessOptimizer.sln
```

_Esquema conceptual de los módulos principales; no pretende sustituir el árbol de archivos real del repositorio._

---

## 🚀 Instalación y compilación

### Requisitos

- Windows 11 de 64 bits.
- Visual Studio 2022.
- Compilador compatible con C++20.
- Windows SDK.
- vcpkg.
- DirectX 11 compatible.

### Dependencias

Instala las dependencias utilizadas por la interfaz gráfica:

```powershell
vcpkg install "imgui[win32-binding,dx11-binding]:x64-windows"
vcpkg install "implot:x64-windows"
```

---

## ⚡ Renderizado bajo demanda

La interfaz no repinta a 60 FPS en reposo. Cada snapshot de monitorización
solo invalida los componentes que dependen de él mediante
`UI::Core::UiScheduler` (`src/UI/Rendering/Core/UiScheduler/UiScheduler.h`):

- `CPU` → página CPU y sección CPU del dashboard.
- `MEMORY` → resumen, gráfico y módulos de RAM, y sección RAM del dashboard.
- `GPU` → página GPU y sección GPU del dashboard.
- `PROCESS` → tabla de procesos y procesos destacados del dashboard.

El dashboard inicial (`src/UI/Screens/Dashboard/OverviewDashboard.cpp`) reúne
tarjetas de CPU/RAM/GPU/Procesos, un gráfico de líneas continuas con CPU en
azul, RAM en verde y una línea por cada adaptador GPU detectado, y los procesos
con mayor carga. Todas las series comparten índice temporal para que las
líneas estén alineadas; el panel de detalles también lista todas las GPU.

El hover se atiende con mensajes de entrada (`WM_MOUSEMOVE`), no realimentando
`needsRedraw_`. La interfaz se construye a 60 Hz para que la respuesta sea
inmediata, pero `ImGuiRenderer::EndFrame()` calcula un hash del draw data y
solo sube los buffers a GPU y llama a `Present` cuando el frame cambió de
verdad. Mover el ratón sobre zona vacía o dejar la UI quieta no presenta nada.
Las páginas GPU y Processes se dibujan en el contexto principal de
ImGui/ImPlot; el renderizador de regiones sigue sin activarse.

---

## 🎨 Componentes, temas e idiomas

La UI se compone con widgets reutilizables de
`src/UI/Components/Widgets/Widgets.h`: títulos, texto atenuado, tarjetas,
badges, botones primarios/secundarios/fantasma, controles segmentados, barras
de uso y muestras de color. El sidebar, el dashboard y Settings ya los usan.

### Configuración

Toda la configuración se guarda en:

```text
%APPDATA%\ProcessOptimizer\settings.json
```

Se crea automáticamente con valores por defecto. Puedes editarlo con la app
cerrada y se aplicará al relanzar:

```json
{
  "mode": "system",
  "design": "default",
  "language": "es"
}
```

- `mode`: `system` (por defecto, sigue el tema de Windows), `light` o `dark`.
- `design`: `default`, `ocean`, `forest`, `sunset`, `graphite` o
  `highcontrast`.
- `language`: `es` o `en` (por defecto `es`).

Cuando `mode` es `system`, la app escucha `WM_SETTINGCHANGE` y reaplica el tema
si cambia el modo claro/oscuro de Windows.

### Idiomas

Los textos están centralizados en `src/UI/Localization/Localization.cpp`.
Están traducidos el sidebar, Overview, Settings y la página CPU. Las páginas
RAM, GPU y Processes se migrarán de forma progresiva.

Los caracteres no ASCII de los textos españoles se escriben como escapes
octales UTF-8 (`\303\261` = ñ, `\303\251` = é, ...). No usar escapes de bytes
de Windows-1252 (`\361`, `\251`, ...) ni `\xNN` seguido de una letra hexadecimal,
porque ImGui necesita UTF-8 y MSVC interpreta mal esas secuencias.

### Corrección de minicharts de CPU

`SharedState::processorHistories` ahora se rellena en cada snapshot de CPU,
usando `processorGroup + logicalProcessorIndex` como clave. Antes el historial
existía pero nadie lo alimentaba, por eso cada procesador lógico mostraba
`Collecting data...`.

## Arquitectura servicio / UI / bandeja

La monitorización se ha separado en tres ejecutables independientes:

| Ejecutable | Función |
| --- | --- |
| `ProcessOptimizer.Service.exe` | Servicio de Windows (`LocalSystem`, arranque automático). Ejecuta `SystemMonitor`, es dueño de los históricos y expone un named pipe `\\.\pipe\ProcessOptimizer.Monitor`. |
| `ProcessOptimizer.UI.exe` | Interfaz completa. Consume el pipe; si el servicio no está disponible sigue monitorizando en local. Incluye el indicador de modo y los botones Iniciar/Detener servicio en Settings. |
| `ProcessOptimizer.Tray.exe` | Mini UI de bandeja separada. Tooltip con CPU/RAM/GPU, popup con el uso actual y el proceso con más carga, y menú contextual. |

Los tres pueden estar activos a la vez o por separado. El servicio mantiene el
histórico, así que cerrar y volver a abrir la UI no pierde las gráficas.

Si DXGI no expone adaptadores desde la sesión 0, el servicio conserva los
contadores PDH `GPU Engine` y los detalles DXGI los consulta la UI en modo local.

### Protocolo

`src/IPC/Protocol.h` define un mensaje binario versionado con `magic`, `versión`,
`tipo` y `tamaño`. `PipeTransport.cpp` implementa el servidor y el cliente con
mensajes `Ping` y `GetState`. No se envían `HICON` entre procesos: la UI obtiene
los iconos de proceso por su cuenta.

### Modo local y reconexión

La UI intenta conectar al iniciar. Si no hay servicio, arranca el monitor local.
Un hilo cliente sigue sondeando el pipe: cuando el servicio aparece, la UI deja
de monitorizar en local y pasa a modo servicio; si el servicio cae, vuelve al
modo local automáticamente.

### Bandeja

El tray usa un mutex nominal para no duplicar iconos. El popup se muestra con
`WM_MOUSEMOVE` del icono y se oculta cuando el cursor sale del icono y del popup.
El menú permite abrir la UI, arrancar/parar el servicio y salir.

### Instalador

`Installer/ProcessOptimizer.iss` instala los tres ejecutables, registra el
servicio con `--install --start` y lo desinstala con `--stop --uninstall`.
Concede a los usuarios interactivos `SERVICE_START`, `SERVICE_STOP` y
`SERVICE_QUERY_STATUS` para que los botones funcionen sin elevación de UAC.

El `.iss` se compila con Inno Setup (`ISCC.exe`). Si no está instalado en este
equipo, el instalador no se puede regenerar aquí; el script queda listo.

### Permisos del servicio

El servicio concede `SERVICE_START | SERVICE_STOP | SERVICE_QUERY_STATUS` a
usuarios autenticados e interactivos, para que la UI y la bandeja puedan
arrancarlo/pararlo sin UAC. El propio servicio reaplica esa DACL al arrancar,
así que basta con reiniciarlo una vez después de actualizar desde una versión
anterior que no la ajustaba.

El named pipe también tiene una DACL explícita (`Assets/logo.ico` no influye):
usuarios `SYSTEM`, administradores, usuarios autenticados e interactivos pueden
conectar, incluso cuando el servidor corre como `LocalSystem` en la sesión 0.

### Logo e iconos

`ProcessOptimizer/Assets/logo.ico` está embebido mediante `App.rc` en los tres
ejecutables: es el icono de la ventana de UI, del icono de bandeja y de los
archivos `.exe`. `Assets/logo.png` se distribuye como recurso gráfico y el
instalador usa el `.ico` para el setup, los accesos directos y el desinstalador.
