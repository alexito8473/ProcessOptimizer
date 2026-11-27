
#include "CpuMonitor.h"
#include <Windows.h>
#include <Pdh.h>
#include <PdhMsg.h>
#include <PowrProf.h>
#include <Psapi.h>
#include <TlHelp32.h>
#include <intrin.h>

#include <cstring>
#include <algorithm>
#include <cstdint>
#include <cwchar>
#include <string>
#include <vector>
#include <unordered_map>
#include <utility>

#pragma comment(lib, "Pdh.lib")
#pragma comment(lib, "PowrProf.lib")
#pragma comment(lib, "Psapi.lib")

// ------------------------------------------------------------
// Destructor
// ------------------------------------------------------------

CpuMonitor::~CpuMonitor()
{
    Shutdown();
}

// ------------------------------------------------------------
// Getters
// ------------------------------------------------------------

bool CpuMonitor::IsInitialized() const
{
    return initialized_;
}

const CpuSnapshot& CpuMonitor::GetSnapshot() const
{
    return snapshot_;
}

// ------------------------------------------------------------
// Initialize
// ------------------------------------------------------------

bool CpuMonitor::Initialize()
{
    Shutdown();
    const auto now = std::chrono::steady_clock::now();

    nextActivityUpdate_ = now + std::chrono::seconds(2);
    nextProcessUpdate_ = now + std::chrono::seconds(5);
    if (!DiscoverProcessorInfo())
        goto error;

    if (!DiscoverTopology())
        goto error;

    if (!DiscoverCacheInfo())
        goto error;

    if (!DiscoverVirtualization())
        goto error;

    if (!InitializeCounters())
        goto error;

    initialized_ = true;

    UpdateFrequencyInfo();
    UpdatePerformanceCounter();
    UpdateMaximumFrequencyCounter();
    UpdateSystemActivity();
    UpdateProcessInformation();

    return true;

error:
    Shutdown();
    return false;
}

// ------------------------------------------------------------
// Processor key
// ------------------------------------------------------------

std::uint32_t CpuMonitor::MakeProcessorKey(
    WORD group,
    BYTE processor)
{
    return
        (static_cast<std::uint32_t>(group) << 8) |
        static_cast<std::uint32_t>(processor);
}

// ------------------------------------------------------------
// Processor information - CPUID
// ------------------------------------------------------------

bool CpuMonitor::DiscoverProcessorInfo()
{
    int registers[4]{};

    __cpuid(registers, 0);

    char vendor[13]{};

    memcpy(vendor, &registers[1], 4);
    memcpy(vendor + 4, &registers[3], 4);
    memcpy(vendor + 8, &registers[2], 4);

    snapshot_.processor.manufacturer = vendor;

    __cpuid(registers, 0x80000000);

    const unsigned int maxExtendedLeaf =
        static_cast<unsigned int>(registers[0]);

    if (maxExtendedLeaf >= 0x80000004)
    {
        char brand[49]{};

        for (int i = 0; i < 3; ++i)
        {
            __cpuid(registers, 0x80000002 + i);

            memcpy(brand + i * 16, registers, 16);
        }

        snapshot_.processor.brand = brand;
    }

    SYSTEM_INFO systemInfo{};

    GetNativeSystemInfo(&systemInfo);

    switch (systemInfo.wProcessorArchitecture)
    {
    case PROCESSOR_ARCHITECTURE_AMD64:
        snapshot_.processor.architecture = "x64";
        break;

    case PROCESSOR_ARCHITECTURE_INTEL:
        snapshot_.processor.architecture = "x86";
        break;

    case PROCESSOR_ARCHITECTURE_ARM64:
        snapshot_.processor.architecture = "ARM64";
        break;

    default:
        snapshot_.processor.architecture = "Unknown";
        break;
    }

    return !snapshot_.processor.manufacturer.empty();
}

// ------------------------------------------------------------
// CPU topology
// ------------------------------------------------------------

