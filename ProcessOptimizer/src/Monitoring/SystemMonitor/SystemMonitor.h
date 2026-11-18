
#pragma once

#include "../../Core/Events/EventSystem.h"
#include "../CpuMonitor/CpuMonitor.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

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

    std::thread monitorThread_;

    std::atomic<bool> running_{ false };

    const std::chrono::milliseconds updateInterval_{ 1000 };

    std::mutex lifecycleMutex_;
    std::mutex stopMutex_;
    std::condition_variable stopCondition_;
};
