#define NOMINMAX
#include "CpuOrchestrator.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cwchar>
#include <map>
#include <unordered_set>
#include <utility>

namespace Optimization
{
    namespace
    {
        // Preferir estabilidad: menos cambios y mas tiempo entre ellos.
        constexpr std::uint64_t CooldownTicks = 10;
        constexpr int MaxChangesPerTick = 2;
        constexpr std::size_t MaxProcesses = 40;
        constexpr std::size_t MaxThreadsPerProcess = 4;
        constexpr std::uint8_t IslandConfirmTicks = 2;

        // Solo migrar si el nucleo nuevo es claramente mejor. Un cambio
        // pequeño no compensa el coste de migrar cache y perturbar el
        // frame pacing.
        constexpr double CoreMoveThreshold = 18.0;

        std::uint64_t FileTime100ns(const FILETIME& value)
        {
            return
                (static_cast<std::uint64_t>(
                     value.dwHighDateTime) << 32) |
                static_cast<std::uint64_t>(
                    value.dwLowDateTime);
        }

        bool HasBit(std::uint64_t mask, DWORD index)
        {
            if (index >= 64)
                return false;

            return (mask & (std::uint64_t{ 1 } << index)) != 0;
        }

        std::uint16_t IslandOf(const TopologyProcessor& processor)
        {
            return processor.l3CacheId != 0
                       ? processor.l3CacheId
                       : processor.l2CacheId;
        }

        std::uint32_t ProcessorKey(WORD group, BYTE logicalIndex)
        {
            return
                (static_cast<std::uint32_t>(group) << 8) |
                static_cast<std::uint32_t>(logicalIndex);
        }

        CpuSetMap QueryCpuSetMap()
        {
            CpuSetMap result;

            ULONG size = 0;
            if (!GetSystemCpuSetInformation(
                    nullptr, 0, &size, nullptr, 0) &&
                GetLastError() != ERROR_INSUFFICIENT_BUFFER)
            {
                return result;
            }

            if (size == 0)
                return result;

            std::vector<BYTE> buffer(size);
            ULONG returned = 0;
            if (!GetSystemCpuSetInformation(
                    reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(
                        buffer.data()),
                    size,
                    &returned,
                    nullptr,
                    0))
            {
                return result;
            }

            BYTE* current = buffer.data();
            BYTE* end = buffer.data() + returned;

            while (current < end)
            {
                auto* info =
                    reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(
                        current);

                if (info->Size == 0 ||
                    current + info->Size > end)
                {
                    break;
                }

                if (info->Type == CpuSetInformation)
                {
                    const auto& set = info->CpuSet;

                    // Un CPU Set es una preferencia blanda: Windows
                    // sigue aplicando gestion de energia y puede ignorar
                    // la sugerencia si no conviene.
                    result.idByProcessor[
                        ProcessorKey(
                            set.Group,
                            set.LogicalProcessorIndex)] = set.Id;
                }

                current += info->Size;
            }

            result.available = !result.idByProcessor.empty();
            return result;
        }

        const wchar_t* CriticalNames[] = {
            L"system",
            L"system idle process",
            L"registry",
            L"smss.exe",
            L"csrss.exe",
            L"wininit.exe",
            L"winlogon.exe",
            L"services.exe",
            L"lsass.exe",
            L"svchost.exe",
            L"fontdrvhost.exe",
            L"dwm.exe",
            L"explorer.exe",
            L"processoptimizer.service.exe",
            L"processoptimizer.ui.exe",
            L"processoptimizer.tray.exe"
        };

        std::wstring ToLower(const std::wstring& value)
        {
            std::wstring result = value;
            std::transform(
                result.begin(), result.end(), result.begin(),
                [](wchar_t character)
                {
                    return static_cast<wchar_t>(
                        towlower(character));
                });
            return result;
        }
    }

    CpuOrchestrator::CpuOrchestrator(Publisher publisher)
        : publisher_(std::move(publisher))
    {
    }

    CpuOrchestrator::~CpuOrchestrator()
    {
        Stop();
    }

