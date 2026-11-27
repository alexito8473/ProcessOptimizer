#include "BackgroundContainment.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <set>
#include <utility>

namespace Optimization
{
    namespace
    {
        bool IsProtected(const std::wstring& name)
        {
            const std::wstring lowerName = [&]()
            {
                std::wstring value = name;
                std::transform(
                    value.begin(), value.end(), value.begin(),
                    [](wchar_t c) { return static_cast<wchar_t>(
                        std::tolower(static_cast<unsigned char>(c))); });
                return value;
            }();

            static const wchar_t* protectedNames[] = {
                L"system", L"system idle process", L"csrss.exe",
                L"winlogon.exe", L"services.exe", L"lsass.exe",
                L"svchost.exe", L" MsMpEng.exe", L"msmpeng.exe",
                L"nissrv.exe", L"securityhealthservice.exe",
                L"processoptimizer.service.exe", L"processoptimizer.ui.exe",
                L"processoptimizer.tray.exe",
                L"discord.exe", L"discordcanary.exe",
                L"discordptb.exe", L"discorddevelopment.exe",
                L"zoom.exe", L"teams.exe", L"ms-teams.exe",
                L"slack.exe", L"skype.exe", L"obs64.exe",
                L"obs32.exe", L"voicemeeter.exe",
                L"voicemeeterpro.exe"
            };

            for (const wchar_t* candidate : protectedNames)
            {
                if (lowerName.find(candidate) != std::wstring::npos)
                    return true;
            }
            return false;
        }

        std::string TargetText(
            DWORD processId, std::uint64_t startTime,
            const std::wstring& name)
        {
            char buffer[160]{};
            std::snprintf(
                buffer, sizeof(buffer), "%ls (PID %lu, inicio %llu)",
                name.c_str(), processId,
                static_cast<unsigned long long>(startTime));
            return buffer;
        }
    }

    BackgroundContainment::BackgroundContainment(
        Publisher publisher, AuditPublisher auditPublisher)
        : publisher_(std::move(publisher)),
          auditPublisher_(std::move(auditPublisher))
    {
    }

    BackgroundContainment::~BackgroundContainment()
    {
        RestoreAll();
    }

