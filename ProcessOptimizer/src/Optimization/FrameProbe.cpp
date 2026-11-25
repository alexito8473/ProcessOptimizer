#define NOMINMAX
#include "FrameProbe.h"

#include <tdh.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <numeric>

namespace Optimization
{
    namespace
    {
        // Microsoft-Windows-DXGI (PresentMon usa el mismo proveedor).
        static const GUID DxgiProviderGuid =
            { 0xCA11C036, 0x0102, 0x4A2D, 0xA6, 0xAD,
              0xF0, 0x3C, 0xFE, 0xD5, 0xD3, 0xC9 };

        // Sesion propia: no afecta a PresentMon ni a otras sesiones ETW.
        static const GUID ProbeSessionGuid =
            { 0x9D7E6F21, 0x4C31, 0x4D0A,
              { 0x9B, 0x12, 0x77, 0xC1,
                0xF2, 0xA3, 0xD4, 0xE5 } };

        constexpr wchar_t SessionName[] =
            L"ProcessOptimizer-FrameProbe";

        constexpr ULONGLONG DxgiEventKeyword =
            0x8000000000000002ULL;

        constexpr USHORT PresentStartId = 0x002a;
        constexpr USHORT PresentStopId = 0x002b;
        constexpr USHORT PresentMpoStartId = 0x0037;
        constexpr USHORT PresentMpoStopId = 0x0038;

        constexpr std::uint32_t DxgiPresentTest = 0x1;
        constexpr std::size_t MaxSamples = 4096;
        constexpr std::uint64_t WindowSeconds = 5;

        // El vector ya viene ordenado: calcular los tres percentiles sobre
        // la misma copia evita dos ordenaciones adicionales por segundo.
        double Percentile(
            const std::vector<double>& sorted,
            double fraction)
        {
            if (sorted.empty())
                return 0.0;

            const double position =
                fraction *
                static_cast<double>(sorted.size() - 1);

            const std::size_t lower =
                static_cast<std::size_t>(std::floor(position));
            const std::size_t upper =
                (std::min)(
                    sorted.size() - 1,
                    lower + 1);

            const double weight = position - lower;

            return sorted[lower] * (1.0 - weight) +
                   sorted[upper] * weight;
        }
    }

    FrameProbe::FrameProbe(Publisher publisher)
        : publisher_(std::move(publisher))
    {
    }

    FrameProbe::~FrameProbe()
    {
        Stop();
    }

