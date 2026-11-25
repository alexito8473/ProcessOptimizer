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
                if (orchestrator_)
                    orchestrator_->OnCpuSnapshot(snapshot);
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
                    if (orchestrator_)
                        orchestrator_->OnProcessSnapshot(
                            snapshot);
                });

        subscribed_ = true;

        topology_ = Optimization::CpuTopology::Detect();
        state_.SetTopology(topology_);

        orchestrator_ =
            std::make_unique<Optimization::CpuOrchestrator>(
                [this](
                    const Optimization::OptimizerState& state)
                {
                    state_.SetOptimizer(state);
                });
        orchestrator_->SetTopology(topology_);
        orchestrator_->Start();

        monitor_ = std::make_shared<SystemMonitor>(events_);
        monitor_->Start();

        IPC::PipeServer::CommandHandler commandHandler =
            [this](
                IPC::Request request,
                const std::vector<std::uint8_t>& payload)
            {
                if (!orchestrator_)
                    return false;

                if (request ==
                    IPC::Request::SetOptimizerConfig)
                {
                    IPC::OptimizerConfigCommand command;
                    if (!IPC::Deserialize(
                            payload.data(),
                            payload.size(),
                            command))
                    {
                        return false;
                    }

                    if (!command.enabled ||
                        command.mode ==
                            Optimization::OptimizerMode::Off)
                    {
                        orchestrator_->SetEnabled(false);
                    }
                    else
                    {
                        orchestrator_->SetMode(command.mode);
                    }

                    return true;
                }

                if (request ==
                    IPC::Request::SetForegroundProcess)
                {
                    IPC::ForegroundProcessCommand command;
                    if (!IPC::Deserialize(
                            payload.data(),
                            payload.size(),
                            command))
                    {
                        return false;
                    }

                    orchestrator_->SetForegroundProcess(
                        command.processId);
                    return true;
                }

                return false;
            };

        pipe_ = std::make_unique<IPC::PipeServer>(
            [this]() { return state_.Snapshot(); },
            commandHandler);
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
