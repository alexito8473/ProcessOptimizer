
#include "SystemMonitor.h"
#include <chrono>
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
        OutputDebugStringW(
            L"[SystemMonitor] Error inicializando GpuMonitor.\n"
        );
        gpuMonitor_.Shutdown();
    }

    if (!processMonitor_.Update())
    {
        // La primera lectura puede no producir snapshot todavía (warm-up de
        // PDH). No apagamos el monitor: Update() se reintentará en el bucle.
        OutputDebugStringW(
            L"[SystemMonitor] ProcessMonitor sin snapshot inicial; se reintentara.\n"
        );
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
    processMonitor_.Shutdown();

}
void SystemMonitor::UpdateProcessMonitor()
{
    if (processMonitor_.Update())
    {
        events_.Publish<ProcessSnapshot>(
            "PROCESS",
            processMonitor_.GetSnapshot()
        );
    }
    else
    {
#ifdef _DEBUG
        OutputDebugStringW(
            L"[SystemMonitor] Error actualizando procesos.\n"
        );
#endif
    }
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
    using Clock = std::chrono::steady_clock;

    auto nextCpu = Clock::now() + cpuInterval_;
    auto nextMemory = Clock::now() + memoryInterval_;
    auto nextGpu = Clock::now() + gpuInterval_;
    auto nextProcess = Clock::now() + processInterval_;

    while (running_.load(std::memory_order_acquire))
    {
        {
            std::unique_lock<std::mutex> lock(stopMutex_);

            // Calcular la siguiente actualización sin std::min.
            auto nextUpdate = nextCpu;

            if (nextMemory < nextUpdate)
                nextUpdate = nextMemory;

            if (nextGpu < nextUpdate)
                nextUpdate = nextGpu;

            if (nextProcess < nextUpdate)
                nextUpdate = nextProcess;

            if (stopCondition_.wait_until(
                lock,
                nextUpdate,
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

        auto now = Clock::now();

        if (now >= nextCpu)
        {
            UpdateCpuMonitor();
            nextCpu = Clock::now() + cpuInterval_;
        }

        if (now >= nextMemory)
        {
            UpdateMemoryMonitor();
            nextMemory = Clock::now() + memoryInterval_;
        }

        if (now >= nextGpu)
        {
            UpdateGpuMonitor();
            nextGpu = Clock::now() + gpuInterval_;
        }

        if (now >= nextProcess)
        {
            UpdateProcessMonitor();
            nextProcess = Clock::now() + processInterval_;
        }
    }
}

