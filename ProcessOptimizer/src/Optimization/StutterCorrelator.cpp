#include "StutterCorrelator.h"

#include <PdhMsg.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>

namespace Optimization
{
    namespace
    {
        constexpr wchar_t EtwSessionName[] =
            L"ProcessOptimizer-StutterProbe";

        std::string WideToUtf8(const std::wstring& value)
        {
            if (value.empty())
                return {};

            const int size = WideCharToMultiByte(
                CP_UTF8, 0, value.data(),
                static_cast<int>(value.size()),
                nullptr, 0, nullptr, nullptr);
            if (size <= 0)
                return {};

            std::string result(static_cast<std::size_t>(size), '\0');
            WideCharToMultiByte(
                CP_UTF8, 0, value.data(),
                static_cast<int>(value.size()),
                result.data(), size, nullptr, nullptr);
            return result;
        }
    }

    StutterCorrelator::StutterCorrelator(Publisher publisher)
        : publisher_(std::move(publisher))
    {
    }

    StutterCorrelator::~StutterCorrelator()
    {
        Stop();
    }

    bool StutterCorrelator::Start()
    {
        if (started_)
            return true;

        if (PdhOpenQueryW(nullptr, 0, &query_) != ERROR_SUCCESS)
            return false;

        counterNames_ = {
            L"\\System\\Context Switches/sec",
            L"\\Memory\\Pages Input/sec",
            L"\\Memory\\Cache Faults/sec",
            L"\\Processor(_Total)\\% DPC Time",
            L"\\Processor(_Total)\\% Interrupt Time"
        };

        for (const auto& name : counterNames_)
        {
            PDH_HCOUNTER counter = nullptr;
            if (PdhAddEnglishCounterW(
                    query_, name.c_str(), 0, &counter) != ERROR_SUCCESS)
            {
                PdhCloseQuery(query_);
                query_ = nullptr;
                counterNames_.clear();
                return false;
            }
            counters_.push_back(counter);
        }

        // La primera lectura inicializa el contador; la segunda da la tasa.
        PdhCollectQueryData(query_);
        PdhCollectQueryData(query_);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            started_ = true;
            snapshot_.available = true;
            snapshot_.status =
                "Correlacion ligera activa; ETW solo bajo demanda";
        }
        Publish();
        return true;
    }

    void StutterCorrelator::Stop()
    {
        SetEtwEnabled(false);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            started_ = false;
            snapshot_.available = false;
            snapshot_.etwActive = false;
            snapshot_.status = "Correlacion detenida";
        }

        if (query_)
        {
            PdhCloseQuery(query_);
            query_ = nullptr;
        }
        counters_.clear();
        Publish();
    }

    bool StutterCorrelator::SetEtwEnabled(bool enabled)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (enabled == etwEnabled_)
                return true;
            etwEnabled_ = enabled;
        }

        if (enabled)
        {
            if (!StartEtw())
            {
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    etwEnabled_ = false;
                    snapshot_.etwActive = false;
                    snapshot_.status =
                        "No se pudo iniciar ETW; puede haber otra sesion activa";
                }
                Publish();
                return false;
            }
        }
        else
        {
            StopEtw();
        }

        Publish();
        return true;
    }

    void StutterCorrelator::SetProcessSnapshot(
        const ProcessSnapshot& snapshot)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        processNames_.clear();
        for (const auto& process : snapshot.processes)
            processNames_[process.processId] = process.name;
    }

    void StutterCorrelator::Tick(const FrameProbeSnapshot& frame)
    {
        if (!started_)
            return;

        // Un solo collection por tick: ReadCounter solo formatea.
        if (query_ && PdhCollectQueryData(query_) != ERROR_SUCCESS)
            return;

        const double context = ReadCounter(
            static_cast<std::size_t>(Counter::ContextSwitches));
        const double pages = ReadCounter(
            static_cast<std::size_t>(Counter::PagesInput));
        const double cache = ReadCounter(
            static_cast<std::size_t>(Counter::CacheFaults));
        const double dpc = ReadCounter(
            static_cast<std::size_t>(Counter::Dpc));
        const double interrupt = ReadCounter(
            static_cast<std::size_t>(Counter::Interrupt));

        const bool badFrame = frame.active &&
            (frame.p95Ms > 20.0 || frame.framesOver25Ms > 0);
        const bool contextSpike = averageContext_ > 0.0 &&
            context > averageContext_ * 1.8;
        const bool pageSpike = averagePages_ > 0.0 &&
            (pages + cache) > averagePages_ * 1.8;
        const bool dpcSpike = dpc > 1.0 || interrupt > 1.0;
        const bool evidence = badFrame &&
            (contextSpike || pageSpike || dpcSpike);

        averageContext_ = averageContext_ == 0.0
            ? context
            : averageContext_ * 0.85 + context * 0.15;
        averagePages_ = averagePages_ == 0.0
            ? pages + cache
            : averagePages_ * 0.85 + (pages + cache) * 0.15;
        averageDpc_ = averageDpc_ * 0.85 + dpc * 0.15;
        averageInterrupt_ = averageInterrupt_ * 0.85 + interrupt * 0.15;

        std::uint64_t eventTotal = 0;
        DWORD topPid = 0;
        std::uint64_t topCount = 0;
        {
            std::lock_guard<std::mutex> countsLock(eventCountsMutex_);
            for (const auto& entry : eventCounts_)
            {
                eventTotal += entry.second;
                if (entry.second > topCount)
                {
                    topCount = entry.second;
                    topPid = entry.first;
                }
            }
            eventCounts_.clear();
        }

        // eventCounts_ ya contiene solo los eventos desde el tick anterior;
        // restar el acumulado historico daba siempre cero.
        const std::uint64_t delta = eventTotal;
        lastEventTotal_ += eventTotal;

        std::string suspect;
        double confidence = 0.0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (topPid != 0 && delta > 0)
            {
                auto it = processNames_.find(topPid);
                suspect = it != processNames_.end()
                    ? WideToUtf8(it->second)
                    : ("PID " + std::to_string(topPid));
                const double ratio =
                    static_cast<double>(topCount) /
                    static_cast<double>(delta);
                confidence = (std::min)(0.95, ratio * 0.8);
            }
            else if (contextSpike)
            {
                suspect = "Picos de context switches";
                confidence = 0.35;
            }
            else if (pageSpike)
            {
                suspect = "Page faults / paginacion";
                confidence = 0.35;
            }
            else if (dpcSpike)
            {
                suspect = "DPC/ISR del sistema";
                confidence = 0.30;
            }

            snapshot_.available = true;
            snapshot_.contextSwitchesPerSec = context;
            snapshot_.hardFaultsPerSec = pages + cache;
            snapshot_.pagesInputPerSec = pages;
            snapshot_.dpcPercent = dpc;
            snapshot_.interruptPercent = interrupt;
            snapshot_.topSuspect = evidence ? suspect : "";
            snapshot_.confidence = evidence ? confidence : 0.0;
            snapshot_.status = evidence
                ? "Evidencia de interferencia: requiere A/B antes de actuar"
                : (snapshot_.etwActive
                    ? "Sin evidencia: solo observacion"
                    : "Correlacion ligera activa; ETW solo bajo demanda");
        }

        Publish();
    }

    StutterSnapshot StutterCorrelator::Snapshot() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return snapshot_;
    }

    VOID WINAPI StutterCorrelator::EventRecordCallback(
        EVENT_RECORD* record)
    {
        if (!record)
            return;

        auto* self = reinterpret_cast<StutterCorrelator*>(
            record->UserContext);
        if (!self)
            return;

        const DWORD pid = record->EventHeader.ProcessId;
        if (pid == 0)
            return;

        std::lock_guard<std::mutex> lock(self->eventCountsMutex_);
        ++self->eventCounts_[pid];
    }

    void StutterCorrelator::EtwLoop()
    {
        EVENT_TRACE_LOGFILEW logfile{};
        logfile.ProcessTraceMode =
            PROCESS_TRACE_MODE_REAL_TIME |
            PROCESS_TRACE_MODE_EVENT_RECORD;
        logfile.EventRecordCallback = &EventRecordCallback;
        logfile.Context = this;
        // Sesion en tiempo real: el nombre va en LoggerName, no en
        // LogFileName. Usar LogFileName hacia a OpenTraceW buscar un
        // archivo inexistente y la activacion fallaba en silencio.
        logfile.LoggerName =
            const_cast<wchar_t*>(EtwSessionName);

        TRACEHANDLE handle = OpenTraceW(&logfile);
        if (handle == INVALID_PROCESSTRACE_HANDLE)
        {
            const DWORD error = GetLastError();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                snapshot_.etwActive = false;
                snapshot_.status =
                    "No se pudo abrir la sesion ETW (error " +
                    std::to_string(error) + ")";
            }
            Publish();
            return;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            etwConsumerHandle_ = handle;
            snapshot_.etwActive = true;
            snapshot_.status = "ETW activo: capturando context switches";
        }
        Publish();

        ProcessTrace(&handle, 1, nullptr, nullptr);
        CloseTrace(handle);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            etwConsumerHandle_ = 0;
            snapshot_.etwActive = false;
        }
    }

    bool StutterCorrelator::StartEtw()
    {
        constexpr std::size_t bufferSize =
            sizeof(EVENT_TRACE_PROPERTIES) +
            (sizeof(EtwSessionName) + 16);
        auto* properties =
            static_cast<EVENT_TRACE_PROPERTIES*>(
                std::calloc(1, bufferSize));
        if (!properties)
            return false;

        properties->Wnode.BufferSize =
            static_cast<ULONG>(bufferSize);
        properties->Wnode.ClientContext = 1; // QPC
        properties->LogFileMode =
            EVENT_TRACE_REAL_TIME_MODE |
            EVENT_TRACE_USE_GLOBAL_SEQUENCE;
        properties->EnableFlags =
            EVENT_TRACE_FLAG_CSWITCH |
            EVENT_TRACE_FLAG_MEMORY_HARD_FAULTS |
            EVENT_TRACE_FLAG_DPC |
            EVENT_TRACE_FLAG_INTERRUPT;
        properties->LoggerNameOffset =
            sizeof(EVENT_TRACE_PROPERTIES);
        properties->LogFileNameOffset = 0;

        ULONG status = StartTraceW(
            &etwSessionHandle_, EtwSessionName, properties);
        if (status == ERROR_ALREADY_EXISTS)
        {
            // El nombre es exclusivo de esta herramienta: es una sesion
            // huerfana de una ejecucion anterior, no una de PresentMon.
            ControlTraceW(
                0, EtwSessionName, properties,
                EVENT_TRACE_CONTROL_STOP);
            status = StartTraceW(
                &etwSessionHandle_, EtwSessionName, properties);
        }
        std::free(properties);

        if (status != ERROR_SUCCESS)
            return false;

        etwStopRequested_.store(false);
        etwThread_ = std::thread(&StutterCorrelator::EtwLoop, this);
        return true;
    }

    void StutterCorrelator::StopEtw()
    {
        etwStopRequested_.store(true);

        if (etwSessionHandle_)
        {
            constexpr std::size_t bufferSize =
                sizeof(EVENT_TRACE_PROPERTIES) +
                (sizeof(EtwSessionName) + 16);
            auto* properties =
                static_cast<EVENT_TRACE_PROPERTIES*>(
                    std::calloc(1, bufferSize));
            if (properties)
            {
                properties->Wnode.BufferSize =
                    static_cast<ULONG>(bufferSize);
                properties->LoggerNameOffset =
                    sizeof(EVENT_TRACE_PROPERTIES);
                ControlTraceW(
                    etwSessionHandle_, EtwSessionName,
                    properties, EVENT_TRACE_CONTROL_STOP);
                std::free(properties);
            }
            etwSessionHandle_ = 0;
        }

        // El handle de consumidor tiene un unico propietario: EtwLoop lo
        // cierra al salir de ProcessTrace. ControlTrace (arriba) fuerza esa
        // salida antes del join.
        if (etwThread_.joinable())
            etwThread_.join();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            snapshot_.etwActive = false;
        }
    }

    double StutterCorrelator::ReadCounter(std::size_t index)
    {
        if (index >= counters_.size() || !query_)
            return 0.0;

        PDH_FMT_COUNTERVALUE value{};
        if (PdhGetFormattedCounterValue(
                counters_[index], PDH_FMT_DOUBLE,
                nullptr, &value) != ERROR_SUCCESS ||
            value.CStatus != PDH_CSTATUS_VALID_DATA)
        {
            return 0.0;
        }
        return value.doubleValue;
    }

    void StutterCorrelator::Publish()
    {
        if (!publisher_)
            return;
        publisher_(Snapshot());
    }
}