bool CpuMonitor::DiscoverTopology()
{
    snapshot_.physicalCores.clear();
    snapshot_.logicalProcessors.clear();
    processorLookup_.clear();

    snapshot_.physicalCores.reserve(128);
    snapshot_.logicalProcessors.reserve(256);
    processorLookup_.reserve(256);

    DWORD bufferSize = 0;

    GetLogicalProcessorInformationEx(
        RelationProcessorCore,
        nullptr,
        &bufferSize
    );

    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER ||
        bufferSize == 0)
    {
        return false;
    }

    std::vector<BYTE> buffer(bufferSize);

    if (!GetLogicalProcessorInformationEx(
        RelationProcessorCore,
        reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(
            buffer.data()),
        &bufferSize))
    {
        return false;
    }

    BYTE* current = buffer.data();
    BYTE* end = buffer.data() + bufferSize;

    while (current < end)
    {
        auto* info =
            reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(
                current);

        if (info->Size == 0 || current + info->Size > end)
            return false;

        if (info->Relationship == RelationProcessorCore)
        {
            PhysicalCoreInfo core;

            core.coreIndex = snapshot_.physicalCores.size();

            const WORD groupCount = info->Processor.GroupCount;

            for (WORD groupIndex = 0;
                groupIndex < groupCount;
                ++groupIndex)
            {
                const GROUP_AFFINITY& affinity =
                    info->Processor.GroupMask[groupIndex];

                for (BYTE processorIndex = 0;
                    processorIndex < sizeof(KAFFINITY) * 8;
                    ++processorIndex)
                {
                    const KAFFINITY bit =
                        static_cast<KAFFINITY>(1) << processorIndex;

                    if ((affinity.Mask & bit) == 0)
                        continue;

                    LogicalProcessorInfo logicalProcessor;

                    logicalProcessor.processorGroup = affinity.Group;
                    logicalProcessor.logicalProcessorIndex =
                        processorIndex;

                    logicalProcessor.physicalCoreIndex =
                        core.coreIndex;

                    const std::size_t position =
                        snapshot_.logicalProcessors.size();

                    snapshot_.logicalProcessors.push_back(
                        logicalProcessor);

                    const std::uint32_t key =
                        MakeProcessorKey(
                            affinity.Group,
                            processorIndex);

                    processorLookup_.emplace(key, position);

                    core.logicalProcessorIndices.push_back(position);
                }
            }

            snapshot_.physicalCores.push_back(std::move(core));
        }

        current += info->Size;
    }

    snapshot_.processor.physicalCoreCount =
        static_cast<DWORD>(snapshot_.physicalCores.size());

    snapshot_.processor.logicalProcessorCount =
        static_cast<DWORD>(snapshot_.logicalProcessors.size());

    // Detectar sockets físicos.

    snapshot_.processor.socketCount = 0;

    DWORD packageBufferSize = 0;

    GetLogicalProcessorInformationEx(
        RelationProcessorPackage,
        nullptr,
        &packageBufferSize);

    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER &&
        packageBufferSize > 0)
    {
        std::vector<BYTE> packageBuffer(packageBufferSize);

        if (GetLogicalProcessorInformationEx(
            RelationProcessorPackage,
            reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(
                packageBuffer.data()),
            &packageBufferSize))
        {
            BYTE* ptr = packageBuffer.data();
            BYTE* packageEnd = packageBuffer.data() + packageBufferSize;

            while (ptr < packageEnd)
            {
                auto* package =
                    reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(
                        ptr);

                if (package->Size == 0 ||
                    ptr + package->Size > packageEnd)
                    break;

                if (package->Relationship == RelationProcessorPackage)
                    ++snapshot_.processor.socketCount;

                ptr += package->Size;
            }
        }
    }

    return !snapshot_.logicalProcessors.empty();
}

// ------------------------------------------------------------
// Cache information
// ------------------------------------------------------------

bool CpuMonitor::DiscoverCacheInfo()
{
    snapshot_.caches.clear();

    DWORD bufferSize = 0;

    GetLogicalProcessorInformationEx(
        RelationCache,
        nullptr,
        &bufferSize);

    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER ||
        bufferSize == 0)
    {
        return false;
    }

    std::vector<BYTE> buffer(bufferSize);

    if (!GetLogicalProcessorInformationEx(
        RelationCache,
        reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(
            buffer.data()),
        &bufferSize))
    {
        return false;
    }

    BYTE* current = buffer.data();
    BYTE* end = buffer.data() + bufferSize;

    while (current < end)
    {
        auto* info =
            reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(
                current);

        if (info->Size == 0 || current + info->Size > end)
            return false;

        if (info->Relationship == RelationCache)
        {
            CpuCacheInfo cache;

            cache.level = info->Cache.Level;
            cache.sizeKB = info->Cache.CacheSize / 1024;
            cache.lineSize = info->Cache.LineSize;
            cache.associativity = info->Cache.Associativity;

            snapshot_.caches.push_back(cache);
        }

        current += info->Size;
    }

    return true;
}

