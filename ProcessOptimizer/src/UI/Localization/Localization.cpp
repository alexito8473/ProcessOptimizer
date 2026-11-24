#include "Localization.h"

#include <array>

namespace UI::Localization
{
    namespace
    {
        constexpr std::size_t Count =
            static_cast<std::size_t>(Text::Count);

        // Las cadenas en espanol usan escapes octales UTF-8 para que los
        // bytes del literal sean siempre UTF-8 valido para ImGui, sin
        // depender de la pagina de codigos del compilador ni del archivo.
        const std::array<const char*, Count> EnglishTexts{{
            "ProcessOptimizer",
            "Dashboard",
            "CPU",
            "RAM",
            "GPU",
            "Processes",
            "Settings",

            "Overview",
            "Live system snapshot",
            "Combined usage",
            "CPU / RAM / all GPUs",
            "Key details",
            "Top processes",
            "by CPU + GPU load",
            "CPU USAGE",
            "RAM USAGE",
            "GPU USAGE",
            "PROCESSES",
            "Waiting for CPU data",
            "Waiting for memory data",
            "Waiting for GPU data",
            "Waiting for process data",
            "Waiting for usage samples...",
            "Name",
            "PID",
            "RAM",
            "dedicated",
            "Top",
            "CPU",
            "GPU",
            "Usage %",
            "Temp",
            "Power",
            "physical cores",
            "logical processors",
            "sockets",
            "GHz",
            "MT/s",
            "used of",
            "Available",
            "slots",
            "monitored processes",
            "Total processor usage",
            "Estimated frequency",
            "Detected physical cores",
            "Available logical processors",
            "Detected CPU packages",
            "PROCESSOR TOPOLOGY",
            "Physical and logical configuration",
            "FREQUENCY",
            "Processor performance information",
            "CACHE INFORMATION",
            "Processor cache hierarchy",
            "VIRTUALIZATION",
            "Hardware and hypervisor status",
            "SYSTEM ACTIVITY",
            "Operating system activity",

            "CPU MONITOR",
            "Real-time processor monitoring",
            "PERFORMANCE HISTORY",
            "Real-time processor utilization",
            "LOGICAL PROCESSORS",
            "Individual processor utilization",
            "Bars",
            "Mini charts",
            "HARDWARE INFORMATION",
            "Collecting data...",
            "No logical processors detected.",
            "No processes reported",

            "Appearance",
            "Theme mode",
            "System",
            "Light",
            "Dark",
            "Design",
            "Default",
            "Ocean",
            "Forest",
            "Sunset",
            "Graphite",
            "High contrast",
            "Language",
            "English",
            "Spanish",
            "Preview",
            "Buttons, cards, text and chart colors",
            "Reset to defaults",
            "Following Windows theme",
            "Buttons",
            "Primary",
            "Secondary",
            "Ghost",
            "Usage bar",
            "Color palette",
            "Saved automatically",

            "Monitoring service",
            "Local mode",
            "Service mode",
            "Connected",
            "Service status",
            "Running",
            "Stopped",
            "Not installed",
            "Start service",
            "Stop service"
        }};

        const std::array<const char*, Count> SpanishTexts{{
            "ProcessOptimizer",
            "Panel",
            "CPU",
            "RAM",
            "GPU",
            "Procesos",
            "Ajustes",

            "Resumen",
            "Estado del sistema en vivo",
            "Uso combinado",
            "CPU / RAM / todas las GPU",
            "Datos principales",
            "Procesos destacados",
            "por carga de CPU + GPU",
            "USO DE CPU",
            "USO DE RAM",
            "USO DE GPU",
            "PROCESOS",
            "Esperando datos de CPU",
            "Esperando datos de memoria",
            "Esperando datos de GPU",
            "Esperando datos de procesos",
            "Esperando muestras de uso...",
            "Nombre",
            "PID",
            "RAM",
            "dedicada",
            "Top",
            "CPU",
            "GPU",
            "Uso %",
            "Temp",
            "Potencia",
            "n\303\272cleos f\303\255sicos",
            "procesadores l\303\263gicos",
            "z\303\263calos",
            "GHz",
            "MT/s",
            "usados de",
            "Disponible",
            "ranuras",
            "procesos monitorizados",
            "Uso total del procesador",
            "Frecuencia estimada",
            "N\303\272cleos f\303\255sicos detectados",
            "Procesadores l\303\263gicos disponibles",
            "Paquetes de CPU detectados",
            "TOPOLOG\303\255A DEL PROCESADOR",
            "Configuraci\303\263n f\303\255sica y l\303\263gica",
            "FRECUENCIA",
            "Informaci\303\263n de rendimiento del procesador",
            "INFORMACI\303\323N DE CACH\303\211",
            "Jerarqu\303\255a de cach\303\251 del procesador",
            "VIRTUALIZACI\303\323N",
            "Estado de hardware e hipervisor",
            "ACTIVIDAD DEL SISTEMA",
            "Actividad del sistema operativo",

            "MONITOR DE CPU",
            "Monitorizaci\303\263n del procesador en tiempo real",
            "HISTORIAL DE RENDIMIENTO",
            "Uso del procesador en tiempo real",
            "PROCESADORES L\303\223GICOS",
            "Uso individual de cada procesador",
            "Barras",
            "Mini gr\303\241ficas",
            "INFORMACI\303\323N DE HARDWARE",
            "Recopilando datos...",
            "No se detectaron procesadores l\303\263gicos.",
            "No hay procesos informados",

            "Apariencia",
            "Modo de tema",
            "Sistema",
            "Claro",
            "Oscuro",
            "Dise\303\261o",
            "Predeterminado",
            "Oc\303\251ano",
            "Bosque",
            "Atardecer",
            "Grafito",
            "Alto contraste",
            "Idioma",
            "English",
            "Espa\303\261ol",
            "Vista previa",
            "Botones, tarjetas, texto y colores de gr\303\241ficas",
            "Restablecer valores",
            "Siguiendo el tema de Windows",
            "Botones",
            "Primario",
            "Secundario",
            "Fantasma",
            "Barra de uso",
            "Paleta de colores",
            "Guardado autom\303\241ticamente",

            "Servicio de monitorizaci\303\263n",
            "Modo local",
            "Modo servicio",
            "Conectado",
            "Estado del servicio",
            "En ejecuci\303\263n",
            "Detenido",
            "No instalado",
            "Iniciar servicio",
            "Detener servicio"
        }};
    }

    const char* Translate(Language language, Text text)
    {
        const std::size_t index =
            static_cast<std::size_t>(text);

        if (index >= Count)
            return "";

        return language == Language::Spanish
            ? SpanishTexts[index]
            : EnglishTexts[index];
    }

    const char* LanguageName(Language language)
    {
        return Translate(
            language,
            language == Language::Spanish
                ? Text::Spanish
                : Text::English);
    }
}
