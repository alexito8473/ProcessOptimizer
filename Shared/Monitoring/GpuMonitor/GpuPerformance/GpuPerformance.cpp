
#include "GpuPerformance.h"
#include <PdhMsg.h>

#include <algorithm>
#include <cwchar>
#include <map>
#include <tuple>
#include "../GpuInfo.h"

#pragma comment(lib, "Pdh.lib")

namespace
{
    struct EngineKey
    {
        DWORD physicalEngine;
        DWORD engineIndex;
        GpuEngineType type;

        bool operator<(const EngineKey& other) const
        {
            return std::tie(
                physicalEngine,
                engineIndex,
                type
            ) < std::tie(
                other.physicalEngine,
                other.engineIndex,
                other.type
            );
        }
    };

    struct EngineValue
    {
        double usage = 0.0;
    };

    bool ParseLuid(
        const std::wstring& instance,
        DWORD& high,
        DWORD& low,
        DWORD& physical,
        DWORD& engine
    )
    {
        const wchar_t* luid = wcsstr(
            instance.c_str(),
            L"luid_0x"
        );

        if (!luid)
            return false;

        unsigned int parsedHigh = 0;
        unsigned int parsedLow = 0;
        unsigned int parsedPhysical = 0;
        unsigned int parsedEngine = 0;

        int matched = swscanf_s(
            luid,
            L"luid_0x%x_0x%x_phys_%u_eng_%u",
            &parsedHigh,
            &parsedLow,
            &parsedPhysical,
            &parsedEngine
        );

        if (matched != 4)
            return false;

        high = parsedHigh;
        low = parsedLow;
        physical = parsedPhysical;
        engine = parsedEngine;

        return true;
    }

    std::wstring ExtractInstance(
        const std::wstring& path
    )
    {
        const std::size_t begin = path.find(L'(');
        const std::size_t end = path.find(L')', begin);

        if (begin == std::wstring::npos ||
            end == std::wstring::npos)
            return {};

        return path.substr(
            begin + 1,
            end - begin - 1
        );
    }
}

GpuPerformance::~GpuPerformance()
{
    Shutdown();
}

void GpuPerformance::RebuildQuery()
{
    lastRebuild_ = std::chrono::steady_clock::now();

    Shutdown();

    PDH_STATUS status = PdhOpenQueryW(
        nullptr,
        0,
        &query_
    );

    if (status != ERROR_SUCCESS)
    {
        query_ = nullptr;
        return;
    }

    if (!EnumerateCounters())
    {
        Shutdown();
        return;
    }

    initialized_ = true;
    firstSample_ = true;
    consecutiveReadFailures_ = 0;
}

bool GpuPerformance::Initialize()
{
    RebuildQuery();
    return initialized_;
}

GpuEngineType GpuPerformance::ParseEngineType(
    const std::wstring& name
)
{
    const std::size_t pos = name.find(L"engtype_");

    if (pos == std::wstring::npos)
        return GpuEngineType::Unknown;

    const std::wstring type = name.substr(
        pos + 8
    );

    // El escritorio usa GDI Render y algunos motores no declaran tipo.
    // Descartarlos hacia que usagePercent quedara a 0 en equipos donde
    // solo esos motores estan activos.
    if (type.empty())
        return GpuEngineType::Other;

    if (type == L"GDI Render")
        return GpuEngineType::Graphics;

    if (type == L"3D")
        return GpuEngineType::ThreeD;

    if (type == L"Copy")
        return GpuEngineType::Copy;

    if (type == L"VideoDecode")
        return GpuEngineType::VideoDecode;

    if (type == L"VideoEncode")
        return GpuEngineType::VideoEncode;

    if (type == L"VideoProcessing")
        return GpuEngineType::VideoProcessing;

    if (type == L"Compute")
        return GpuEngineType::Compute;

    if (type == L"Graphics")
        return GpuEngineType::Graphics;

    if (type == L"Overlay")
        return GpuEngineType::Overlay;

    return GpuEngineType::Unknown;
}

bool GpuPerformance::EnumerateCounters()
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
        DWORD high = 0;
        DWORD low = 0;
        DWORD physical = 0;
        DWORD engine = 0;

        std::wstring instanceName(instance);

        if (!ParseLuid(
            instanceName,
            high,
            low,
            physical,
            engine
        ))
            continue;

        GpuEngineType type = ParseEngineType(
            instanceName
        );

        if (type == GpuEngineType::Unknown)
            continue;

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
        entry.luidHigh = high;
        entry.luidLow = low;
        entry.physicalEngine = physical;
        entry.engineIndex = engine;
        entry.type = type;
        entry.name = instanceName;

        counters_.push_back(std::move(entry));
    }

    return !counters_.empty();
}

