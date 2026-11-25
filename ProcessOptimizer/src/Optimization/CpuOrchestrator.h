#pragma once

#include "CpuTopology.h"
#include "OptimizationTypes.h"
#include "ThreadMonitor.h"

#include "../Monitoring/CpuMonitor/CpuInfo.h"
#include "../Monitoring/ProcessMonitor/ProcessInfo.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace Optimization
{
    // Orquestador real: decide, aplica y verifica afinidad, nucleo ideal
    // y prioridades. Trabaja con el planificador de Windows: nunca amplia
    // la afinidad original ni toca procesos criticos.
    class CpuOrchestrator
    {
    public:
        using Publisher =
            std::function<void(const OptimizerState&)>;

        explicit CpuOrchestrator(Publisher publisher);
        ~CpuOrchestrator();

        CpuOrchestrator(const CpuOrchestrator&) = delete;
        CpuOrchestrator& operator=(const CpuOrchestrator&) = delete;

        void Start();
        void Stop();

        void SetTopology(const TopologySnapshot& topology);
        void OnCpuSnapshot(const CpuSnapshot& snapshot);
        void OnProcessSnapshot(const ProcessSnapshot& snapshot);

        void SetEnabled(bool enabled);
        void SetMode(OptimizerMode mode);
        void SetForegroundProcess(DWORD processId);

        OptimizerState State() const;

    private:
        struct ProcessStart
        {
            DWORD processId = 0;
            std::uint64_t startTime100ns = 0;
        };

        struct OriginalProcessState
        {
            std::uint64_t startTime100ns = 0;
            int priority = 0;
            bool hasPriority = false;
        };

        struct OriginalThreadState
        {
            std::uint64_t startTime100ns = 0;
            WORD group = 0;
            std::uint64_t affinity = 0;
            DWORD idealProcessor = 0;
            int priority = 0;
            bool hasAffinity = false;
            bool hasIdeal = false;
            bool hasPriority = false;
        };

        struct Candidate
        {
            DWORD processId = 0;
            std::wstring name;
            double cpuUsagePercent = 0.0;
            double gpuUsagePercent = 0.0;
            bool foreground = false;
            bool critical = false;
        };

        void WorkerLoop();
        void Tick();
        void BuildPlanAndApply();
        void RestoreAll();

        bool RestoreThread(
            std::uint64_t threadKey,
            const OriginalThreadState& original);
        bool RestoreProcess(
            DWORD processId,
            const OriginalProcessState& original);

        bool GetProcessStart(
            DWORD processId,
            std::uint64_t& startTime100ns) const;
        bool IsCritical(const std::wstring& name) const;
        bool IsUserSession(DWORD processId) const;

        OriginalProcessState EnsureProcessOriginal(
            DWORD processId,
            std::uint64_t startTime100ns,
            HANDLE process);
        OriginalThreadState EnsureThreadOriginal(
            DWORD processId,
            DWORD threadId,
            std::uint64_t startTime100ns,
            HANDLE thread);

        std::uint16_t ChooseIsland(
            std::uint16_t previousIsland,
            bool targetProcess,
            bool targetIslandExcluded,
            std::uint16_t excludedIsland,
            WORD group,
            std::uint64_t originalAffinity,
            const std::vector<TopologyProcessor>& processors) const;

        bool BuildTarget(
            const TopologySnapshot& topology,
            std::uint16_t island,
            WORD group,
            std::uint64_t originalAffinity,
            bool targetProcess,
            std::vector<std::size_t>& chosenProcessors,
            std::uint64_t& targetAffinity,
            DWORD& idealProcessor) const;

        void Publish();

        Publisher publisher_;

        // Serializa aplicacion/restauracion para que un cambio de modo no
        // interrumpa un ciclo de afinidades a mitad.
        std::mutex applyMutex_;
        mutable std::mutex mutex_;
        std::atomic<bool> running_{ false };
        std::thread worker_;

        CpuSnapshot cpuSnapshot_;
        ProcessSnapshot processSnapshot_;
        TopologySnapshot topology_;
        OptimizerState state_;

        DWORD foregroundProcessId_ = 0;

        ThreadMonitor threadMonitor_;
        std::uint64_t tick_ = 0;

        std::unordered_map<
            std::uint32_t, OriginalProcessState> originalProcesses_;
        std::unordered_map<
            std::uint64_t, OriginalThreadState> originalThreads_;
        std::unordered_map<
            std::uint64_t, std::uint64_t> threadLastChange_;
        std::unordered_map<
            std::uint64_t, std::uint16_t> processIslands_;
    };
}
