#pragma once

#include "OptimizationTypes.h"

#include "../Monitoring/ProcessMonitor/ProcessInfo.h"

#include <Windows.h>

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace Optimization
{
    // Contencion de segundo plano estilo ProBalance: solo con evidencia de
    // interferencia, nunca sobre el juego/primer plano, y siempre reversible.
    class BackgroundContainment
    {
    public:
        using Publisher =
            std::function<void(const ContainmentSnapshot&)>;
        using AuditPublisher =
            std::function<void(const AuditEntry&)>;

        BackgroundContainment(
            Publisher publisher,
            AuditPublisher auditPublisher);
        ~BackgroundContainment();

        BackgroundContainment(const BackgroundContainment&) = delete;
        BackgroundContainment& operator=(const BackgroundContainment&) = delete;

        void SetEnabled(bool enabled);
        // Solo despues de un A/B positivo conservado y con el perfil activo.
        void SetAbValidated(bool validated);
        void OnProcesses(
            const ProcessSnapshot& snapshot,
            const StutterSnapshot& stutter,
            const TopologySnapshot& topology,
            DWORD foregroundPid,
            DWORD gamePid);
        void RestoreAll();
        ContainmentSnapshot Snapshot() const;

    private:
        struct InstanceKey
        {
            DWORD processId = 0;
            std::uint64_t startTime = 0;

            bool operator<(const InstanceKey& other) const
            {
                if (processId != other.processId)
                    return processId < other.processId;
                return startTime < other.startTime;
            }
        };

        struct Backup
        {
            std::wstring name;
            PROCESS_POWER_THROTTLING_STATE power{};
            bool hasPower = false;
            ULONG memoryPriority = MEMORY_PRIORITY_NORMAL;
            bool hasMemory = false;
            int priority = 0;
            bool hasPriority = false;
            std::vector<DWORD> cpuSets;
            bool hasCpuSets = false;
        };

        bool Contain(const ProcessInfo& process,
                     const TopologySnapshot& topology);
        void Restore(const InstanceKey& key, const Backup& backup);
        bool BuildColdCpuSets(
            const TopologySnapshot& topology,
            std::vector<DWORD>& ids) const;
        void Publish();
        void PublishLocked();

        Publisher publisher_;
        AuditPublisher auditPublisher_;
        mutable std::mutex mutex_;

        bool enabled_ = false;
        bool abValidated_ = false;
        int quietTicks_ = 0;
        std::uint32_t contained_ = 0;
        std::uint32_t reverted_ = 0;
        std::string status_ = "Desactivado";
        std::map<InstanceKey, Backup> backups_;
    };
}