bool GpuPerformance::Update(
    std::vector<GpuAdapterInfo>& adapters
)
{
    // GPU Engine puede no tener instancias cuando se inicializa el
    // servicio. Reintentar periodicamente en lugar de desactivar la
    // lectura para siempre.
    constexpr auto RebuildInterval = std::chrono::seconds(5);

    const auto now = std::chrono::steady_clock::now();
    const bool retryDue = now - lastRebuild_ >= RebuildInterval;

    if (!initialized_ ||
        (counters_.empty() && retryDue) ||
        (consecutiveReadFailures_ >= 3 && retryDue))
    {
        RebuildQuery();
    }

    for (auto& adapter : adapters)
        adapter.performanceAvailable = false;

    if (!initialized_ || !query_ || counters_.empty())
        return false;

    PDH_STATUS status = PdhCollectQueryData(query_);

    if (status != ERROR_SUCCESS)
    {
        ++consecutiveReadFailures_;
        return false;
    }

    consecutiveReadFailures_ = 0;

    // PDH necesita una primera recogida para
    // disponer de datos de los contadores.
    if (firstSample_)
    {
        firstSample_ = false;
        return false;
    }

    for (auto& adapter : adapters)
    {
        adapter.engines.clear();

        adapter.usagePercent = 0.0;
        adapter.usage3D = 0.0;
        adapter.usageCopy = 0.0;
        adapter.usageVideoDecode = 0.0;
        adapter.usageVideoEncode = 0.0;
        adapter.usageVideoProcessing = 0.0;
        adapter.usageCompute = 0.0;

        const DWORD high =
            static_cast<DWORD>(adapter.luidHighPart);
        const DWORD low = adapter.luidLowPart;

        std::map<EngineKey, EngineValue> physicalEngines;

        for (const CounterEntry& entry : counters_)
        {
            // El formato del contador no garantiza el orden de las
            // partes del LUID. Aceptar ambos evita que todos los
            // contadores queden sin emparejar con el adaptador DXGI.
            const bool luidMatches =
                (entry.luidHigh == high &&
                 entry.luidLow == low) ||
                (entry.luidHigh == low &&
                 entry.luidLow == high);

            if (!luidMatches)
                continue;

            PDH_FMT_COUNTERVALUE value{};

            status = PdhGetFormattedCounterValue(
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

            adapter.performanceAvailable = true;

            double usage = value.doubleValue;

            if (usage < 0.0)
                usage = 0.0;

            if (usage > 100.0)
                usage = 100.0;

            EngineKey key{
                entry.physicalEngine,
                entry.engineIndex,
                entry.type
            };

            // Los contadores por proceso pueden
            // representar partes del mismo motor.
            physicalEngines[key].usage += usage;
        }

        std::map<GpuEngineType, double> typeUsage;

        for (const auto& [key, value] : physicalEngines)
        {
            double usage = value.usage;

            if (usage > 100.0)
                usage = 100.0;

            auto& current = typeUsage[key.type];

            // Entre motores físicos equivalentes
            // usamos el máximo, no sumamos porcentajes.
            if (usage > current)
                current = usage;
        }

        for (const auto& [type, usage] : typeUsage)
        {
            GpuEngineInfo engine{};

            engine.type = type;
            engine.usagePercent = usage;

            switch (type)
            {
            case GpuEngineType::ThreeD:
                engine.name = L"3D";
                adapter.usage3D = usage;
                break;

            case GpuEngineType::Copy:
                engine.name = L"Copy";
                adapter.usageCopy = usage;
                break;

            case GpuEngineType::VideoDecode:
                engine.name = L"Video Decode";
                adapter.usageVideoDecode = usage;
                break;

            case GpuEngineType::VideoEncode:
                engine.name = L"Video Encode";
                adapter.usageVideoEncode = usage;
                break;

            case GpuEngineType::VideoProcessing:
                engine.name = L"Video Processing";
                adapter.usageVideoProcessing = usage;
                break;

            case GpuEngineType::Compute:
                engine.name = L"Compute";
                adapter.usageCompute = usage;
                break;

            case GpuEngineType::Graphics:
                engine.name = L"Graphics";
                break;

            case GpuEngineType::Overlay:
                engine.name = L"Overlay";
                break;

            default:
                engine.name = L"Other";
                break;
            }

            adapter.engines.push_back(std::move(engine));

            if (usage > adapter.usagePercent)
                adapter.usagePercent = usage;
        }
    }

    return true;
}

void GpuPerformance::Shutdown()
{
    counters_.clear();

    if (query_)
    {
        PdhCloseQuery(query_);
        query_ = nullptr;
    }

    initialized_ = false;
    firstSample_ = true;
    consecutiveReadFailures_ = 0;
}

bool GpuPerformance::IsInitialized() const
{
    return initialized_;
}
