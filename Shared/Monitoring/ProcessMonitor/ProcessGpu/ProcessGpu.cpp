
#include "ProcessGpu.h"
#include <PdhMsg.h>

#include <cwchar>
#include <map>
#include <tuple>

#pragma comment(lib, "Pdh.lib")

namespace
{
    bool ParseInstance(
        const std::wstring& instance,
        DWORD& processId,
        DWORD& luidHigh,
        DWORD& luidLow,
        DWORD& physicalEngine,
        DWORD& engineIndex)
    {
        unsigned int pid = 0;
        unsigned int high = 0;
        unsigned int low = 0;
        unsigned int physical = 0;
        unsigned int engine = 0;

        const int matched = swscanf_s(
            instance.c_str(),
            L"pid_%u_luid_0x%x_0x%x_phys_%u_eng_%u",
            &pid,
            &high,
            &low,
            &physical,
            &engine
        );

        if (matched != 5)
            return false;

        processId = pid;
        luidHigh = high;
        luidLow = low;
        physicalEngine = physical;
        engineIndex = engine;

        return true;
    }

    std::wstring ExtractInstance(const std::wstring& path)
    {
        const std::size_t begin = path.find(L'(');
        const std::size_t end = path.find(L')', begin);

        if (begin == std::wstring::npos ||
            end == std::wstring::npos)
        {
            return {};
        }

        return path.substr(begin + 1, end - begin - 1);
    }

    std::wstring ExtractEngineType(const std::wstring& instance)
    {
        const std::size_t pos = instance.find(L"engtype_");

        if (pos == std::wstring::npos)
            return L"Unknown";

        return instance.substr(pos + 8);
    }
}

ProcessGpu::~ProcessGpu()
{
    Shutdown();
}

bool ProcessGpu::Initialize()
{
    Shutdown();

    if (PdhOpenQueryW(nullptr, 0, &query_) != ERROR_SUCCESS)
        return false;

    if (!EnumerateCounters())
    {
        Shutdown();
        return false;
    }

    initialized_ = true;
    firstSample_ = true;

    return true;
}

bool ProcessGpu::EnumerateCounters()
{
    DWORD counterSize = 0;
    DWORD instanceSize = 0;

    PDH_STATUS status = PdhEnumObjectItemsW(
        nullptr,
        nullptr,
        L"GPU Engine",
        nullptr,
        &counterSize,
        nullptr,
        &instanceSize,
        PERF_DETAIL_WIZARD,
        0
    );

    if (status != PDH_MORE_DATA)
        return false;

    std::vector<wchar_t> counters(counterSize);
    std::vector<wchar_t> instances(instanceSize);

    status = PdhEnumObjectItemsW(
        nullptr,
        nullptr,
        L"GPU Engine",
        counters.data(),
        &counterSize,
        instances.data(),
        &instanceSize,
        PERF_DETAIL_WIZARD,
        0
    );

    if (status != ERROR_SUCCESS)
        return false;

    for (const wchar_t* instance = instances.data();
        *instance;
        instance += wcslen(instance) + 1)
    {
        std::wstring instanceName(instance);

        DWORD pid = 0;
        DWORD high = 0;
        DWORD low = 0;
        DWORD physical = 0;
        DWORD engine = 0;

        if (!ParseInstance(
            instanceName,
            pid,
            high,
            low,
            physical,
            engine))
        {
            continue;
        }

        std::wstring path =
            L"\\GPU Engine(" +
            instanceName +
            L")\\Utilization Percentage";

        PDH_HCOUNTER counter = nullptr;

        status = PdhAddEnglishCounterW(
            query_,
            path.c_str(),
            0,
            &counter
        );

        if (status != ERROR_SUCCESS)
            continue;

        CounterEntry entry{};

        entry.counter = counter;
        entry.processId = pid;
        entry.luidHigh = high;
        entry.luidLow = low;
        entry.physicalEngine = physical;
        entry.engineIndex = engine;
        entry.engineName = ExtractEngineType(instanceName);

        counters_.push_back(std::move(entry));
    }

    return !counters_.empty();
}

bool ProcessGpu::Update()
{
    if (!initialized_ || !query_)
        return false;

    if (PdhCollectQueryData(query_) != ERROR_SUCCESS)
        return false;

    if (firstSample_)
    {
        // La primera lectura solo establece la línea base de PDH. No es un
        // fallo: devolvemos éxito con uso vacío para que ProcessMonitor no
        // destruya la query durante el warm-up.
        firstSample_ = false;
        usage_.clear();
        return true;
    }

    usage_.clear();

    for (const CounterEntry& entry : counters_)
    {
        PDH_FMT_COUNTERVALUE value{};

        const PDH_STATUS status = PdhGetFormattedCounterValue(
            entry.counter,
            PDH_FMT_DOUBLE,
            nullptr,
            &value
        );

        if (status != ERROR_SUCCESS ||
            value.CStatus != ERROR_SUCCESS)
        {
            continue;
        }

        ProcessGpuEngineUsage item{};

        item.processId = entry.processId;
        item.luidLow = entry.luidLow;
        item.luidHigh = entry.luidHigh;
        item.physicalEngine = entry.physicalEngine;
        item.engineIndex = entry.engineIndex;
        item.engineName = entry.engineName;

        item.usagePercent = value.doubleValue;

        if (item.usagePercent < 0.0)
            item.usagePercent = 0.0;

        if (item.usagePercent > 100.0)
            item.usagePercent = 100.0;

        usage_.push_back(std::move(item));
    }

    return true;
}

void ProcessGpu::Shutdown()
{
    counters_.clear();
    usage_.clear();

    if (query_)
    {
        PdhCloseQuery(query_);
        query_ = nullptr;
    }

    initialized_ = false;
    firstSample_ = true;
}

bool ProcessGpu::IsInitialized() const
{
    return initialized_;
}

const std::vector<ProcessGpuEngineUsage>& ProcessGpu::GetUsage() const
{
    return usage_;
}