// ------------------------------------------------------------
// Virtualization
// ------------------------------------------------------------

bool CpuMonitor::DiscoverVirtualization()
{
    int registers[4]{};

    __cpuid(registers, 0);

    const unsigned int maxBasicLeaf =
        static_cast<unsigned int>(registers[0]);

    std::string vendor(12, '\0');

    memcpy(vendor.data(), &registers[1], 4);
    memcpy(vendor.data() + 4, &registers[3], 4);
    memcpy(vendor.data() + 8, &registers[2], 4);

    if (maxBasicLeaf >= 1)
    {
        __cpuid(registers, 1);

        const unsigned int ecx =
            static_cast<unsigned int>(registers[2]);

        snapshot_.virtualization.hypervisorPresent =
            (ecx & (1u << 31)) != 0;

        if (vendor == "GenuineIntel")
        {
            snapshot_.virtualization.supported =
                (ecx & (1u << 5)) != 0;
        }
        else if (vendor == "AuthenticAMD")
        {
            __cpuid(registers, 0x80000000);

            if (static_cast<unsigned int>(registers[0]) >= 0x80000001)
            {
                __cpuid(registers, 0x80000001);

                snapshot_.virtualization.supported =
                    (static_cast<unsigned int>(registers[2]) &
                        (1u << 2)) != 0;
            }
        }
    }

#ifdef PF_VIRT_FIRMWARE_ENABLED
    snapshot_.virtualization.firmwareEnabled =
        IsProcessorFeaturePresent(PF_VIRT_FIRMWARE_ENABLED) != FALSE;
#endif

    return true;
}

// ------------------------------------------------------------
// Initialize PDH
// ------------------------------------------------------------

bool CpuMonitor::InitializeCounters()
{
    PDH_STATUS status = PdhOpenQueryW(
        nullptr,
        0,
        &query_);

    if (status != ERROR_SUCCESS)
    {
        query_ = nullptr;
        return false;
    }

    // Uso de CPU por procesador lógico.

    status = PdhAddEnglishCounterW(
        query_,
        L"\\Processor Information(*)\\% Processor Time",
        0,
        &counter_);

    if (status != ERROR_SUCCESS)
    {
        counter_ = nullptr;
        return false;
    }

    // Frecuencia del procesador.

    status = PdhAddEnglishCounterW(
        query_,
        L"\\Processor Information(_Total)\\Processor Frequency",
        0,
        &frequencyCounter_);

    if (status != ERROR_SUCCESS)
        frequencyCounter_ = nullptr;

    // Rendimiento relativo del procesador.

    status = PdhAddEnglishCounterW(
        query_,
        L"\\Processor Information(_Total)\\% Processor Performance",
        0,
        &performanceCounter_);

    if (status != ERROR_SUCCESS)
        performanceCounter_ = nullptr;

    // Porcentaje respecto a frecuencia máxima.

    status = PdhAddEnglishCounterW(
        query_,
        L"\\Processor Information(_Total)\\% of Maximum Frequency",
        0,
        &maximumFrequencyCounter_);

    if (status != ERROR_SUCCESS)
        maximumFrequencyCounter_ = nullptr;
    for (const auto& processor : snapshot_.logicalProcessors)
    {
        const std::uint32_t key = MakeProcessorKey(
            processor.processorGroup,
            processor.logicalProcessorIndex
        );

        wchar_t counterPath[256]{};

        swprintf_s(
            counterPath,
            L"\\Processor Information(%u,%u)\\%% Processor Performance",
            processor.processorGroup,
            processor.logicalProcessorIndex
        );

        PDH_HCOUNTER processorCounter = nullptr;

        PDH_STATUS status = PdhAddEnglishCounterW(
            query_,
            counterPath,
            0,
            &processorCounter
        );

        if (status == ERROR_SUCCESS)
        {
            processorPerformanceCounters_[key] = processorCounter;
        }
    }
    status = PdhCollectQueryData(query_);

    if (status != ERROR_SUCCESS)
        return false;

    formattedBuffer_.resize(4096);

    return true;
}

