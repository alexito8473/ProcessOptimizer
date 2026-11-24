#include "ServiceApp.h"

#include <utility>

namespace Service
{
    ServiceApp::~ServiceApp()
    {
        Stop();
    }

    bool ServiceApp::Start()
    {
        if (subscribed_)
            return true;

        cpuSubscription_ = events_.Subscribe<CpuSnapshot>(
            "CPU",
            [this](const CpuSnapshot& snapshot)
            {
                state_.ApplyCpu(snapshot);
            });

        memorySubscription_ =
            events_.Subscribe<MemorySnapshot>(
                "MEMORY",
                [this](const MemorySnapshot& snapshot)
                {
                    state_.ApplyMemory(snapshot);
                });

        gpuSubscription_ = events_.Subscribe<GpuSnapshot>(
            "GPU",
            [this](const GpuSnapshot& snapshot)
            {
                state_.ApplyGpu(snapshot);
            });

        processSubscription_ =
            events_.Subscribe<ProcessSnapshot>(
                "PROCESS",
                [this](const ProcessSnapshot& snapshot)
                {
                    state_.ApplyProcesses(snapshot);
                });

        subscribed_ = true;

        monitor_ = std::make_shared<SystemMonitor>(events_);
        monitor_->Start();

        pipe_ = std::make_unique<IPC::PipeServer>(
            [this]() { return state_.Snapshot(); });
        pipe_->Start();

        return true;
    }

    void ServiceApp::Stop()
    {
        if (pipe_)
        {
            pipe_->Stop();
            pipe_.reset();
        }

        if (monitor_)
        {
            monitor_->Stop();
            monitor_.reset();
        }

        if (!subscribed_)
            return;

        events_.Unsubscribe<CpuSnapshot>(
            "CPU", cpuSubscription_);
        events_.Unsubscribe<MemorySnapshot>(
            "MEMORY", memorySubscription_);
        events_.Unsubscribe<GpuSnapshot>(
            "GPU", gpuSubscription_);
        events_.Unsubscribe<ProcessSnapshot>(
            "PROCESS", processSubscription_);

        subscribed_ = false;
    }
}
