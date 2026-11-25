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
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.mode = mode;
            state_.enabled = mode != OptimizerMode::Off;
            disable = mode == OptimizerMode::Off;

            if (disable)
                state_.status = "Optimizacion desactivada";
            else
                state_.status = mode == OptimizerMode::Gaming
                                    ? "Perfil Gaming"
                                    : "Perfil Multitarea";
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
                std::chrono::milliseconds(2000));

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

        BuildPlanAndApply();
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

    void CpuOrchestrator::BuildPlanAndApply()
    {
        OptimizerMode mode = OptimizerMode::Off;
        DWORD foregroundPid = 0;
        std::uint64_t foregroundStart = 0;
        std::uint64_t currentTick = 0;
        TopologySnapshot topology;
        CpuSnapshot cpu;
        ProcessSnapshot processes;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            mode = state_.mode;
            foregroundPid = foregroundProcessId_;
            foregroundStart = foregroundStart100ns_;
            currentTick = tick_;
            topology = topology_;
            cpu = cpuSnapshot_;
            processes = processSnapshot_;
        }

        if (!topology.available)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.status =
                "Sin topologia: Windows decide";
            state_.running = false;
            return;
        }

        CpuTopology::RefreshUsage(topology, cpu);

        const CpuSetMap cpuSets = QueryCpuSetMap();

        std::vector<Candidate> candidates;
        std::unordered_set<DWORD> candidatePids;

        for (const ProcessInfo& process :
             processes.processes)
        {
            if (process.processId == 0 ||
                process.processId == 4)
            {
                continue;
            }

            if (IsCritical(process.name) ||
                !IsUserSession(process.processId))
            {
                continue;
            }

            std::uint64_t processStart = 0;
            if (!GetProcessStart(
                    process.processId, processStart))
            {
                continue;
            }

            const bool foreground =
                process.processId == foregroundPid &&
                foregroundStart != 0 &&
                processStart == foregroundStart;

            if (!foreground &&
                process.cpuUsagePercent < 0.25 &&
                process.gpuUsagePercent < 0.25)
            {
                continue;
            }

            if (candidatePids.find(
                    process.processId) !=
                candidatePids.end())
            {
                continue;
            }

            Candidate candidate;
            candidate.processId = process.processId;
            candidate.startTime100ns = processStart;
            candidate.name = process.name;
            candidate.cpuUsagePercent =
                process.cpuUsagePercent;
            candidate.gpuUsagePercent =
                process.gpuUsagePercent;
            candidate.foreground = foreground;

            candidates.push_back(candidate);
            candidatePids.insert(process.processId);
        }

        std::sort(
            candidates.begin(), candidates.end(),
            [](const Candidate& left, const Candidate& right)
            {
                if (left.foreground != right.foreground)
                    return left.foreground;

                return left.cpuUsagePercent >
                       right.cpuUsagePercent;
            });

        if (candidates.size() > MaxProcesses)
            candidates.resize(MaxProcesses);

        DWORD targetPid = 0;
        std::uint64_t targetStart = 0;

        if (mode == OptimizerMode::Gaming)
        {
            for (const Candidate& candidate : candidates)
            {
                if (!candidate.foreground)
                    continue;

                if (candidate.cpuUsagePercent >= 1.5 ||
                    candidate.gpuUsagePercent >= 1.5)
                {
                    targetPid = candidate.processId;
                    targetStart =
                        candidate.startTime100ns;
                }

                break;
            }
        }

        const std::uint16_t targetIsland =
            targetPid != 0
                ? ChooseIsland(
                      0, true, false, 0,
                      0xFFFF, 0, topology.processors)
                : 0;

        // En Gaming solo se mira el proceso con foco. Los hilos del
        // resto se dejan al planificador de Windows: moverlos era una
        // de las causas de caidas de frames.
        std::unordered_set<DWORD> sampledPids;
        if (mode == OptimizerMode::Multitasking)
        {
            // Gaming no muestrea hilos del juego: Windows decide.
            for (DWORD pid : candidatePids)
                sampledPids.insert(pid);
        }

        std::vector<ThreadLoad> loads =
            threadMonitor_.Sample(
                sampledPids, topology.processors.size());

        std::unordered_map<DWORD, std::vector<ThreadLoad>>
            threadsByProcess;

        for (ThreadLoad& load : loads)
            threadsByProcess[load.processId].push_back(load);

        for (auto& entry : threadsByProcess)
        {
            std::vector<ThreadLoad>& threads = entry.second;

            std::sort(
                threads.begin(), threads.end(),
                [](const ThreadLoad& left,
                   const ThreadLoad& right)
                {
                    return left.usagePercent >
                           right.usagePercent;
                });

            if (threads.size() > MaxThreadsPerProcess)
                threads.resize(MaxThreadsPerProcess);
        }

        OptimizerState output;
        output.enabled = true;
        output.mode = mode;
        output.running = true;
        output.foregroundProcessId = foregroundPid;
        output.targetProcessId = targetPid;
        output.lastUpdateMs = static_cast<std::uint64_t>(
            GetTickCount64());

        if (mode == OptimizerMode::Gaming)
        {
            // El estado real de la sonda se muestra en su propia tarjeta;
            // aqui no afirmar que se esta midiendo si ETW no esta activo.
            output.status = "Gaming: juego intacto, Windows decide";
        }
        else
        {
            output.status =
                "Multitarea: solo procesos de segundo plano";
        }

        std::size_t bestFrequencyIndex = 0;
        double bestFrequencyCeiling = 0.0;

        for (std::size_t i = 0;
             i < topology.processors.size();
             ++i)
        {
            const TopologyProcessor& processor =
                topology.processors[i];

            if (!processor.frequencyAvailable ||
                processor.parked)
            {
                continue;
            }

            const double ceiling =
                processor.maxMHz > 0.0
                    ? processor.maxMHz
                    : processor.mhzLimitMHz;

            if (ceiling > bestFrequencyCeiling)
            {
                bestFrequencyCeiling = ceiling;
                bestFrequencyIndex = i;
            }
        }

        for (std::size_t i = 0;
             i < topology.processors.size();
             ++i)
        {
            const TopologyProcessor& processor =
                topology.processors[i];

            CoreAssignment assignment;
            assignment.group = processor.group;
            assignment.logicalIndex =
                processor.logicalIndex;
            assignment.physicalCoreIndex =
                processor.physicalCoreIndex;
            assignment.coreClass = processor.coreClass;
            assignment.l2CacheId = processor.l2CacheId;
            assignment.l3CacheId = processor.l3CacheId;
            assignment.usagePercent =
                processor.usagePercent;
            assignment.frequencyAvailable =
                processor.frequencyAvailable;
            assignment.bestFrequency =
                i == bestFrequencyIndex &&
                bestFrequencyCeiling > 0.0;
            assignment.maxMHz = processor.maxMHz;
            assignment.currentMHz = processor.currentMHz;
            assignment.mhzLimitMHz = processor.mhzLimitMHz;

            if (assignment.bestFrequency)
            {
                char detail[96]{};
                std::snprintf(
                    detail,
                    sizeof(detail),
                    "mejor frecuencia: %.0f MHz",
                    bestFrequencyCeiling);
                assignment.reason = detail;
            }
            else
            {
                assignment.reason =
                    processor.l3CacheId != 0
                        ? "L3 compartida"
                        : (processor.l2CacheId != 0
                               ? "L2 compartida"
                               : "sin datos de cache");
            }
            output.cores.push_back(assignment);
        }

        int changesThisTick = 0;
        std::uint32_t applied = 0;
        std::uint32_t failed = 0;

        std::unordered_set<
            ThreadInstance, ThreadInstanceHash> activeThreads;
        std::unordered_set<
            ProcessInstance, ProcessInstanceHash> activeProcesses;
        std::unordered_set<std::size_t> usedCores;

        for (const Candidate& candidate : candidates)
        {
            const std::uint64_t startTime =
                candidate.startTime100ns;

            const ProcessInstance instance{
                candidate.processId, startTime };

            const bool isTarget =
                candidate.processId == targetPid &&
                startTime == targetStart;

            ProcessPlan plan;
            plan.processId = candidate.processId;
            plan.name = candidate.name;
            plan.foreground = candidate.foreground;
            plan.cpuUsagePercent = candidate.cpuUsagePercent;
            plan.gpuUsagePercent = candidate.gpuUsagePercent;

            const bool leaveAlone =
                mode == OptimizerMode::Gaming ||
                candidate.foreground;

            if (leaveAlone)
            {
                plan.currentPriority =
                    NORMAL_PRIORITY_CLASS;
                plan.targetPriority =
                    NORMAL_PRIORITY_CLASS;
                plan.decision =
                    mode == OptimizerMode::Gaming
                        ? "juego intacto: sin CPU Sets ni prioridades"
                        : "primer plano intacto: Windows decide";
                output.processes.push_back(plan);
                continue;
            }

            activeProcesses.insert(instance);

            const auto threadsIt =
                threadsByProcess.find(candidate.processId);

            if (threadsIt != threadsByProcess.end())
            {
                for (const ThreadLoad& load :
                     threadsIt->second)
                {
                    HANDLE thread = OpenThread(
                        THREAD_QUERY_LIMITED_INFORMATION |
                            THREAD_SET_INFORMATION,
                        FALSE,
                        load.threadId);

                    if (thread == nullptr)
                        continue;

                    const ThreadInstance threadInstance{
                        candidate.processId,
                        startTime,
                        load.threadId,
                        load.start100ns };

                    const OriginalThreadState original =
                        EnsureThreadOriginal(
                            threadInstance.processId,
                            threadInstance.processStart100ns,
                            threadInstance.threadId,
                            threadInstance.threadStart100ns,
                            thread);

                    WORD currentGroup = original.group;
                    std::uint64_t currentAffinity =
                        original.affinity;
                    DWORD currentIdeal =
                        original.idealProcessor;
                    int currentPriority =
                        original.priority;

                    GROUP_AFFINITY currentGroupAffinity{};
                    if (GetThreadGroupAffinity(
                            thread, &currentGroupAffinity))
                    {
                        currentGroup =
                            currentGroupAffinity.Group;
                        currentAffinity =
                            currentGroupAffinity.Mask;
                    }

                    PROCESSOR_NUMBER currentIdealNumber{};
                    if (GetThreadIdealProcessorEx(
                            thread, &currentIdealNumber))
                    {
                        currentIdeal =
                            currentIdealNumber.Number;
                    }

                    const int queriedPriority =
                        GetThreadPriority(thread);
                    if (queriedPriority !=
                        THREAD_PRIORITY_ERROR_RETURN)
                    {
                        currentPriority = queriedPriority;
                    }

                    ThreadDecision decision;
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        const auto it =
                            threadDecisions_.find(
                                threadInstance);
                        if (it != threadDecisions_.end())
                            decision = it->second;
                    }

                    std::vector<
                        std::pair<double, std::size_t>> ranking;

                    const bool hasRanking = BuildCoreRanking(
                        topology,
                        isTarget ? targetIsland : 0,
                        currentGroup,
                        original.hasAffinity
                            ? original.affinity
                            : currentAffinity,
                        isTarget,
                        decision.coreIndex,
                        ranking);

                    if (!hasRanking)
                    {
                        CloseHandle(thread);
                        continue;
                    }

                    std::size_t chosenCore = ranking.front().second;
                    double chosenScore = ranking.front().first;

                    for (const auto& entry : ranking)
                    {
                        if (usedCores.find(entry.second) ==
                            usedCores.end())
                        {
                            chosenCore = entry.second;
                            chosenScore = entry.first;
                            break;
                        }
                    }

                    std::size_t effectiveCore = chosenCore;

                    if (decision.coreIndex != NoCore)
                    {
                        const double currentScore = CoreScore(
                            topology,
                            decision.coreIndex,
                            isTarget ? targetIsland : 0,
                            currentGroup,
                            original.hasAffinity
                                ? original.affinity
                                : currentAffinity,
                            isTarget,
                            0.0,
                            decision.coreIndex);

                        const bool cooledDown =
                            currentTick >= decision.changedTick &&
                            currentTick - decision.changedTick >=
                                CooldownTicks;

                        if (!cooledDown ||
                            chosenScore + CoreMoveThreshold >=
                                currentScore)
                        {
                            effectiveCore = decision.coreIndex;
                            chosenScore = currentScore;
                            decision.candidateCore = NoCore;
                            decision.confirmations = 0;
                        }
                        else if (
                            decision.candidateCore ==
                            chosenCore)
                        {
                            ++decision.confirmations;

                            if (decision.confirmations >=
                                IslandConfirmTicks)
                            {
                                decision.confirmations = 0;
                                decision.candidateCore = NoCore;
                            }
                            else
                            {
                                effectiveCore =
                                    decision.coreIndex;
                            }
                        }
                        else
                        {
                            decision.candidateCore =
                                chosenCore;
                            decision.confirmations = 1;
                            effectiveCore =
                                decision.coreIndex;
                        }
                    }

                    std::vector<ULONG> desiredIds =
                        CpuSetIdsForCore(
                            topology,
                            effectiveCore,
                            currentGroup,
                            original.hasAffinity
                                ? original.affinity
                                : currentAffinity,
                            cpuSets);

                    DWORD desiredIdeal = currentIdeal;
                    std::size_t representative =
                        topology.processors.size();

                    for (std::size_t index = 0;
                         index < topology.processors.size();
                         ++index)
                    {
                        const TopologyProcessor& processor =
                            topology.processors[index];

                        if (processor.physicalCoreIndex !=
                                effectiveCore ||
                            processor.group != currentGroup)
                        {
                            continue;
                        }

                        if (representative ==
                                topology.processors.size() ||
                            processor.logicalIndex <
                                topology.processors[
                                    representative]
                                    .logicalIndex)
                        {
                            representative = index;
                        }
                    }

                    if (representative <
                        topology.processors.size())
                    {
                        desiredIdeal =
                            topology.processors[
                                representative].logicalIndex;
                    }

                    std::vector<ULONG> currentIds;
                    const bool hasCurrentIds =
                        QueryThreadCpuSets(thread, currentIds);

                    const bool useCpuSets = !desiredIds.empty();
                    const bool restoreIdeal =
                        original.idealChanged && useCpuSets;

                    const bool needsSetChange =
                        useCpuSets &&
                        (!hasCurrentIds ||
                         currentIds != desiredIds);

                    const bool needsIdealChange =
                        !useCpuSets &&
                        desiredIdeal != currentIdeal;

                    const bool needsChange =
                        needsSetChange || needsIdealChange ||
                        restoreIdeal;

                    bool appliedThread = !needsChange;
                    bool verified = !needsChange;

                    if (needsChange)
                    {
                        bool allowed = true;
                        {
                            std::lock_guard<std::mutex> lock(mutex_);
                            const auto lastIt =
                                threadLastChange_.find(
                                    threadInstance);

                            allowed =
                                allowed &&
                                (lastIt ==
                                     threadLastChange_.end() ||
                                 currentTick -
                                         lastIt->second >=
                                     CooldownTicks);
                        }

                        if (allowed &&
                            changesThisTick <
                                MaxChangesPerTick)
                        {
                            bool ok = true;

                            if (restoreIdeal &&
                                original.hasIdeal)
                            {
                                PROCESSOR_NUMBER number{};
                                number.Group = original.group;
                                number.Number =
                                    static_cast<BYTE>(
                                        std::min<DWORD>(
                                            original.idealProcessor,
                                            63));

                                ok =
                                    SetThreadIdealProcessorEx(
                                        thread, &number,
                                        nullptr) != FALSE;
                            }

                            if (ok && needsSetChange)
                            {
                                ok =
                                    SetThreadSelectedCpuSets(
                                        thread,
                                        desiredIds.data(),
                                        static_cast<ULONG>(
                                            desiredIds.size())) !=
                                    FALSE;
                            }

                            if (ok && needsIdealChange)
                            {
                                PROCESSOR_NUMBER number{};
                                number.Group = currentGroup;
                                number.Number =
                                    static_cast<BYTE>(
                                        std::min<DWORD>(
                                            desiredIdeal, 63));

                                ok =
                                    SetThreadIdealProcessorEx(
                                        thread, &number,
                                        nullptr) != FALSE;
                            }

                            appliedThread = ok;

                            if (ok)
                            {
                                ++changesThisTick;
                                ++applied;
                                verified = true;

                                decision.coreIndex =
                                    effectiveCore;
                                decision.candidateCore =
                                    NoCore;
                                decision.confirmations = 0;
                                decision.changedTick =
                                    currentTick;

                                {
                                    std::lock_guard<std::mutex>
                                        lock(mutex_);

                                    threadLastChange_[
                                        threadInstance] =
                                            currentTick;
                                    threadDecisions_[
                                        threadInstance] =
                                            decision;

                                    auto it =
                                        originalThreads_.find(
                                            threadInstance);
                                    if (it !=
                                        originalThreads_.end())
                                    {
                                        it->second.idealChanged =
                                            !useCpuSets &&
                                            needsIdealChange;
                                    }
                                }
                            }
                            else
                            {
                                ++failed;
                            }
                        }
                        else
                        {
                            appliedThread = false;
                        }
                    }
                    else
                    {
                        decision.coreIndex = effectiveCore;
                        decision.candidateCore = NoCore;
                        decision.confirmations = 0;

                        std::lock_guard<std::mutex> lock(mutex_);
                        threadDecisions_[threadInstance] =
                            decision;
                    }

                    if (appliedThread && needsChange)
                    {
                        if (needsSetChange)
                        {
                            std::vector<ULONG> afterIds;
                            verified =
                                verified &&
                                QueryThreadCpuSets(
                                    thread, afterIds) &&
                                afterIds == desiredIds;
                        }

                        if (needsIdealChange)
                        {
                            PROCESSOR_NUMBER afterIdeal{};
                            verified =
                                verified &&
                                GetThreadIdealProcessorEx(
                                    thread, &afterIdeal) &&
                                afterIdeal.Number ==
                                    desiredIdeal;
                        }
                    }

                    CloseHandle(thread);

                    activeThreads.insert(threadInstance);
                    usedCores.insert(effectiveCore);

                    ThreadPlan threadPlan;
                    threadPlan.threadId = load.threadId;
                    threadPlan.cpuUsagePercent =
                        load.usagePercent;
                    threadPlan.currentGroup = currentGroup;
                    threadPlan.currentAffinity =
                        currentAffinity;
                    threadPlan.targetGroup = currentGroup;
                    threadPlan.targetAffinity =
                        currentAffinity;
                    threadPlan.currentIdealProcessor =
                        currentIdeal;
                    threadPlan.targetIdealProcessor =
                        useCpuSets
                            ? desiredIdeal
                            : (needsIdealChange
                                   ? desiredIdeal
                                   : currentIdeal);
                    threadPlan.currentPriority =
                        currentPriority;
                    threadPlan.targetPriority =
                        currentPriority;

                    if (effectiveCore <
                        topology.cores.size())
                    {
                        const TopologyCore& core =
                            topology.cores[effectiveCore];

                        for (std::uint32_t index :
                             core.logicalIndices)
                        {
                            if (index < output.cores.size())
                                ++output.cores[index]
                                      .threadCount;
                        }

                        if (!output.cores.empty() &&
                            representative <
                                output.cores.size())
                        {
                            threadPlan.l2CacheId =
                                output.cores[
                                    representative].l2CacheId;
                            threadPlan.l3CacheId =
                                output.cores[
                                    representative].l3CacheId;
                        }
                    }

                    if (needsChange && !appliedThread)
                    {
                        threadPlan.status =
                            PlanStatus::Planned;
                    }
                    else if (!verified)
                    {
                        threadPlan.status =
                            PlanStatus::Failed;
                    }
                    else
                    {
                        threadPlan.status =
                            needsChange
                                ? PlanStatus::Applied
                                : PlanStatus::Verified;
                    }

                    threadPlan.verified = verified;

                    if (useCpuSets)
                    {
                        threadPlan.reason =
                            "CPU Set blando: nucleo fisico "
                            "exclusivo, sin afinidad dura";
                    }
                    else if (needsIdealChange)
                    {
                        threadPlan.reason =
                            "nucleo ideal alternativo sin CPU Sets";
                    }
                    else
                    {
                        threadPlan.reason =
                            "prioridad y afinidad intactas";
                    }

                    plan.threads.push_back(threadPlan);
                }
            }

            plan.decision =
                "CPU Sets blandos solo en segundo plano";

            plan.threadCount =
                static_cast<DWORD>(plan.threads.size());

            output.processes.push_back(plan);
        }

        std::vector<std::pair<
            ThreadInstance, OriginalThreadState>> staleThreads;
        std::vector<std::pair<
            ProcessInstance, OriginalProcessState>> staleProcesses;

        {
            std::lock_guard<std::mutex> lock(mutex_);

            for (const auto& entry : originalThreads_)
            {
                if (activeThreads.find(entry.first) ==
                    activeThreads.end())
                {
                    staleThreads.push_back(entry);
                }
            }

            for (const auto& entry : originalProcesses_)
            {
                if (activeProcesses.find(entry.first) ==
                    activeProcesses.end())
                {
                    staleProcesses.push_back(entry);
                }
            }
        }

        for (const auto& entry : staleThreads)
        {
            if (RestoreThread(entry.first, entry.second))
                ++output.revertedChanges;

            std::lock_guard<std::mutex> lock(mutex_);
            originalThreads_.erase(entry.first);
            threadLastChange_.erase(entry.first);
            threadDecisions_.erase(entry.first);
        }

        for (const auto& entry : staleProcesses)
        {
            if (RestoreProcess(entry.first, entry.second))
                ++output.revertedChanges;

            std::lock_guard<std::mutex> lock(mutex_);
            originalProcesses_.erase(entry.first);
            pendingIslands_.erase(entry.first);
        }

        output.appliedChanges = applied;
        output.failedChanges = failed;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.enabled = true;
            state_.running = true;
            state_.status = output.status;
            state_.foregroundProcessId =
                output.foregroundProcessId;
            state_.targetProcessId = output.targetProcessId;
            state_.lastUpdateMs = output.lastUpdateMs;
            state_.appliedChanges = output.appliedChanges;
            state_.failedChanges = output.failedChanges;
            state_.revertedChanges +=
                output.revertedChanges;
            state_.processes = output.processes;
            state_.cores = output.cores;
        }

        Publish();
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