    void BackgroundContainment::SetEnabled(bool enabled)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (enabled_ == enabled)
                return;
            enabled_ = enabled;
            if (!enabled)
            {
                for (const auto& entry : backups_)
                    Restore(entry.first, entry.second);
                backups_.clear();
                contained_ = 0;
                quietTicks_ = 0;
                status_ = "Desactivado: todo restaurado";
            }
            else
            {
                status_ = "Activado; esperando evidencia de interferencia";
            }
        }
        Publish();
    }

    void BackgroundContainment::SetAbValidated(bool validated)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (abValidated_ == validated)
                return;

            abValidated_ = validated;
            if (!validated)
            {
                for (const auto& entry : backups_)
                    Restore(entry.first, entry.second);
                backups_.clear();
                contained_ = 0;
                quietTicks_ = 0;
                status_ = "A/B no validado: contencion retirada";
            }
            else
            {
                status_ =
                    "A/B validado; esperando evidencia de interferencia";
            }
        }
        Publish();
    }

    void BackgroundContainment::SetSafeMode(bool enabled)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (safeMode_ == enabled)
                return;

            safeMode_ = enabled;
            if (!enabled)
            {
                for (const auto& entry : backups_)
                    Restore(entry.first, entry.second);
                backups_.clear();
                contained_ = 0;
                quietTicks_ = 0;
                status_ = "Modo seguro desactivado";
            }
            else
            {
                status_ =
                    "Modo seguro (sin DXGI): contencion solo con evidencia";
            }
        }
        Publish();
    }

    void BackgroundContainment::OnProcesses(
        const ProcessSnapshot& snapshot,
        const StutterSnapshot& stutter,
        const TopologySnapshot& topology,
        DWORD foregroundPid,
        DWORD gamePid)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            // Sin A/B positivo o sin identidad de primer plano/juego no se
            // toca ningun proceso: la evidencia por si sola no autoriza.
            // Con que haya al menos una identidad valida (primer plano o
            // juego) se puede excluir al proceso activo. Antes se exigian
            // las dos y la contencion quedaba bloqueada en muchos equipos.
            const bool identityValid =
                foregroundPid != 0 || gamePid != 0;
            if (!enabled_ || !(abValidated_ || safeMode_) || !identityValid)
            {
                if (!backups_.empty())
                {
                    for (const auto& entry : backups_)
                        Restore(entry.first, entry.second);
                    backups_.clear();
                    contained_ = 0;
                }
                status_ = !enabled_
                    ? "Desactivado: todo restaurado"
                    : !(abValidated_ || safeMode_)
                        ? "A/B pendiente: sin intervencion"
                        : "Sin juego en primer plano: sin intervencion";
                PublishLocked();
                return;
            }

            if (stutter.topSuspect.empty())
            {
                ++quietTicks_;
                const std::uint64_t nowMs = GetTickCount64();
                const bool holdActive = (lastContainMs_ != 0 &&
                    (nowMs - lastContainMs_) < 8000);
                if (quietTicks_ >= 3 && !backups_.empty() && !holdActive)
                {
                    for (const auto& entry : backups_)
                        Restore(entry.first, entry.second);
                    backups_.clear();
                    contained_ = 0;
                    status_ = "Sin evidencia: contencion retirada";
                }
                else if (holdActive && !backups_.empty())
                {
                    status_ = "Sin evidencia reciente: manteniendo contencion";
                }
                else if (backups_.empty())
                {
                    status_ = "Activado; sin evidencia de interferencia";
                }
                PublishLocked();
                return;
            }

            quietTicks_ = 0;
            const std::uint64_t nowMs = GetTickCount64();
            std::set<InstanceKey> wanted;

            for (const auto& process : snapshot.processes)
            {
                if (process.processId == 0 ||
                    process.processId == foregroundPid ||
                    process.processId == gamePid ||
                    IsProtected(process.name) ||
                    process.cpuUsagePercent < 2.0)
                {
                    continue;
                }

                InstanceKey key{
                    process.processId, process.startTime100ns };
                wanted.insert(key);
            }

            // Como mucho dos procesos: contencion quirurgica, no global.
            std::vector<const ProcessInfo*> candidates;
            for (const auto& process : snapshot.processes)
            {
                InstanceKey key{
                    process.processId, process.startTime100ns };
                if (wanted.count(key) != 0 &&
                    process.processId != foregroundPid &&
                    process.processId != gamePid &&
                    !IsProtected(process.name))
                {
                    candidates.push_back(&process);
                }
            }
            std::sort(
                candidates.begin(), candidates.end(),
                [](const ProcessInfo* a, const ProcessInfo* b)
                {
                    return a->cpuUsagePercent > b->cpuUsagePercent;
                });
            if (candidates.size() > 2)
                candidates.resize(2);
            // En modo seguro (sin DXGI) seamos mas conservadores:
            // como maximo un proceso contenido.
            if (safeMode_ && candidates.size() > 1)
                candidates.resize(1);

            wanted.clear();
            for (const ProcessInfo* process : candidates)
                wanted.insert({
                    process->processId, process->startTime100ns });

            for (auto it = backups_.begin(); it != backups_.end();)
            {
                const bool holdExpired =
                    (nowMs - it->second.containedAtMs) >= 8000;
                if (wanted.count(it->first) == 0 && holdExpired)
                {
                    Restore(it->first, it->second);
                    it = backups_.erase(it);
                    lastRestoreMs_ = nowMs;
                }
                else
                {
                    ++it;
                }
            }

            const bool cooldownActive = (lastRestoreMs_ != 0 &&
                (nowMs - lastRestoreMs_) < 5000);
            std::uint32_t newlyContained = 0;
            for (const ProcessInfo* process : candidates)
            {
                InstanceKey key{
                    process->processId, process->startTime100ns };
                if (backups_.find(key) != backups_.end())
                    continue;
                if (cooldownActive)
                    continue;
                if (Contain(*process, topology))
                    ++newlyContained;
                    lastContainMs_ = nowMs;
            }

            contained_ = static_cast<std::uint32_t>(backups_.size());
            status_ = contained_ == 0
                ? "Evidencia detectada; sin candidatos seguros"
                : "Contencion reversible activa sobre " +
                    std::to_string(contained_) + " proceso(s)";
            (void)newlyContained;
        }
        Publish();
    }

    void BackgroundContainment::RestoreAll()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& entry : backups_)
                Restore(entry.first, entry.second);
            backups_.clear();
            contained_ = 0;
            status_ = "Todo restaurado";
        }
        Publish();
    }

    ContainmentSnapshot BackgroundContainment::Snapshot() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ContainmentSnapshot snapshot{};
        snapshot.enabled = enabled_;
        snapshot.evidence = !backups_.empty();
        snapshot.containedProcesses = contained_;
        snapshot.revertedProcesses = reverted_;
        snapshot.status = status_;
        return snapshot;
    }

    bool BackgroundContainment::Contain(
        const ProcessInfo& process,
        const TopologySnapshot& topology)
    {
        HANDLE handle = OpenProcess(
            PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_SET_INFORMATION,
            FALSE, process.processId);
        if (!handle)
            return false;

        InstanceKey key{ process.processId, process.startTime100ns };
        Backup backup;
        backup.name = process.name;
        bool applied = false;

        PROCESS_POWER_THROTTLING_STATE power{};
        power.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
        if (GetProcessInformation(
                handle, ProcessPowerThrottling,
                &power, sizeof(power)))
        {
            backup.hasPower = true;
            backup.power = power;

            PROCESS_POWER_THROTTLING_STATE throttled{};
            throttled.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
            throttled.ControlMask =
                PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
            throttled.StateMask =
                PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
            if (SetProcessInformation(
                    handle, ProcessPowerThrottling,
                    &throttled, sizeof(throttled)))
            {
                applied = true;
            }
        }
        // Si no se pudo leer/escribir power throttling (proceso protegido
        // o version antigua), se sigue con las demas mitigaciones; la
        // accion sigue siendo reversible para las que si se aplicaron.

        MEMORY_PRIORITY_INFORMATION memory{};
        memory.MemoryPriority = MEMORY_PRIORITY_NORMAL;
        if (GetProcessInformation(
                handle, ProcessMemoryPriority,
                &memory, sizeof(memory)))
        {
            backup.hasMemory = true;
            backup.memoryPriority = memory.MemoryPriority;
            MEMORY_PRIORITY_INFORMATION low{};
            low.MemoryPriority = MEMORY_PRIORITY_LOW;
            if (SetProcessInformation(
                    handle, ProcessMemoryPriority,
                    &low, sizeof(low)))
            {
                applied = true;
            }
        }

        const int currentPriority = GetPriorityClass(handle);
        if (currentPriority != 0)
        {
            backup.hasPriority = true;
            backup.priority = currentPriority;

            // ProBalance-style: bajar solo procesos de segundo plano
            // ruidosos. Nunca se sube prioridad ni se toca el juego.
            if (currentPriority > BELOW_NORMAL_PRIORITY_CLASS &&
                SetPriorityClass(
                    handle, BELOW_NORMAL_PRIORITY_CLASS))
            {
                applied = true;
            }
        }

        std::vector<DWORD> coldSets;
        if (BuildColdCpuSets(topology, coldSets))
        {
            DWORD count = 0;
            GetProcessDefaultCpuSets(handle, nullptr, 0, &count);
            if (count > 0)
            {
                std::vector<DWORD> previous(count);
                if (GetProcessDefaultCpuSets(
                        handle, previous.data(), count, &count))
                {
                    backup.hasCpuSets = true;
                    backup.cpuSets = previous;
                    if (SetProcessDefaultCpuSets(
                            handle, coldSets.data(),
                            static_cast<DWORD>(coldSets.size())))
                    {
                        applied = true;
                    }
                }
            }
        }

        CloseHandle(handle);

        if (!applied)
            return false;

        backup.containedAtMs = GetTickCount64();
        backups_[key] = backup;
        if (auditPublisher_)
        {
            AuditEntry entry{};
            entry.timeMs = GetTickCount64();
            entry.action = "Contencion de segundo plano";
            entry.target = TargetText(key.processId, key.startTime, backup.name);
            entry.detail =
                "EcoQoS + prioridad baja (ProBalance) + "
                "memoria baja + CPU Sets frios si aplica";
            auditPublisher_(entry);
        }
        return true;
    }

    void BackgroundContainment::Restore(
        const InstanceKey& key, const Backup& backup)
    {
        HANDLE handle = OpenProcess(
            PROCESS_SET_INFORMATION, FALSE, key.processId);
        if (handle)
        {
            if (backup.hasCpuSets)
            {
                SetProcessDefaultCpuSets(
                    handle, backup.cpuSets.data(),
                    static_cast<DWORD>(backup.cpuSets.size()));
            }

            MEMORY_PRIORITY_INFORMATION memory{};
            memory.MemoryPriority = backup.memoryPriority;
            if (backup.hasMemory)
            {
                SetProcessInformation(
                    handle, ProcessMemoryPriority,
                    &memory, sizeof(memory));
            }

            if (backup.hasPriority)
            {
                SetPriorityClass(handle, backup.priority);
            }

            if (backup.hasPower)
            {
                PROCESS_POWER_THROTTLING_STATE power = backup.power;
                SetProcessInformation(
                    handle, ProcessPowerThrottling,
                    &power, sizeof(power));
            }
            CloseHandle(handle);
        }

        ++reverted_;
        if (auditPublisher_)
        {
            AuditEntry entry{};
            entry.timeMs = GetTickCount64();
            entry.action = "Restauracion";
            entry.target = TargetText(key.processId, key.startTime, backup.name);
            entry.detail = handle
                ? "Valores originales restaurados"
                : "Proceso finalizado; no habia nada que restaurar";
            entry.reverted = true;
            auditPublisher_(entry);
        }
    }

    bool BackgroundContainment::BuildColdCpuSets(
        const TopologySnapshot& topology,
        std::vector<DWORD>& ids) const
    {
        ids.clear();
        if (!topology.available || topology.caches.empty())
            return false;

        std::uint32_t largestL3 = 0;
        std::uint16_t hotL3Id = 0;
        for (const auto& cache : topology.caches)
        {
            if (cache.level == 3 && cache.sizeKB > largestL3)
            {
                largestL3 = cache.sizeKB;
                hotL3Id = cache.id;
            }
        }
        if (largestL3 == 0)
            return false;

        DWORD size = 0;
        if (GetSystemCpuSetInformation(
                nullptr, 0, &size, nullptr, 0) != ERROR_INSUFFICIENT_BUFFER ||
            size == 0)
        {
            return false;
        }

        std::vector<std::uint8_t> buffer(size);
        if (!GetSystemCpuSetInformation(
                reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(
                    buffer.data()),
                static_cast<ULONG>(buffer.size()),
                &size, nullptr, 0))
        {
            return false;
        }

        std::size_t offset = 0;
        while (offset + sizeof(SYSTEM_CPU_SET_INFORMATION) <= buffer.size())
        {
            auto* entry = reinterpret_cast<SYSTEM_CPU_SET_INFORMATION*>(
                buffer.data() + offset);
            if (entry->Size == 0)
                break;

            if (entry->Type == CpuSetInformation)
            {
                const WORD group = entry->CpuSet.Group;
                const BYTE logical = entry->CpuSet.LogicalProcessorIndex;
                for (const auto& processor : topology.processors)
                {
                    if (processor.group == group &&
                        processor.logicalIndex == logical &&
                        processor.l3CacheId != hotL3Id)
                    {
                        ids.push_back(entry->CpuSet.Id);
                        break;
                    }
                }
            }
            offset += entry->Size;
        }

        return !ids.empty();
    }

    void BackgroundContainment::Publish()
    {
        if (!publisher_)
            return;
        publisher_(Snapshot());
    }

    void BackgroundContainment::PublishLocked()
    {
        if (!publisher_)
            return;

        ContainmentSnapshot snapshot{};
        snapshot.enabled = enabled_;
        snapshot.evidence = !backups_.empty();
        snapshot.containedProcesses = contained_;
        snapshot.revertedProcesses = reverted_;
        snapshot.status = status_;
        publisher_(snapshot);
    }
}
