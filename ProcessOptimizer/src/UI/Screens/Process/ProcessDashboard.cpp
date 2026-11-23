
#include "ProcessDashboard.h"
#include <cctype>
#include <cstdio>
#include <utility>
#include <imgui.h>
#include <string>
#include <algorithm>
#include <Windows.h>

#include <shellapi.h>

static std::string WideToUtf8(const std::wstring& text)
{
    if (text.empty())
        return {};

    const int size = WideCharToMultiByte(
        CP_UTF8,
        0,
        text.c_str(),
        -1,
        nullptr,
        0,
        nullptr,
        nullptr
    );

    if (size <= 1)
        return {};

    std::string result(static_cast<size_t>(size), '\0');

    const int converted = WideCharToMultiByte(
        CP_UTF8,
        0,
        text.c_str(),
        -1,
        result.data(),
        size,
        nullptr,
        nullptr
    );

    if (converted == 0)
        return {};

    result.pop_back(); // Eliminar el terminador nulo.
    return result;
}
void ProcessDashboard::Render(const ProcessSnapshot& snapshot)
{
    RenderHeader(snapshot);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    RenderProcessTable(snapshot);
}

void ProcessDashboard::RenderHeader(const ProcessSnapshot& snapshot)
{
    const size_t processCount = snapshot.processes.size();

    ImGui::Text("Process Monitoring");
    ImGui::SameLine();

    ImGui::TextDisabled("| %zu processes", processCount);

    ImGui::Spacing();

    // Resumen general.
    double totalCpu = 0.0;
    SIZE_T totalMemory = 0;

    for (const ProcessInfo& process : snapshot.processes)
    {
        totalCpu += process.cpuUsagePercent;
        totalMemory += process.workingSetBytes;
    }

    const float availableWidth = ImGui::GetContentRegionAvail().x;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float cardWidth = (availableWidth - spacing * 2.0f) / 3.0f;

    auto RenderCard = [](const char* title, const char* value, float width)
        {
            ImGui::BeginChild(
                title,
                ImVec2(width, 75.0f),
                true
            );

            ImGui::TextDisabled("%s", title);
            ImGui::Spacing();
            ImGui::Text("%s", value);

            ImGui::EndChild();
        };

    char processBuffer[32];
    char cpuBuffer[32];
    char memoryBuffer[64];

    snprintf(processBuffer, sizeof(processBuffer), "%zu", processCount);
    snprintf(cpuBuffer, sizeof(cpuBuffer), "%.1f%%", totalCpu);

    const double memoryMB =
        static_cast<double>(totalMemory) / (1024.0 * 1024.0);

    snprintf(memoryBuffer, sizeof(memoryBuffer), "%.1f MB", memoryMB);

    RenderCard("Processes", processBuffer, cardWidth);
    ImGui::SameLine();

    RenderCard("Combined CPU", cpuBuffer, cardWidth);
    ImGui::SameLine();

    RenderCard("Memory", memoryBuffer, cardWidth);
}

