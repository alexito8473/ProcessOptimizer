#include "CpuTopology.h"

#include <PowrProf.h>

#include <algorithm>
#include <functional>
#include <unordered_map>
#include <utility>
#include <vector>

#pragma comment(lib, "PowrProf.lib")

namespace Optimization
{
    namespace
    {
        constexpr BYTE InvalidEfficiencyClass = 0xFF;

        // PROCESSOR_POWER_INFORMATION no viene en el SDK de usuario
        // instalado, pero la ABI de CallNtPowerInformation es estable:
        // seis ULONG en este orden (24 bytes).
        struct ProcessorPowerInformation
        {
            ULONG Number = 0;
            ULONG MaxMhz = 0;
            ULONG CurrentMhz = 0;
            ULONG MhzLimit = 0;
            ULONG MaxIdleState = 0;
            ULONG CurrentIdleState = 0;
        };

        std::uint32_t ProcessorKey(WORD group, BYTE logicalIndex)
        {
            return
                (static_cast<std::uint32_t>(group) << 8) |
                static_cast<std::uint32_t>(logicalIndex);
        }

        std::vector<BYTE> QueryRelations()
        {
            DWORD size = 0;

            GetLogicalProcessorInformationEx(
                RelationAll, nullptr, &size);

            if (GetLastError() != ERROR_INSUFFICIENT_BUFFER ||
                size == 0)
            {
                return {};
            }

            std::vector<BYTE> buffer(size);

            if (!GetLogicalProcessorInformationEx(
                    RelationAll,
                    reinterpret_cast<
                        PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(
                        buffer.data()),
                    &size))
            {
                return {};
            }

            buffer.resize(size);
            return buffer;
        }

        void ForEachRelation(
            std::vector<BYTE>& buffer,
            LOGICAL_PROCESSOR_RELATIONSHIP relationship,
            const std::function<void(
                PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)>& callback)
        {
            if (buffer.empty())
                return;

            BYTE* current = buffer.data();
            BYTE* end = buffer.data() + buffer.size();

            while (current < end)
            {
                auto* info =
                    reinterpret_cast<
                        PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(
                        current);

                if (info->Size == 0 ||
                    current + info->Size > end)
                {
                    break;
                }

                if (info->Relationship == relationship)
                    callback(info);

                current += info->Size;
            }
        }

        void ForEachLogicalProcessor(
            WORD groupCount,
            const GROUP_AFFINITY* groupMasks,
            const std::function<void(WORD, BYTE)>& callback)
        {
            for (WORD groupIndex = 0;
                 groupIndex < groupCount;
                 ++groupIndex)
            {
                const GROUP_AFFINITY& affinity =
                    groupMasks[groupIndex];

                for (BYTE logicalIndex = 0;
                     logicalIndex < 64;
                     ++logicalIndex)
                {
                    const std::uint64_t bit =
                        std::uint64_t{ 1 } << logicalIndex;

                    if ((affinity.Mask & bit) == 0)
                        continue;

                    callback(affinity.Group, logicalIndex);
                }
            }
        }

        // CallNtPowerInformation devuelve una entrada por procesador logico
        // del sistema. El "Number" de PROCESSOR_POWER_INFORMATION no es un
        // indice del vector de topologia ni un PROCESSOR_NUMBER de grupo:
        // se usa el ordinal global calculado a partir de grupo + indice.
        void ApplyProcessorPower(TopologySnapshot& topology)
        {
            if (topology.processors.empty())
                return;

            const DWORD count =
                GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);

            if (count == 0 || count < topology.processors.size())
                return;

            std::vector<DWORD> groupCounts(
                static_cast<std::size_t>(topology.groupCount) + 1, 0);

            for (const TopologyProcessor& processor :
                 topology.processors)
            {
                if (processor.group >= groupCounts.size())
                    continue;

                ++groupCounts[processor.group];
            }

            std::vector<DWORD> groupBase(groupCounts.size(), 0);
            for (std::size_t i = 1; i < groupCounts.size(); ++i)
                groupBase[i] = groupBase[i - 1] + groupCounts[i - 1];

            std::vector<DWORD> ordinals(
                topology.processors.size(), 0);

            for (std::size_t i = 0;
                 i < topology.processors.size();
                 ++i)
            {
                const WORD group = topology.processors[i].group;
                if (group >= groupBase.size())
                    continue;

                ordinals[i] =
                    groupBase[group] +
                    topology.processors[i].logicalIndex;
            }

            std::vector<ProcessorPowerInformation> power(count);
            const NTSTATUS status = CallNtPowerInformation(
                ProcessorInformation,
                nullptr,
                0,
                power.data(),
                static_cast<ULONG>(
                    power.size() * sizeof(power[0])));