// ------------------------------------------------------------
// Parse PDH processor instance
// ------------------------------------------------------------

bool CpuMonitor::ParseProcessorInstance(
    const wchar_t* name,
    WORD& group,
    BYTE& processor) const
{
    if (!name || !*name)
        return false;

    const wchar_t* current = name;

    unsigned int groupValue = 0;
    unsigned int processorValue = 0;

    if (*current < L'0' || *current > L'9')
        return false;

    while (*current >= L'0' && *current <= L'9')
    {
        groupValue =
            groupValue * 10 +
            static_cast<unsigned int>(*current - L'0');

        if (groupValue > 0xFFFF)
            return false;

        ++current;
    }

    if (*current++ != L',')
        return false;

    if (*current < L'0' || *current > L'9')
        return false;

    while (*current >= L'0' && *current <= L'9')
    {
        processorValue =
            processorValue * 10 +
            static_cast<unsigned int>(*current - L'0');

        if (processorValue > 0xFF)
            return false;

        ++current;
    }

    if (*current != L'\0')
        return false;

    group = static_cast<WORD>(groupValue);
    processor = static_cast<BYTE>(processorValue);

    return true;
}

// ------------------------------------------------------------
// CPU usage
// ------------------------------------------------------------

bool CpuMonitor::UpdateCpuUsage()
{
    if (!query_ || !counter_)
        return false;

    // Actualizar todos los contadores de la consulta.

    PDH_STATUS status = PdhCollectQueryData(query_);

    if (status != ERROR_SUCCESS)
        return false;

    DWORD bufferSize =
        static_cast<DWORD>(formattedBuffer_.size());

    DWORD itemCount = 0;

    status = PdhGetFormattedCounterArrayW(
        counter_,
        PDH_FMT_DOUBLE,
        &bufferSize,
        &itemCount,
        reinterpret_cast<PPDH_FMT_COUNTERVALUE_ITEM_W>(
            formattedBuffer_.data()));

    if (status == PDH_MORE_DATA)
    {
        if (bufferSize == 0)
            return false;

        formattedBuffer_.resize(bufferSize);

        DWORD retrySize =
            static_cast<DWORD>(formattedBuffer_.size());

        status = PdhGetFormattedCounterArrayW(
            counter_,
            PDH_FMT_DOUBLE,
            &retrySize,
            &itemCount,
            reinterpret_cast<PPDH_FMT_COUNTERVALUE_ITEM_W>(
                formattedBuffer_.data()));
    }

    if (status != ERROR_SUCCESS || itemCount == 0)
        return false;

    auto* items =
        reinterpret_cast<PPDH_FMT_COUNTERVALUE_ITEM_W>(
            formattedBuffer_.data());

    double totalUsage = 0.0;
    std::size_t validProcessors = 0;

    for (DWORD i = 0; i < itemCount; ++i)
    {
        const auto& item = items[i];

        if (item.FmtValue.CStatus != PDH_CSTATUS_VALID_DATA &&
            item.FmtValue.CStatus != PDH_CSTATUS_NEW_DATA)
            continue;

        WORD group = 0;
        BYTE processor = 0;

        if (!ParseProcessorInstance(
            item.szName,
            group,
            processor))
            continue;

        const std::uint32_t key =
            MakeProcessorKey(group, processor);

        const auto lookup = processorLookup_.find(key);

        if (lookup == processorLookup_.end())
            continue;

        const double usage = std::clamp(
            item.FmtValue.doubleValue,
            0.0,
            100.0);

        snapshot_.logicalProcessors[lookup->second].usagePercent =
            usage;

        totalUsage += usage;
        ++validProcessors;
    }

    if (validProcessors == 0)
        return false;

    snapshot_.totalUsagePercent =
        totalUsage / static_cast<double>(validProcessors);

    return true;
}

