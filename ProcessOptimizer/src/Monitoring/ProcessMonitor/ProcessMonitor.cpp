#include "ProcessMonitor.h"
#include "ProcessEnumerator/ProcessEnumerator.h"

#include <unordered_map>
#include <utility>

ProcessMonitor::~ProcessMonitor()
{
    Shutdown();
}
bool ProcessMonitor::Update()
{
    OutputDebugStringA("\n[ProcessMonitor] Iniciando Update...\n");

    try
    {
        // 1. Enumerar procesos.
        OutputDebugStringA("[ProcessMonitor] Enumerando procesos...\n");

        m_snapshot = ProcessEnumerator::Enumerate();

        OutputDebugStringA("[ProcessMonitor] Enumeracion completada.\n");

        // 2. Actualizar CPU y memoria.
        OutputDebugStringA("[ProcessMonitor] Actualizando rendimiento...\n");

        m_performance.Update(m_snapshot);

        OutputDebugStringA("[ProcessMonitor] Rendimiento actualizado.\n");

        // 3. Inicializar GPU.
        if (!m_processGpu.IsInitialized())
        {
            OutputDebugStringA("[ProcessMonitor] Inicializando ProcessGpu...\n");

            if (!m_processGpu.Initialize())
            {
                OutputDebugStringA(
                    "[ERROR] ProcessGpu::Initialize() devolvio false.\n");

                return false;
            }

            OutputDebugStringA("[ProcessMonitor] ProcessGpu inicializado.\n");
        }

        // 4. Actualizar GPU.
        OutputDebugStringA("[ProcessMonitor] Actualizando GPU...\n");

        if (!m_processGpu.Update())
        {
            OutputDebugStringA(
                "[ERROR] ProcessGpu::Update() devolvio false.\n");

            return false;
        }

        OutputDebugStringA("[ProcessMonitor] GPU actualizado.\n");

        // 5. Crear índice de procesos.
        std::unordered_map<DWORD, ProcessInfo*> processById;

        processById.reserve(m_snapshot.processes.size());

        for (ProcessInfo& process : m_snapshot.processes)
        {
            process.gpuUsagePercent = 0.0;
            process.gpuEngine.clear();

            processById.emplace(
                process.processId,
                &process
            );
        }

        // 6. Asociar utilización GPU.
        for (const ProcessGpuEngineUsage& engine : m_processGpu.GetUsage())
        {
            auto it = processById.find(engine.processId);

            if (it == processById.end())
                continue;

            ProcessInfo& process = *it->second;

            if (engine.usagePercent > process.gpuUsagePercent)
            {
                process.gpuUsagePercent = engine.usagePercent;
                process.gpuEngine = engine.engineName;
            }
        }

        OutputDebugStringA(
            "[ProcessMonitor] Update completado correctamente.\n");

        return true;
    }
    catch (const std::exception& e)
    {
        OutputDebugStringA("[ERROR] Excepcion en ProcessMonitor: ");
        OutputDebugStringA(e.what());
        OutputDebugStringA("\n");

        return false;
    }
    catch (...)
    {
        OutputDebugStringA(
            "[ERROR] Excepcion desconocida en ProcessMonitor.\n");

        return false;
    }
}

const ProcessSnapshot& ProcessMonitor::GetSnapshot() const
{
    return m_snapshot;
}

void ProcessMonitor::Shutdown()
{
    m_processGpu.Shutdown();
    m_snapshot.processes.clear();
}