            if (status != 0)
                return;

            for (std::size_t i = 0;
                 i < topology.processors.size();
                 ++i)
            {
                if (ordinals[i] >= count)
                    continue;

                const ProcessorPowerInformation& info =
                    power[ordinals[i]];

                TopologyProcessor& processor =
                    topology.processors[i];

                processor.frequencyAvailable = true;
                processor.maxMHz =
                    static_cast<double>(info.MaxMhz);
                processor.currentMHz =
                    static_cast<double>(info.CurrentMhz);
                processor.mhzLimitMHz =
                    static_cast<double>(info.MhzLimit);
                processor.currentIdleState =
                    info.CurrentIdleState;

                // No marcar como aparcado solo por un idle state alto:
                // algunos equipos siguen informando la frecuencia maxima.
                processor.parked =
                    info.MaxMhz == 0 ||
                    (info.CurrentMhz == 0 &&
                     info.MhzLimit == 0);
            }
        }

        bool SameCacheGroups(
            const TopologyCache& cache,
            BYTE level,
            WORD groupCount,
            const GROUP_AFFINITY* groupMasks)
        {
            if (cache.level != level ||
                cache.groups.size() != groupCount)
            {
                return false;
            }

            for (WORD i = 0; i < groupCount; ++i)
            {
                if (cache.groups[i].group !=
                        groupMasks[i].Group ||
                    cache.groups[i].mask !=
                        groupMasks[i].Mask)
                {
                    return false;
                }
            }

            return true;
        }
    }

    TopologySnapshot CpuTopology::Detect()
    {
        TopologySnapshot topology;

        std::vector<BYTE> buffer = QueryRelations();
        if (buffer.empty())
            return topology;

        std::unordered_map<std::uint32_t, std::size_t> processorByKey;
        BYTE minimumEfficiency = InvalidEfficiencyClass;
        BYTE maximumEfficiency = 0;

        // Núcleos físicos y procesadores lógicos.
        ForEachRelation(
            buffer,
            RelationProcessorCore,
            [&](PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX info)
            {
                TopologyCore core;
                core.coreIndex =
                    static_cast<std::uint32_t>(
                        topology.cores.size());

                const BYTE efficiency =
                    info->Processor.EfficiencyClass;

                minimumEfficiency =
                    (std::min)(minimumEfficiency, efficiency);
                maximumEfficiency =
                    (std::max)(maximumEfficiency, efficiency);

                ForEachLogicalProcessor(
                    info->Processor.GroupCount,
                    info->Processor.GroupMask,
                    [&](WORD group, BYTE logicalIndex)
                    {
                        TopologyProcessor processor;
                        processor.group = group;
                        processor.logicalIndex = logicalIndex;
                        processor.physicalCoreIndex =
                            core.coreIndex;
                        processor.efficiencyClass = efficiency;
                        processor.coreClass =
                            CoreClass::Unknown;

                        const std::size_t position =
                            topology.processors.size();

                        processorByKey[
                            ProcessorKey(group, logicalIndex)] =
                            position;

                        topology.processors.push_back(processor);
                        core.logicalIndices.push_back(
                            static_cast<std::uint32_t>(position));
                    });

                for (WORD i = 0;
                     i < info->Processor.GroupCount;
                     ++i)
                {
                    GroupMask mask;
                    mask.group =
                        info->Processor.GroupMask[i].Group;
                    mask.mask =
                        info->Processor.GroupMask[i].Mask;
                    core.groups.push_back(mask);

                    if (i == 0)
                        core.group = mask.group;
                }

                topology.cores.push_back(core);
            });

        if (topology.processors.empty())
            return topology;

        const bool hybrid =
            minimumEfficiency != InvalidEfficiencyClass &&
            maximumEfficiency > minimumEfficiency;

        topology.kind = hybrid
            ? TopologyKind::Hybrid
            : TopologyKind::Uniform;

        for (TopologyCore& core : topology.cores)
        {
            const BYTE efficiency =
                topology.processors.empty()
                    ? 0
                    : topology.processors[
                        core.logicalIndices.empty()
                            ? 0
                            : core.logicalIndices[0]]
                          .efficiencyClass;

            core.coreClass = hybrid
                ? (efficiency > minimumEfficiency
                       ? CoreClass::Performance
                       : CoreClass::Efficiency)
                : CoreClass::Uniform;
        }

        for (TopologyProcessor& processor :
             topology.processors)
        {
            processor.coreClass = hybrid
                ? (processor.efficiencyClass >
                           minimumEfficiency
                       ? CoreClass::Performance
                       : CoreClass::Efficiency)
                : CoreClass::Uniform;
        }

        // Cachés L1/L2/L3 compartidas. Un mismo id significa que esos
        // procesadores pueden reutilizar la misma línea de caché.
        ForEachRelation(
            buffer,
            RelationCache,
            [&](PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX info)
            {
                const BYTE level = info->Cache.Level;

                if (level < 1 || level > 3)
                    return;

                std::uint16_t cacheId = 0;

                for (const TopologyCache& existing :
                     topology.caches)
                {
                    if (SameCacheGroups(
                            existing,
                            level,
                            info->Cache.GroupCount,
                            info->Cache.GroupMasks))
                    {
                        cacheId = existing.id;
                        break;
                    }
                }

                if (cacheId == 0)
                {
                    cacheId = static_cast<std::uint16_t>(
                        topology.caches.size() + 1);

                    TopologyCache cache;
                    cache.id = cacheId;
                    cache.level = level;
                    cache.sizeKB =
                        info->Cache.CacheSize / 1024;
                    cache.lineSize = info->Cache.LineSize;

                    for (WORD i = 0;
                         i < info->Cache.GroupCount;
                         ++i)
                    {
                        GroupMask mask;
                        mask.group =
                            info->Cache.GroupMasks[i].Group;
                        mask.mask =
                            info->Cache.GroupMasks[i].Mask;
                        cache.groups.push_back(mask);
                    }

                    topology.caches.push_back(cache);
                }

                ForEachLogicalProcessor(
                    info->Cache.GroupCount,
                    info->Cache.GroupMasks,
                    [&](WORD group, BYTE logicalIndex)
                    {
                        const auto it = processorByKey.find(
                            ProcessorKey(group, logicalIndex));

                        if (it == processorByKey.end())
                            return;

                        TopologyProcessor& processor =
                            topology.processors[it->second];

                        if (level == 1)
                            processor.l1CacheId = cacheId;
                        else if (level == 2)
                            processor.l2CacheId = cacheId;
                        else
                            processor.l3CacheId = cacheId;
                    });
            });

        // Nodos NUMA.
        std::uint16_t numaNode = 0;

        ForEachRelation(
            buffer,
            RelationNumaNode,
            [&](PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX info)
            {
                ForEachLogicalProcessor(
                    info->NumaNode.GroupCount,
                    info->NumaNode.GroupMasks,
                    [&](WORD group, BYTE logicalIndex)
                    {
                        const auto it = processorByKey.find(
                            ProcessorKey(group, logicalIndex));

                        if (it == processorByKey.end())
                            return;

                        topology.processors[it->second]
                            .numaNode = numaNode;
                    });

                ++numaNode;
            });

        topology.numaNodeCount = numaNode;

        WORD maximumGroup = 0;
        for (const TopologyProcessor& processor :
             topology.processors)
        {
            maximumGroup = (std::max)(
                maximumGroup, processor.group);
        }

        topology.groupCount =
            static_cast<WORD>(maximumGroup + 1);

        // Completar la caché representativa de cada núcleo a partir de
        // su primer procesador lógico.
        for (TopologyCore& core : topology.cores)
        {
            if (core.logicalIndices.empty())
                continue;

            // core.logicalIndices guarda indices de procesador dentro del
            // vector global, asi que se usa directamente.
            const std::size_t index = core.logicalIndices[0];
            if (index >= topology.processors.size())
                continue;

            const TopologyProcessor& processor =
                topology.processors[index];

            core.l2CacheId = processor.l2CacheId;
            core.l3CacheId = processor.l3CacheId;
        }

        ApplyProcessorPower(topology);

        topology.available = true;
        return topology;
    }

    void CpuTopology::RefreshUsage(
        TopologySnapshot& topology,
        const CpuSnapshot& snapshot)
    {
        for (const LogicalProcessorInfo& processor :
             snapshot.logicalProcessors)
        {
            for (TopologyProcessor& target :
                 topology.processors)
            {
                if (target.group ==
                        processor.processorGroup &&
                    target.logicalIndex ==
                        processor.logicalProcessorIndex)
                {
                    target.usagePercent =
                        processor.usagePercent;
                    break;
                }
            }
        }
    }

    const TopologyProcessor* CpuTopology::FindProcessor(
        const TopologySnapshot& topology,
        WORD group,
        BYTE logicalIndex)
    {
        for (const TopologyProcessor& processor :
             topology.processors)
        {
            if (processor.group == group &&
                processor.logicalIndex == logicalIndex)
            {
                return &processor;
            }
        }

        return nullptr;
    }
}
