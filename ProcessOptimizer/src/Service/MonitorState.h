#pragma once

#include "../IPC/Protocol.h"

#include <cstdint>
#include <memory>
#include <mutex>

namespace Service
{
    namespace ChangeMask
    {
        inline constexpr std::uint32_t None = 0;
        inline constexpr std::uint32_t Cpu = 1u << 0;
        inline constexpr std::uint32_t Memory = 1u << 1;
        inline constexpr std::uint32_t Gpu = 1u << 2;
        inline constexpr std::uint32_t Processes = 1u << 3;
        inline constexpr std::uint32_t Topology = 1u << 4;
        inline constexpr std::uint32_t Optimization = 1u << 5;

        inline constexpr std::uint32_t Overview =
            Cpu | Memory | Gpu | Processes;
    }

    // Dueno de los historiales: lo usa el servicio y tambien la UI cuando
    // trabaja en modo local, de modo que ambos producen exactamente la
    // misma informacion.
    class MonitorState
    {
    public:
        MonitorState();

        std::uint32_t ApplyCpu(const CpuSnapshot& snapshot);
        std::uint32_t ApplyMemory(const MemorySnapshot& snapshot);
        std::uint32_t ApplyGpu(const GpuSnapshot& snapshot);
        std::uint32_t ApplyProcesses(const ProcessSnapshot& snapshot);

        std::uint32_t SetTopology(
            const Optimization::TopologySnapshot& topology);
        std::uint32_t SetOptimizer(
            const Optimization::OptimizerState& optimizer);
        std::uint32_t SetBenchmark(
            const Optimization::BenchmarkState& benchmark);
        std::uint32_t SetFrameProbe(
            const Optimization::FrameProbeSnapshot& frameProbe);

        std::uint32_t SetAb(const Optimization::AbResult& ab);
        std::uint32_t SetContainment(
            const Optimization::ContainmentSnapshot& containment);
        std::uint32_t SetStutter(
            const Optimization::StutterSnapshot& stutter);
        std::uint32_t SetEnergy(
            const Optimization::EnergySnapshot& energy);
        std::uint32_t SetDiagnostics(
            const Optimization::DiagnosticsSnapshot& diagnostics);
        std::uint32_t AddAudit(const Optimization::AuditEntry& entry);

        IPC::State Snapshot() const;

        // Devuelve una instantanea compartida solo cuando la secuencia
        // cambio. Evita copiar ~100 KB en cada sondeo de la UI.
        std::shared_ptr<const IPC::State> CachedSnapshot() const;

        // Aplica un estado recibido por IPC en modo cliente.
        void Replace(const IPC::State& state);

    private:
        void AppendCpuHistory(const CpuSnapshot& snapshot);
        void AppendGpuHistory(const GpuSnapshot& snapshot);
        void AppendOverviewSample();

        mutable std::mutex mutex_;
        IPC::State state_;
        mutable std::shared_ptr<const IPC::State> cachedState_;
        mutable std::uint64_t cachedSequence_ = 0;
    };
}
