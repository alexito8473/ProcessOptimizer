#pragma once

#include "OptimizationTypes.h"

#include <Windows.h>
#include <evntrace.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace Optimization
{
    // Sonda de frame time basada en eventos ETW de Microsoft-Windows-DXGI.
    // No inyecta nada en el juego: solo escucha Present()/PresentStop del
    // proceso objetivo. Es la misma familia de eventos que usa PresentMon.
    class FrameProbe
    {
    public:
        using Publisher =
            std::function<void(const FrameProbeSnapshot&)>;

        explicit FrameProbe(Publisher publisher);
        ~FrameProbe();

        FrameProbe(const FrameProbe&) = delete;
        FrameProbe& operator=(const FrameProbe&) = delete;

        bool Start();
        void Stop();

        void SetTargetProcess(DWORD processId);
        void SetGpuUsagePercent(double usagePercent);

        FrameProbeSnapshot Snapshot() const;

    private:
        struct Sample
        {
            std::uint64_t endTick = 0;
            double intervalMs = 0.0;
            double cpuInPresentMs = 0.0;
            std::int32_t syncInterval = 0;
            std::uint32_t flags = 0;
        };

        static VOID WINAPI EventRecordCallback(
            EVENT_RECORD* record);
        void OnEvent(EVENT_RECORD* record);
        void AggregateLoop();
        void PublishSnapshot(const FrameProbeSnapshot& snapshot);

        bool GetNumericProperty(
            EVENT_RECORD* record,
            const wchar_t* name,
            std::uint64_t& value) const;

        double TicksToMs(std::uint64_t ticks) const;

        Publisher publisher_;
        mutable std::mutex lifecycleMutex_;
        std::atomic<bool> stopRequested_{ false };
        std::atomic<bool> running_{ false };
        std::atomic<DWORD> targetPid_{ 0 };
        std::atomic<double> gpuUsagePercent_{ 0.0 };
        std::uint64_t qpcFrequency_ = 1;

        TRACEHANDLE traceHandle_ = 0;
        TRACEHANDLE consumerHandle_ =
            INVALID_PROCESSTRACE_HANDLE;
        std::thread consumerThread_;
        std::thread aggregateThread_;

        mutable std::mutex mutex_;
        std::vector<Sample> samples_;
        std::unordered_map<DWORD, std::uint64_t> lastStartByThread_;
        std::unordered_map<DWORD, std::uint64_t> startByThread_;
        std::unordered_map<DWORD, double> pendingCpuByThread_;
        FrameProbeSnapshot snapshot_;
    };
}
