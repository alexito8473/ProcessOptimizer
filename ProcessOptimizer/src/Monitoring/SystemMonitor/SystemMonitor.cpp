
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

    if (running_.load())
        return;

    if (monitorThread_.joinable())
        monitorThread_.join();

    if (!cpuMonitor_.Initialize())
    {
        OutputDebugStringW(
            L"[SystemMonitor] Error inicializando CpuMonitor.\n"
        );
        return;
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

    cpuMonitor_.Shutdown();
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

        if (!cpuMonitor_.Update())
        {
#ifdef _DEBUG
            OutputDebugStringW(
                L"[SystemMonitor] Error actualizando CPU.\n"
            );
#endif
            continue;
        }

        events_.Publish<CpuSnapshot>(
            "CPU",
            cpuMonitor_.GetSnapshot()
        );
    }
}