    void CpuOrchestrator::Start()
    {
        bool expected = false;
        if (!running_.compare_exchange_strong(
                expected, true))
        {
            return;
        }

        worker_ = std::thread(&CpuOrchestrator::WorkerLoop, this);
    }

    void CpuOrchestrator::Stop()
    {
        if (!running_.exchange(false))
            return;

        if (worker_.joinable())
            worker_.join();

        RestoreAll();
    }

    void CpuOrchestrator::SetTopology(
        const TopologySnapshot& topology)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            topology_ = topology;
            state_.status = topology.available
                                ? "Topologia detectada"
                                : "Topologia no disponible";
        }
        Publish();
    }

    void CpuOrchestrator::OnCpuSnapshot(
        const CpuSnapshot& snapshot)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cpuSnapshot_ = snapshot;
    }

    void CpuOrchestrator::OnProcessSnapshot(
        const ProcessSnapshot& snapshot)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        processSnapshot_ = snapshot;
    }

    void CpuOrchestrator::SetEnabled(bool enabled)
    {
        std::lock_guard<std::mutex> applyLock(applyMutex_);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.enabled = enabled;

            if (!enabled)
            {
                state_.mode = OptimizerMode::Off;
                state_.status = "Optimizacion desactivada";
            }
            else
            {
                state_.status = "Optimizacion activada";
            }
        }

        if (!enabled)
            RestoreAll();

        Publish();
    }

    void CpuOrchestrator::SetMode(OptimizerMode mode)
    {
        std::lock_guard<std::mutex> applyLock(applyMutex_);

        bool disable = false;
        OptimizerMode previousMode = OptimizerMode::Off;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            previousMode = state_.mode;
            state_.mode = mode;
            state_.enabled = mode != OptimizerMode::Off;
            disable = mode == OptimizerMode::Off;

            if (disable)
                state_.status = "Optimizacion desactivada";
            else
                state_.status = "Perfil Balanceado";
        }

        // Cambiar de perfil no puede arrastrar CPU Sets del anterior.
        if (previousMode != OptimizerMode::Off &&
            previousMode != mode)
        {
            RestoreAll();
        }

        if (disable)
            RestoreAll();

        Publish();
    }

    void CpuOrchestrator::SetForegroundProcess(DWORD processId)
    {
        std::uint64_t start = 0;
        if (processId != 0)
            GetProcessStart(processId, start);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            foregroundProcessId_ = processId;
            foregroundStart100ns_ = start;
        }
        Publish();
    }

    OptimizerState CpuOrchestrator::State() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_;
    }

    void CpuOrchestrator::SetBenchmarkProfile(
        const CpuBenchmarkProfile& profile)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            benchmarkProfile_ = profile;
        }

        Publish();
    }

    bool CpuOrchestrator::HasBenchmarkProfile() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return benchmarkProfile_.HasData();
    }

    void CpuOrchestrator::SetStatusMessage(
        const std::string& status)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.status = status;
        }

        Publish();
    }

    void CpuOrchestrator::WorkerLoop()
    {
        while (running_.load())
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(5000));

            if (!running_.load())
                break;

            Tick();
        }
    }

    void CpuOrchestrator::Tick()
    {
        std::lock_guard<std::mutex> applyLock(applyMutex_);

        bool enabled = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            enabled = state_.enabled &&
                      state_.mode != OptimizerMode::Off;
            ++tick_;
        }

        if (!enabled)
        {
            RestoreAll();
            return;
        }

        OptimizerMode currentMode = OptimizerMode::Off;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            currentMode = state_.mode;
        }

        // Balanceado no reubica hilos: Windows planifica mejor que cualquier
        // afinidad manual. Solo se publica estado y se limpian cambios
        // residuales. La contencion real de segundo plano la gestiona
        // BackgroundContainment con evidencia y A/B validado.
        if (currentMode == OptimizerMode::Balanced)
        {
            RestoreAll();

            {
                std::lock_guard<std::mutex> lock(mutex_);
                state_.running = true;
                state_.processes.clear();
                for (auto& core : state_.cores)
                    core.threadCount = 0;
                state_.status =
                    "Balanceado: juego intacto, Windows decide";
            }
            Publish();
            return;
        }

        // No queda ningun plan que aplicar: Off ya retorno y Balanceado
        // tambien. Este punto no deberia alcanzarse.
        Publish();
    }

    bool CpuOrchestrator::GetProcessStart(
        DWORD processId,
        std::uint64_t& startTime100ns) const
    {
        HANDLE process = OpenProcess(
            PROCESS_QUERY_LIMITED_INFORMATION,
            FALSE,
            processId);

        if (process == nullptr)
            return false;

        FILETIME creation{};
        FILETIME exitTime{};
        FILETIME kernel{};
        FILETIME user{};

        const bool ok = GetProcessTimes(
                            process, &creation, &exitTime,
                            &kernel, &user) != FALSE;

        CloseHandle(process);

        if (!ok)
            return false;

        startTime100ns = FileTime100ns(creation);
        return true;
    }

    bool CpuOrchestrator::GetThreadStart(
        DWORD threadId,
        std::uint64_t& startTime100ns) const
    {
        HANDLE thread = OpenThread(
            THREAD_QUERY_LIMITED_INFORMATION,
            FALSE,
            threadId);

        if (thread == nullptr)
            return false;

        FILETIME creation{};
        FILETIME exitTime{};
        FILETIME kernel{};
        FILETIME user{};

        const bool ok = GetThreadTimes(
                            thread, &creation, &exitTime,
                            &kernel, &user) != FALSE;

        CloseHandle(thread);

        if (!ok)
            return false;

        startTime100ns = FileTime100ns(creation);
        return true;
    }

    bool CpuOrchestrator::QueryThreadCpuSets(
        HANDLE thread,
        std::vector<ULONG>& ids) const
    {
        ULONG needed = 0;
        if (!GetThreadSelectedCpuSets(
                thread, nullptr, 0, &needed))
        {
            return false;
        }

        ids.assign(needed, 0);
        if (needed == 0)
            return true;

        if (!GetThreadSelectedCpuSets(
                thread, ids.data(), needed, &needed))
        {
            ids.clear();
            return false;
        }

        ids.resize(needed);
        return true;
    }

    bool CpuOrchestrator::IsCritical(
        const std::wstring& name) const
    {
        if (name.empty())
            return false;

        const std::wstring lower = ToLower(name);

        for (const wchar_t* critical : CriticalNames)
        {
            if (lower == critical)
                return true;
        }

        return false;
    }

    bool CpuOrchestrator::IsUserSession(DWORD processId) const
    {
        DWORD sessionId = 0;
        if (!ProcessIdToSessionId(processId, &sessionId))
            return false;

        return sessionId != 0;
    }

    CpuOrchestrator::OriginalThreadState
    CpuOrchestrator::EnsureThreadOriginal(
        DWORD processId,
        std::uint64_t processStart100ns,
        DWORD threadId,
        std::uint64_t threadStart100ns,
        HANDLE thread)
    {
        const ThreadInstance key{
            processId,
            processStart100ns,
            threadId,
            threadStart100ns };

        OriginalThreadState original;
        original.startTime100ns = processStart100ns;

        GROUP_AFFINITY affinity{};
        if (GetThreadGroupAffinity(thread, &affinity))
        {
            original.group = affinity.Group;
            original.affinity = affinity.Mask;
            original.hasAffinity = true;
        }

        PROCESSOR_NUMBER ideal{};
        if (GetThreadIdealProcessorEx(thread, &ideal))
        {
            original.idealProcessor = ideal.Number;
            original.hasIdeal = true;
        }

        const int priority = GetThreadPriority(thread);
        if (priority != THREAD_PRIORITY_ERROR_RETURN)
        {
            original.priority = priority;
            original.hasPriority = true;
        }

        std::vector<ULONG> cpuSetIds;
        if (QueryThreadCpuSets(thread, cpuSetIds) &&
            !cpuSetIds.empty())
        {
            original.cpuSetIds = std::move(cpuSetIds);
            original.hasCpuSets = true;
        }

        std::lock_guard<std::mutex> lock(mutex_);

        const auto it = originalThreads_.find(key);
        if (it != originalThreads_.end())
            return it->second;

        originalThreads_[key] = original;
        return original;
    }

    bool CpuOrchestrator::RestoreThread(
        const ThreadInstance& threadInstance,
        const OriginalThreadState& original)
    {
        std::uint64_t processStart = 0;
        if (!GetProcessStart(
                threadInstance.processId, processStart) ||
            processStart !=
                threadInstance.processStart100ns)
        {
            return false;
        }

        std::uint64_t threadStart = 0;
        if (!GetThreadStart(
                threadInstance.threadId, threadStart) ||
            threadStart !=
                threadInstance.threadStart100ns)
        {
            return false;
        }

        HANDLE thread = OpenThread(
            THREAD_QUERY_LIMITED_INFORMATION |
                THREAD_SET_INFORMATION,
            FALSE,
            threadInstance.threadId);

        if (thread == nullptr)
            return false;

        bool ok = true;

        // Si el hilo tenia un CPU Set explicito se recupera. Si no,
        // se limpia el que puso el orquestador para volver al default
        // del proceso.
        std::vector<ULONG> currentIds;
        const bool queried = QueryThreadCpuSets(thread, currentIds);

        if (original.hasCpuSets ||
            (queried && !currentIds.empty()))
        {
            std::vector<ULONG> idsToRestore =
                original.cpuSetIds;

            ULONG* ids = original.hasCpuSets
                             ? idsToRestore.data()
                             : nullptr;
            const ULONG count =
                original.hasCpuSets
                    ? static_cast<ULONG>(
                          idsToRestore.size())
                    : 0;

            if (!SetThreadSelectedCpuSets(
                    thread, ids, count))
            {
                ok = false;
            }
        }

        // Solo se restaura el ideal processor si esta version tuvo que
        // usarlo como alternativa cuando no habia CPU Sets.
        if (ok && original.idealChanged && original.hasIdeal)
        {
            PROCESSOR_NUMBER number{};
            number.Group = original.group;
            number.Number = static_cast<BYTE>(
                std::min<DWORD>(
                    original.idealProcessor, 63));

            if (!SetThreadIdealProcessorEx(
                    thread, &number, nullptr))
            {
                ok = false;
            }
        }

        CloseHandle(thread);
        return ok;
    }

    bool CpuOrchestrator::RestoreProcess(
        const ProcessInstance& processInstance,
        const OriginalProcessState&)
    {
        // Esta version no cambia prioridades de proceso. Solo valida que
        // la instancia siga viva para no dejar estado pendiente.
        std::uint64_t currentStart = 0;
        return GetProcessStart(
                   processInstance.processId, currentStart) &&
               currentStart ==
                   processInstance.startTime100ns;
    }

    void CpuOrchestrator::RestoreAll()
    {
        std::unordered_map<
            ThreadInstance,
            OriginalThreadState,
            ThreadInstanceHash> threads;
        std::unordered_map<
            ProcessInstance,
            OriginalProcessState,
            ProcessInstanceHash> processes;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            threads = originalThreads_;
            processes = originalProcesses_;
            originalThreads_.clear();
            originalProcesses_.clear();
            threadLastChange_.clear();
            processIslands_.clear();
            pendingIslands_.clear();
            threadDecisions_.clear();

            state_.revertedChanges +=
                static_cast<std::uint32_t>(
                    threads.size() + processes.size());
        }

        for (const auto& entry : threads)
            RestoreThread(entry.first, entry.second);

        for (const auto& entry : processes)
            RestoreProcess(entry.first, entry.second);
    }

    std::uint16_t CpuOrchestrator::ChooseIsland(
        std::uint16_t previousIsland,
        bool targetProcess,
        bool,
        std::uint16_t excludedIsland,
        WORD group,
        std::uint64_t originalAffinity,
        const std::vector<TopologyProcessor>& processors) const
    {
        struct IslandInfo
        {
            double usage = 0.0;
            std::size_t count = 0;
        };

        std::map<std::uint16_t, IslandInfo> islands;

        bool hasPerformance = false;
        if (targetProcess)
        {
            for (const TopologyProcessor& processor : processors)
            {
                if (processor.coreClass ==
                    CoreClass::Performance)
                {
                    hasPerformance = true;
                    break;
                }
            }
        }

        for (const TopologyProcessor& processor : processors)
        {
            if (group != 0xFFFF &&
                processor.group != group)
            {
                continue;
            }

            if (originalAffinity != 0 &&
                !HasBit(
                    originalAffinity,
                    processor.logicalIndex))
            {
                continue;
            }

            if (targetProcess && hasPerformance &&
                processor.coreClass !=
                    CoreClass::Performance)
            {
                continue;
            }

            const std::uint16_t island = IslandOf(processor);
            if (island == 0)
                continue;

            if (excludedIsland != 0 &&
                island == excludedIsland)
            {
                continue;
            }

            IslandInfo& info = islands[island];
            info.usage += (std::max)(
                0.0, processor.usagePercent);
            ++info.count;
        }

        if (islands.empty())
            return 0;

        std::uint16_t best = 0;
        double bestAverage = 0.0;

        for (const auto& entry : islands)
        {
            const double average =
                entry.second.usage /
                static_cast<double>(
                    std::max<std::size_t>(
                        entry.second.count, 1));

            if (best == 0 || average < bestAverage)
            {
                best = entry.first;
                bestAverage = average;
            }
        }

        if (previousIsland != 0 &&
            islands.find(previousIsland) != islands.end())
        {
            const auto& previous = islands.at(previousIsland);

            const double previousAverage =
                previous.usage /
                static_cast<double>(
                    std::max<std::size_t>(
                        previous.count, 1));

            if (previousAverage <= bestAverage + 12.0)
                return previousIsland;
        }

        return best;
    }

    double CpuOrchestrator::CoreScore(
        const TopologySnapshot& topology,
        std::size_t coreIndex,
        std::uint16_t island,
        WORD group,
        std::uint64_t originalAffinity,
        bool targetProcess,
        double bestCeiling,
        std::size_t preferredCore) const
    {
        const TopologyCore* core = nullptr;
        for (const TopologyCore& candidate : topology.cores)
        {
            if (candidate.coreIndex == coreIndex)
            {
                core = &candidate;
                break;
            }
        }

        if (core == nullptr || core->logicalIndices.empty())
            return 1e100;

        double load = 0.0;
        double ceiling = 0.0;
        bool any = false;
        bool parked = true;
        bool classMismatch = false;

        for (std::uint32_t index : core->logicalIndices)
        {
            if (index >= topology.processors.size())
                continue;

            const TopologyProcessor& processor =
                topology.processors[index];

            if (processor.group != group)
                continue;

            if (originalAffinity != 0 &&
                !HasBit(
                    originalAffinity,
                    processor.logicalIndex))
            {
                continue;
            }

            if (island != 0 &&
                IslandOf(processor) != island)
            {
                continue;
            }

            if (targetProcess &&
                topology.kind == TopologyKind::Hybrid &&
                processor.coreClass ==
                    CoreClass::Efficiency)
            {
                classMismatch = true;
            }

            load += (std::max)(
                0.0, processor.usagePercent);

            const double logicalCeiling =
                !processor.frequencyAvailable ||
                        processor.parked
                    ? 0.0
                    : (processor.maxMHz > 0.0
                           ? processor.maxMHz
                           : processor.mhzLimitMHz);

            ceiling = (std::max)(ceiling, logicalCeiling);
            parked = parked && processor.parked;
            any = true;
        }

        if (!any)
            return 1e100;

        double score = load * 2.0;

        if (ceiling > 0.0 && bestCeiling > 0.0)
        {
            score +=
                (1.0 - ceiling / bestCeiling) * 25.0;
        }
        else
        {
            score += 25.0;
        }

        if (parked)
            score += 1000.0;

        if (classMismatch)
            score += 60.0;

        if (preferredCore == coreIndex)
            score -= 15.0;

        // El perfil de la prueba bajo demanda es una medida real, no una
        // suposicion de P/E. Solo desempata entre nucleos del mismo grupo
        // y nunca sustituye a la carga observada en tiempo real.
        double measuredScore = -1.0;
        {
            std::lock_guard<std::mutex> lock(mutex_);

            for (const ProcessorBenchmark& benchmark :
                 benchmarkProfile_.processors)
            {
                if (benchmark.group != group ||
                    benchmark.physicalCoreIndex != coreIndex)
                {
                    continue;
                }

                measuredScore =
                    (std::max)(measuredScore, benchmark.score);
            }
        }

        if (measuredScore > 0.0)
            score -= measuredScore * 0.35;

        return score;
    }

    bool CpuOrchestrator::BuildCoreRanking(
        const TopologySnapshot& topology,
        std::uint16_t island,
        WORD group,
        std::uint64_t originalAffinity,
        bool targetProcess,
        std::size_t preferredCore,
        std::vector<std::pair<double, std::size_t>>& ranking) const
    {
        double bestCeiling = 0.0;

        for (const TopologyProcessor& processor :
             topology.processors)
        {
            if (processor.group != group)
                continue;

            if (originalAffinity != 0 &&
                !HasBit(
                    originalAffinity,
                    processor.logicalIndex))
            {
                continue;
            }

            if (island != 0 &&
                IslandOf(processor) != island)
            {
                continue;
            }

            if (targetProcess &&
                topology.kind == TopologyKind::Hybrid &&
                processor.coreClass ==
                    CoreClass::Efficiency)
            {
                continue;
            }

            if (!processor.frequencyAvailable ||
                processor.parked)
            {
                continue;
            }

            bestCeiling = (std::max)(
                bestCeiling,
                processor.maxMHz > 0.0
                    ? processor.maxMHz
                    : processor.mhzLimitMHz);
        }

        ranking.clear();

        for (const TopologyCore& core : topology.cores)
        {
            const double score = CoreScore(
                topology,
                core.coreIndex,
                island,
                group,
                originalAffinity,
                targetProcess,
                bestCeiling,
                preferredCore);

            if (score >= 1e100)
                continue;

            ranking.emplace_back(score, core.coreIndex);
        }

        std::sort(
            ranking.begin(), ranking.end(),
            [](const std::pair<double, std::size_t>& left,
               const std::pair<double, std::size_t>& right)
            {
                if (left.first == right.first)
                    return left.second < right.second;

                return left.first < right.first;
            });

        return !ranking.empty();
    }

    std::vector<ULONG> CpuOrchestrator::CpuSetIdsForCore(
        const TopologySnapshot& topology,
        std::size_t coreIndex,
        WORD group,
        std::uint64_t affinity,
        const CpuSetMap& cpuSets) const
    {
        std::vector<ULONG> ids;

        if (!cpuSets.available || coreIndex == NoCore)
            return ids;

        for (const TopologyProcessor& processor :
             topology.processors)
        {
            if (processor.physicalCoreIndex != coreIndex ||
                processor.group != group)
            {
                continue;
            }

            if (affinity != 0 &&
                !HasBit(affinity, processor.logicalIndex))
            {
                continue;
            }

            const auto it = cpuSets.idByProcessor.find(
                ProcessorKey(
                    processor.group,
                    processor.logicalIndex));

            if (it == cpuSets.idByProcessor.end())
                continue;

            ids.push_back(it->second);
        }

        std::sort(ids.begin(), ids.end());
        ids.erase(
            std::unique(ids.begin(), ids.end()), ids.end());

        return ids;
    }

    void CpuOrchestrator::Publish()
    {
        OptimizerState snapshot;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            snapshot = state_;
        }

        if (publisher_)
            publisher_(snapshot);
    }
}
