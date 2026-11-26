#pragma once

#include "OptimizationTypes.h"

#include "../Monitoring/ProcessMonitor/ProcessInfo.h"

#include <Windows.h>
#include <evntrace.h>
#include <evntcons.h>
#include <pdh.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace Optimization
{
    // Correlacion ligera: contadores PDH a 1 Hz y, solo si se activa
    // explicitamente, una sesion ETW del sistema con CSWITCH, hard faults,
    // DPC e ISR. Nunca inicia automaticamente para no pisar PresentMon/xperf.
    class StutterCorrelator
    {
    public:
        using Publisher =
            std::function<void(const StutterSnapshot&)>;

        explicit StutterCorrelator(Publisher publisher);
        ~StutterCorrelator();

        StutterCorrelator(const StutterCorrelator&) = delete;
        StutterCorrelator& operator=(const StutterCorrelator&) = delete;

        bool Start();
        void Stop();
        bool SetEtwEnabled(bool enabled);
        void SetProcessSnapshot(const ProcessSnapshot& snapshot);
        void Tick(const FrameProbeSnapshot& frame);
        StutterSnapshot Snapshot() const;

    private:
        static VOID WINAPI EventRecordCallback(EVENT_RECORD* record);
        void EtwLoop();
        bool StartEtw();
        void StopEtw();
        double ReadCounter(std::size_t index);
        void Publish();

        enum class Counter
        {
            ContextSwitches,
            PagesInput,
            CacheFaults,
            Dpc,
            Interrupt,
            Count
        };

        Publisher publisher_;
        mutable std::mutex mutex_;

        PDH_HQUERY query_ = nullptr;
        std::vector<PDH_HCOUNTER> counters_;
        std::vector<std::wstring> counterNames_;

        std::atomic<bool> started_{ false };
        bool etwEnabled_ = false;
        bool etwActive_ = false;
        std::atomic<bool> etwStopRequested_{ false };
        TRACEHANDLE etwSessionHandle_ = 0;
        TRACEHANDLE etwConsumerHandle_ = 0;
        std::thread etwThread_;

        std::unordered_map<DWORD, std::wstring> processNames_;
        std::unordered_map<DWORD, std::uint64_t> eventCounts_;
        std::mutex eventCountsMutex_;

        double averageContext_ = 0.0;
        double averagePages_ = 0.0;
        double averageDpc_ = 1.0;
        double averageInterrupt_ = 1.0;
        std::uint64_t lastEventTotal_ = 0;
        StutterSnapshot snapshot_;
    };
}
