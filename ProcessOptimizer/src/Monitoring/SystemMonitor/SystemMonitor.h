
#pragma once

#include "../../Core/Events/EventSystem.h"
#include "../CpuMonitor/CpuMonitor.h"
#include "../MemoryMonitor/MemoryMonitor.h"
#include "../GpuMonitor/GpuMonitor.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include "../ProcessMonitor/ProcessMonitor.h"

class SystemMonitor
{
public:
    explicit SystemMonitor(EventSystem& events);
    ~SystemMonitor();

    SystemMonitor(const SystemMonitor&) = delete;
    SystemMonitor& operator=(const SystemMonitor&) = delete;

    void Start();
    void Stop();

private:
    void MonitorLoop();

    EventSystem& events_;

    CpuMonitor cpuMonitor_;
    MemoryMonitor memoryMonitor_;
    GpuMonitor gpuMonitor_;
    ProcessMonitor processMonitor_;
    std::thread monitorThread_;

    std::atomic<bool> running_{ false };

    const std::chrono::milliseconds cpuInterval_{ 1000 };
    const std::chrono::milliseconds memoryInterval_{ 2000 };
    const std::chrono::milliseconds gpuInterval_{ 2000 };
    const std::chrono::milliseconds processInterval_{ 3000 };

    void UpdateCpuMonitor();
	void UpdateMemoryMonitor();
    void UpdateGpuMonitor();
    void UpdateProcessMonitor();
    std::mutex lifecycleMutex_;
    std::mutex stopMutex_;
    std::condition_variable stopCondition_;
};
