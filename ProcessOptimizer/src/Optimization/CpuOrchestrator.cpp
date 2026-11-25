#define NOMINMAX
#include "CpuOrchestrator.h"

#include <algorithm>
#include <chrono>
#include <cwchar>
#include <map>
#include <unordered_set>
#include <utility>

namespace Optimization
{
    namespace
    {
        constexpr std::uint64_t CooldownTicks = 3;
        constexpr int MaxChangesPerTick = 4;
        constexpr std::size_t MaxProcesses = 40;
        constexpr std::size_t MaxThreadsPerProcess = 8;

        std::uint64_t ThreadKey(DWORD processId, DWORD threadId)
        {
            return
                (static_cast<std::uint64_t>(processId) << 32) |
                static_cast<std::uint64_t>(threadId);
        }

        std::uint64_t ProcessInstanceKey(
            DWORD processId,
            std::uint64_t startTime100ns)
        {
            return
                (static_cast<std::uint64_t>(processId) << 32) ^
                startTime100ns;
        }

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
            {
                state_.status = "Optimizacion desactivada";
            }
            else
            {
                state_.status = mode == OptimizerMode::Gaming
                    ? "Perfil Gaming"
                    : "Perfil Multitarea";
            }
        }

        if (disable)
            RestoreAll();

        Publish();
    }

    void CpuOrchestrator::SetForegroundProcess(DWORD processId)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            foregroundProcessId_ = processId;
        }
        Publish();
    }

    OptimizerState CpuOrchestrator::State() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_;
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
            process, &creation, &exitTime, &kernel, &user) != FALSE;

        CloseHandle(process);

        if (!ok)
            return false;

        startTime100ns = FileTime100ns(creation);
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

        // El servicio corre en la sesion 0: no debe orquestar procesos
        // del sistema ni de otras sesiones sin interaccion del usuario.
        return sessionId != 0;
    }

    CpuOrchestrator::OriginalProcessState
    CpuOrchestrator::EnsureProcessOriginal(
        DWORD processId,
        std::uint64_t startTime100ns,
        HANDLE process)
    {
        const auto key =
            static_cast<std::uint32_t>(processId);

        const auto it = originalProcesses_.find(key);
        if (it != originalProcesses_.end() &&
            it->second.startTime100ns == startTime100ns)
        {
            return it->second;
        }

        OriginalProcessState original;
        original.startTime100ns = startTime100ns;

        const DWORD priority = GetPriorityClass(process);
        if (priority != 0)
        {
            original.priority = static_cast<int>(priority);
            original.hasPriority = true;
        }

        originalProcesses_[key] = original;
        return original;
    }

    CpuOrchestrator::OriginalThreadState
    CpuOrchestrator::EnsureThreadOriginal(
        DWORD processId,
        DWORD threadId,
        std::uint64_t startTime100ns,
        HANDLE thread)
    {
        const std::uint64_t key =
            ThreadKey(processId, threadId);

        const auto it = originalThreads_.find(key);
        if (it != originalThreads_.end() &&
            it->second.startTime100ns == startTime100ns)
        {
            return it->second;
        }

        OriginalThreadState original;
        original.startTime100ns = startTime100ns;

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

        originalThreads_[key] = original;
        return original;
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

            // Histéresis: conservar la isla evita migraciones de cache
            // si la diferencia de carga es pequena.
            if (previousAverage <= bestAverage + 12.0)
                return previousIsland;
        }

        return best;
    }

    bool CpuOrchestrator::BuildTarget(
        const TopologySnapshot& topology,
        std::uint16_t island,
        WORD group,
        std::uint64_t originalAffinity,
        bool targetProcess,
        std::vector<std::size_t>& chosenProcessors,
        std::uint64_t& targetAffinity,
        DWORD& idealProcessor) const
    {
        bool hasPerformance = false;
        if (targetProcess &&
            topology.kind == TopologyKind::Hybrid)
        {
            for (const TopologyProcessor& processor :
                 topology.processors)
            {
                if (processor.coreClass ==
                    CoreClass::Performance)
                {
                    hasPerformance = true;
                    break;
                }
            }
        }

        auto matches = [&](std::size_t index, bool useIsland,
                           bool useClass)
        {
            const TopologyProcessor& processor =
                topology.processors[index];

            if (processor.group != group)
                return false;

            if (originalAffinity != 0 &&
                !HasBit(
                    originalAffinity,
                    processor.logicalIndex))
            {
                return false;
            }

            if (useIsland &&
                island != 0 &&
                IslandOf(processor) != island)
            {
                return false;
            }

            if (useClass && hasPerformance &&
                processor.coreClass !=
                    CoreClass::Performance)
            {
                return false;
            }

            return true;
        };

        std::vector<std::size_t> candidates;

        for (std::size_t i = 0;
             i < topology.processors.size();
             ++i)
        {
            if (matches(i, true, true))
                candidates.push_back(i);
        }

        if (candidates.empty())
        {
            for (std::size_t i = 0;
                 i < topology.processors.size();
                 ++i)
            {
                if (matches(i, true, false))
                    candidates.push_back(i);
            }
        }

        if (candidates.empty())
        {
            for (std::size_t i = 0;
                 i < topology.processors.size();
                 ++i)
            {
                if (matches(i, false, false))
                    candidates.push_back(i);
            }
        }

        if (candidates.empty())
            return false;

        const auto score =
            [&](std::size_t index)
        {
            const TopologyProcessor& processor =
                topology.processors[index];

            double siblingUsage = 0.0;
            std::size_t siblingCount = 0;

            for (const TopologyCore& core : topology.cores)
            {
                if (core.coreIndex !=
                    processor.physicalCoreIndex)
                {
                    continue;
                }

                for (std::uint32_t sibling :
                     core.logicalIndices)
                {
                    if (sibling >=
                        topology.processors.size())
                    {
                        continue;
                    }

                    siblingUsage += (std::max)(
                        0.0,
                        topology.processors[sibling]
                            .usagePercent);
                    ++siblingCount;
                }
            }

            const double smtPenalty =
                siblingCount > 1 ? 4.0 : 0.0;

            return processor.usagePercent * 2.0 +
                   siblingUsage * 0.7 +
                   smtPenalty;
        };

        std::sort(
            candidates.begin(), candidates.end(),
            [&](std::size_t left, std::size_t right)
            {
                const double leftScore = score(left);
                const double rightScore = score(right);

                if (leftScore == rightScore)
                {
                    return topology.processors[left]
                               .logicalIndex <
                           topology.processors[right]
                               .logicalIndex;
                }

                return leftScore < rightScore;
            });

        const std::size_t limit =
            std::min<std::size_t>(4, candidates.size());

        targetAffinity = 0;
        chosenProcessors.clear();

        for (std::size_t i = 0; i < limit; ++i)
        {
            const std::size_t index = candidates[i];
            const TopologyProcessor& processor =
                topology.processors[index];

            targetAffinity |=
                std::uint64_t{ 1 } <<
                processor.logicalIndex;
            chosenProcessors.push_back(index);
        }

        const std::size_t best = candidates.front();
        idealProcessor =
            topology.processors[best].logicalIndex;
        return true;
    }

    bool CpuOrchestrator::RestoreThread(
        std::uint64_t threadKey,
        const OriginalThreadState& original)
    {
        const DWORD processId =
            static_cast<DWORD>(threadKey >> 32);
        const DWORD threadId =
            static_cast<DWORD>(threadKey & 0xFFFFFFFFULL);

        std::uint64_t currentStart = 0;
        if (!GetProcessStart(processId, currentStart) ||
            currentStart != original.startTime100ns)
        {
            return false;
        }

        HANDLE thread = OpenThread(
            THREAD_QUERY_LIMITED_INFORMATION |
                THREAD_SET_INFORMATION,
            FALSE,
            threadId);

        if (thread == nullptr)
            return false;

        bool ok = true;

        if (original.hasAffinity)
        {
            GROUP_AFFINITY affinity{};
            affinity.Group = original.group;
            affinity.Mask = original.affinity;

            if (!SetThreadGroupAffinity(
                    thread, &affinity, nullptr))
            {
                ok = false;
            }
        }

        if (ok && original.hasIdeal)
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

        if (ok && original.hasPriority)
        {
            if (!SetThreadPriority(
                    thread, original.priority))
            {
                ok = false;
            }
        }

        CloseHandle(thread);
        return ok;
    }

    bool CpuOrchestrator::RestoreProcess(
        DWORD processId,
        const OriginalProcessState& original)
    {
        std::uint64_t currentStart = 0;
        if (!GetProcessStart(processId, currentStart) ||
            currentStart != original.startTime100ns)
        {
            return false;
        }

        HANDLE process = OpenProcess(
            PROCESS_SET_INFORMATION,
            FALSE,
            processId);

        if (process == nullptr)
            return false;

        bool ok = true;
        if (original.hasPriority)
        {
            ok = SetPriorityClass(
                     process,
                     static_cast<DWORD>(
                         original.priority)) != FALSE;
        }

        CloseHandle(process);
        return ok;
    }

    void CpuOrchestrator::RestoreAll()
    {
        std::unordered_map<
            std::uint64_t, OriginalThreadState> threads;
        std::unordered_map<
            std::uint32_t, OriginalProcessState> processes;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            threads = originalThreads_;
            processes = originalProcesses_;
            originalThreads_.clear();
            originalProcesses_.clear();
            threadLastChange_.clear();
            processIslands_.clear();

            state_.revertedChanges +=
                static_cast<std::uint32_t>(
                    threads.size() + processes.size());
        }

        for (const auto& entry : threads)
        {
            RestoreThread(entry.first, entry.second);
        }

        for (const auto& entry : processes)
        {
            RestoreProcess(entry.first, entry.second);
        }
    }

    void CpuOrchestrator::BuildPlanAndApply()
    {
        OptimizerMode mode = OptimizerMode::Off;
        DWORD foregroundPid = 0;
        TopologySnapshot topology;
        CpuSnapshot cpu;
        ProcessSnapshot processes;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            mode = state_.mode;
            foregroundPid = foregroundProcessId_;
            topology = topology_;
            cpu = cpuSnapshot_;
            processes = processSnapshot_;
        }

        if (!topology.available)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.status =
                "Sin topologia: solo equilibrio de carga";
            state_.running = false;
            return;
        }

        CpuTopology::RefreshUsage(topology, cpu);

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

            const bool foreground =
                process.processId == foregroundPid;

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
            candidate.name = process.name;
            candidate.cpuUsagePercent =
                process.cpuUsagePercent;
            candidate.gpuUsagePercent =
                process.gpuUsagePercent;
            candidate.foreground = foreground;
            candidate.critical = false;

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

        std::vector<ThreadLoad> loads =
            threadMonitor_.Sample(
                candidatePids, topology.processors.size());

        std::unordered_map<DWORD, std::vector<ThreadLoad>>
            threadsByProcess;

        for (ThreadLoad& load : loads)
        {
            threadsByProcess[load.processId].push_back(load);
        }

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

        output.status =
            targetPid != 0
                ? "Gaming: proceso con foco en nucleos rapidos"
                : (mode == OptimizerMode::Gaming
                       ? "Gaming: reparto normal"
                       : "Multitarea: reparto por cache");

        for (const TopologyProcessor& processor :
             topology.processors)
        {
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
            assignment.reason =
                processor.l3CacheId != 0
                    ? "L3 compartida"
                    : (processor.l2CacheId != 0
                           ? "L2 compartida"
                           : "sin datos de cache");
            output.cores.push_back(assignment);
        }

        int changesThisTick = 0;
        std::uint32_t applied = 0;
        std::uint32_t failed = 0;

        std::unordered_set<std::uint64_t> activeThreads;
        std::unordered_set<DWORD> activeProcesses;

        for (const Candidate& candidate : candidates)
        {
            std::uint64_t startTime = 0;
            if (!GetProcessStart(
                    candidate.processId, startTime))
            {
                continue;
            }

            activeProcesses.insert(candidate.processId);

            ProcessPlan plan;
            plan.processId = candidate.processId;
            plan.name = candidate.name;
            plan.foreground = candidate.foreground;
            plan.systemCritical = candidate.critical;
            plan.cpuUsagePercent = candidate.cpuUsagePercent;
            plan.gpuUsagePercent = candidate.gpuUsagePercent;

            const bool isTarget =
                candidate.processId == targetPid;

            const std::uint64_t instanceKey =
                ProcessInstanceKey(
                    candidate.processId, startTime);

            std::uint16_t previousIsland = 0;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                const auto it =
                    processIslands_.find(instanceKey);
                if (it != processIslands_.end())
                    previousIsland = it->second;
            }

            std::uint16_t island = 0;
            if (isTarget)
            {
                island = targetIsland;
            }
            else if (mode == OptimizerMode::Gaming &&
                     targetIsland != 0)
            {
                island = ChooseIsland(
                    previousIsland,
                    false,
                    true,
                    targetIsland,
                    0xFFFF,
                    0,
                    topology.processors);
            }
            else
            {
                island = ChooseIsland(
                    previousIsland,
                    false,
                    false,
                    0,
                    0xFFFF,
                    0,
                    topology.processors);
            }

            if (island == 0 && previousIsland != 0)
                island = previousIsland;

            {
                std::lock_guard<std::mutex> lock(mutex_);
                processIslands_[instanceKey] = island;
            }

            HANDLE process = OpenProcess(
                PROCESS_QUERY_LIMITED_INFORMATION |
                    PROCESS_SET_INFORMATION,
                FALSE,
                candidate.processId);

            int processPriority = NORMAL_PRIORITY_CLASS;
            int targetProcessPriority = NORMAL_PRIORITY_CLASS;

            if (mode == OptimizerMode::Gaming)
            {
                if (isTarget)
                {
                    targetProcessPriority =
                        HIGH_PRIORITY_CLASS;
                }
                else if (candidate.cpuUsagePercent > 5.0)
                {
                    targetProcessPriority =
                        BELOW_NORMAL_PRIORITY_CLASS;
                }
            }
            else if (candidate.foreground)
            {
                targetProcessPriority =
                    ABOVE_NORMAL_PRIORITY_CLASS;
            }
            else if (candidate.cpuUsagePercent > 25.0)
            {
                targetProcessPriority =
                    BELOW_NORMAL_PRIORITY_CLASS;
            }

            if (process != nullptr)
            {
                const DWORD current =
                    GetPriorityClass(process);
                if (current != 0)
                    processPriority =
                        static_cast<int>(current);

                const OriginalProcessState original =
                    EnsureProcessOriginal(
                        candidate.processId,
                        startTime,
                        process);

                if (original.hasPriority &&
                    current !=
                        static_cast<DWORD>(
                            targetProcessPriority))
                {
                    if (SetPriorityClass(
                            process,
                            static_cast<DWORD>(
                                targetProcessPriority)))
                    {
                        ++applied;
                    }
                    else
                    {
                        ++failed;
                    }
                }
            }

            plan.currentPriority = processPriority;
            plan.targetPriority = targetProcessPriority;

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

                    const OriginalThreadState original =
                        EnsureThreadOriginal(
                            candidate.processId,
                            load.threadId,
                            startTime,
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
                            thread,
                            &currentGroupAffinity))
                    {
                        currentGroup =
                            currentGroupAffinity.Group;
                        currentAffinity =
                            currentGroupAffinity.Mask;
                    }

                    PROCESSOR_NUMBER currentIdealNumber{};
                    if (GetThreadIdealProcessorEx(
                            thread,
                            &currentIdealNumber))
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

                    std::vector<std::size_t> chosen;
                    std::uint64_t targetAffinity = 0;
                    DWORD targetIdeal = 0;

                    const bool hasTarget = BuildTarget(
                        topology,
                        island,
                        currentGroup,
                        currentAffinity,
                        isTarget,
                        chosen,
                        targetAffinity,
                        targetIdeal);

                    if (!hasTarget)
                    {
                        targetAffinity = currentAffinity;
                        targetIdeal = currentIdeal;
                    }

                    int targetThreadPriority =
                        THREAD_PRIORITY_NORMAL;

                    if (mode == OptimizerMode::Gaming)
                    {
                        if (isTarget)
                        {
                            targetThreadPriority =
                                THREAD_PRIORITY_ABOVE_NORMAL;
                        }
                        else if (
                            candidate.cpuUsagePercent > 5.0)
                        {
                            targetThreadPriority =
                                THREAD_PRIORITY_BELOW_NORMAL;
                        }
                    }
                    else if (candidate.foreground)
                    {
                        targetThreadPriority =
                            THREAD_PRIORITY_ABOVE_NORMAL;
                    }

                    const std::uint64_t threadKey =
                        ThreadKey(
                            candidate.processId,
                            load.threadId);

                    activeThreads.insert(threadKey);

                    const bool needsChange =
                        targetAffinity != currentAffinity ||
                        targetIdeal != currentIdeal ||
                        targetThreadPriority !=
                            currentPriority;

                    bool appliedThread = !needsChange;
                    bool verified = !needsChange;

                    if (needsChange)
                    {
                        bool allowed = false;
                        {
                            std::lock_guard<std::mutex>
                                lock(mutex_);

                            const auto lastIt =
                                threadLastChange_.find(
                                    threadKey);

                            allowed =
                                lastIt ==
                                    threadLastChange_.end() ||
                                tick_ - lastIt->second >=
                                    CooldownTicks;
                        }

                        if (allowed &&
                            changesThisTick <
                                MaxChangesPerTick)
                        {
                            GROUP_AFFINITY desired{};
                            desired.Group = currentGroup;
                            desired.Mask = targetAffinity;

                            PROCESSOR_NUMBER desiredIdeal{};
                            desiredIdeal.Group = currentGroup;
                            desiredIdeal.Number =
                                static_cast<BYTE>(
                                    std::min<DWORD>(
                                        targetIdeal, 63));

                            bool ok =
                                SetThreadGroupAffinity(
                                    thread, &desired,
                                    nullptr) != FALSE;

                            if (ok)
                            {
                                ok =
                                    SetThreadIdealProcessorEx(
                                        thread,
                                        &desiredIdeal,
                                        nullptr) != FALSE;
                            }

                            if (ok)
                            {
                                ok =
                                    SetThreadPriority(
                                        thread,
                                        targetThreadPriority) !=
                                    FALSE;
                            }

                            appliedThread = ok;

                            if (ok)
                            {
                                ++changesThisTick;
                                ++applied;

                                verified = true;
                                {
                                    std::lock_guard<
                                        std::mutex>
                                        lock(mutex_);
                                    threadLastChange_[
                                        threadKey] = tick_;
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

                    if (appliedThread && needsChange)
                    {
                        GROUP_AFFINITY afterAffinity{};
                        PROCESSOR_NUMBER afterIdeal{};

                        if (GetThreadGroupAffinity(
                                thread, &afterAffinity))
                        {
                            verified =
                                verified &&
                                afterAffinity.Mask ==
                                    targetAffinity;
                        }
                        else
                        {
                            verified = false;
                        }

                        if (GetThreadIdealProcessorEx(
                                thread, &afterIdeal))
                        {
                            verified =
                                verified &&
                                afterIdeal.Number ==
                                    targetIdeal;
                        }
                        else
                        {
                            verified = false;
                        }
                    }

                    CloseHandle(thread);

                    ThreadPlan threadPlan;
                    threadPlan.threadId = load.threadId;
                    threadPlan.cpuUsagePercent =
                        load.usagePercent;
                    threadPlan.currentGroup = currentGroup;
                    threadPlan.currentAffinity =
                        currentAffinity;
                    threadPlan.targetGroup = currentGroup;
                    threadPlan.targetAffinity =
                        targetAffinity;
                    threadPlan.currentIdealProcessor =
                        currentIdeal;
                    threadPlan.targetIdealProcessor =
                        targetIdeal;
                    threadPlan.currentPriority =
                        currentPriority;
                    threadPlan.targetPriority =
                        targetThreadPriority;

                    {
                        // `chosen` contiene indices reales del vector de
                        // topologia; usar targetIdeal como indice solo
                        // seria valido en el grupo 0.
                        if (!chosen.empty() &&
                            chosen.front() <
                                topology.processors.size())
                        {
                            const TopologyProcessor& processor =
                                topology.processors[
                                    chosen.front()];

                            threadPlan.l2CacheId =
                                processor.l2CacheId;
                            threadPlan.l3CacheId =
                                processor.l3CacheId;
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

                    if (isTarget &&
                        topology.kind ==
                            TopologyKind::Hybrid)
                    {
                        threadPlan.reason =
                            "proceso con foco: P-cores, misma L3";
                    }
                    else if (isTarget)
                    {
                        threadPlan.reason =
                            "proceso con foco: isla menos cargada";
                    }
                    else if (island != 0)
                    {
                        threadPlan.reason =
                            "misma cache L2/L3, evita SMT cargado";
                    }
                    else
                    {
                        threadPlan.reason =
                            "equilibrio de carga sin datos de cache";
                    }

                    plan.threads.push_back(threadPlan);

                    for (std::size_t index : chosen)
                    {
                        if (index >= output.cores.size())
                            continue;

                        CoreAssignment& assignment =
                            output.cores[index];
                        ++assignment.threadCount;

                        if (assignment.reason.empty() ||
                            isTarget)
                        {
                            assignment.reason =
                                threadPlan.reason;
                        }
                    }
                }
            }

            if (isTarget)
            {
                plan.decision =
                    "aislamiento del proceso con foco";
            }
            else if (island != 0)
            {
                plan.decision =
                    "reparto por isla de cache";
            }
            else
            {
                plan.decision = "reparto por carga";
            }

            plan.threadCount =
                static_cast<DWORD>(plan.threads.size());

            output.processes.push_back(plan);

            if (process != nullptr)
                CloseHandle(process);
        }

        // Restaurar hilos y prioridades que ya no son candidatos.
        std::vector<std::pair<
            std::uint64_t, OriginalThreadState>> staleThreads;
        std::vector<std::pair<
            DWORD, OriginalProcessState>> staleProcesses;

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
        }

        for (const auto& entry : staleProcesses)
        {
            if (RestoreProcess(entry.first, entry.second))
                ++output.revertedChanges;

            std::lock_guard<std::mutex> lock(mutex_);
            originalProcesses_.erase(entry.first);
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
