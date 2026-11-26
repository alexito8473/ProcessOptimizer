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
        Balanced = 1
    };

    enum class PlanStatus : std::int32_t
    {
        Planned = 0,
        Applied = 1,
        Verified = 2,
        Skipped = 3,
        Failed = 4
    };

    enum class BenchmarkStatus : std::int32_t
    {
        Idle = 0,
        Running = 1,
        Completed = 2,
        Failed = 3
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

    // Resultado medido de un procesador logico durante una prueba corta
    // y bajo demanda. No se estima: peakMHz y workRate vienen de lecturas
    // reales tomadas mientras los hilos de prueba estaban cargando el CPU.
    struct ProcessorBenchmark
    {
        WORD group = 0;
        BYTE logicalIndex = 0;
        std::uint32_t physicalCoreIndex = 0;
        CoreClass coreClass = CoreClass::Unknown;

        double peakMHz = 0.0;
        double averageMHz = 0.0;
        double workRate = 0.0;
        double score = 0.0;
        std::uint16_t rank = 0;
    };

    struct CpuBenchmarkProfile
    {
        std::uint32_t version = 1;
        std::uint64_t createdMs = 0;
        std::uint32_t durationMs = 0;
        std::uint64_t topologySignature = 0;
        std::vector<ProcessorBenchmark> processors;

        bool HasData() const
        {
            return !processors.empty();
        }
    };

    struct BenchmarkState
    {
        BenchmarkStatus status = BenchmarkStatus::Idle;
        std::uint32_t progressPercent = 0;
        std::string statusText;
        CpuBenchmarkProfile profile;
    };

    enum class FrameVerdict : std::int32_t
    {
        NoData = 0,
        Stable = 1,
        Capped = 2,
        GpuBound = 3,
        CpuBound = 4,
        Unstable = 5,
        Unavailable = 6
    };

    // Resumen agregado de frame time. Se calcula en el servicio y se envia
    // a la UI una vez por segundo: nunca se manda una muestra por frame.
    struct FrameProbeSnapshot
    {
        bool active = false;
        DWORD processId = 0;
        std::uint64_t lastUpdateMs = 0;
        std::uint32_t sampleCount = 0;
        double fps = 0.0;
        double p50Ms = 0.0;
        double p95Ms = 0.0;
        double p99Ms = 0.0;
        double maxMs = 0.0;
        double avgCpuInPresentMs = 0.0;
        std::int32_t syncInterval = 0;
        FrameVerdict verdict = FrameVerdict::NoData;
        std::string statusText;

        // Contadores agregados para la comparacion A/B. Se acumulan en la
        // ventana actual y se publican ya resumidos: nunca una muestra
        // por frame a traves de IPC.
        std::uint32_t framesOver20Ms = 0;
        std::uint32_t framesOver25Ms = 0;
        std::uint64_t sampleWindowMs = 0;
    };

    struct FrameMeasurement
    {
        std::uint32_t sampleCount = 0;
        double p50Ms = 0.0;
        double p95Ms = 0.0;
        double p99Ms = 0.0;
        double fps = 0.0;
        std::uint32_t framesOver20Ms = 0;
        std::uint32_t framesOver25Ms = 0;
        double cpuInPresentMs = 0.0;
    };

    // Resultado de la prueba A/B de 60 s sin cambios frente a 60 s con
    // perfil. El cambio solo se conserva si baja p95 sin empeorar p99.
    struct AbResult
    {
        bool running = false;
        bool completed = false;
        bool kept = false;
        std::string phase;
        std::uint32_t progressPercent = 0;
        FrameMeasurement baseline;
        FrameMeasurement candidate;
        double p95DeltaPercent = 0.0;
        double p99DeltaPercent = 0.0;
        std::string verdict;
        std::string csvPath;
    };

    struct ContainmentSnapshot
    {
        bool enabled = false;
        bool evidence = false;
        std::uint32_t containedProcesses = 0;
        std::uint32_t revertedProcesses = 0;
        std::string status;
    };

    struct StutterSnapshot
    {
        bool available = false;
        bool etwActive = false;
        double contextSwitchesPerSec = 0.0;
        double hardFaultsPerSec = 0.0;
        double pagesInputPerSec = 0.0;
        double dpcPercent = 0.0;
        double interruptPercent = 0.0;
        std::string topSuspect;
        double confidence = 0.0;
        std::string status;
    };

    struct EnergySnapshot
    {
        bool available = false;
        bool customSchemeActive = false;
        std::string activeSchemeName;
        double minProcessorState = 0.0;
        double maxProcessorState = 0.0;
        std::int32_t boostMode = -1;
        std::uint32_t parkedProcessors = 0;
        std::string gpuPriority;
        bool experimentalProfile = false;
        std::string status;
    };

    struct DiagnosticsSnapshot
    {
        bool available = false;
        std::uint32_t physicalMemoryMb = 0;
        std::uint32_t largePageMinimumKb = 0;
        bool largePagesAvailable = false;
        std::uint16_t numaNodes = 0;
        bool memoryCompressionEnabled = false;
        bool sysMainEnabled = false;
        bool bypassIoSupported = false;
        std::uint32_t l3Domains = 0;
        std::uint32_t hotL3CacheKb = 0;
        std::string notes;
    };

    struct AuditEntry
    {
        std::uint64_t timeMs = 0;
        std::string action;
        std::string target;
        std::string detail;
        bool reverted = false;
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
        BenchmarkState benchmark;
        FrameProbeSnapshot frameProbe;
        AbResult ab;
        ContainmentSnapshot containment;
        StutterSnapshot stutter;
        EnergySnapshot energy;
        DiagnosticsSnapshot diagnostics;
        std::vector<AuditEntry> audit;
        std::vector<ProcessPlan> processes;
        std::vector<CoreAssignment> cores;
    };
}
