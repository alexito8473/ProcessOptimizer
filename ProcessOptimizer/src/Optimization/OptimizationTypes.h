#pragma once

#include <Windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Optimization
{
    enum class TopologyKind : std::int32_t
    {
        Unknown = 0,
        Uniform = 1,
        Hybrid = 2
    };

    enum class CoreClass : std::int32_t
    {
        Unknown = 0,
        Uniform = 1,
        Efficiency = 2,
        Performance = 3
    };

    enum class OptimizerMode : std::int32_t
    {
        Off = 0,
        Gaming = 1,
        Multitasking = 2
    };

    enum class PlanStatus : std::int32_t
    {
        Planned = 0,
        Applied = 1,
        Verified = 2,
        Skipped = 3,
        Failed = 4
    };

    struct GroupMask
    {
        WORD group = 0;
        std::uint64_t mask = 0;
    };

    struct TopologyProcessor
    {
        WORD group = 0;
        BYTE logicalIndex = 0;
        std::uint32_t physicalCoreIndex = 0;
        std::uint16_t numaNode = 0;
        std::uint16_t l1CacheId = 0;
        std::uint16_t l2CacheId = 0;
        std::uint16_t l3CacheId = 0;
        BYTE efficiencyClass = 0;
        CoreClass coreClass = CoreClass::Unknown;
        double usagePercent = 0.0;

        // Frecuencia real informada por CallNtPowerInformation.
        // frequencyAvailable=false significa que 0 MHz no es una lectura.
        bool frequencyAvailable = false;
        bool parked = false;
        double maxMHz = 0.0;
        double currentMHz = 0.0;
        double mhzLimitMHz = 0.0;
        std::uint32_t currentIdleState = 0;
    };

    struct TopologyCore
    {
        std::uint32_t coreIndex = 0;
        WORD group = 0;
        CoreClass coreClass = CoreClass::Unknown;
        std::uint16_t numaNode = 0;
        std::uint16_t l2CacheId = 0;
        std::uint16_t l3CacheId = 0;
        std::vector<GroupMask> groups;
        // Indices dentro de TopologySnapshot::processors.
        std::vector<std::uint32_t> logicalIndices;
    };

    struct TopologyCache
    {
        std::uint16_t id = 0;
        BYTE level = 0;
        std::uint32_t sizeKB = 0;
        std::uint32_t lineSize = 0;
        std::vector<GroupMask> groups;
    };

    struct TopologySnapshot
    {
        TopologyKind kind = TopologyKind::Unknown;
        WORD groupCount = 0;
        std::uint16_t numaNodeCount = 0;
        bool available = false;

        std::vector<TopologyProcessor> processors;
        std::vector<TopologyCore> cores;
        std::vector<TopologyCache> caches;
    };

    struct ThreadPlan
    {
        DWORD threadId = 0;
        double cpuUsagePercent = 0.0;

        WORD currentGroup = 0;
        std::uint64_t currentAffinity = 0;
        WORD targetGroup = 0;
        std::uint64_t targetAffinity = 0;

        DWORD currentIdealProcessor = 0;
        DWORD targetIdealProcessor = 0;

        int currentPriority = 0;
        int targetPriority = 0;

        std::uint16_t l2CacheId = 0;
        std::uint16_t l3CacheId = 0;

        std::string reason;
        PlanStatus status = PlanStatus::Planned;
        bool verified = false;
    };

    struct ProcessPlan
    {
        DWORD processId = 0;
        std::wstring name;
        bool foreground = false;
        bool systemCritical = false;
        double cpuUsagePercent = 0.0;
        double gpuUsagePercent = 0.0;
        DWORD threadCount = 0;

        int currentPriority = 0;
        int targetPriority = 0;

        std::string decision;
        std::vector<ThreadPlan> threads;
    };

    struct CoreAssignment
    {
        WORD group = 0;
        BYTE logicalIndex = 0;
        std::uint32_t physicalCoreIndex = 0;
        CoreClass coreClass = CoreClass::Unknown;
        std::uint16_t l2CacheId = 0;
        std::uint16_t l3CacheId = 0;
        double usagePercent = 0.0;
        DWORD threadCount = 0;

        bool frequencyAvailable = false;
        bool bestFrequency = false;
        double maxMHz = 0.0;
        double currentMHz = 0.0;
        double mhzLimitMHz = 0.0;

        std::string reason;
    };

    struct OptimizerState
    {
        bool enabled = false;
        OptimizerMode mode = OptimizerMode::Off;
        bool running = false;

        DWORD foregroundProcessId = 0;
        DWORD targetProcessId = 0;

        std::uint64_t lastUpdateMs = 0;
        std::uint32_t appliedChanges = 0;
        std::uint32_t revertedChanges = 0;
        std::uint32_t failedChanges = 0;

        std::string status;
        std::vector<ProcessPlan> processes;
        std::vector<CoreAssignment> cores;
    };
}
