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
    try
    {
        m_snapshot = ProcessEnumerator::Enumerate();

        // Actualizar CPU y memoria de los procesos.
        m_performance.Update(m_snapshot);

        // Inicializar GPU cuando sea necesario.
        if (!m_processGpu.IsInitialized())
        {
            if (!m_processGpu.Initialize())
                return false;
        }

        // Actualizar utilización GPU por proceso.
        if (!m_processGpu.Update())
            return false;

        // Crear índice de procesos por PID.
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

        // Asociar utilización GPU con cada proceso.
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

        return true;
    }
    catch (...)
    {
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