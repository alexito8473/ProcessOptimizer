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
        constexpr std::uint64_t FocusFreezeTicks = 3;
        constexpr std::uint8_t IslandConfirmTicks = 2;

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
        std::uint64_t start = 0;
        if (processId != 0)
            GetProcessStart(processId, start);

        bool changed = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            changed =
                foregroundProcessId_ != processId ||
                foregroundStart100ns_ != start;

            foregroundProcessId_ = processId;
            foregroundStart100ns_ = start;

            if (changed)
                lastFocusChangeTick_ = tick_;
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
            thread, &creation, &exitTime, &kernel, &user) != FALSE;

        CloseHandle(thread);

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
        const ProcessInstance key{ processId, startTime100ns };

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

        const auto it = originalThreads_.find(key);
        if (it != originalThreads_.end())
        {
            return it->second;
        }

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

        const auto frequencyCeiling =
            [&](std::size_t index)
        {
            const TopologyProcessor& processor =
                topology.processors[index];

            if (!processor.frequencyAvailable ||
                processor.parked)
            {
                return 0.0;
            }

            return processor.maxMHz > 0.0
                       ? processor.maxMHz
                       : processor.mhzLimitMHz;
        };

        double bestCeiling = 0.0;
        for (std::size_t index : candidates)
            bestCeiling = (std::max)(
                bestCeiling, frequencyCeiling(index));

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
                    if (sibling == index ||
                        sibling >=
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

            // Un hermano SMT ocupado es peor que un nucleo fisico libre.
            const double smtPenalty =
                siblingCount > 0 && siblingUsage > 5.0
                    ? 12.0
                    : 0.0;

            const double ceiling = frequencyCeiling(index);
            const double frequencyPenalty =
                bestCeiling > 0.0 && ceiling > 0.0
                    ? (1.0 - ceiling / bestCeiling) * 20.0
                    : 0.0;

            const double parkedPenalty =
                processor.parked ? 1000.0 : 0.0;

            return processor.usagePercent * 2.0 +
                   siblingUsage * 0.7 +
                   smtPenalty +
                   frequencyPenalty +
                   parkedPenalty;
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

        // Una sola pista de nucleo ideal por hilo: menos rescheduling.
        const std::size_t limit = 1;

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
        const ProcessInstance& processInstance,
        const OriginalProcessState& original)
    {
        std::uint64_t currentStart = 0;
        if (!GetProcessStart(
                processInstance.processId, currentStart) ||
            currentStart !=
                processInstance.startTime100ns)
        {
            return false;
        }

        HANDLE process = OpenProcess(
            PROCESS_SET_INFORMATION,
            FALSE,
            processInstance.processId);

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
        std::uint64_t foregroundStart = 0;
        std::uint64_t currentTick = 0;
        std::uint64_t lastFocusChange = 0;
        TopologySnapshot topology;
        CpuSnapshot cpu;
        ProcessSnapshot processes;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            mode = state_.mode;
            foregroundPid = foregroundProcessId_;
            foregroundStart = foregroundStart100ns_;
            currentTick = tick_;
            lastFocusChange = lastFocusChangeTick_;
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

        // Tras un cambio de foco no conviene mover hilos mientras el juego
        // esta calentando cache: eso incrementa los picos de frame time.
        const bool focusFrozen =
            currentTick >= lastFocusChange &&
            currentTick - lastFocusChange <
                FocusFreezeTicks;

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

        for (const Candidate& candidate : candidates)
        {
            const std::uint64_t startTime =
                candidate.startTime100ns;

            const ProcessInstance instance{
                candidate.processId, startTime };

            activeProcesses.insert(instance);

            ProcessPlan plan;
            plan.processId = candidate.processId;
            plan.name = candidate.name;
            plan.foreground = candidate.foreground;
            plan.systemCritical = candidate.critical;
            plan.cpuUsagePercent = candidate.cpuUsagePercent;
            plan.gpuUsagePercent = candidate.gpuUsagePercent;

            const bool isTarget =
                candidate.processId == targetPid;

            std::uint16_t previousIsland = 0;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                const auto it =
                    processIslands_.find(instance);
                if (it != processIslands_.end())
                    previousIsland = it->second;
            }

            std::uint16_t desiredIsland = 0;
            if (isTarget)
            {
                desiredIsland = targetIsland;
            }
            else if (mode == OptimizerMode::Gaming &&
                     targetIsland != 0)
            {
                desiredIsland = ChooseIsland(
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
                desiredIsland = ChooseIsland(
                    previousIsland,
                    false,
                    false,
                    0,
                    0xFFFF,
                    0,
                    topology.processors);
            }

            if (desiredIsland == 0 && previousIsland != 0)
                desiredIsland = previousIsland;

            std::uint16_t island = desiredIsland;

            // Exigir dos muestras seguidas antes de cambiar de isla evita
            // oscilaciones de cache cuando dos islas tienen carga similar.
            if (previousIsland != 0 &&
                desiredIsland != previousIsland &&
                !isTarget)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                PendingIsland& pending = pendingIslands_[instance];

                if (pending.island == desiredIsland)
                    ++pending.confirmations;
                else
                {
                    pending.island = desiredIsland;
                    pending.confirmations = 1;
                }

                if (pending.confirmations < IslandConfirmTicks)
                    island = previousIsland;
                else
                    pending.confirmations = 0;
            }
            else
            {
                std::lock_guard<std::mutex> lock(mutex_);
                pendingIslands_.erase(instance);
            }

            if (focusFrozen && previousIsland != 0)
                island = previousIsland;

            {
                std::lock_guard<std::mutex> lock(mutex_);
                processIslands_[instance] = island;
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
                    // ABOVE_NORMAL evita invertir prioridades con hilos
                    // auxiliares del propio juego.
                    targetProcessPriority =
                        ABOVE_NORMAL_PRIORITY_CLASS;
                }
                // Segundo plano se deja en prioridad normal: bajarlo
                // puede ajustar auxiliares que el juego necesita.
            }
            else if (mode == OptimizerMode::Multitasking &&
                     candidate.foreground)
            {
                targetProcessPriority =
                    ABOVE_NORMAL_PRIORITY_CLASS;
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

                if (!focusFrozen &&
                    original.hasPriority &&
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
                        original.hasAffinity
                            ? original.affinity
                            : currentAffinity,
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
                    }
                    else if (mode ==
                                 OptimizerMode::Multitasking &&
                             candidate.foreground)
                    {
                        targetThreadPriority =
                            THREAD_PRIORITY_ABOVE_NORMAL;
                    }

                    activeThreads.insert(threadInstance);

                    // Nunca ampliar ni estrechar la afinidad original.
                    // Como mucho restaurarla si una version anterior
                    // del orquestador la habia limitado.
                    const bool affinityNeeded =
                        original.hasAffinity &&
                        currentAffinity != original.affinity;

                    const bool needsChange =
                        affinityNeeded ||
                        targetIdeal != currentIdeal ||
                        targetThreadPriority !=
                            currentPriority;

                    bool appliedThread = !needsChange;
                    bool verified = !needsChange;

                    if (needsChange)
                    {
                        bool allowed = !focusFrozen;
                        {
                            std::lock_guard<std::mutex>
                                lock(mutex_);

                            const auto lastIt =
                                threadLastChange_.find(
                                    threadInstance);

                            allowed =
                                allowed &&
                                (lastIt ==
                                     threadLastChange_.end() ||
                                 tick_ - lastIt->second >=
                                     CooldownTicks);
                        }

                        if (allowed &&
                            changesThisTick <
                                MaxChangesPerTick)
                        {
                            bool ok = true;

                            // Solo restaurar la afinidad original si
                            // una pasada anterior la habia estrechado.
                            if (affinityNeeded)
                            {
                                GROUP_AFFINITY desired{};
                                desired.Group =
                                    original.hasAffinity
                                        ? original.group
                                        : currentGroup;
                                desired.Mask =
                                    original.affinity;

                                ok =
                                    SetThreadGroupAffinity(
                                        thread, &desired,
                                        nullptr) != FALSE;
                            }

                            if (ok &&
                                (targetIdeal != currentIdeal ||
                                 affinityNeeded))
                            {
                                PROCESSOR_NUMBER desiredIdeal{};
                                desiredIdeal.Group =
                                    currentGroup;
                                desiredIdeal.Number =
                                    static_cast<BYTE>(
                                        std::min<DWORD>(
                                            targetIdeal, 63));

                                ok =
                                    SetThreadIdealProcessorEx(
                                        thread,
                                        &desiredIdeal,
                                        nullptr) != FALSE;
                            }

                            if (ok &&
                                targetThreadPriority !=
                                    currentPriority)
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
                                        threadInstance] =
                                            tick_;
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
                        if (affinityNeeded)
                        {
                            GROUP_AFFINITY afterAffinity{};

                            if (GetThreadGroupAffinity(
                                    thread, &afterAffinity))
                            {
                                verified =
                                    verified &&
                                    afterAffinity.Mask ==
                                        original.affinity;
                            }
                            else
                            {
                                verified = false;
                            }
                        }

                        if (targetIdeal != currentIdeal ||
                            affinityNeeded)
                        {
                            PROCESSOR_NUMBER afterIdeal{};

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

                        if (targetThreadPriority !=
                            currentPriority)
                        {
                            verified =
                                verified &&
                                GetThreadPriority(thread) ==
                                    targetThreadPriority;
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
                        affinityNeeded
                            ? original.affinity
                            : currentAffinity;
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

                    if (focusFrozen)
                    {
                        threadPlan.reason =
                            "congelado tras cambio de foco";
                    }
                    else if (isTarget &&
                        topology.kind ==
                            TopologyKind::Hybrid)
                    {
                        threadPlan.reason =
                            "foco: nucleo ideal, sin fijar afinidad";
                    }
                    else if (isTarget)
                    {
                        threadPlan.reason =
                            "foco: ideal por carga/frecuencia/SMT";
                    }
                    else if (island != 0)
                    {
                        threadPlan.reason =
                            "misma L2/L3, ideal sin fijar afinidad";
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