// ------------------------------------------------------------
// Frequency information
// ------------------------------------------------------------

bool CpuMonitor::UpdateFrequencyInfo()
{
    if (!frequencyCounter_)
        return false;

    PDH_FMT_COUNTERVALUE value{};

    PDH_STATUS status = PdhGetFormattedCounterValue(
        frequencyCounter_,
        PDH_FMT_LONG,
        nullptr,
        &value);

    if (status != ERROR_SUCCESS)
        return false;

    if (value.CStatus != PDH_CSTATUS_VALID_DATA &&
        value.CStatus != PDH_CSTATUS_NEW_DATA)
        return false;

    if (value.longValue <= 0)
        return false;

    snapshot_.frequency.baseMHz =
        static_cast<DWORD>(value.longValue);

#ifdef _DEBUG
    wchar_t debugText[128]{};

    swprintf_s(
        debugText,
        L"[CPU] PDH Frequency: %lu MHz\n",
        snapshot_.frequency.baseMHz
    );

    OutputDebugStringW(debugText);
#endif

    return true;
}

// ------------------------------------------------------------
// Processor Performance
// ------------------------------------------------------------

bool CpuMonitor::UpdatePerformanceCounter()
{
    if (processorPerformanceCounters_.empty())
        return false;

    double totalPerformance = 0.0;
    std::size_t validProcessors = 0;

    for (const auto& [key, counter] : processorPerformanceCounters_)
    {
        if (!counter)
            continue;

        PDH_FMT_COUNTERVALUE value{};

        PDH_STATUS status = PdhGetFormattedCounterValue(
            counter,
            PDH_FMT_DOUBLE,
            nullptr,
            &value
        );

        if (status != ERROR_SUCCESS)
            continue;

        if (value.CStatus != PDH_CSTATUS_VALID_DATA &&
            value.CStatus != PDH_CSTATUS_NEW_DATA)
            continue;

        const double performance = value.doubleValue;

        if (performance <= 0.0)
            continue;

        const auto lookup = processorLookup_.find(key);

        if (lookup == processorLookup_.end())
            continue;

        LogicalProcessorInfo& processor =
            snapshot_.logicalProcessors[lookup->second];

        // Rendimiento individual.
        processor.performancePercent = performance;

        // Frecuencia estimada individual.
        processor.estimatedFrequencyMHz =
            static_cast<double>(snapshot_.frequency.baseMHz) *
            (performance / 100.0);

        totalPerformance += performance;
        ++validProcessors;
    }

    if (validProcessors == 0)
        return false;

    // Rendimiento medio de los procesadores lógicos.
    const double averagePerformance =
        totalPerformance / static_cast<double>(validProcessors);

    // Rendimiento global.
    snapshot_.frequency.performancePercent = averagePerformance;

    // Frecuencia estimada global.
    snapshot_.frequency.estimatedMHz =
        static_cast<double>(snapshot_.frequency.baseMHz) *
        (averagePerformance / 100.0);

#ifdef _DEBUG
    wchar_t debugText[256]{};

    swprintf_s(
        debugText,
        L"[CPU] Average Processor Performance: %.2f%%\n"
        L"[CPU] Estimated Frequency: %.2f MHz (%.2f GHz)\n",
        averagePerformance,
        snapshot_.frequency.estimatedMHz,
        snapshot_.frequency.estimatedMHz / 1000.0
    );

    OutputDebugStringW(debugText);
#endif

    return true;
}

// ------------------------------------------------------------
// Maximum Frequency Percentage
// ------------------------------------------------------------

