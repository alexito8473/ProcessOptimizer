
<div align="center">

# ⚡ ProcessOptimizer

### Una herramienta moderna de monitorización y optimización para Windows.

Monitoriza tu hardware. Comprende tu sistema. Controla su rendimiento.

<br/>

![C++20](https://img.shields.io/badge/C++-20-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)
![Windows 11](https://img.shields.io/badge/Plataforma-Windows%2011-0078D6?style=for-the-badge&logo=windows&logoColor=white)
![DirectX 11](https://img.shields.io/badge/Gráficos-DirectX%2011-0078D6?style=for-the-badge)
![Desarrollo](https://img.shields.io/badge/Estado-En%20desarrollo-orange?style=for-the-badge)

<br/>

</div>

---

## 🖥️ Sobre el proyecto

**ProcessOptimizer** es una aplicación nativa para Windows diseñada para ofrecer una visión detallada y en tiempo real del hardware y del rendimiento del ordenador.

El objetivo es desarrollar una herramienta de monitorización moderna, ligera y completa que reúna estadísticas del hardware, gráficos de rendimiento y gestión de procesos en una única aplicación.

Desarrollada en **C++20**, ProcessOptimizer prioriza el rendimiento, la eficiencia y una arquitectura modular.

---

## 🎯 Objetivos del proyecto

- 📊 Monitorización del rendimiento del sistema en tiempo real.
- ⚙️ Información detallada del hardware.
- 🧠 Monitorización del procesador y sus núcleos lógicos.
- 📈 Gráficos de rendimiento e historial de actividad.
- 🖥️ Interfaz de escritorio moderna e intuitiva.
- 🚀 Bajo consumo de recursos.
- 🔧 Incorporación progresiva de herramientas de optimización.

---

## ✨ Funcionalidades

### 🧠 Monitorización de CPU

- Uso total del procesador en tiempo real.
- Uso individual de cada procesador lógico.
- Detección de núcleos físicos y procesadores lógicos.
- Información sobre topología y sockets.
- Frecuencia estimada del procesador.
- Información de los distintos niveles de caché.
- Detección de virtualización por hardware.
- Estadísticas de actividad del sistema.
- Gráficas históricas de rendimiento.

### 🎨 Interfaz gráfica

- Aplicación nativa para Windows.
- Interfaz moderna con tema oscuro.
- Panel principal de rendimiento.
- Gráficas en tiempo real.
- Monitorización individual de procesadores.
- Componentes visuales modulares.

---

## 🛠️ Tecnologías utilizadas

| Tecnología | Utilización |
|:---|:---|
| **C++20** | Desarrollo de la aplicación |
| **Win32 API** | Integración nativa con Windows |
| **DirectX 11** | Renderizado gráfico |
| **Dear ImGui** | Interfaz de usuario |
| **ImPlot** | Visualización de estadísticas |
| **Windows PDH** | Contadores de rendimiento |
| **Windows API** | Obtención de información del hardware |
| **vcpkg** | Gestión de dependencias |
| **Visual Studio 2022** | Entorno de desarrollo |

---

## 🗺️ Hoja de ruta

El proyecto se encuentra en desarrollo. Estas son las principales funcionalidades previstas:

| Módulo | Estado |
|:---|:---|
| Monitorización de CPU | 🟢 En desarrollo |
| Panel de CPU | 🟢 En desarrollo |
| Monitorización de memoria RAM | ⚪ Planificado |
| Monitorización de GPU | ⚪ Planificado |
| Administrador de procesos | ⚪ Planificado |
| Optimización del sistema | ⚪ Planificado |
| Configuración de la aplicación | ⚪ Planificado |

---

## 🏗️ Arquitectura del proyecto

ProcessOptimizer utiliza una arquitectura modular que separa la monitorización, la comunicación mediante eventos y la representación de la interfaz gráfica.

```text
ProcessOptimizer/
│
├── Core/
│   └── Events/
│
├── Monitoring/
│   ├── CpuMonitor/
│   └── SystemMonitor/
│
├── UI/
│   ├── Windows/
│   ├── Dashboard/
│   │   └── Cpu/
│   └── Components/
│       └── Charts/
│
└── ProcessOptimizer.sln
```

La capa de monitorización recopila información del sistema y publica actualizaciones mediante un sistema de eventos. La interfaz recibe estos datos y los representa de forma independiente.

Esta separación facilita el mantenimiento y la incorporación de nuevos módulos.

---

## 🚀 Instalación y compilación

### Requisitos

- Windows 11 (64 bits).
- Visual Studio 2022.
- Compilador compatible con C++20.
- Windows SDK.
- Gestor de paquetes vcpkg.

### Instalación de dependencias

Instala las bibliotecas necesarias mediante vcpkg:

```powershell
vcpkg install "imgui[win32-binding,dx11-binding]:x64-windows"
vcpkg install implot:x64-windows
```

### Compilar el proyecto

**1. Clonar el repositorio**

```bash
git clone https://github.com/alexito8473/ProcessOptimizer.git
```

**2. Abrir la solución**

Abre `ProcessOptimizer.sln` con Visual Studio 2022.

**3. Configurar la compilación**

Selecciona:

- Configuración: `Release`
- Plataforma: `x64`

**4. Compilar**

Compila la solución desde Visual Studio.

---

## 🔮 Visión del proyecto

El objetivo a largo plazo de ProcessOptimizer es convertirse en una herramienta completa para comprender, monitorizar y gestionar el rendimiento de los equipos Windows.

Desde la monitorización del hardware hasta la gestión de procesos y la optimización del sistema, todo estará centralizado en una aplicación ligera, intuitiva y eficiente.

---

<div align="center">

### ⚡ ProcessOptimizer

**Desarrollado con C++20. Diseñado para Windows.**

[Ver repositorio en GitHub](https://github.com/alexito8473/ProcessOptimizer)

</div>

