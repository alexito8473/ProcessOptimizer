#pragma once

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

#include "../Monitoring/CpuMonitor/CpuInfo.h"
#include "../Monitoring/GpuMonitor/GpuInfo.h"
#include "../Monitoring/MemoryMonitor/MemoryInfo.h"
#include "../Monitoring/ProcessMonitor/ProcessInfo.h"
#include "../Optimization/OptimizationTypes.h"

namespace IPC
{
    inline constexpr wchar_t PipeName[] =
        L"\\\\.\\pipe\\ProcessOptimizer.Monitor";
    inline constexpr std::uint32_t ProtocolMagic = 0x504F4D31; // "POM1"
    inline constexpr std::uint32_t ProtocolVersion = 4;

    enum class Request : std::uint32_t
    {
        Ping = 1,
        GetState = 2,
        SetOptimizerConfig = 3,
        SetForegroundProcess = 4
    };

    enum class ResponseType : std::uint32_t
    {
        Pong = 1,
        State = 2,
        Error = 3,
        Ack = 4
    };

    struct OptimizerConfigCommand
    {
        bool enabled = false;
        Optimization::OptimizerMode mode =
            Optimization::OptimizerMode::Off;
    };

    struct ForegroundProcessCommand
    {
        DWORD processId = 0;
    };

    struct ResponseHeader
    {
        std::uint32_t magic = ProtocolMagic;
        std::uint32_t version = ProtocolVersion;
        std::uint32_t type = 0;
        std::uint32_t payloadSize = 0;
        std::uint64_t sequence = 0;
        std::uint64_t timestampMs = 0;
    };

    constexpr std::size_t CpuHistoryCapacity = 300;
    constexpr std::size_t MemoryHistoryCapacity = 300;
    constexpr std::size_t GpuHistoryCapacity = 120;
    constexpr std::size_t OverviewHistoryCapacity = 120;

    template<std::size_t N>
    struct FloatRing
    {
        std::array<float, N> values{};
        std::size_t index = 0;
        std::size_t count = 0;
    };

    using CpuRing = FloatRing<CpuHistoryCapacity>;
    using MemoryRing = FloatRing<MemoryHistoryCapacity>;
    using GpuRing = FloatRing<GpuHistoryCapacity>;
    using OverviewRing = FloatRing<OverviewHistoryCapacity>;

    struct ProcessorHistory
    {
        WORD group = 0;
        BYTE logicalIndex = 0;
        CpuRing values;
    };

    struct GpuSeriesHistory
    {
        std::uint32_t luidLowPart = 0;
        std::int32_t luidHighPart = 0;
        std::string name;
        OverviewRing usage;
    };

    struct GpuAdapterHistory
    {
        std::uint32_t luidLowPart = 0;
        std::int32_t luidHighPart = 0;
        GpuRing usage;
        GpuRing usage3D;
        GpuRing usageCopy;
        GpuRing usageCompute;
        GpuRing usageVideoDecode;
        GpuRing usageVideoEncode;
        GpuRing usageVideoProcessing;
        GpuRing dedicatedMemory;
        GpuRing sharedMemory;
    };

    struct OverviewHistory
    {
        OverviewRing cpu;
        OverviewRing memory;
        std::vector<GpuSeriesHistory> gpus;
    };

    struct State
    {
        std::uint64_t sequence = 0;
        std::uint64_t timestampMs = 0;

        // Mascara de ChangeMask del ultimo snapshot aplicado.
        std::uint32_t changedMask = 0;

        bool hasCpu = false;
        bool hasMemory = false;
        bool hasGpu = false;
        bool hasProcesses = false;
        bool hasTopology = false;
        bool hasOptimizer = false;

        CpuSnapshot cpu;
        MemorySnapshot memory;
        GpuSnapshot gpu;
        ProcessSnapshot processes;

        Optimization::TopologySnapshot topology;
        Optimization::OptimizerState optimizer;

        CpuRing cpuHistory;
        MemoryRing memoryHistory;
        std::vector<ProcessorHistory> processorHistories;
        std::vector<GpuAdapterHistory> gpuHistories;
        OverviewHistory overview;
    };

    // ------------------------------------------------------------
    // Serializacion binaria compacta. Todos los tipos basicos se
    // escriben en little-endian nativo: productor y consumidor se
    // compilan juntos desde el mismo arbol de fuentes.
    // ------------------------------------------------------------
    class Writer
    {
    public:
        std::vector<std::uint8_t> data;

        void Raw(const void* source, std::size_t size)
        {
            const auto* bytes =
                static_cast<const std::uint8_t*>(source);
            data.insert(data.end(), bytes, bytes + size);
        }

        template<typename T>
        void Pod(const T& value)
        {
            static_assert(
                std::is_trivially_copyable_v<T>,
                "Pod requiere un tipo copiable por bytes");
            Raw(&value, sizeof(value));
        }

        void Bool(bool value)
        {
            const std::uint8_t byte = value ? 1 : 0;
            Pod(byte);
        }

        void Str(const std::string& value)
        {
            const std::uint32_t size =
                static_cast<std::uint32_t>(value.size());
            Pod(size);
            if (size != 0)
                Raw(value.data(), size);
        }

        void WStr(const std::wstring& value)
        {
            const std::uint32_t size =
                static_cast<std::uint32_t>(value.size());
            Pod(size);
            if (size != 0)
                Raw(value.data(), size * sizeof(wchar_t));
        }

