#pragma once

#include "CpuTopology.h"
#include "OptimizationTypes.h"
#include "ThreadMonitor.h"

#include "../Monitoring/CpuMonitor/CpuInfo.h"
#include "../Monitoring/ProcessMonitor/ProcessInfo.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <limits>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Optimization
{
    struct CpuSetMap
    {
        std::unordered_map<std::uint32_t, ULONG> idByProcessor;
        bool available = false;
    };

    // Orquestador real: reparte los hilos cargados por nucleos fisicos
    // usando CPU Sets (afinidad blanda compatible con la gestion de
    // energia de Windows). No toca prioridades ni afinidades duras y
    // nunca interactua con procesos criticos.
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

        void SetBenchmarkProfile(
            const CpuBenchmarkProfile& profile);
        bool HasBenchmarkProfile() const;
        void SetStatusMessage(const std::string& status);

        OptimizerState State() const;

    private:
        struct ProcessStart
        {
            DWORD processId = 0;
            std::uint64_t startTime100ns = 0;
        };

        // Un PID o un TID suelto NO identifica una instancia: Windows los
        // reutiliza. Todas las claves persistentes incluyen el tiempo de
        // creacion del proceso y, para hilos, el del propio hilo.
        struct ProcessInstance
        {
            DWORD processId = 0;
            std::uint64_t startTime100ns = 0;

            bool operator==(const ProcessInstance& other) const
            {
                return processId == other.processId &&
                       startTime100ns == other.startTime100ns;
            }
        };

        struct ProcessInstanceHash
        {
            std::size_t operator()(const ProcessInstance& key) const
            {
                return std::hash<std::uint64_t>()(
                    (static_cast<std::uint64_t>(key.processId) << 32) ^
                    key.startTime100ns ^
                    (key.startTime100ns >> 32));
            }
        };

        struct ThreadInstance
        {
            DWORD processId = 0;
            std::uint64_t processStart100ns = 0;
            DWORD threadId = 0;
            std::uint64_t threadStart100ns = 0;

            bool operator==(const ThreadInstance& other) const
            {
                return processId == other.processId &&
                       processStart100ns ==
                           other.processStart100ns &&
                       threadId == other.threadId &&
                       threadStart100ns ==
                           other.threadStart100ns;
            }
        };

        struct ThreadInstanceHash
        {
            std::size_t operator()(const ThreadInstance& key) const
            {
                std::uint64_t value =
                    (static_cast<std::uint64_t>(key.processId)
                         << 32) ^
                    key.processStart100ns ^
                    (key.processStart100ns >> 13) ^
                    (static_cast<std::uint64_t>(key.threadId)
                         << 17) ^
                    key.threadStart100ns ^
                    (key.threadStart100ns >> 29);

                return std::hash<std::uint64_t>()(value);
            }
        };

        static constexpr std::size_t NoCore =
            (std::numeric_limits<std::size_t>::max)();

        struct PendingIsland
        {
            std::uint16_t island = 0;
            std::uint8_t confirmations = 0;
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
            std::vector<ULONG> cpuSetIds;
            bool hasAffinity = false;
            bool hasIdeal = false;
            bool hasPriority = false;
            bool hasCpuSets = false;
            bool idealChanged = false;
        };

        // Ultima decision estable por hilo. Se conserva aunque la carga
        // cambie un poco: migrar cada pocos segundos empeora el frame time.
        struct ThreadDecision
        {
            std::size_t coreIndex = NoCore;
            std::size_t candidateCore = NoCore;
            std::uint8_t confirmations = 0;
            std::uint64_t changedTick = 0;
        };

        struct Candidate
        {
            DWORD processId = 0;
            std::uint64_t startTime100ns = 0;
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
            const ThreadInstance& thread,
            const OriginalThreadState& original);
        bool RestoreProcess(
            const ProcessInstance& process,
            const OriginalProcessState& original);

        bool GetProcessStart(
            DWORD processId,
            std::uint64_t& startTime100ns) const;
        bool GetThreadStart(
            DWORD threadId,
            std::uint64_t& startTime100ns) const;
        bool QueryThreadCpuSets(
            HANDLE thread,
            std::vector<ULONG>& ids) const;
        bool IsCritical(const std::wstring& name) const;
        bool IsUserSession(DWORD processId) const;

        OriginalThreadState EnsureThreadOriginal(
            DWORD processId,
            std::uint64_t processStart100ns,
            DWORD threadId,
            std::uint64_t threadStart100ns,
            HANDLE thread);

        std::uint16_t ChooseIsland(
            std::uint16_t previousIsland,
            bool targetProcess,
            bool targetIslandExcluded,
            std::uint16_t excludedIsland,
            WORD group,
            std::uint64_t originalAffinity,
            const std::vector<TopologyProcessor>& processors) const;

        double CoreScore(
            const TopologySnapshot& topology,
            std::size_t coreIndex,
            std::uint16_t island,
            WORD group,
            std::uint64_t originalAffinity,
            bool targetProcess,
            double bestCeiling,
            std::size_t preferredCore) const;

        bool BuildCoreRanking(
            const TopologySnapshot& topology,
            std::uint16_t island,
            WORD group,
            std::uint64_t originalAffinity,
            bool targetProcess,
            std::size_t preferredCore,
            std::vector<std::pair<double, std::size_t>>& ranking) const;

        std::vector<ULONG> CpuSetIdsForCore(
            const TopologySnapshot& topology,
            std::size_t coreIndex,
            WORD group,
            std::uint64_t affinity,
            const CpuSetMap& cpuSets) const;

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
        CpuBenchmarkProfile benchmarkProfile_;

        DWORD foregroundProcessId_ = 0;

        ThreadMonitor threadMonitor_;
        std::uint64_t tick_ = 0;

        std::unordered_map<
            ProcessInstance,
            OriginalProcessState,
            ProcessInstanceHash> originalProcesses_;
        std::unordered_map<
            ThreadInstance,
            OriginalThreadState,
            ThreadInstanceHash> originalThreads_;
        std::unordered_map<
            ThreadInstance,
            std::uint64_t,
            ThreadInstanceHash> threadLastChange_;
        std::unordered_map<
            ProcessInstance,
            std::uint16_t,
            ProcessInstanceHash> processIslands_;
        std::unordered_map<
            ProcessInstance,
            PendingIsland,
            ProcessInstanceHash> pendingIslands_;
        std::unordered_map<
            ThreadInstance,
            ThreadDecision,
            ThreadInstanceHash> threadDecisions_;
        std::uint64_t foregroundStart100ns_ = 0;
    };
}
