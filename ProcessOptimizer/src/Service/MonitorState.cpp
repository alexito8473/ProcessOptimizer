#include "MonitorState.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace Service
{
    namespace
    {
        float ClampUsage(double value)
        {
            return static_cast<float>(
                (std::max)(0.0, (std::min)(value, 100.0)));
        }

        std::string AdapterNameToUtf8(const std::wstring& value)
        {
            if (value.empty())
                return {};

            const int size = WideCharToMultiByte(
                CP_UTF8, 0, value.c_str(), -1,
                nullptr, 0, nullptr, nullptr);

            if (size <= 1)
                return {};

            std::string result(
                static_cast<std::size_t>(size), '\0');

            WideCharToMultiByte(
                CP_UTF8, 0, value.c_str(), -1,
                result.data(), size, nullptr, nullptr);

            result.pop_back();
            return result;
        }

        bool SameGpu(const IPC::GpuSeriesHistory& series,
                     const GpuAdapterInfo& gpu)
        {
            return series.luidLowPart == gpu.luidLowPart &&
                   series.luidHighPart == gpu.luidHighPart;
        }

        std::uint64_t NowMs()
        {
            using namespace std::chrono;
            return static_cast<std::uint64_t>(
                duration_cast<milliseconds>(
                    system_clock::now().time_since_epoch())
                    .count());
        }
    }

    std::uint32_t MonitorState::ApplyCpu(
        const CpuSnapshot& snapshot)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        const bool usageChanged =
            std::abs(state_.cpu.totalUsagePercent -
                     snapshot.totalUsagePercent) > 0.001;

        bool processorsChanged = false;

        if (state_.cpu.logicalProcessors.size() !=
            snapshot.logicalProcessors.size())
        {
            processorsChanged = true;
        }
        else
        {
            for (std::size_t i = 0;
                 i < snapshot.logicalProcessors.size(); ++i)
            {
                const auto& before =
                    state_.cpu.logicalProcessors[i];
                const auto& after =
                    snapshot.logicalProcessors[i];

                if (before.processorGroup !=
                        after.processorGroup ||
                    before.logicalProcessorIndex !=
                        after.logicalProcessorIndex ||
                    std::abs(before.usagePercent -
                             after.usagePercent) > 0.001)
                {
                    processorsChanged = true;
                    break;
                }
            }
        }

        state_.cpu = snapshot;
        state_.hasCpu = true;
        AppendCpuHistory(snapshot);

        std::uint32_t changed = ChangeMask::None;

        if (usageChanged || processorsChanged)
        {
            const std::size_t index = state_.cpuHistory.index;

            state_.cpuHistory.values[index] =
                ClampUsage(snapshot.totalUsagePercent);
            state_.cpuHistory.index =
                (index + 1) % state_.cpuHistory.values.size();

            if (state_.cpuHistory.count <
                state_.cpuHistory.values.size())
            {
                ++state_.cpuHistory.count;
            }

            AppendOverviewSample();
            changed |= ChangeMask::Cpu;
        }

        state_.changedMask = changed;
        ++state_.sequence;
        state_.timestampMs = NowMs();
        return changed;
    }

    std::uint32_t MonitorState::ApplyMemory(
        const MemorySnapshot& snapshot)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        const bool changed =
            std::abs(state_.memory.usagePercent -
                     snapshot.usagePercent) > 0.001;

        const std::size_t index =
            state_.memoryHistory.index;

        state_.memory = snapshot;
        state_.hasMemory = true;

        state_.memoryHistory.values[index] =
            ClampUsage(snapshot.usagePercent);
        state_.memoryHistory.index =
            (index + 1) % state_.memoryHistory.values.size();

        if (state_.memoryHistory.count <
            state_.memoryHistory.values.size())
        {
            ++state_.memoryHistory.count;
        }

        std::uint32_t mask = ChangeMask::None;

        if (changed)
        {
            AppendOverviewSample();
            mask = ChangeMask::Memory;
        }

        state_.changedMask = mask;
        ++state_.sequence;
        state_.timestampMs = NowMs();
        return mask;
    }

    std::uint32_t MonitorState::ApplyGpu(
        const GpuSnapshot& snapshot)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        double previousMax = 0.0;
        double currentMax = 0.0;

        for (const GpuAdapterInfo& gpu :
             state_.gpu.gpus)
        {
            previousMax =
                (std::max)(previousMax, gpu.usagePercent);
        }

        for (const GpuAdapterInfo& gpu : snapshot.gpus)
        {
            currentMax =
                (std::max)(currentMax, gpu.usagePercent);
        }

        const bool changed =
            std::abs(previousMax - currentMax) > 0.01;

        state_.gpu = snapshot;
        state_.hasGpu = true;

        if (changed)
        {
            AppendGpuHistory(snapshot);
            AppendOverviewSample();
        }

        state_.changedMask =
            changed ? ChangeMask::Gpu : ChangeMask::None;
        ++state_.sequence;
        state_.timestampMs = NowMs();
        return state_.changedMask;
    }

    std::uint32_t MonitorState::ApplyProcesses(
        const ProcessSnapshot& snapshot)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        state_.processes = snapshot;
        state_.hasProcesses = true;
        state_.changedMask = ChangeMask::Processes;
        ++state_.sequence;
        state_.timestampMs = NowMs();
        return ChangeMask::Processes;
    }

    std::uint32_t MonitorState::SetTopology(
        const Optimization::TopologySnapshot& topology)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        state_.topology = topology;
        state_.hasTopology = true;
        state_.changedMask = ChangeMask::Topology;
        ++state_.sequence;
        state_.timestampMs = NowMs();
        return ChangeMask::Topology;
    }

    std::uint32_t MonitorState::SetOptimizer(
        const Optimization::OptimizerState& optimizer)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        state_.optimizer = optimizer;
        state_.hasOptimizer = true;
        state_.changedMask = ChangeMask::Optimization;
        ++state_.sequence;
        state_.timestampMs = NowMs();
        return ChangeMask::Optimization;
    }

    IPC::State MonitorState::Snapshot() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_;
    }

    void MonitorState::Replace(const IPC::State& state)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = state;
    }

    void MonitorState::AppendCpuHistory(
        const CpuSnapshot& snapshot)
    {
        auto& histories = state_.processorHistories;

        for (const LogicalProcessorInfo& processor :
             snapshot.logicalProcessors)
        {
            auto it = std::find_if(
                histories.begin(), histories.end(),
                [&processor](const IPC::ProcessorHistory& h)
                {
                    return h.group == processor.processorGroup &&
                           h.logicalIndex ==
                               processor.logicalProcessorIndex;
                });

            if (it == histories.end())
            {
                histories.push_back({});
                it = std::prev(histories.end());
                it->group = processor.processorGroup;
                it->logicalIndex =
                    processor.logicalProcessorIndex;
            }

            auto& history = *it;
            const std::size_t index = history.values.index;

            history.values.values[index] =
                ClampUsage(processor.usagePercent);
            history.values.index =
                (index + 1) % history.values.values.size();

            if (history.values.count <
                history.values.values.size())
            {
                ++history.values.count;
            }
        }
    }

    void MonitorState::AppendGpuHistory(
        const GpuSnapshot& snapshot)
    {
        auto& histories = state_.gpuHistories;

        for (const GpuAdapterInfo& gpu : snapshot.gpus)
        {
            auto it = std::find_if(
                histories.begin(), histories.end(),
                [&gpu](const IPC::GpuAdapterHistory& h)
                {
                    return h.luidLowPart == gpu.luidLowPart &&
                           h.luidHighPart == gpu.luidHighPart;
                });

            if (it == histories.end())
            {
                histories.push_back({});
                it = std::prev(histories.end());
                it->luidLowPart = gpu.luidLowPart;
                it->luidHighPart = gpu.luidHighPart;
            }

            auto& entry = *it;

            const auto append =
                [](IPC::GpuRing& ring, double value)
                {
                    const std::size_t index = ring.index;
                    ring.values[index] = ClampUsage(value);
                    ring.index =
                        (index + 1) % ring.values.size();
                    if (ring.count < ring.values.size())
                        ++ring.count;
                };

            append(entry.usage, gpu.usagePercent);
            append(entry.usage3D, gpu.usage3D);
            append(entry.usageCopy, gpu.usageCopy);
            append(entry.usageCompute, gpu.usageCompute);
            append(entry.usageVideoDecode,
                   gpu.usageVideoDecode);
            append(entry.usageVideoEncode,
                   gpu.usageVideoEncode);
            append(entry.usageVideoProcessing,
                   gpu.usageVideoProcessing);
            append(entry.dedicatedMemory,
                   gpu.processDedicatedMemoryUsedBytes /
                       (1024.0 * 1024.0));
            append(entry.sharedMemory,
                   gpu.processSharedMemoryUsedBytes /
                       (1024.0 * 1024.0));
        }
    }

    void MonitorState::AppendOverviewSample()
    {
        constexpr std::size_t capacity =
            IPC::OverviewHistoryCapacity;

        const double cpuUsage =
            state_.hasCpu ? state_.cpu.totalUsagePercent : 0.0;
        const double memoryUsage =
            state_.hasMemory ? state_.memory.usagePercent : 0.0;

        auto& history = state_.overview;
        const std::size_t index = history.cpu.index;

        if (state_.hasGpu)
        {
            for (const GpuAdapterInfo& gpu : state_.gpu.gpus)
            {
                auto it = std::find_if(
                    history.gpus.begin(), history.gpus.end(),
                    [&gpu](const IPC::GpuSeriesHistory& series)
                    {
                        return SameGpu(series, gpu);
                    });

                if (it == history.gpus.end())
                {
                    IPC::GpuSeriesHistory series{};
                    series.luidLowPart = gpu.luidLowPart;
                    series.luidHighPart = gpu.luidHighPart;
                    series.name =
                        AdapterNameToUtf8(gpu.name);
                    history.gpus.push_back(series);
                }
                else if (it->name.empty())
                {
                    it->name = AdapterNameToUtf8(gpu.name);
                }
            }
        }

        history.cpu.values[index] = ClampUsage(cpuUsage);
        history.memory.values[index] = ClampUsage(memoryUsage);

        for (IPC::GpuSeriesHistory& series : history.gpus)
        {
            double gpuUsage = 0.0;

            if (state_.hasGpu)
            {
                auto it = std::find_if(
                    state_.gpu.gpus.begin(),
                    state_.gpu.gpus.end(),
                    [&series](const GpuAdapterInfo& gpu)
                    {
                        return SameGpu(series, gpu);
                    });

                if (it != state_.gpu.gpus.end())
                    gpuUsage = it->usagePercent;
            }

            series.usage.values[index] =
                ClampUsage(gpuUsage);
        }

        history.cpu.index = (index + 1) % capacity;
        history.memory.index = history.cpu.index;

        if (history.cpu.count < capacity)
            ++history.cpu.count;
        if (history.memory.count < capacity)
            ++history.memory.count;

        for (IPC::GpuSeriesHistory& series : history.gpus)
        {
            series.usage.index = history.cpu.index;
            if (series.usage.count < capacity)
                ++series.usage.count;
        }
    }
}