bool CpuMonitor::UpdateMaximumFrequencyCounter()
{
    if (!maximumFrequencyCounter_)
        return false;

    PDH_FMT_COUNTERVALUE value{};

    PDH_STATUS status = PdhGetFormattedCounterValue(
        maximumFrequencyCounter_,
        PDH_FMT_DOUBLE,
        nullptr,
        &value);

    if (status != ERROR_SUCCESS)
        return false;

    if (value.CStatus != PDH_CSTATUS_VALID_DATA &&
        value.CStatus != PDH_CSTATUS_NEW_DATA)
        return false;

    const double maximumFrequency = value.doubleValue;

    snapshot_.frequency.maximumFrequencyPercent =
        maximumFrequency;

    // "Processor Frequency" ya es una medida en MHz; no volver a
    // multiplicarla por % Processor Performance. effectiveMHz queda
    // como lectura real y estimatedMHz como estimacion relativa.
    if (maximumFrequency > 0.0 &&
        snapshot_.frequency.baseMHz > 0)
    {
        snapshot_.frequency.effectiveMHz =
            static_cast<double>(
                snapshot_.frequency.baseMHz) *
            maximumFrequency / 100.0;
    }

#ifdef _DEBUG
    wchar_t debugText[128]{};

    swprintf_s(
        debugText,
        L"[CPU] Maximum Frequency: %.2f%%\n",
        maximumFrequency);

    OutputDebugStringW(debugText);
#endif

    return true;
}

// ------------------------------------------------------------
// System activity
// ------------------------------------------------------------

bool CpuMonitor::UpdateSystemActivity()
{
    PERFORMANCE_INFORMATION performance{};

    performance.cb = sizeof(performance);

    if (!GetPerformanceInfo(&performance, sizeof(performance)))
        return false;

    snapshot_.activity.processCount =
        performance.ProcessCount;

    snapshot_.activity.threadCount =
        performance.ThreadCount;

    snapshot_.activity.handleCount =
        performance.HandleCount;

    return true;
}

// ------------------------------------------------------------
// Processes and threads
// ------------------------------------------------------------

bool CpuMonitor::UpdateProcessInformation()
{
    snapshot_.processes.clear();

    HANDLE processSnapshot = CreateToolhelp32Snapshot(
        TH32CS_SNAPPROCESS,
        0);

    if (processSnapshot == INVALID_HANDLE_VALUE)
        return false;

    PROCESSENTRY32W processEntry{};
    processEntry.dwSize = sizeof(processEntry);

    std::unordered_map<DWORD, std::size_t> processLookup;

    if (Process32FirstW(processSnapshot, &processEntry))
    {
        do
        {
            CpuProcessInfo process;

            process.processId = processEntry.th32ProcessID;
            process.name = processEntry.szExeFile;
            process.threadCount = processEntry.cntThreads;

            HANDLE handle = OpenProcess(
                PROCESS_QUERY_LIMITED_INFORMATION,
                FALSE,
                process.processId);

            if (handle)
            {
                DWORD handleCount = 0;

                if (GetProcessHandleCount(handle, &handleCount))
                    process.handleCount = handleCount;

                CloseHandle(handle);
            }

            const std::size_t index =
                snapshot_.processes.size();

            processLookup.emplace(process.processId, index);

            snapshot_.processes.push_back(std::move(process));

        } while (Process32NextW(processSnapshot, &processEntry));
    }

    CloseHandle(processSnapshot);

   
    return true;
}

// ------------------------------------------------------------
// Update
// ------------------------------------------------------------


bool CpuMonitor::Update()
{
    if (!initialized_)
        return false;

    // Métricas principales: cada actualización.
    const bool cpuUpdated = UpdateCpuUsage();

    UpdateFrequencyInfo();
    UpdatePerformanceCounter();
    UpdateMaximumFrequencyCounter();

    const auto now = std::chrono::steady_clock::now();

    // Actividad del sistema: cada 2 segundos.
    if (now >= nextActivityUpdate_)
    {
        UpdateSystemActivity();
        nextActivityUpdate_ = now + std::chrono::seconds(2);
    }

    // Información de procesos: cada 5 segundos.
    if (now >= nextProcessUpdate_)
    {
        UpdateProcessInformation();
        nextProcessUpdate_ = now + std::chrono::seconds(5);
    }

    return cpuUpdated;
}

// ------------------------------------------------------------
// Shutdown
// ------------------------------------------------------------

void CpuMonitor::Shutdown()
{
    initialized_ = false;

    if (query_)
    {
        PdhCloseQuery(query_);
        query_ = nullptr;
    }

    counter_ = nullptr;
    frequencyCounter_ = nullptr;
    performanceCounter_ = nullptr;
    maximumFrequencyCounter_ = nullptr;

    processorLookup_.clear();
    formattedBuffer_.clear();
    processorPerformanceCounters_.clear();
    snapshot_ = {};
}
