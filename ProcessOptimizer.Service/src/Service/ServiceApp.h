#pragma once

#include "IPC/PipeTransport.h"
#include "Monitoring/SystemMonitor/SystemMonitor.h"
#include "Optimization/CpuTopology.h"
#include "Optimization/CpuOrchestrator.h"
#include "Optimization/CpuBenchmark.h"
#include "Optimization/FrameProbe.h"
#include "Optimization/AbController.h"
#include "Optimization/BackgroundContainment.h"
#include "Optimization/StutterCorrelator.h"
#include "Optimization/EnergyManager.h"
#include "Optimization/ResourceDiagnostics.h"
#include "Monitoring/ProcessMonitor/ProcessInfo.h"
#include "Service/MonitorState.h"
#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <thread>

namespace Service
{
    class ServiceApp
    {
    public:
        ServiceApp() = default;
        ~ServiceApp();

        ServiceApp(const ServiceApp&) = delete;
        ServiceApp& operator=(const ServiceApp&) = delete;

        bool Start();
        void Stop();

        // Refina el PID observado: si el primer plano es un launcher,
        // sigue al hijo (o al proceso con GPU real) sin tocar prioridades.
        void UpdateFrameProbeTarget(
            const ProcessSnapshot& snapshot);

        const MonitorState& State() const
        {
            return state_;
        }

    private:
        void ManagementLoop();

        EventSystem events_;
        std::shared_ptr<SystemMonitor> monitor_;
        std::unique_ptr<IPC::PipeServer> pipe_;
        std::unique_ptr<Optimization::CpuOrchestrator> orchestrator_;
        std::unique_ptr<Optimization::CpuBenchmark> benchmark_;
        std::unique_ptr<Optimization::FrameProbe> frameProbe_;
        std::unique_ptr<Optimization::AbController> ab_;
        std::unique_ptr<Optimization::BackgroundContainment> containment_;
        std::unique_ptr<Optimization::StutterCorrelator> stutter_;
        std::unique_ptr<Optimization::EnergyManager> energy_;
        std::unique_ptr<Optimization::ResourceDiagnostics> diagnostics_;
        MonitorState state_;

        Optimization::TopologySnapshot topology_;
        Optimization::OptimizerState optimizer_;

        bool subscribed_ = false;
        std::atomic<bool> stopping_{ false };
        std::atomic<bool> benchmarkResume_{ false };
        Optimization::OptimizerMode previousModeBeforeBenchmark_ =
            Optimization::OptimizerMode::Off;
        std::mutex optimizerRequestMutex_;
        bool requestedOptimizerEnabled_ = false;
        Optimization::OptimizerMode requestedOptimizerMode_ =
            Optimization::OptimizerMode::Off;
        std::mutex foregroundRequestMutex_;
        DWORD requestedForegroundPid_ = 0;
        std::uint64_t foregroundRequestMs_ = 0;

        std::mutex telemetryMutex_;
        ProcessSnapshot lastProcesses_;
        DWORD resolvedGamePid_ = 0;

        std::atomic<bool> managementRunning_{ false };
        std::atomic<bool> autoCalibrateRequested_{ false };
        std::thread managementThread_;

        std::size_t cpuSubscription_ = 0;
        std::size_t memorySubscription_ = 0;
        std::size_t gpuSubscription_ = 0;
        std::size_t processSubscription_ = 0;
    };
}
