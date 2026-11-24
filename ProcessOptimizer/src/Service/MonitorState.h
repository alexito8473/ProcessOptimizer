#pragma once

#include "../IPC/Protocol.h"

#include <cstdint>
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

        inline constexpr std::uint32_t Overview =
            Cpu | Memory | Gpu | Processes;
    }

    // Dueno de los historiales: lo usa el servicio y tambien la UI cuando
    // trabaja en modo local, de modo que ambos producen exactamente la
    // misma informacion.
    class MonitorState
    {
    public:
        std::uint32_t ApplyCpu(const CpuSnapshot& snapshot);
        std::uint32_t ApplyMemory(const MemorySnapshot& snapshot);
        std::uint32_t ApplyGpu(const GpuSnapshot& snapshot);
        std::uint32_t ApplyProcesses(const ProcessSnapshot& snapshot);

        IPC::State Snapshot() const;

        // Aplica un estado recibido por IPC en modo cliente.
        void Replace(const IPC::State& state);

    private:
        void AppendCpuHistory(const CpuSnapshot& snapshot);
        void AppendGpuHistory(const GpuSnapshot& snapshot);
        void AppendOverviewSample();

        mutable std::mutex mutex_;
        IPC::State state_;
    };
}
