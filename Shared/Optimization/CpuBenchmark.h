#pragma once

#include "CpuTopology.h"
#include "OptimizationTypes.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>

namespace Optimization
{
    // Prueba de rendimiento bajo demanda. No mide de forma continua:
    // solo crea hilos de carga cuando el usuario pulsa el boton. El perfil
    // resultante se guarda en disco y el orquestador lo reutiliza despues.
    class CpuBenchmark
    {
    public:
        using StateCallback =
            std::function<void(const BenchmarkState&)>;

        explicit CpuBenchmark(StateCallback callback);
        ~CpuBenchmark();

        CpuBenchmark(const CpuBenchmark&) = delete;
        CpuBenchmark& operator=(const CpuBenchmark&) = delete;

        bool Start(
            const TopologySnapshot& topology,
            std::uint32_t durationMs);
        void Stop();

        BenchmarkState State() const;

        void SetProfile(const CpuBenchmarkProfile& profile);

        static CpuBenchmarkProfile Load(
            const TopologySnapshot& topology);

    private:
        struct CoreWork
        {
            std::size_t coreIndex = 0;
            std::vector<ULONG> cpuSetIds;
            DWORD idealProcessor = 0;
            bool hasIdeal = false;
            std::atomic<bool> start{ false };
            std::atomic<bool> stop{ false };
            std::atomic<std::uint64_t> iterations{ 0 };
        };

        struct FrequencyAccumulator
        {
            double peakMHz = 0.0;
            double sumMHz = 0.0;
            std::uint32_t samples = 0;
        };

        void Worker(
            TopologySnapshot topology,
            std::uint32_t durationMs);
        void Publish(const BenchmarkState& state);
        bool Save(const CpuBenchmarkProfile& profile) const;

        StateCallback callback_;
        mutable std::mutex mutex_;
        std::atomic<bool> running_{ false };
        std::atomic<bool> cancel_{ false };
        std::thread worker_;
        BenchmarkState state_;
    };
}
