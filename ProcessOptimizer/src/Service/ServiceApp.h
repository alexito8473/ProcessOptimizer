#pragma once

#include "../IPC/PipeTransport.h"
#include "../Monitoring/SystemMonitor/SystemMonitor.h"
#include "../Optimization/CpuTopology.h"
#include "../Optimization/CpuOrchestrator.h"
#include "../Optimization/CpuBenchmark.h"
#include "../Optimization/FrameProbe.h"
#include "../Monitoring/ProcessMonitor/ProcessInfo.h"
#include "MonitorState.h"

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>

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
        EventSystem events_;
        std::shared_ptr<SystemMonitor> monitor_;
        std::unique_ptr<IPC::PipeServer> pipe_;
        std::unique_ptr<Optimization::CpuOrchestrator> orchestrator_;
        std::unique_ptr<Optimization::CpuBenchmark> benchmark_;
        std::unique_ptr<Optimization::FrameProbe> frameProbe_;
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

        std::size_t cpuSubscription_ = 0;
        std::size_t memorySubscription_ = 0;
        std::size_t gpuSubscription_ = 0;
        std::size_t processSubscription_ = 0;
    };
}
