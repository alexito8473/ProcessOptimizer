
#include "SystemMonitor.h"

#include <Windows.h>

SystemMonitor::SystemMonitor(EventSystem& events)
    : events_(events)
{
}

SystemMonitor::~SystemMonitor()
{
    Stop();
}

void SystemMonitor::Start()
{
    std::lock_guard<std::mutex> lock(lifecycleMutex_);

    if (running_.load(std::memory_order_acquire))
        return;

    if (monitorThread_.joinable())
        monitorThread_.join();

    // Inicializar monitor de CPU.
    if (!cpuMonitor_.Initialize())
    {
        OutputDebugStringW(
            L"[SystemMonitor] Error inicializando CpuMonitor.\n"
        );

        cpuMonitor_.Shutdown();
        return;
    }

    // Inicializar monitor de memoria RAM.
    if (!memoryMonitor_.Initialize())
    {
        OutputDebugStringW(
            L"[SystemMonitor] Error inicializando MemoryMonitor.\n"
        );

        cpuMonitor_.Shutdown();
        memoryMonitor_.Shutdown();
        return;
    }
    if (!gpuMonitor_.Initialize())
    {
        gpuMonitor_.Shutdown();
    }
    running_.store(true, std::memory_order_release);

    monitorThread_ = std::thread(
        &SystemMonitor::MonitorLoop,
        this
    );
}

void SystemMonitor::Stop()
{
    std::lock_guard<std::mutex> lock(lifecycleMutex_);

    if (!running_.exchange(false, std::memory_order_acq_rel))
        return;

    stopCondition_.notify_all();

    if (monitorThread_.joinable())
        monitorThread_.join();

    // Liberar recursos de los monitores.
    cpuMonitor_.Shutdown();
    memoryMonitor_.Shutdown();
    gpuMonitor_.Shutdown();
}

void SystemMonitor::UpdateCpuMonitor()
{
    if (cpuMonitor_.Update())
    {
        events_.Publish<CpuSnapshot>(
            "CPU",
            cpuMonitor_.GetSnapshot()
        );
    }
    else
    {
#ifdef _DEBUG
        OutputDebugStringW(
            L"[SystemMonitor] Error actualizando CPU.\n"
        );
#endif
    }
}
void SystemMonitor::UpdateMemoryMonitor()
{
    if (memoryMonitor_.Update())
    {
        events_.Publish<MemorySnapshot>(
            "MEMORY",
            memoryMonitor_.GetSnapshot()
        );
    }
    else
    {
#ifdef _DEBUG
        OutputDebugStringW(
            L"[SystemMonitor] Error actualizando memoria RAM.\n"
        );
#endif
    }

}
void SystemMonitor::UpdateGpuMonitor()
{
	if (gpuMonitor_.Update())
	{
		events_.Publish<GpuSnapshot>(
			"GPU",
			gpuMonitor_.GetSnapshot()
		);
	}
	else
	{
        #ifdef _DEBUG
		OutputDebugStringW(
			L"[SystemMonitor] Error actualizando GPU.\n"
		);
#endif
	}
}
void SystemMonitor::MonitorLoop()
{
    while (running_.load(std::memory_order_acquire))
    {
        {
            std::unique_lock<std::mutex> lock(stopMutex_);

            if (stopCondition_.wait_for(
                lock,
                updateInterval_,
                [this]()
                {
                    return !running_.load(std::memory_order_acquire);
                }))
            {
                break;
            }
        }

        if (!running_.load(std::memory_order_acquire))
            break;

        // Actualizar CPU.
        UpdateCpuMonitor();

        // Actualizar memoria RAM.
        UpdateMemoryMonitor();

		// Actualizar GPU.
        UpdateGpuMonitor();
    }
}