    bool FrameProbe::Start()
    {
        std::lock_guard<std::mutex> lifecycle(
            lifecycleMutex_);

        if (stopRequested_.load())
            return false;

        // Sin objetivo no hay eventos que analizar: no abrir una sesion
        // ETW que solo consumiria CPU mientras el juego no esta en primer
        // plano. SetTargetProcess() inicia la captura cuando haya PID.
        if (targetPid_.load() == 0)
        {
            FrameProbeSnapshot idle{};
            idle.statusText = "Sin objetivo de frame time";
            PublishSnapshot(idle);
            return true;
        }

        if (running_.exchange(true))
            return true;

        LARGE_INTEGER frequency{};
        if (!QueryPerformanceFrequency(&frequency) ||
            frequency.QuadPart <= 0)
        {
            running_.store(false);
            return false;
        }
        qpcFrequency_ =
            static_cast<std::uint64_t>(frequency.QuadPart);

        const std::size_t nameBytes =
            (wcslen(SessionName) + 1) * sizeof(wchar_t);
        std::vector<std::uint8_t> propertiesBuffer(
            sizeof(EVENT_TRACE_PROPERTIES) + nameBytes, 0);

        auto* properties =
            reinterpret_cast<EVENT_TRACE_PROPERTIES*>(
                propertiesBuffer.data());

        properties->Wnode.BufferSize =
            static_cast<ULONG>(propertiesBuffer.size());
        properties->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
        properties->Wnode.Guid = ProbeSessionGuid;
        properties->Wnode.ClientContext = 1; // QPC, como PresentMon.
        properties->LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
        properties->LoggerNameOffset =
            sizeof(EVENT_TRACE_PROPERTIES);

        ULONG status = StartTraceW(
            &traceHandle_,
            SessionName,
            properties);

        if (status == ERROR_ALREADY_EXISTS)
        {
            // Resto de una sesion anterior tras un crash del servicio.
            ControlTraceW(
                0, SessionName, properties,
                EVENT_TRACE_CONTROL_STOP);
            status = StartTraceW(
                &traceHandle_,
                SessionName,
                properties);
        }

        if (status != ERROR_SUCCESS)
        {
            traceHandle_ = 0;
            running_.store(false);

            FrameProbeSnapshot error{};
            error.active = false;
            error.statusText =
                "No se pudo iniciar la captura de frames";
            PublishSnapshot(error);
            return false;
        }

        // Filtro por ID: el proveedor DXGI solo entrega Present y
        // PresentMultiplaneOverlay. Recibir menos eventos baja el coste
        // del servicio sin perder precisión.
        constexpr USHORT FilterEventIds[] = {
            PresentStartId, PresentStopId,
            PresentMpoStartId, PresentMpoStopId };

        constexpr std::size_t filterCount =
            sizeof(FilterEventIds) / sizeof(FilterEventIds[0]);

        const std::size_t filterBytes =
            offsetof(EVENT_FILTER_EVENT_ID, Events) +
            sizeof(USHORT) * filterCount;

        std::vector<std::uint8_t> filterBuffer(
            filterBytes, 0);

        auto* filterHeader =
            reinterpret_cast<EVENT_FILTER_EVENT_ID*>(
                filterBuffer.data());
        filterHeader->FilterIn = TRUE;
        filterHeader->Count =
            static_cast<ULONG>(filterCount);

        auto* filterIds =
            reinterpret_cast<USHORT*>(
                filterBuffer.data() +
                offsetof(EVENT_FILTER_EVENT_ID, Events));

        for (std::size_t i = 0; i < filterCount; ++i)
            filterIds[i] = FilterEventIds[i];

        EVENT_FILTER_DESCRIPTOR filterDescriptor{};
        filterDescriptor.Ptr =
            reinterpret_cast<ULONGLONG>(filterBuffer.data());
        filterDescriptor.Size =
            static_cast<ULONG>(filterBuffer.size());
        filterDescriptor.Type = EVENT_FILTER_TYPE_EVENT_ID;

        ENABLE_TRACE_PARAMETERS enableParameters{};
        enableParameters.Version =
            ENABLE_TRACE_PARAMETERS_VERSION_2;
        enableParameters.FilterDescCount = 1;
        enableParameters.EnableFilterDesc = &filterDescriptor;

        status = EnableTraceEx2(
            traceHandle_,
            &DxgiProviderGuid,
            EVENT_CONTROL_CODE_ENABLE_PROVIDER,
            0,
            DxgiEventKeyword,
            0,
            0,
            &enableParameters);

        if (status != ERROR_SUCCESS)
        {
            // Windows antiguo o proveedor sin filtro por ID: seguir sin
            // filtro antes que renunciar a la telemetría.
            enableParameters.FilterDescCount = 0;
            enableParameters.EnableFilterDesc = nullptr;

            status = EnableTraceEx2(
                traceHandle_,
                &DxgiProviderGuid,
                EVENT_CONTROL_CODE_ENABLE_PROVIDER,
                0,
                DxgiEventKeyword,
                0,
                0,
                &enableParameters);
        }

        if (status != ERROR_SUCCESS)
        {
            ControlTraceW(
                traceHandle_, nullptr, properties,
                EVENT_TRACE_CONTROL_STOP);
            traceHandle_ = 0;
            running_.store(false);

            FrameProbeSnapshot error{};
            error.statusText =
                "No se pudo suscribir al proveedor DXGI";
            PublishSnapshot(error);
            return false;
        }

        EVENT_TRACE_LOGFILEW logFile{};
        logFile.LoggerName =
            const_cast<wchar_t*>(SessionName);
        logFile.ProcessTraceMode =
            PROCESS_TRACE_MODE_REAL_TIME |
            PROCESS_TRACE_MODE_EVENT_RECORD |
            PROCESS_TRACE_MODE_RAW_TIMESTAMP;
        logFile.EventRecordCallback =
            &FrameProbe::EventRecordCallback;
        logFile.Context = this;

        consumerHandle_ = OpenTraceW(&logFile);

        if (consumerHandle_ == INVALID_PROCESSTRACE_HANDLE)
        {
            enableParameters.Version =
                ENABLE_TRACE_PARAMETERS_VERSION_2;
            EnableTraceEx2(
                traceHandle_,
                &DxgiProviderGuid,
                EVENT_CONTROL_CODE_DISABLE_PROVIDER,
                0,
                DxgiEventKeyword,
                0,
                0,
                &enableParameters);
            ControlTraceW(
                traceHandle_, nullptr, properties,
                EVENT_TRACE_CONTROL_STOP);
            traceHandle_ = 0;
            running_.store(false);

            FrameProbeSnapshot error{};
            error.statusText =
                "No se pudo abrir el consumidor ETW";
            PublishSnapshot(error);
            return false;
        }

        consumerThread_ = std::thread(
            [this]()
            {
                TRACEHANDLE handle = consumerHandle_;
                ProcessTrace(
                    &handle,
                    1,
                    nullptr,
                    nullptr);
            });

        aggregateThread_ = std::thread(
            [this]() { AggregateLoop(); });

        return true;
    }

