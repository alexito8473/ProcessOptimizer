#include "ServiceApp.h"

#include <algorithm>
#include <chrono>
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

        stopping_.store(false);

        cpuSubscription_ = events_.Subscribe<CpuSnapshot>(
            "CPU",
            [this](const CpuSnapshot& snapshot)
            {
                if (stopping_.load())
                    return;

                state_.ApplyCpu(snapshot);
                if (orchestrator_)
                    orchestrator_->OnCpuSnapshot(snapshot);
            });

        memorySubscription_ =
            events_.Subscribe<MemorySnapshot>(
                "MEMORY",
                [this](const MemorySnapshot& snapshot)
                {
                    if (stopping_.load())
                        return;

                    state_.ApplyMemory(snapshot);
                });

        gpuSubscription_ = events_.Subscribe<GpuSnapshot>(
            "GPU",
            [this](const GpuSnapshot& snapshot)
            {
                if (stopping_.load())
                    return;

                state_.ApplyGpu(snapshot);

                if (frameProbe_)
                {
                    double maxUsage = 0.0;
                    for (const GpuAdapterInfo& gpu :
                         snapshot.gpus)
                    {
                        maxUsage = (std::max)(
                            maxUsage, gpu.usagePercent);
                    }
                    frameProbe_->SetGpuUsagePercent(maxUsage);
                }
            });

        processSubscription_ =
            events_.Subscribe<ProcessSnapshot>(
                "PROCESS",
                [this](const ProcessSnapshot& snapshot)
                {
                    if (stopping_.load())
                        return;

                    state_.ApplyProcesses(snapshot);
                    UpdateFrameProbeTarget(snapshot);
                    if (orchestrator_)
                        orchestrator_->OnProcessSnapshot(
                            snapshot);
                    if (stutter_)
                        stutter_->SetProcessSnapshot(snapshot);
                    {
                        std::lock_guard<std::mutex> lock(
                            telemetryMutex_);
                        lastProcesses_ = snapshot;
                    }
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

        // Telemetria primero: sin una medida de frame time no se toca
        // ningun hilo del juego. La sonda corre en LocalSystem y no
        // inyecta codigo en el proceso observado.
        frameProbe_ =
            std::make_unique<Optimization::FrameProbe>(
                [this](
                    const Optimization::FrameProbeSnapshot&
                        snapshot)
                {
                    state_.SetFrameProbe(snapshot);
                });
        frameProbe_->Start();

        // A/B, energia y contencion: ninguno toca el juego sin evidencia.
        energy_ = std::make_unique<Optimization::EnergyManager>(
            [this](const Optimization::EnergySnapshot& snapshot)
            {
                state_.SetEnergy(snapshot);
            });
        energy_->Start();

        ab_ = std::make_unique<Optimization::AbController>(
            [this](const Optimization::AbResult& result)
            {
                state_.SetAb(result);
            });

        stutter_ = std::make_unique<Optimization::StutterCorrelator>(
            [this](const Optimization::StutterSnapshot& snapshot)
            {
                state_.SetStutter(snapshot);
            });
        stutter_->Start();

        containment_ =
            std::make_unique<Optimization::BackgroundContainment>(
                [this](
                    const Optimization::ContainmentSnapshot& snapshot)
                {
                    state_.SetContainment(snapshot);
                },
                [this](const Optimization::AuditEntry& entry)
                {
                    state_.AddAudit(entry);
                });
        // Activada por defecto, pero solo interviene si el correlacionador
        // encuentra evidencia de interferencia con el frame time.
        containment_->SetEnabled(true);

        diagnostics_ =
            std::make_unique<Optimization::ResourceDiagnostics>();
        diagnostics_->Refresh(topology_);
        state_.SetDiagnostics(diagnostics_->Snapshot());

        managementRunning_.store(true);
        managementThread_ =
            std::thread(&ServiceApp::ManagementLoop, this);

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
                        autoCalibrateRequested_.store(false);
                        if (ab_)
                            ab_->Stop();
                        return true;
                    }

                    autoCalibrateRequested_.store(true);

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

                    {
                        std::lock_guard<std::mutex> lock(
                            foregroundRequestMutex_);
                        requestedForegroundPid_ =
                            command.processId;
                        foregroundRequestMs_ = GetTickCount64();
                    }

                    if (frameProbe_)
                        frameProbe_->SetTargetProcess(
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

                if (request == IPC::Request::StartAb)
                {
                    IPC::AbCommand command;
                    if (!IPC::Deserialize(
                            payload.data(),
                            payload.size(),
                            command) ||
                        !ab_ || ab_->Snapshot().running)
                    {
                        return false;
                    }
                    return ab_->Start(
                        command.phaseSeconds,
                        command.applyCandidate,
                        frameProbe_.get(),
                        energy_.get());
                }

                if (request == IPC::Request::StopAb)
                {
                    if (!ab_)
                        return false;
                    ab_->Stop();
                    return true;
                }

                if (request ==
                    IPC::Request::SetExperimentalProfile)
                {
                    IPC::ToggleCommand command;
                    if (!IPC::Deserialize(
                            payload.data(),
                            payload.size(),
                            command) ||
                        !energy_)
                    {
                        return false;
                    }
                    if (ab_ && ab_->Snapshot().running)
                    {
                        Optimization::AuditEntry rejected{};
                        rejected.timeMs = GetTickCount64();
                        rejected.action =
                            "Cambio de energia rechazado durante A/B";
                        rejected.target = "Energia / GPU";
                        rejected.detail =
                            "El perfil solo debe cambiarlo el A/B";
                        rejected.reverted = true;
                        state_.AddAudit(rejected);
                        return false;
                    }
                    const bool ok =
                        energy_->SetExperimentalProfile(
                            command.enabled);

                    Optimization::AuditEntry entry{};
                    entry.timeMs = GetTickCount64();
                    entry.action = command.enabled
                        ? "Perfil experimental activado"
                        : "Perfil experimental desactivado";
                    entry.target = "Energia / GPU";
                    entry.detail = ok
                        ? "Cambio reversible; plan duplicado"
                        : "No se pudo aplicar el cambio";
                    entry.reverted = !command.enabled;
                    state_.AddAudit(entry);
                    return ok;
                }

                if (request ==
                    IPC::Request::SetEtwStutterProbe)
                {
                    IPC::ToggleCommand command;
                    if (!IPC::Deserialize(
                            payload.data(),
                            payload.size(),
                            command) ||
                        !stutter_)
                    {
                        return false;
                    }
                    return stutter_->SetEtwEnabled(
                        command.enabled);
                }

                if (request ==
                    IPC::Request::SetContainment)
                {
                    IPC::ToggleCommand command;
                    if (!IPC::Deserialize(
                            payload.data(),
                            payload.size(),
                            command) ||
                        !containment_)
                    {
                        return false;
                    }
                    containment_->SetEnabled(command.enabled);

                    Optimization::AuditEntry entry{};
                    entry.timeMs = GetTickCount64();
                    entry.action = command.enabled
                        ? "Contencion activada"
                        : "Contencion desactivada";
                    entry.target = "Procesos de segundo plano";
                    entry.detail =
                        "Solo actua con evidencia; todo se restaura";
                    entry.reverted = !command.enabled;
                    state_.AddAudit(entry);
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

    void ServiceApp::UpdateFrameProbeTarget(
        const ProcessSnapshot& snapshot)
    {
        if (!frameProbe_)
            return;

        DWORD requested = 0;
        {
            std::lock_guard<std::mutex> lock(
                foregroundRequestMutex_);
            requested = requestedForegroundPid_;
        }

        if (requested == 0)
        {
            frameProbe_->SetTargetProcess(0);
            return;
        }

        const ProcessInfo* requestedInfo = nullptr;
        DWORD childPid = 0;
        double childGpu = 0.0;

        for (const ProcessInfo& process :
             snapshot.processes)
        {
            if (process.processId == requested)
                requestedInfo = &process;

            if (process.parentProcessId != requested)
                continue;

            const bool own =
                process.processId == GetCurrentProcessId() ||
                process.name.find(L"ProcessOptimizer") !=
                    std::wstring::npos;

            if (own)
                continue;

            if (process.gpuUsagePercent > childGpu)
            {
                childPid = process.processId;
                childGpu = process.gpuUsagePercent;
            }
        }

        // El PID del primer plano desaparecio: dejar de medir antes que
        // arriesgarse a observar un proceso distinto que reutilizo el ID.
        if (requestedInfo == nullptr)
        {
            frameProbe_->SetTargetProcess(0);
            {
                std::lock_guard<std::mutex> lock(telemetryMutex_);
                resolvedGamePid_ = 0;
            }
            if (energy_)
                energy_->SetGameProcess(0, false);
            return;
        }

        DWORD target = requested;

        // LeagueClient.exe suele tener el foco mientras el render lo hace
        // League of Legends.exe. Seguir unicamente al hijo directo con GPU
        // real; nunca a un proceso no relacionado. No se toca prioridad
        // ni afinidad de ninguno de los dos.
        if (requestedInfo->gpuUsagePercent < 2.0 &&
            childPid != 0 && childGpu >= 2.0)
        {
            target = childPid;
        }

        frameProbe_->SetTargetProcess(target);
        {
            std::lock_guard<std::mutex> lock(telemetryMutex_);
            resolvedGamePid_ = target;
        }
        if (energy_)
            energy_->SetGameProcess(target, target == requested);
    }

    void ServiceApp::ManagementLoop()
    {
        std::uint32_t tick = 0;
        while (managementRunning_.load() && !stopping_.load())
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(1000));
            if (!managementRunning_.load() || stopping_.load())
                break;

            ++tick;
            Optimization::FrameProbeSnapshot frame{};
            ProcessSnapshot processes;
            Optimization::TopologySnapshot topology;
            DWORD foreground = 0;
            DWORD game = 0;

            if (frameProbe_)
                frame = frameProbe_->Snapshot();

            {
                std::lock_guard<std::mutex> lock(telemetryMutex_);
                processes = lastProcesses_;
                topology = topology_;
                game = resolvedGamePid_;
            }
            {
                std::lock_guard<std::mutex> lock(
                    foregroundRequestMutex_);
                // La identidad caduca si la UI desaparece: sin ella nunca
                // se puede garantizar que el juego quede excluido.
                if (requestedForegroundPid_ != 0 &&
                    foregroundRequestMs_ != 0 &&
                    GetTickCount64() - foregroundRequestMs_ <= 5000)
                {
                    foreground = requestedForegroundPid_;
                }
            }
            if (game != 0)
            {
                const bool gameAlive = std::any_of(
                    processes.processes.begin(),
                    processes.processes.end(),
                    [game](const ProcessInfo& process)
                    {
                        return process.processId == game;
                    });
                if (!gameAlive)
                    game = 0;
            }

            if (stutter_)
                stutter_->Tick(frame);
            if (ab_)
                ab_->Tick();

            // Auto-calibracion: cuando Balanceado se activa, el servicio
            // corre A/B solo una vez, sin intervencion del usuario.
            if (autoCalibrateRequested_.load() && ab_ &&
                orchestrator_->State().enabled &&
                orchestrator_->State().mode !=
                    Optimization::OptimizerMode::Off)
            {
                const auto abState = ab_->Snapshot();
                if (!abState.running && !abState.completed &&
                    orchestrator_->HasBenchmarkProfile() &&
                    frameProbe_)
                {
                    ab_->Start(20, true, frameProbe_.get(),
                               energy_.get());
                }
                if (ab_->Snapshot().completed)
                    autoCalibrateRequested_.store(false);
            }

            if (containment_)
            {
                const auto abState = ab_
                    ? ab_->Snapshot()
                    : Optimization::AbResult{};
                const auto energyState = energy_
                    ? energy_->Snapshot()
                    : Optimization::EnergySnapshot{};
                containment_->SetAbValidated(
                    abState.completed && abState.kept &&
                    energyState.experimentalProfile);

                const auto stutterState = stutter_
                    ? stutter_->Snapshot()
                    : Optimization::StutterSnapshot{};
                containment_->OnProcesses(
                    processes, stutterState, topology,
                    foreground, game);
            }
            if (energy_ && tick % 5 == 0)
                energy_->Refresh();
            if (diagnostics_ && tick % 30 == 0)
            {
                diagnostics_->Refresh(topology);
                state_.SetDiagnostics(
                    diagnostics_->Snapshot());
            }
        }
    }

    void ServiceApp::Stop()
    {
        stopping_.store(true);

        managementRunning_.store(false);
        if (managementThread_.joinable())
            managementThread_.join();

        {
            std::lock_guard<std::mutex> lock(
                foregroundRequestMutex_);
            requestedForegroundPid_ = 0;
            foregroundRequestMs_ = 0;
        }

        if (pipe_)
        {
            pipe_->Stop();
            pipe_.reset();
        }

        // El monitor se calla primero: sus callbacks leen los objetos
        // de optimizacion. Un join tardio permitia usar punteros ya
        // liberados mientras el servicio se apagaba.
        if (subscribed_)
        {
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

        if (monitor_)
        {
            monitor_->Stop();
            monitor_.reset();
        }

        if (benchmark_)
        {
            benchmark_->Stop();
            benchmark_.reset();
        }

        if (orchestrator_)
        {
            orchestrator_->Stop();
            orchestrator_.reset();
        }

        // La prueba A/B debe revertir el perfil antes de liberar energia.
        if (ab_)
        {
            ab_->Stop();
            ab_.reset();
        }

        if (frameProbe_)
        {
            frameProbe_->Stop();
            frameProbe_.reset();
        }

        if (containment_)
        {
            containment_->RestoreAll();
            containment_.reset();
        }

        if (stutter_)
        {
            stutter_->Stop();
            stutter_.reset();
        }

        if (energy_)
        {
            energy_->Stop();
            energy_.reset();
        }

        diagnostics_.reset();
    }
}