        template<typename T, typename Fn>
        void Vec(const std::vector<T>& values, Fn writeElement)
        {
            const std::uint32_t size =
                static_cast<std::uint32_t>(values.size());
            Pod(size);
            for (const T& value : values)
                writeElement(*this, value);
        }

        template<typename T>
        void PodVec(const std::vector<T>& values)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            const std::uint32_t size =
                static_cast<std::uint32_t>(values.size());
            Pod(size);
            if (size != 0)
                Raw(values.data(), size * sizeof(T));
        }
    };

    class Reader
    {
    public:
        Reader(const std::uint8_t* data, std::size_t size)
            : data_(data), size_(size)
        {
        }

        bool Raw(void* destination, std::size_t length)
        {
            if (length > size_ - position_)
                return false;

            std::memcpy(
                destination, data_ + position_, length);
            position_ += length;
            return true;
        }

        template<typename T>
        bool Pod(T& value)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            return Raw(&value, sizeof(value));
        }

        bool Bool(bool& value)
        {
            std::uint8_t byte = 0;
            if (!Pod(byte))
                return false;
            value = byte != 0;
            return true;
        }

        bool Str(std::string& value)
        {
            std::uint32_t size = 0;
            if (!Pod(size) || size > size_ - position_)
                return false;

            value.assign(
                reinterpret_cast<const char*>(data_ + position_),
                size);
            position_ += size;
            return true;
        }

        bool WStr(std::wstring& value)
        {
            std::uint32_t size = 0;
            if (!Pod(size))
                return false;

            const std::uint64_t bytes =
                static_cast<std::uint64_t>(size) *
                sizeof(wchar_t);

            if (bytes > size_ - position_)
                return false;

            value.assign(
                reinterpret_cast<const wchar_t*>(
                    data_ + position_),
                size);
            position_ += static_cast<std::size_t>(bytes);
            return true;
        }

        template<typename T, typename Fn>
        bool Vec(std::vector<T>& values, Fn readElement)
        {
            std::uint32_t size = 0;
            if (!Pod(size) || size > 100000)
                return false;

            values.resize(size);

            for (T& value : values)
            {
                if (!readElement(*this, value))
                    return false;
            }

            return true;
        }

        template<typename T>
        bool PodVec(std::vector<T>& values)
        {
            static_assert(std::is_trivially_copyable_v<T>);

            std::uint32_t size = 0;
            if (!Pod(size))
                return false;

            const std::uint64_t bytes =
                static_cast<std::uint64_t>(size) * sizeof(T);

            if (bytes > size_ - position_)
                return false;

            values.resize(size);

            if (size != 0)
            {
                std::memcpy(
                    values.data(), data_ + position_,
                    static_cast<std::size_t>(bytes));
            }

            position_ += static_cast<std::size_t>(bytes);
            return true;
        }

        std::size_t Remaining() const
        {
            return size_ - position_;
        }

    private:
        const std::uint8_t* data_ = nullptr;
        std::size_t size_ = 0;
        std::size_t position_ = 0;
    };

    namespace Detail
    {
        template<std::size_t N>
        void WriteRing(Writer& w, const FloatRing<N>& ring)
        {
            w.PodVec(
                std::vector<float>(
                    ring.values.begin(), ring.values.end()));
            w.Pod(ring.index);
            w.Pod(ring.count);
        }

        template<std::size_t N>
        bool ReadRing(Reader& r, FloatRing<N>& ring)
        {
            std::vector<float> values;
            if (!r.PodVec(values))
                return false;
            if (values.size() > N)
                return false;
            std::copy(
                values.begin(), values.end(), ring.values.begin());
            if (!r.Pod(ring.index) || !r.Pod(ring.count))
                return false;
            return true;
        }

        inline void WriteEnum(Writer& w, auto value)
        {
            const auto integer = static_cast<std::int32_t>(value);
            w.Pod(integer);
        }

        template<typename T>
        bool ReadEnum(Reader& r, T& value)
        {
            std::int32_t integer = 0;
            if (!r.Pod(integer))
                return false;
            value = static_cast<T>(integer);
            return true;
        }

        inline void Write(Writer& w, const LogicalProcessorInfo& v)
        {
            w.Pod(v);
        }
        inline bool Read(Reader& r, LogicalProcessorInfo& v)
        {
            return r.Pod(v);
        }

        inline void Write(Writer& w, const PhysicalCoreInfo& v)
        {
            w.Pod(v.coreIndex);
            w.PodVec(v.logicalProcessorIndices);
        }
        inline bool Read(Reader& r, PhysicalCoreInfo& v)
        {
            return r.Pod(v.coreIndex) &&
                   r.PodVec(v.logicalProcessorIndices);
        }

        inline void Write(Writer& w, const CpuProcessorInfo& v)
        {
            w.Str(v.manufacturer);
            w.Str(v.brand);
            w.Str(v.architecture);
            w.Pod(v.logicalProcessorCount);
            w.Pod(v.physicalCoreCount);
            w.Pod(v.socketCount);
        }
        inline bool Read(Reader& r, CpuProcessorInfo& v)
        {
            return r.Str(v.manufacturer) && r.Str(v.brand) &&
                   r.Str(v.architecture) &&
                   r.Pod(v.logicalProcessorCount) &&
                   r.Pod(v.physicalCoreCount) &&
                   r.Pod(v.socketCount);
        }

        inline void Write(Writer& w, const CpuCacheInfo& v)
        {
            w.Pod(v);
        }
        inline bool Read(Reader& r, CpuCacheInfo& v)
        {
            return r.Pod(v);
        }

        inline void Write(Writer& w, const CpuFrequencyInfo& v)
        {
            w.Pod(v);
        }
        inline bool Read(Reader& r, CpuFrequencyInfo& v)
        {
            return r.Pod(v);
        }

        inline void Write(Writer& w, const CpuVirtualizationInfo& v)
        {
            w.Bool(v.supported);
            w.Bool(v.firmwareEnabled);
            w.Bool(v.hypervisorPresent);
        }
        inline bool Read(Reader& r, CpuVirtualizationInfo& v)
        {
            return r.Bool(v.supported) &&
                   r.Bool(v.firmwareEnabled) &&
                   r.Bool(v.hypervisorPresent);
        }

        inline void Write(Writer& w, const CpuProcessInfo& v)
        {
            w.Pod(v.processId);
            w.WStr(v.name);
            w.Pod(v.threadCount);
            w.Pod(v.handleCount);
            w.Pod(v.cpuUsagePercent);
        }
        inline bool Read(Reader& r, CpuProcessInfo& v)
        {
            return r.Pod(v.processId) && r.WStr(v.name) &&
                   r.Pod(v.threadCount) && r.Pod(v.handleCount) &&
                   r.Pod(v.cpuUsagePercent);
        }

        inline void Write(Writer& w, const CpuSystemActivity& v)
        {
            w.Pod(v);
        }
        inline bool Read(Reader& r, CpuSystemActivity& v)
        {
            return r.Pod(v);
        }

        inline void Write(Writer& w, const CpuSnapshot& v)
        {
            w.Pod(v.totalUsagePercent);
            Write(w, v.processor);
            Write(w, v.frequency);
            Write(w, v.virtualization);
            w.Vec(v.caches, [](Writer& w, const auto& v) { Write(w, v); });
            w.Vec(v.physicalCores, [](Writer& w, const auto& v) { Write(w, v); });
            w.Vec(v.logicalProcessors, [](Writer& w, const auto& v) { Write(w, v); });
            w.Vec(v.processes, [](Writer& w, const auto& v) { Write(w, v); });
            Write(w, v.activity);
        }
        inline bool Read(Reader& r, CpuSnapshot& v)
        {
            return r.Pod(v.totalUsagePercent) &&
                   Read(r, v.processor) &&
                   Read(r, v.frequency) &&
                   Read(r, v.virtualization) &&
                   r.Vec(v.caches, [](Reader& r, auto& v) { return Read(r, v); }) &&
                   r.Vec(v.physicalCores, [](Reader& r, auto& v) { return Read(r, v); }) &&
                   r.Vec(v.logicalProcessors, [](Reader& r, auto& v) { return Read(r, v); }) &&
                   r.Vec(v.processes, [](Reader& r, auto& v) { return Read(r, v); }) &&
                   Read(r, v.activity);
        }

        inline void Write(Writer& w, const MemoryModuleInfo& v)
        {
            w.Bool(v.isPopulated);
            w.WStr(v.deviceLocator);
            w.WStr(v.bankLabel);
            w.WStr(v.manufacturer);
            w.WStr(v.partNumber);
            w.WStr(v.serialNumber);
            w.Pod(v.capacityBytes);
            w.Pod(v.speed);
            w.Pod(v.configuredSpeed);
            w.Pod(v.memoryType);
        }
        inline bool Read(Reader& r, MemoryModuleInfo& v)
        {
            return r.Bool(v.isPopulated) &&
                   r.WStr(v.deviceLocator) &&
                   r.WStr(v.bankLabel) &&
                   r.WStr(v.manufacturer) &&
                   r.WStr(v.partNumber) &&
                   r.WStr(v.serialNumber) &&
                   r.Pod(v.capacityBytes) && r.Pod(v.speed) &&
                   r.Pod(v.configuredSpeed) &&
                   r.Pod(v.memoryType);
        }

        inline void Write(Writer& w, const MemorySnapshot& v)
        {
            w.Pod(v.totalPhysicalBytes);
            w.Pod(v.usedPhysicalBytes);
            w.Pod(v.availablePhysicalBytes);
            w.Pod(v.usagePercent);
            w.Pod(v.committedBytes);
            w.Pod(v.commitLimitBytes);
            w.Pod(v.cachedBytes);
            w.Pod(v.pagedPoolBytes);
            w.Pod(v.nonPagedPoolBytes);
            w.Pod(v.totalSlots);
            w.Pod(v.usedSlots);
            w.Vec(v.modules, [](Writer& w, const auto& v) { Write(w, v); });
            w.Bool(v.moduleInformationAvailable);
        }
        inline bool Read(Reader& r, MemorySnapshot& v)
        {
            return r.Pod(v.totalPhysicalBytes) &&
                   r.Pod(v.usedPhysicalBytes) &&
                   r.Pod(v.availablePhysicalBytes) &&
                   r.Pod(v.usagePercent) &&
                   r.Pod(v.committedBytes) &&
                   r.Pod(v.commitLimitBytes) &&
                   r.Pod(v.cachedBytes) &&
                   r.Pod(v.pagedPoolBytes) &&
                   r.Pod(v.nonPagedPoolBytes) &&
                   r.Pod(v.totalSlots) && r.Pod(v.usedSlots) &&
                   r.Vec(v.modules, [](Reader& r, auto& v) { return Read(r, v); }) &&
                   r.Bool(v.moduleInformationAvailable);
        }

        inline void Write(Writer& w, const GpuEngineInfo& v)
        {
            WriteEnum(w, v.type);
            w.WStr(v.name);
            w.Pod(v.usagePercent);
        }
        inline bool Read(Reader& r, GpuEngineInfo& v)
        {
            return ReadEnum(r, v.type) && r.WStr(v.name) &&
                   r.Pod(v.usagePercent);
        }

        inline void Write(Writer& w, const GpuAdapterInfo& v)
        {
            w.Pod(v.index);
            w.WStr(v.name);
            w.Pod(v.vendorId);
            w.Pod(v.deviceId);
            w.Pod(v.subsystemId);
            w.Pod(v.revision);
            w.Pod(v.dedicatedVideoMemoryBytes);
            w.Pod(v.dedicatedSystemMemoryBytes);
            w.Pod(v.sharedSystemMemoryBytes);
            w.Pod(v.luidLowPart);
            w.Pod(v.luidHighPart);
            w.Bool(v.isSoftware);
            w.Pod(v.processDedicatedMemoryUsedBytes);
            w.Pod(v.processDedicatedMemoryBudgetBytes);
            w.Pod(v.processDedicatedMemoryUsagePercent);
            w.Pod(v.processSharedMemoryUsedBytes);
            w.Pod(v.processSharedMemoryBudgetBytes);
            w.Pod(v.processSharedMemoryUsagePercent);
            w.Pod(v.usagePercent);
            w.Bool(v.performanceAvailable);
            w.Vec(v.engines, [](Writer& w, const auto& v) { Write(w, v); });
            w.Pod(v.usage3D);
            w.Pod(v.usageCopy);
            w.Pod(v.usageVideoDecode);
            w.Pod(v.usageVideoEncode);
            w.Pod(v.usageVideoProcessing);
            w.Pod(v.usageCompute);
            w.WStr(v.driverProvider);
            w.WStr(v.driverVersion);
            w.WStr(v.driverDate);
            w.WStr(v.driverDescription);
            w.WStr(v.deviceLocation);
            w.Bool(v.driverInfoAvailable);
            w.WStr(v.direct3DVersion);
            w.WStr(v.featureLevel);
            w.Bool(v.d3d11Supported);
            w.Bool(v.d3d12Supported);
            w.Bool(v.directComputeSupported);
            w.WStr(v.wddmVersion);
            w.Bool(v.wddmInfoAvailable);
            w.Pod(v.temperatureCelsius);
            w.Pod(v.coreClockMHz);
            w.Pod(v.memoryClockMHz);
            w.Pod(v.fanSpeedPercent);
            w.Pod(v.fanSpeedRPM);
            w.Pod(v.powerUsageWatts);
            w.Pod(v.powerLimitWatts);
            w.Bool(v.temperatureAvailable);
            w.Bool(v.coreClockAvailable);
            w.Bool(v.memoryClockAvailable);
            w.Bool(v.fanSpeedAvailable);
            w.Bool(v.powerUsageAvailable);
            w.Pod(v.pciBus);
            w.Pod(v.pciDevice);
            w.Pod(v.pciFunction);
            w.Bool(v.pciLocationAvailable);
            w.Bool(v.isIntegrated);
            w.Bool(v.adapterTypeAvailable);
        }
        inline bool Read(Reader& r, GpuAdapterInfo& v)
        {
            return r.Pod(v.index) && r.WStr(v.name) &&
                   r.Pod(v.vendorId) && r.Pod(v.deviceId) &&
                   r.Pod(v.subsystemId) && r.Pod(v.revision) &&
                   r.Pod(v.dedicatedVideoMemoryBytes) &&
                   r.Pod(v.dedicatedSystemMemoryBytes) &&
                   r.Pod(v.sharedSystemMemoryBytes) &&
                   r.Pod(v.luidLowPart) &&
                   r.Pod(v.luidHighPart) &&
                   r.Bool(v.isSoftware) &&
                   r.Pod(v.processDedicatedMemoryUsedBytes) &&
                   r.Pod(v.processDedicatedMemoryBudgetBytes) &&
                   r.Pod(v.processDedicatedMemoryUsagePercent) &&
                   r.Pod(v.processSharedMemoryUsedBytes) &&
                   r.Pod(v.processSharedMemoryBudgetBytes) &&
                   r.Pod(v.processSharedMemoryUsagePercent) &&
                   r.Pod(v.usagePercent) &&
                   r.Bool(v.performanceAvailable) &&
                   r.Vec(v.engines, [](Reader& r, auto& v) { return Read(r, v); }) &&
                   r.Pod(v.usage3D) && r.Pod(v.usageCopy) &&
                   r.Pod(v.usageVideoDecode) &&
                   r.Pod(v.usageVideoEncode) &&
                   r.Pod(v.usageVideoProcessing) &&
                   r.Pod(v.usageCompute) &&
                   r.WStr(v.driverProvider) &&
                   r.WStr(v.driverVersion) &&
                   r.WStr(v.driverDate) &&
                   r.WStr(v.driverDescription) &&
                   r.WStr(v.deviceLocation) &&
                   r.Bool(v.driverInfoAvailable) &&
                   r.WStr(v.direct3DVersion) &&
                   r.WStr(v.featureLevel) &&
                   r.Bool(v.d3d11Supported) &&
                   r.Bool(v.d3d12Supported) &&
                   r.Bool(v.directComputeSupported) &&
                   r.WStr(v.wddmVersion) &&
                   r.Bool(v.wddmInfoAvailable) &&
                   r.Pod(v.temperatureCelsius) &&
                   r.Pod(v.coreClockMHz) &&
                   r.Pod(v.memoryClockMHz) &&
                   r.Pod(v.fanSpeedPercent) &&
                   r.Pod(v.fanSpeedRPM) &&
                   r.Pod(v.powerUsageWatts) &&
                   r.Pod(v.powerLimitWatts) &&
                   r.Bool(v.temperatureAvailable) &&
                   r.Bool(v.coreClockAvailable) &&
                   r.Bool(v.memoryClockAvailable) &&
                   r.Bool(v.fanSpeedAvailable) &&
                   r.Bool(v.powerUsageAvailable) &&
                   r.Pod(v.pciBus) && r.Pod(v.pciDevice) &&
                   r.Pod(v.pciFunction) &&
                   r.Bool(v.pciLocationAvailable) &&
                   r.Bool(v.isIntegrated) &&
                   r.Bool(v.adapterTypeAvailable);
        }

        inline void Write(Writer& w, const GpuSnapshot& v)
        {
            w.Vec(v.gpus, [](Writer& w, const auto& v) { Write(w, v); });
        }
        inline bool Read(Reader& r, GpuSnapshot& v)
        {
            return r.Vec(v.gpus, [](Reader& r, auto& v) { return Read(r, v); });
        }

        inline void Write(Writer& w, const ProcessInfo& v)
        {
            w.Pod(v.processId);
            w.Pod(v.parentProcessId);
            w.WStr(v.name);
            w.WStr(v.executableName);
            w.WStr(v.executablePath);
            w.WStr(v.publisher);
            w.WStr(v.commandLine);
            w.WStr(v.userName);
            w.Bool(v.isApplication);
            w.Bool(v.isResponding);
            w.Bool(v.isSuspended);
            w.Bool(v.isElevated);
            w.Bool(v.is64Bit);
            w.Pod(v.cpuUsagePercent);
            w.Pod(v.cpuTime100ns);
            w.Pod(v.workingSetBytes);
            w.Pod(v.privateMemoryBytes);
            w.Pod(v.sharedMemoryBytes);
            w.Pod(v.peakWorkingSetBytes);
            w.Pod(v.diskReadBytes);
            w.Pod(v.diskWriteBytes);
            w.Pod(v.diskReadBytesPerSecond);
            w.Pod(v.diskWriteBytesPerSecond);
            w.Pod(v.networkReceiveBytes);
            w.Pod(v.networkSendBytes);
            w.Pod(v.networkReceiveBytesPerSecond);
            w.Pod(v.networkSendBytesPerSecond);
            w.Pod(v.gpuUsagePercent);
            w.WStr(v.gpuEngine);
            w.Pod(v.gpuDedicatedMemoryBytes);
            w.Pod(v.gpuSharedMemoryBytes);
            WriteEnum(w, v.powerLevel);
            WriteEnum(w, v.powerTrend);
            w.Bool(v.isAppContainer);
            w.Bool(v.isIsolated);
            // Los HICON no son validos entre procesos.
            const std::uint64_t noIcon = 0;
            w.Pod(noIcon);
        }
        inline bool Read(Reader& r, ProcessInfo& v)
        {
            std::uint64_t ignoredIcon = 0;
            return r.Pod(v.processId) &&
                   r.Pod(v.parentProcessId) && r.WStr(v.name) &&
                   r.WStr(v.executableName) &&
                   r.WStr(v.executablePath) &&
                   r.WStr(v.publisher) && r.WStr(v.commandLine) &&
                   r.WStr(v.userName) &&
                   r.Bool(v.isApplication) &&
                   r.Bool(v.isResponding) &&
                   r.Bool(v.isSuspended) && r.Bool(v.isElevated) &&
                   r.Bool(v.is64Bit) &&
                   r.Pod(v.cpuUsagePercent) &&
                   r.Pod(v.cpuTime100ns) &&
                   r.Pod(v.workingSetBytes) &&
                   r.Pod(v.privateMemoryBytes) &&
                   r.Pod(v.sharedMemoryBytes) &&
                   r.Pod(v.peakWorkingSetBytes) &&
                   r.Pod(v.diskReadBytes) &&
                   r.Pod(v.diskWriteBytes) &&
                   r.Pod(v.diskReadBytesPerSecond) &&
                   r.Pod(v.diskWriteBytesPerSecond) &&
                   r.Pod(v.networkReceiveBytes) &&
                   r.Pod(v.networkSendBytes) &&
                   r.Pod(v.networkReceiveBytesPerSecond) &&
                   r.Pod(v.networkSendBytesPerSecond) &&
                   r.Pod(v.gpuUsagePercent) &&
                   r.WStr(v.gpuEngine) &&
                   r.Pod(v.gpuDedicatedMemoryBytes) &&
                   r.Pod(v.gpuSharedMemoryBytes) &&
                   ReadEnum(r, v.powerLevel) &&
                   ReadEnum(r, v.powerTrend) &&
                   r.Bool(v.isAppContainer) && r.Bool(v.isIsolated) &&
                   r.Pod(ignoredIcon);
        }

        inline void Write(Writer& w, const ProcessSnapshot& v)
        {
            w.Vec(v.processes, [](Writer& w, const auto& v) { Write(w, v); });
        }
        inline bool Read(Reader& r, ProcessSnapshot& v)
        {
            return r.Vec(v.processes, [](Reader& r, auto& v) { return Read(r, v); });
        }

        inline void Write(Writer& w, const ProcessorHistory& v)
        {
            w.Pod(v.group);
            w.Pod(v.logicalIndex);
            WriteRing(w, v.values);
        }
        inline bool Read(Reader& r, ProcessorHistory& v)
        {
            return r.Pod(v.group) && r.Pod(v.logicalIndex) &&
                   ReadRing(r, v.values);
        }

        inline void Write(Writer& w, const GpuSeriesHistory& v)
        {
            w.Pod(v.luidLowPart);
            w.Pod(v.luidHighPart);
            w.Str(v.name);
            WriteRing(w, v.usage);
        }
        inline bool Read(Reader& r, GpuSeriesHistory& v)
        {
            return r.Pod(v.luidLowPart) &&
                   r.Pod(v.luidHighPart) && r.Str(v.name) &&
                   ReadRing(r, v.usage);
        }

        inline void Write(Writer& w, const GpuAdapterHistory& v)
        {
            w.Pod(v.luidLowPart);
            w.Pod(v.luidHighPart);
            WriteRing(w, v.usage);
            WriteRing(w, v.usage3D);
            WriteRing(w, v.usageCopy);
            WriteRing(w, v.usageCompute);
            WriteRing(w, v.usageVideoDecode);
            WriteRing(w, v.usageVideoEncode);
            WriteRing(w, v.usageVideoProcessing);
            WriteRing(w, v.dedicatedMemory);
            WriteRing(w, v.sharedMemory);
        }
        inline bool Read(Reader& r, GpuAdapterHistory& v)
        {
            return r.Pod(v.luidLowPart) &&
                   r.Pod(v.luidHighPart) &&
                   ReadRing(r, v.usage) &&
                   ReadRing(r, v.usage3D) &&
                   ReadRing(r, v.usageCopy) &&
                   ReadRing(r, v.usageCompute) &&
                   ReadRing(r, v.usageVideoDecode) &&
                   ReadRing(r, v.usageVideoEncode) &&
                   ReadRing(r, v.usageVideoProcessing) &&
                   ReadRing(r, v.dedicatedMemory) &&
                   ReadRing(r, v.sharedMemory);
        }

        inline void Write(Writer& w, const OverviewHistory& v)
        {
            WriteRing(w, v.cpu);
            WriteRing(w, v.memory);
            w.Vec(v.gpus, [](Writer& w, const auto& v) { Write(w, v); });
        }
        inline bool Read(Reader& r, OverviewHistory& v)
        {
            return ReadRing(r, v.cpu) && ReadRing(r, v.memory) &&
                   r.Vec(v.gpus, [](Reader& r, auto& v) { return Read(r, v); });
        }

        inline void Write(Writer& w, const Optimization::GroupMask& v)
        {
            w.Pod(v.group);
            w.Pod(v.mask);
        }
        inline bool Read(Reader& r, Optimization::GroupMask& v)
        {
            return r.Pod(v.group) && r.Pod(v.mask);
        }

        inline void Write(Writer& w, const Optimization::TopologyProcessor& v)
        {
            w.Pod(v.group);
            w.Pod(v.logicalIndex);
            w.Pod(v.physicalCoreIndex);
            w.Pod(v.numaNode);
            w.Pod(v.l1CacheId);
            w.Pod(v.l2CacheId);
            w.Pod(v.l3CacheId);
            w.Pod(v.efficiencyClass);
            WriteEnum(w, v.coreClass);
            w.Pod(v.usagePercent);
            w.Bool(v.frequencyAvailable);
            w.Bool(v.parked);
            w.Pod(v.maxMHz);
            w.Pod(v.currentMHz);
            w.Pod(v.mhzLimitMHz);
            w.Pod(v.currentIdleState);
        }
        inline bool Read(Reader& r, Optimization::TopologyProcessor& v)
        {
            return r.Pod(v.group) &&
                   r.Pod(v.logicalIndex) &&
                   r.Pod(v.physicalCoreIndex) &&
                   r.Pod(v.numaNode) &&
                   r.Pod(v.l1CacheId) &&
                   r.Pod(v.l2CacheId) &&
                   r.Pod(v.l3CacheId) &&
                   r.Pod(v.efficiencyClass) &&
                   ReadEnum(r, v.coreClass) &&
                   r.Pod(v.usagePercent) &&
                   r.Bool(v.frequencyAvailable) &&
                   r.Bool(v.parked) &&
                   r.Pod(v.maxMHz) &&
                   r.Pod(v.currentMHz) &&
                   r.Pod(v.mhzLimitMHz) &&
                   r.Pod(v.currentIdleState);
        }

        inline void Write(Writer& w, const Optimization::TopologyCore& v)
        {
            w.Pod(v.coreIndex);
            w.Pod(v.group);
            WriteEnum(w, v.coreClass);
            w.Pod(v.numaNode);
            w.Pod(v.l2CacheId);
            w.Pod(v.l3CacheId);
            w.Vec(v.groups, [](Writer& w, const auto& v) { Write(w, v); });
            w.PodVec(v.logicalIndices);
        }
        inline bool Read(Reader& r, Optimization::TopologyCore& v)
        {
            return r.Pod(v.coreIndex) &&
                   r.Pod(v.group) &&
                   ReadEnum(r, v.coreClass) &&
                   r.Pod(v.numaNode) &&
                   r.Pod(v.l2CacheId) &&
                   r.Pod(v.l3CacheId) &&
                   r.Vec(v.groups, [](Reader& r, auto& v) { return Read(r, v); }) &&
                   r.PodVec(v.logicalIndices);
        }

        inline void Write(Writer& w, const Optimization::TopologyCache& v)
        {
            w.Pod(v.id);
            w.Pod(v.level);
            w.Pod(v.sizeKB);
            w.Pod(v.lineSize);
            w.Vec(v.groups, [](Writer& w, const auto& v) { Write(w, v); });
        }
        inline bool Read(Reader& r, Optimization::TopologyCache& v)
        {
            return r.Pod(v.id) &&
                   r.Pod(v.level) &&
                   r.Pod(v.sizeKB) &&
                   r.Pod(v.lineSize) &&
                   r.Vec(v.groups, [](Reader& r, auto& v) { return Read(r, v); });
        }

        inline void Write(Writer& w, const Optimization::TopologySnapshot& v)
        {
            WriteEnum(w, v.kind);
            w.Pod(v.groupCount);
            w.Pod(v.numaNodeCount);
            w.Bool(v.available);
            w.Vec(v.processors, [](Writer& w, const auto& v) { Write(w, v); });
            w.Vec(v.cores, [](Writer& w, const auto& v) { Write(w, v); });
            w.Vec(v.caches, [](Writer& w, const auto& v) { Write(w, v); });
        }
        inline bool Read(Reader& r, Optimization::TopologySnapshot& v)
        {
            return ReadEnum(r, v.kind) &&
                   r.Pod(v.groupCount) &&
                   r.Pod(v.numaNodeCount) &&
                   r.Bool(v.available) &&
                   r.Vec(v.processors, [](Reader& r, auto& v) { return Read(r, v); }) &&
                   r.Vec(v.cores, [](Reader& r, auto& v) { return Read(r, v); }) &&
                   r.Vec(v.caches, [](Reader& r, auto& v) { return Read(r, v); });
        }

        inline void Write(Writer& w, const Optimization::ThreadPlan& v)
        {
            w.Pod(v.threadId);
            w.Pod(v.cpuUsagePercent);
            w.Pod(v.currentGroup);
            w.Pod(v.currentAffinity);
            w.Pod(v.targetGroup);
            w.Pod(v.targetAffinity);
            w.Pod(v.currentIdealProcessor);
            w.Pod(v.targetIdealProcessor);
            w.Pod(v.currentPriority);
            w.Pod(v.targetPriority);
            w.Pod(v.l2CacheId);
            w.Pod(v.l3CacheId);
            w.Str(v.reason);
            WriteEnum(w, v.status);
            w.Bool(v.verified);
        }
        inline bool Read(Reader& r, Optimization::ThreadPlan& v)
        {
            return r.Pod(v.threadId) &&
                   r.Pod(v.cpuUsagePercent) &&
                   r.Pod(v.currentGroup) &&
                   r.Pod(v.currentAffinity) &&
                   r.Pod(v.targetGroup) &&
                   r.Pod(v.targetAffinity) &&
                   r.Pod(v.currentIdealProcessor) &&
                   r.Pod(v.targetIdealProcessor) &&
                   r.Pod(v.currentPriority) &&
                   r.Pod(v.targetPriority) &&
                   r.Pod(v.l2CacheId) &&
                   r.Pod(v.l3CacheId) &&
                   r.Str(v.reason) &&
                   ReadEnum(r, v.status) &&
                   r.Bool(v.verified);
        }

        inline void Write(Writer& w, const Optimization::ProcessPlan& v)
        {
            w.Pod(v.processId);
            w.WStr(v.name);
            w.Bool(v.foreground);
            w.Bool(v.systemCritical);
            w.Pod(v.cpuUsagePercent);
            w.Pod(v.gpuUsagePercent);
            w.Pod(v.threadCount);
            w.Pod(v.currentPriority);
            w.Pod(v.targetPriority);
            w.Str(v.decision);
            w.Vec(v.threads, [](Writer& w, const auto& v) { Write(w, v); });
        }
        inline bool Read(Reader& r, Optimization::ProcessPlan& v)
        {
            return r.Pod(v.processId) &&
                   r.WStr(v.name) &&
                   r.Bool(v.foreground) &&
                   r.Bool(v.systemCritical) &&
                   r.Pod(v.cpuUsagePercent) &&
                   r.Pod(v.gpuUsagePercent) &&
                   r.Pod(v.threadCount) &&
                   r.Pod(v.currentPriority) &&
                   r.Pod(v.targetPriority) &&
                   r.Str(v.decision) &&
                   r.Vec(v.threads, [](Reader& r, auto& v) { return Read(r, v); });
        }

        inline void Write(Writer& w, const Optimization::CoreAssignment& v)
        {
            w.Pod(v.group);
            w.Pod(v.logicalIndex);
            w.Pod(v.physicalCoreIndex);
            WriteEnum(w, v.coreClass);
            w.Pod(v.l2CacheId);
            w.Pod(v.l3CacheId);
            w.Pod(v.usagePercent);
            w.Pod(v.threadCount);
            w.Bool(v.frequencyAvailable);
            w.Bool(v.bestFrequency);
            w.Pod(v.maxMHz);
            w.Pod(v.currentMHz);
            w.Pod(v.mhzLimitMHz);
            w.Str(v.reason);
        }
        inline bool Read(Reader& r, Optimization::CoreAssignment& v)
        {
            return r.Pod(v.group) &&
                   r.Pod(v.logicalIndex) &&
                   r.Pod(v.physicalCoreIndex) &&
                   ReadEnum(r, v.coreClass) &&
                   r.Pod(v.l2CacheId) &&
                   r.Pod(v.l3CacheId) &&
                   r.Pod(v.usagePercent) &&
                   r.Pod(v.threadCount) &&
                   r.Bool(v.frequencyAvailable) &&
                   r.Bool(v.bestFrequency) &&
                   r.Pod(v.maxMHz) &&
                   r.Pod(v.currentMHz) &&
                   r.Pod(v.mhzLimitMHz) &&
                   r.Str(v.reason);
        }

        inline void Write(Writer& w, const Optimization::OptimizerState& v)
        {
            w.Bool(v.enabled);
            WriteEnum(w, v.mode);
            w.Bool(v.running);
            w.Pod(v.foregroundProcessId);
            w.Pod(v.targetProcessId);
            w.Pod(v.lastUpdateMs);
            w.Pod(v.appliedChanges);
            w.Pod(v.revertedChanges);
            w.Pod(v.failedChanges);
            w.Str(v.status);
            w.Vec(v.processes, [](Writer& w, const auto& v) { Write(w, v); });
            w.Vec(v.cores, [](Writer& w, const auto& v) { Write(w, v); });
        }
        inline bool Read(Reader& r, Optimization::OptimizerState& v)
        {
            return r.Bool(v.enabled) &&
                   ReadEnum(r, v.mode) &&
                   r.Bool(v.running) &&
                   r.Pod(v.foregroundProcessId) &&
                   r.Pod(v.targetProcessId) &&
                   r.Pod(v.lastUpdateMs) &&
                   r.Pod(v.appliedChanges) &&
                   r.Pod(v.revertedChanges) &&
                   r.Pod(v.failedChanges) &&
                   r.Str(v.status) &&
                   r.Vec(v.processes, [](Reader& r, auto& v) { return Read(r, v); }) &&
                   r.Vec(v.cores, [](Reader& r, auto& v) { return Read(r, v); });
        }
    }

    inline std::vector<std::uint8_t> Serialize(
        const OptimizerConfigCommand& command)
    {
        Writer writer;
        writer.Bool(command.enabled);
        Detail::WriteEnum(writer, command.mode);
        return writer.data;
    }

    inline bool Deserialize(
        const std::uint8_t* data,
        std::size_t size,
        OptimizerConfigCommand& command)
    {
        Reader reader(data, size);
        return reader.Bool(command.enabled) &&
               Detail::ReadEnum(reader, command.mode) &&
               reader.Remaining() == 0;
    }

    inline std::vector<std::uint8_t> Serialize(
        const ForegroundProcessCommand& command)
    {
        Writer writer;
        writer.Pod(command.processId);
        return writer.data;
    }

    inline bool Deserialize(
        const std::uint8_t* data,
        std::size_t size,
        ForegroundProcessCommand& command)
    {
        Reader reader(data, size);
        return reader.Pod(command.processId) &&
               reader.Remaining() == 0;
    }

    inline std::vector<std::uint8_t> Serialize(const State& state)
    {
        Writer writer;
        writer.Pod(state.sequence);
        writer.Pod(state.timestampMs);
        writer.Pod(state.changedMask);
        writer.Bool(state.hasCpu);
        writer.Bool(state.hasMemory);
        writer.Bool(state.hasGpu);
        writer.Bool(state.hasProcesses);
        writer.Bool(state.hasTopology);
        writer.Bool(state.hasOptimizer);
        Detail::Write(writer, state.cpu);
        Detail::Write(writer, state.memory);
        Detail::Write(writer, state.gpu);
        Detail::Write(writer, state.processes);
        Detail::Write(writer, state.topology);
        Detail::Write(writer, state.optimizer);
        Detail::WriteRing(writer, state.cpuHistory);
        Detail::WriteRing(writer, state.memoryHistory);
        writer.Vec(state.processorHistories, [](Writer& w, const auto& v) { Detail::Write(w, v); });
        writer.Vec(state.gpuHistories, [](Writer& w, const auto& v) { Detail::Write(w, v); });
        Detail::Write(writer, state.overview);
        return writer.data;
    }

    inline bool Deserialize(
        const std::uint8_t* data,
        std::size_t size,
        State& state)
    {
        Reader reader(data, size);

        if (!reader.Pod(state.sequence) ||
            !reader.Pod(state.timestampMs) ||
            !reader.Pod(state.changedMask) ||
            !reader.Bool(state.hasCpu) ||
            !reader.Bool(state.hasMemory) ||
            !reader.Bool(state.hasGpu) ||
            !reader.Bool(state.hasProcesses) ||
            !reader.Bool(state.hasTopology) ||
            !reader.Bool(state.hasOptimizer) ||
            !Detail::Read(reader, state.cpu) ||
            !Detail::Read(reader, state.memory) ||
            !Detail::Read(reader, state.gpu) ||
            !Detail::Read(reader, state.processes) ||
            !Detail::Read(reader, state.topology) ||
            !Detail::Read(reader, state.optimizer) ||
            !Detail::ReadRing(reader, state.cpuHistory) ||
            !Detail::ReadRing(reader, state.memoryHistory) ||
            !reader.Vec(
                state.processorHistories, [](Reader& r, auto& v) { return Detail::Read(r, v); }) ||
            !reader.Vec(state.gpuHistories, [](Reader& r, auto& v) { return Detail::Read(r, v); }) ||
            !Detail::Read(reader, state.overview))
        {
            return false;
        }

        return reader.Remaining() == 0;
    }
}
