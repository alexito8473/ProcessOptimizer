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

        benchmark_ =
            std::make_unique<Optimization::CpuBenchmark>(
                [this](
                    const Optimization::BenchmarkState& state)
                {
                    state_.SetBenchmark(state);

                    if (orchestrator_)
                    {
                        orchestrator_->SetBenchmarkProfile(
                            state.profile);
                    }

                    const bool finished =
                        state.status ==
                            Optimization::BenchmarkStatus::
                                Completed ||
                        state.status ==
                            Optimization::BenchmarkStatus::
                                Failed;

                    if (!finished || !orchestrator_)
                        return;

                    bool resumePaused =
                        benchmarkResume_.exchange(false);
                    bool requestedEnabled = false;
                    Optimization::OptimizerMode requestedMode =
                        Optimization::OptimizerMode::Off;

                    {
                        std::lock_guard<std::mutex> lock(
                            optimizerRequestMutex_);
                        requestedEnabled =
                            requestedOptimizerEnabled_;
                        requestedMode =
                            requestedOptimizerMode_;
                    }

                    if (resumePaused && requestedEnabled)
                    {
                        orchestrator_->SetMode(
                            requestedMode !=
                                    Optimization::
                                        OptimizerMode::Off
                                ? requestedMode
                                : previousModeBeforeBenchmark_);
                    }
                    else if (!resumePaused &&
                             requestedEnabled &&
                             state.status ==
                                 Optimization::
                                     BenchmarkStatus::
                                         Completed &&
                             state.profile.HasData())
                    {
                        // Configuracion guardada antes de tener perfil:
                        // se aplica en cuanto la prueba termina.
                        orchestrator_->SetMode(requestedMode);
                    }
                });

        const auto savedProfile =
            Optimization::CpuBenchmark::Load(topology_);

        benchmark_->SetProfile(savedProfile);
        orchestrator_->SetBenchmarkProfile(savedProfile);

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

                    const bool wantsEnabled =
                        command.enabled &&
                        command.mode !=
                            Optimization::OptimizerMode::Off;

                    {
                        std::lock_guard<std::mutex> lock(
                            optimizerRequestMutex_);
                        requestedOptimizerEnabled_ =
                            wantsEnabled;
                        requestedOptimizerMode_ =
                            wantsEnabled
                                ? command.mode
                                : Optimization::
                                      OptimizerMode::Off;
                    }

                    if (!wantsEnabled)
                    {
                        orchestrator_->SetEnabled(false);
                        return true;
                    }

                    if (!orchestrator_->HasBenchmarkProfile())
                    {
                        orchestrator_->SetEnabled(false);
                        orchestrator_->SetStatusMessage(
                            "Primero ejecuta la prueba de "
                            "rendimiento");
                        return true;
                    }

                    orchestrator_->SetMode(command.mode);
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

                if (request == IPC::Request::StartBenchmark)
                {
                    IPC::BenchmarkCommand command;
                    if (!IPC::Deserialize(
                            payload.data(),
                            payload.size(),
                            command))
                    {
                        return false;
                    }

                    if (!benchmark_ ||
                        benchmark_->State().status ==
                            Optimization::BenchmarkStatus::
                                Running)
                    {
                        return false;
                    }

                    previousModeBeforeBenchmark_ =
                        orchestrator_->State().mode;

                    benchmarkResume_.store(
                        orchestrator_->State().enabled &&
                        previousModeBeforeBenchmark_ !=
                            Optimization::OptimizerMode::Off);

                    if (benchmarkResume_.load())
                        orchestrator_->SetEnabled(false);

                    if (!benchmark_->Start(
                            topology_, command.durationMs))
                    {
                        const bool resume =
                            benchmarkResume_.exchange(false);

                        if (resume)
                        {
                            orchestrator_->SetEnabled(true);
                            orchestrator_->SetMode(
                                previousModeBeforeBenchmark_);
                        }

                        return false;
                    }

                    return true;
                }

                return false;
            };

        pipe_ = std::make_unique<IPC::PipeServer>(
            [this]() { return state_.CachedSnapshot(); },
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

        if (benchmark_)
        {
            benchmark_->Stop();
            benchmark_.reset();
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