void ProcessDashboard::RenderProcessTable(const ProcessSnapshot& snapshot)
{
    static ImGuiTableFlags tableFlags =
        ImGuiTableFlags_Borders |
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Resizable |
        ImGuiTableFlags_Reorderable |
        ImGuiTableFlags_ScrollX |
        ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_SizingFixedFit;

    // Barra de búsqueda.
    ImGui::SetNextItemWidth(350.0f);
    ImGui::InputTextWithHint(
        "##ProcessSearch",
        "Buscar proceso por nombre o PID...",
        searchBuffer_,
        sizeof(searchBuffer_)
    );

    ImGui::Spacing();

    if (!ImGui::BeginTable(
        "ProcessTable",
        10,
        tableFlags,
        ImVec2(0.0f, 0.0f)))
    {
        return;
    }

    ImGui::TableSetupScrollFreeze(0, 1);

    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 220.0f);
    ImGui::TableSetupColumn("PID", ImGuiTableColumnFlags_WidthFixed, 75.0f);
    ImGui::TableSetupColumn("CPU", ImGuiTableColumnFlags_WidthFixed, 80.0f);
    ImGui::TableSetupColumn("Memory", ImGuiTableColumnFlags_WidthFixed, 110.0f);
    ImGui::TableSetupColumn("Disk Read", ImGuiTableColumnFlags_WidthFixed, 100.0f);
    ImGui::TableSetupColumn("Disk Write", ImGuiTableColumnFlags_WidthFixed, 100.0f);
    ImGui::TableSetupColumn("GPU", ImGuiTableColumnFlags_WidthFixed, 75.0f);
    ImGui::TableSetupColumn("GPU Engine", ImGuiTableColumnFlags_WidthFixed, 110.0f);
    ImGui::TableSetupColumn("Power", ImGuiTableColumnFlags_WidthFixed, 100.0f);
    ImGui::TableSetupColumn("Isolation", ImGuiTableColumnFlags_WidthFixed, 100.0f);

    ImGui::TableHeadersRow();

    // Preparar texto de búsqueda.
    std::string search = searchBuffer_;

    std::transform(
        search.begin(),
        search.end(),
        search.begin(),
        [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        }
    );

    for (const ProcessInfo& process : snapshot.processes)
    {
        // Convertir nombre y PID a texto.
        const std::string processName = WideToUtf8(process.name);
        const std::string processPid = std::to_string(process.processId);

        // Filtrar por nombre o PID.
        if (!search.empty())
        {
            std::string lowerName = processName;

            std::transform(
                lowerName.begin(),
                lowerName.end(),
                lowerName.begin(),
                [](unsigned char c)
                {
                    return static_cast<char>(std::tolower(c));
                }
            );

            if (lowerName.find(search) == std::string::npos &&
                processPid.find(search) == std::string::npos)
            {
                continue;
            }
        }

        // ID único para cada proceso.
        ImGui::PushID(static_cast<int>(process.processId));

        ImGui::TableNextRow();

        // Nombre.
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(processName.c_str());

        // Menú contextual del proceso.
        if (ImGui::BeginPopupContextItem("ProcessContextMenu"))
        {
            ImGui::Text("%s (PID: %lu)",
                processName.c_str(),
                process.processId);

            ImGui::Separator();

            // Mostrar ruta del ejecutable.
            if (!process.executablePath.empty())
            {
                ImGui::TextWrapped(
                    "Ruta: %ls",
                    process.executablePath.c_str()
                );
            }
            else
            {
                ImGui::TextDisabled("Ruta del ejecutable no disponible");
            }

            ImGui::Separator();

            // Finalizar tarea.
            if (ImGui::MenuItem("Finalizar tarea"))
            {
                if (process.processId > 4)
                {
                    HANDLE processHandle = OpenProcess(
                        PROCESS_TERMINATE,
                        FALSE,
                        process.processId
                    );

                    if (processHandle)
                    {
                        if (!TerminateProcess(processHandle, 1))
                            MessageBoxW(nullptr, L"No se pudo finalizar el proceso.", L"Error", MB_ICONERROR);

                        CloseHandle(processHandle);
                    }
                    else
                    {
                        MessageBoxW(nullptr, L"No se pudo acceder al proceso.", L"Error", MB_ICONERROR);
                    }
                }
            }

            ImGui::Separator();

            // Opciones que necesitan la ruta del ejecutable.
            if (!process.executablePath.empty())
            {
                // Abrir ubicación del archivo.
                if (ImGui::MenuItem("Abrir ubicación del archivo"))
                {
                    const std::wstring arguments =
                        L"/select,\"" + process.executablePath + L"\"";

                    ShellExecuteW(
                        nullptr,
                        L"open",
                        L"explorer.exe",
                        arguments.c_str(),
                        nullptr,
                        SW_SHOWNORMAL
                    );
                }

                // Propiedades.
                if (ImGui::MenuItem("Propiedades"))
                {
                    SHELLEXECUTEINFOW info{};
                    info.cbSize = sizeof(info);
                    info.fMask = SEE_MASK_INVOKEIDLIST;
                    info.lpVerb = L"properties";
                    info.lpFile = process.executablePath.c_str();
                    info.nShow = SW_SHOWNORMAL;

                    ShellExecuteExW(&info);
                }
            }
            // Buscar en Internet.
            if (ImGui::MenuItem("Buscar en línea"))
            {
                const std::wstring url =
                    L"https://www.google.com/search?q=" + process.name;

                ShellExecuteW(
                    nullptr,
                    L"open",
                    url.c_str(),
                    nullptr,
                    nullptr,
                    SW_SHOWNORMAL
                );
            }

            ImGui::EndPopup();
        }

        // PID.
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%lu", process.processId);

        // CPU.
        ImGui::TableSetColumnIndex(2);
        ImGui::Text("%.1f%%", process.cpuUsagePercent);

        // Memoria.
        ImGui::TableSetColumnIndex(3);

        const double memoryMB =
            static_cast<double>(process.workingSetBytes) /
            (1024.0 * 1024.0);

        ImGui::Text("%.1f MB", memoryMB);

        // Lectura de disco.
        ImGui::TableSetColumnIndex(4);
        ImGui::Text("%.1f MB/s",
            static_cast<double>(process.diskReadBytesPerSecond) /
            (1024.0 * 1024.0));

        // Escritura de disco.
        ImGui::TableSetColumnIndex(5);
        ImGui::Text("%.1f MB/s",
            static_cast<double>(process.diskWriteBytesPerSecond) /
            (1024.0 * 1024.0));

        // GPU.
        ImGui::TableSetColumnIndex(6);
        ImGui::Text("%.1f%%", process.gpuUsagePercent);

        // Motor GPU.
        ImGui::TableSetColumnIndex(7);

        if (!process.gpuEngine.empty())
        {
            const std::string gpuEngine = WideToUtf8(process.gpuEngine);
            ImGui::TextUnformatted(gpuEngine.c_str());
        }
        else
        {
            ImGui::TextDisabled("-");
        }

        // Consumo energético estimado.
        ImGui::TableSetColumnIndex(8);
        ImGui::TextUnformatted(GetPowerLevelText(process.powerLevel));

        // Aislamiento.
        ImGui::TableSetColumnIndex(9);

        if (process.isAppContainer || process.isIsolated)
        {
            ImGui::TextColored(
                ImVec4(0.3f, 0.8f, 0.4f, 1.0f),
                "Isolated"
            );
        }
        else
        {
            ImGui::TextDisabled("Normal");
        }

        // Cerrar ID único del proceso.
        ImGui::PopID();
    }

    ImGui::EndTable();
}


const char* ProcessDashboard::GetPowerLevelText(ProcessPowerLevel level)
{
    switch (level)
    {
    case ProcessPowerLevel::VeryLow:  return "Very low";
    case ProcessPowerLevel::Low:      return "Low";
    case ProcessPowerLevel::Moderate: return "Moderate";
    case ProcessPowerLevel::High:     return "High";
    case ProcessPowerLevel::VeryHigh: return "Very high";
    default:                          return "Unknown";
    }
}

const char* ProcessDashboard::GetPowerTrendText(ProcessPowerLevel level)
{
    switch (level)
    {
    case ProcessPowerLevel::VeryLow:  return "Very low";
    case ProcessPowerLevel::Low:      return "Low";
    case ProcessPowerLevel::Moderate: return "Moderate";
    case ProcessPowerLevel::High:     return "High";
    case ProcessPowerLevel::VeryHigh: return "Very high";
    default:                          return "Unknown";
    }
}