    void FrameProbe::Stop()
    {
        stopRequested_.store(true);

        std::lock_guard<std::mutex> lifecycle(
            lifecycleMutex_);

        if (!running_.exchange(false))
            return;

        ENABLE_TRACE_PARAMETERS enableParameters{};
        enableParameters.Version =
            ENABLE_TRACE_PARAMETERS_VERSION_2;

        if (traceHandle_ != 0)
        {
            EnableTraceEx2(
                traceHandle_,
                &DxgiProviderGuid,
                EVENT_CONTROL_CODE_DISABLE_PROVIDER,
                0, 0, 0, 0,
                &enableParameters);
        }

        const std::size_t nameBytes =
            (wcslen(SessionName) + 1) * sizeof(wchar_t);
        std::vector<std::uint8_t> propertiesBuffer(
            sizeof(EVENT_TRACE_PROPERTIES) + nameBytes, 0);

        auto* properties =
            reinterpret_cast<EVENT_TRACE_PROPERTIES*>(
                propertiesBuffer.data());
        properties->Wnode.BufferSize =
            static_cast<ULONG>(propertiesBuffer.size());
        properties->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
        properties->LoggerNameOffset =
            sizeof(EVENT_TRACE_PROPERTIES);

        // Primero se detiene la sesion y se cierra el consumidor; hasta
        // entonces ProcessTrace() no puede salir y un join previo bloquearia
        // el apagado del servicio.
        ControlTraceW(
            traceHandle_, nullptr, properties,
            EVENT_TRACE_CONTROL_STOP);
        traceHandle_ = 0;

        if (consumerHandle_ != INVALID_PROCESSTRACE_HANDLE)
        {
            CloseTrace(consumerHandle_);
            consumerHandle_ =
                INVALID_PROCESSTRACE_HANDLE;
        }

        if (consumerThread_.joinable())
            consumerThread_.join();
        if (aggregateThread_.joinable())
            aggregateThread_.join();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            samples_.clear();
            lastStartByThread_.clear();
            startByThread_.clear();
            pendingCpuByThread_.clear();
            snapshot_ = FrameProbeSnapshot{};
        }
    }

    void FrameProbe::SetTargetProcess(DWORD processId)
    {
        const DWORD previous = targetPid_.exchange(processId);

        if (previous == processId)
            return;

        FrameProbeSnapshot reset{};
        {
            std::lock_guard<std::mutex> lock(mutex_);
            samples_.clear();
            lastStartByThread_.clear();
            startByThread_.clear();
            pendingCpuByThread_.clear();

            // active=true aunque no haya objetivo: asi MonitorState no
            // conserva las metricas del juego anterior y la UI puede
            // mostrar el estado "sin objetivo" de inmediato.
            reset.active = true;
            reset.processId = processId;
            reset.lastUpdateMs = GetTickCount64();
            reset.verdict = FrameVerdict::NoData;
            snapshot_ = reset;
        }
        PublishSnapshot(reset);

        if (processId != 0 && !running_.load())
            Start();
    }

    void FrameProbe::SetGpuUsagePercent(double usagePercent)
    {
        gpuUsagePercent_.store(
            (std::max)(0.0, (std::min)(usagePercent, 100.0)));
    }

    FrameProbeSnapshot FrameProbe::Snapshot() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return snapshot_;
    }

    double FrameProbe::TicksToMs(std::uint64_t ticks) const
    {
        return static_cast<double>(ticks) * 1000.0 /
               static_cast<double>(
                   (std::max<std::uint64_t>(qpcFrequency_, 1)));
    }

    bool FrameProbe::GetNumericProperty(
        EVENT_RECORD* record,
        const wchar_t* name,
        std::uint64_t& value) const
    {
        PROPERTY_DATA_DESCRIPTOR descriptor{};
        descriptor.PropertyName =
            reinterpret_cast<ULONGLONG>(name);
        descriptor.ArrayIndex = ULONG_MAX;

        ULONG size = 0;
        if (TdhGetPropertySize(
                record, 0, nullptr,
                1, &descriptor, &size) != ERROR_SUCCESS ||
            size == 0 || size > sizeof(std::uint64_t))
        {
            return false;
        }

        std::uint8_t buffer[sizeof(std::uint64_t)]{};
        if (TdhGetProperty(
                record, 0, nullptr,
                1, &descriptor,
                static_cast<ULONG>(sizeof(buffer)),
                buffer) != ERROR_SUCCESS)
        {
            return false;
        }

        if (size == sizeof(std::uint32_t))
        {
            std::uint32_t narrow = 0;
            std::memcpy(&narrow, buffer, sizeof(narrow));
            value = narrow;
        }
        else
        {
            std::uint64_t wide = 0;
            std::memcpy(&wide, buffer, sizeof(wide));
            value = wide;
        }

        return true;
    }

    VOID WINAPI FrameProbe::EventRecordCallback(
        EVENT_RECORD* record)
    {
        FrameProbe* instance =
            record == nullptr
                ? nullptr
                : static_cast<FrameProbe*>(
                      record->UserContext);

        if (instance != nullptr)
            instance->OnEvent(record);
    }

    void FrameProbe::OnEvent(EVENT_RECORD* record)
    {
        if (record == nullptr || !running_.load())
            return;

        const DWORD pid = record->EventHeader.ProcessId;
        if (pid == 0 || pid != targetPid_.load())
            return;

        const USHORT id = record->EventHeader.EventDescriptor.Id;
        if (!IsEqualGUID(
                record->EventHeader.ProviderId,
                DxgiProviderGuid))
        {
            return;
        }

        const bool isStart =
            id == PresentStartId || id == PresentMpoStartId;
        const bool isStop =
            id == PresentStopId || id == PresentMpoStopId;

        if (!isStart && !isStop)
            return;

        const DWORD threadId = record->EventHeader.ThreadId;
        const std::uint64_t stamp =
            static_cast<std::uint64_t>(
                record->EventHeader.TimeStamp.QuadPart);

        if (isStart)
        {
            std::uint64_t flagsValue = 0;
            std::uint64_t syncValue = 0;

            GetNumericProperty(
                record, L"Flags", flagsValue);
            GetNumericProperty(
                record, L"SyncInterval", syncValue);

            const std::uint32_t flags =
                static_cast<std::uint32_t>(flagsValue);

            if ((flags & DxgiPresentTest) != 0)
                return;

            double cpuMs = 0.0;
            std::int32_t syncInterval = 0;

            {
                std::lock_guard<std::mutex> lock(mutex_);

                auto pending =
                    pendingCpuByThread_.find(threadId);
                if (pending != pendingCpuByThread_.end())
                {
                    cpuMs = pending->second;
                    pendingCpuByThread_.erase(pending);
                }

                const auto last =
                    lastStartByThread_.find(threadId);
                bool sampleAdded = false;

                if (last != lastStartByThread_.end() &&
                    stamp > last->second)
                {
                    const double intervalMs =
                        TicksToMs(stamp - last->second);

                    if (intervalMs >= 1.0 &&
                        intervalMs <= 500.0 &&
                        samples_.size() < MaxSamples)
                    {
                        Sample sample{};
                        sample.endTick = stamp;
                        sample.intervalMs = intervalMs;
                        sample.cpuInPresentMs = cpuMs;
                        sample.syncInterval = 0;
                        sample.flags = flags;

                        samples_.push_back(sample);
                        sampleAdded = true;
                    }
                }

                lastStartByThread_[threadId] = stamp;
                startByThread_[threadId] = stamp;

                syncInterval = static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(syncValue));
                if (sampleAdded)
                    samples_.back().syncInterval =
                        (std::max)(0, syncInterval);
            }

            return;
        }

        std::lock_guard<std::mutex> lock(mutex_);

        const auto start = startByThread_.find(threadId);
        if (start == startByThread_.end() ||
            stamp <= start->second)
        {
            return;
        }

        pendingCpuByThread_[threadId] =
            TicksToMs(stamp - start->second);
        startByThread_.erase(start);
    }

    void FrameProbe::AggregateLoop()
    {
        while (running_.load())
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(1000));

            if (!running_.load())
                break;

            const DWORD pid = targetPid_.load();
            if (pid == 0)
                continue;

            LARGE_INTEGER nowQpc{};
            QueryPerformanceCounter(&nowQpc);
            const std::uint64_t cutoff =
                static_cast<std::uint64_t>(
                    nowQpc.QuadPart) -
                qpcFrequency_ * WindowSeconds;

            std::vector<double> intervals;
            std::vector<double> cpuTimes;
            std::uint32_t sampleCount = 0;
            std::int32_t maxSync = 0;
            double maxInterval = 0.0;

            {
                std::lock_guard<std::mutex> lock(mutex_);

                samples_.erase(
                    std::remove_if(
                        samples_.begin(), samples_.end(),
                        [cutoff](const Sample& sample)
                        {
                            return sample.endTick < cutoff;
                        }),
                    samples_.end());

                intervals.reserve(samples_.size());
                cpuTimes.reserve(samples_.size());

                for (const Sample& sample : samples_)
                {
                    intervals.push_back(sample.intervalMs);
                    cpuTimes.push_back(sample.cpuInPresentMs);
                    maxSync = (std::max)(
                        maxSync, sample.syncInterval);
                    maxInterval = (std::max)(
                        maxInterval, sample.intervalMs);
                }

                sampleCount =
                    static_cast<std::uint32_t>(samples_.size());
            }

            FrameProbeSnapshot result{};
            result.active = true;
            result.processId = pid;
            result.lastUpdateMs = GetTickCount64();
            result.sampleCount = sampleCount;
            result.maxMs = maxInterval;
            result.syncInterval = maxSync;

            if (sampleCount < 30 || intervals.empty())
            {
                result.verdict = FrameVerdict::NoData;
                result.statusText =
                    "Midiendo frame time...";
                PublishSnapshot(result);
                continue;
            }

            std::sort(intervals.begin(), intervals.end());

            const double p50 = Percentile(intervals, 0.50);
            const double p95 = Percentile(intervals, 0.95);
            const double p99 = Percentile(intervals, 0.99);

            result.p50Ms = p50;
            result.p95Ms = p95;
            result.p99Ms = p99;
            result.fps =
                p50 > 0.0 ? 1000.0 / p50 : 0.0;
            result.avgCpuInPresentMs =
                std::accumulate(
                    cpuTimes.begin(), cpuTimes.end(), 0.0) /
                static_cast<double>(cpuTimes.size());

            const double gpuUsage =
                gpuUsagePercent_.load();

            if (maxSync > 0 && p50 >= 15.0 &&
                p95 - p50 < 4.0)
            {
                result.verdict = FrameVerdict::Capped;
                result.statusText =
                    "Posible cap/VSync: no intervenir";
            }
            else if (gpuUsage >= 88.0 && p95 > 20.0)
            {
                result.verdict = FrameVerdict::GpuBound;
                result.statusText =
                    "GPU al limite: no intervenir";
            }
            else if (result.avgCpuInPresentMs >= 6.0 &&
                     p95 > 20.0)
            {
                result.verdict = FrameVerdict::CpuBound;
                result.statusText =
                    "CPU-bound: revisar antes de tocar hilos";
            }
            else if (p95 <= 20.0 && p99 <= 25.0)
            {
                result.verdict = FrameVerdict::Stable;
                result.statusText =
                    "Frame time estable: Windows decide";
            }
            else
            {
                result.verdict = FrameVerdict::Unstable;
                result.statusText =
                    "Stutter detectado: medir A/B antes de actuar";
            }

            PublishSnapshot(result);
        }
    }

    void FrameProbe::PublishSnapshot(
        const FrameProbeSnapshot& snapshot)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            snapshot_ = snapshot;
        }

        if (publisher_)
            publisher_(snapshot);
    }
}
