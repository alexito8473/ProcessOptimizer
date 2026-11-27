#include "CpuBenchmark.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Optimization
{
    namespace
    {
        constexpr char ProfileMagic[4] = { 'P', 'O', 'C', '1' };
        constexpr std::uint32_t ProfileVersion = 1;
        constexpr std::size_t MaxBenchmarkCores = 64;

        std::uint32_t ProcessorKey(WORD group, BYTE logicalIndex)
        {
            return
                (static_cast<std::uint32_t>(group) << 8) |
                static_cast<std::uint32_t>(logicalIndex);
        }

        std::uint64_t TopologySignature(
            const TopologySnapshot& topology)
        {
            // FNV-1a: suficiente para detectar que el perfil pertenece a
            // otra CPU sin depender del nombre comercial ni del PID.
            std::uint64_t hash = 1469598103934665603ULL;

            const auto mix = [&hash](std::uint64_t value)
            {
                hash ^= value;
                hash *= 1099511628211ULL;
            };

            mix(static_cast<std::uint64_t>(topology.kind));
            mix(topology.groupCount);
            mix(topology.numaNodeCount);
            mix(topology.processors.size());
            mix(topology.cores.size());

            for (const TopologyProcessor& processor :
                 topology.processors)
            {
                mix(processor.group);
                mix(processor.logicalIndex);
                mix(processor.physicalCoreIndex);
                mix(processor.numaNode);
                mix(processor.l1CacheId);
                mix(processor.l2CacheId);
                mix(processor.l3CacheId);
                mix(processor.efficiencyClass);
                mix(static_cast<std::uint64_t>(
                    processor.coreClass));
            }

            for (const TopologyCore& core : topology.cores)
                mix(core.logicalIndices.size());

            return hash;
        }

        std::wstring EnvironmentDirectory(
            const wchar_t* variable)
        {
            const DWORD length =
                GetEnvironmentVariableW(variable, nullptr, 0);

            if (length == 0)
                return {};

            std::wstring value(length, L'\0');

            if (GetEnvironmentVariableW(
                    variable, value.data(), length) == 0)
            {
                return {};
            }

            value.resize(length - 1);
            return value;
        }

        std::wstring ProfileDirectory()
        {
            std::wstring root =
                EnvironmentDirectory(L"ProgramData");

            if (root.empty())
                root = EnvironmentDirectory(L"LOCALAPPDATA");

            if (root.empty())
                return {};

            const std::wstring directory =
                root + L"\\ProcessOptimizer";

            CreateDirectoryW(directory.c_str(), nullptr);
            return directory;
        }

        std::wstring ProfilePath()
        {
            const std::wstring directory =
                ProfileDirectory();

            if (directory.empty())
                return {};

            return directory + L"\\cpu-profile.bin";
        }

        bool WriteAll(
            HANDLE file,
            const void* data,
            std::size_t size)
        {
            const auto* bytes =
                static_cast<const std::uint8_t*>(data);

            std::size_t written = 0;
            while (written < size)
            {
                DWORD chunk = 0;
                if (!WriteFile(
                        file,
                        bytes + written,
                        static_cast<DWORD>(size - written),
                        &chunk,
                        nullptr) ||
                    chunk == 0)
                {
                    return false;
                }

                written += chunk;
            }

            return true;
        }

        bool ReadAll(
            HANDLE file,
            void* data,
            std::size_t size)
        {
            auto* bytes =
                static_cast<std::uint8_t*>(data);

            std::size_t read = 0;
            while (read < size)
            {
                DWORD chunk = 0;
                if (!ReadFile(
                        file,
                        bytes + read,
                        static_cast<DWORD>(size - read),
                        &chunk,
                        nullptr) ||
                    chunk == 0)
                {
                    return false;
                }

                read += chunk;
            }

            return true;
        }

        template<typename T>
        bool WritePod(HANDLE file, const T& value)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            return WriteAll(file, &value, sizeof(value));
        }

        template<typename T>
        bool ReadPod(HANDLE file, T& value)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            return ReadAll(file, &value, sizeof(value));
        }

        std::unordered_map<std::uint32_t, ULONG>
        QueryCpuSetIds()
        {
            std::unordered_map<std::uint32_t, ULONG> result;

            ULONG size = 0;
            if (!GetSystemCpuSetInformation(
                    nullptr, 0, &size, nullptr, 0) &&
                GetLastError() != ERROR_INSUFFICIENT_BUFFER)
            {
                return result;
            }

            if (size == 0)
                return result;

            std::vector<BYTE> buffer(size);
            ULONG returned = 0;
            if (!GetSystemCpuSetInformation(
                    reinterpret_cast<
                        PSYSTEM_CPU_SET_INFORMATION>(
                            buffer.data()),
                    size,
                    &returned,
                    nullptr,
                    0))
            {
                return result;
            }

            BYTE* current = buffer.data();
            BYTE* end = buffer.data() + returned;

            while (current < end)
            {
                auto* info =
                    reinterpret_cast<
                        PSYSTEM_CPU_SET_INFORMATION>(
                            current);

                if (info->Size == 0 ||
                    current + info->Size > end)
                {
                    break;
                }

                if (info->Type == CpuSetInformation)
                {
                    const auto& set = info->CpuSet;
                    result[ProcessorKey(
                        set.Group,
                        set.LogicalProcessorIndex)] = set.Id;
                }

                current += info->Size;
            }

            return result;
        }

        std::vector<ULONG> CpuSetIdsForCore(
            const TopologyCore& core,
            const TopologySnapshot& topology,
            const std::unordered_map<
                std::uint32_t, ULONG>& cpuSets)
        {
            std::vector<ULONG> ids;

            for (std::uint32_t index : core.logicalIndices)
            {
                if (index >= topology.processors.size())
                    continue;

                const TopologyProcessor& processor =
                    topology.processors[index];

                const auto it = cpuSets.find(
                    ProcessorKey(
                        processor.group,
                        processor.logicalIndex));

                if (it == cpuSets.end())
                    continue;

                ids.push_back(it->second);
            }

            std::sort(ids.begin(), ids.end());
            ids.erase(
                std::unique(ids.begin(), ids.end()),
                ids.end());

            return ids;
        }
    }

    CpuBenchmark::CpuBenchmark(
        StateCallback callback)
        : callback_(std::move(callback))
    {
    }

    CpuBenchmark::~CpuBenchmark()
    {
        Stop();
    }

    void CpuBenchmark::Publish(
        const BenchmarkState& state)
    {
        StateCallback callback;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_ = state;
            callback = callback_;
        }

        if (callback)
            callback(state);
    }

    BenchmarkState CpuBenchmark::State() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_;
    }

    void CpuBenchmark::SetProfile(
        const CpuBenchmarkProfile& profile)
    {
        if (running_.load())
            return;

        BenchmarkState state;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            state = state_;
            state.profile = profile;

            if (profile.HasData())
            {
                state.status = BenchmarkStatus::Completed;
                state.progressPercent = 100;
                state.statusText =
                    "Perfil de CPU cargado desde disco";
            }
        }

        Publish(state);
    }

    bool CpuBenchmark::Start(
        const TopologySnapshot& topology,
        std::uint32_t durationMs)
    {
        bool expected = false;
        if (!running_.compare_exchange_strong(
                expected, true))
        {
            return false;
        }

        if (worker_.joinable())
            worker_.join();

        cancel_.store(false);

        TopologySnapshot copy = topology;
        worker_ = std::thread(
            &CpuBenchmark::Worker,
            this,
            std::move(copy),
            durationMs);

        return true;
    }

    void CpuBenchmark::Stop()
    {
        if (!running_.load())
            return;

        cancel_.store(true);

        if (worker_.joinable())
            worker_.join();

        running_.store(false);
    }

    bool CpuBenchmark::Save(
        const CpuBenchmarkProfile& profile) const
    {
        const std::wstring path = ProfilePath();
        if (path.empty())
            return false;

        const std::wstring temporary = path + L".tmp";

        HANDLE file = CreateFileW(
            temporary.c_str(),
            GENERIC_WRITE,
            0,
            nullptr,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);

        if (file == INVALID_HANDLE_VALUE)
            return false;

        bool ok =
            WriteAll(file, ProfileMagic, sizeof(ProfileMagic)) &&
            WritePod(file, ProfileVersion) &&
            WritePod(file, profile.topologySignature) &&
            WritePod(file, profile.createdMs) &&
            WritePod(file, profile.durationMs);

        const std::uint32_t count =
            static_cast<std::uint32_t>(
                profile.processors.size());

        ok = ok && WritePod(file, count);

        if (ok)
        {
            for (const ProcessorBenchmark& entry :
                 profile.processors)
            {
                const std::int32_t coreClass =
                    static_cast<std::int32_t>(
                        entry.coreClass);

                ok =
                    WritePod(file, entry.group) &&
                    WritePod(file, entry.logicalIndex) &&
                    WritePod(file, entry.physicalCoreIndex) &&
                    WritePod(file, coreClass) &&
                    WritePod(file, entry.peakMHz) &&
                    WritePod(file, entry.averageMHz) &&
                    WritePod(file, entry.workRate) &&
                    WritePod(file, entry.score) &&
                    WritePod(file, entry.rank);

                if (!ok)
                    break;
            }
        }

        CloseHandle(file);

        if (!ok)
        {
            DeleteFileW(temporary.c_str());
            return false;
        }

        if (!MoveFileExW(
                temporary.c_str(),
                path.c_str(),
                MOVEFILE_REPLACE_EXISTING |
                    MOVEFILE_WRITE_THROUGH))
        {
            DeleteFileW(temporary.c_str());
            return false;
        }

        return true;
    }

    CpuBenchmarkProfile CpuBenchmark::Load(
        const TopologySnapshot& topology)
    {
        CpuBenchmarkProfile empty;
        const std::wstring path = ProfilePath();

        if (path.empty())
            return empty;

        HANDLE file = CreateFileW(
            path.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);

        if (file == INVALID_HANDLE_VALUE)
            return empty;

        CpuBenchmarkProfile profile;

        char magic[4]{};
        std::uint32_t version = 0;
        std::uint32_t count = 0;

        bool ok =
            ReadAll(file, magic, sizeof(magic)) &&
            std::memcmp(
                magic, ProfileMagic,
                sizeof(ProfileMagic)) == 0 &&
            ReadPod(file, version) &&
            version == ProfileVersion &&
            ReadPod(file, profile.topologySignature) &&
            ReadPod(file, profile.createdMs) &&
            ReadPod(file, profile.durationMs) &&
            ReadPod(file, count) &&
            count != 0 &&
            count <= 256;

        if (ok)
        {
            if (profile.topologySignature !=
                TopologySignature(topology))
            {
                ok = false;
            }
        }

        if (ok)
        {
            profile.version = ProfileVersion;
            profile.processors.resize(count);

            for (ProcessorBenchmark& entry :
                 profile.processors)
            {
                std::int32_t coreClass = 0;

                ok =
                    ReadPod(file, entry.group) &&
                    ReadPod(file, entry.logicalIndex) &&
                    ReadPod(file, entry.physicalCoreIndex) &&
                    ReadPod(file, coreClass) &&
                    ReadPod(file, entry.peakMHz) &&
                    ReadPod(file, entry.averageMHz) &&
                    ReadPod(file, entry.workRate) &&
                    ReadPod(file, entry.score) &&
                    ReadPod(file, entry.rank);

                if (!ok)
                    break;

                entry.coreClass =
                    static_cast<CoreClass>(coreClass);
            }
        }

        CloseHandle(file);

        return ok ? profile : empty;
    }

    void CpuBenchmark::Worker(
        TopologySnapshot topology,
        std::uint32_t durationMs)
    {
        using Clock = std::chrono::steady_clock;

        durationMs = (std::min)(
            (std::max)(durationMs, 1000U), 15000U);

        BenchmarkState initial;
        initial.status = BenchmarkStatus::Running;
        initial.progressPercent = 0;
        initial.statusText =
            "Prueba de rendimiento en curso";
        Publish(initial);

        if (topology.processors.empty() ||
            topology.cores.empty())
        {
            BenchmarkState failed;
            failed.status = BenchmarkStatus::Failed;
            failed.statusText =
                "No hay topologia de CPU disponible";
            Publish(failed);
            running_.store(false);
            return;
        }

        CpuTopology::RefreshFrequencies(topology);

        const auto cpuSets = QueryCpuSetIds();

        std::vector<std::unique_ptr<CoreWork>> workItems;
        std::vector<std::thread> threads;

        for (const TopologyCore& core : topology.cores)
        {
            if (workItems.size() >= MaxBenchmarkCores)
                break;

            if (core.logicalIndices.empty())
                continue;

            auto work = std::make_unique<CoreWork>();
            work->coreIndex = core.coreIndex;
            work->cpuSetIds =
                CpuSetIdsForCore(core, topology, cpuSets);

            if (work->cpuSetIds.empty())
            {
                const std::uint32_t index =
                    core.logicalIndices.front();

                if (index < topology.processors.size())
                {
                    work->idealProcessor =
                        topology.processors[index]
                            .logicalIndex;
                    work->hasIdeal = true;
                }
            }

            CoreWork* raw = work.get();
            workItems.push_back(std::move(work));

            threads.emplace_back(
                [raw]()
                {
                    HANDLE current = GetCurrentThread();

                    if (!raw->cpuSetIds.empty())
                    {
                        SetThreadSelectedCpuSets(
                            current,
                            raw->cpuSetIds.data(),
                            static_cast<ULONG>(
                                raw->cpuSetIds.size()));
                    }
                    else if (raw->hasIdeal)
                    {
                        PROCESSOR_NUMBER number{};
                        number.Number = static_cast<BYTE>(
                            std::min<DWORD>(
                                raw->idealProcessor, 63));

                        SetThreadIdealProcessorEx(
                            current, &number, nullptr);
                    }

                    while (!raw->start.load(
                               std::memory_order_acquire))
                    {
                        std::this_thread::yield();
                    }

                    volatile std::uint64_t counter = 0;

                    while (!raw->stop.load(
                               std::memory_order_acquire))
                    {
                        ++counter;

                        if ((counter & 0xFFFFULL) == 0)
                            std::this_thread::yield();
                    }

                    raw->iterations.store(
                        counter,
                        std::memory_order_release);
                });
        }

        if (threads.empty())
        {
            BenchmarkState failed;
            failed.status = BenchmarkStatus::Failed;
            failed.statusText =
                "No se pudieron crear hilos de prueba";
            Publish(failed);
            running_.store(false);
            return;
        }

        std::vector<FrequencyAccumulator> frequencies(
            topology.processors.size());

        LARGE_INTEGER frequency{};
        LARGE_INTEGER startCounter{};

        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&startCounter);

        const auto started = Clock::now();

        for (const auto& work : workItems)
            work->start.store(true, std::memory_order_release);

        std::uint32_t lastProgress = 0;

        while (!cancel_.load())
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(50));

            CpuTopology::RefreshFrequencies(topology);

            for (std::size_t i = 0;
                 i < topology.processors.size();
                 ++i)
            {
                const TopologyProcessor& processor =
                    topology.processors[i];

                if (!processor.frequencyAvailable ||
                    processor.parked)
                {
                    continue;
                }

                const double observed =
                    processor.currentMHz > 0.0
                        ? processor.currentMHz
                        : (processor.mhzLimitMHz > 0.0
                               ? processor.mhzLimitMHz
                               : processor.maxMHz);

                if (observed <= 0.0)
                    continue;

                FrequencyAccumulator& accumulator =
                    frequencies[i];

                accumulator.peakMHz =
                    (std::max)(accumulator.peakMHz, observed);
                accumulator.sumMHz += observed;
                ++accumulator.samples;
            }

            const auto elapsed = std::chrono::duration_cast<
                std::chrono::milliseconds>(
                    Clock::now() - started);

            const std::uint32_t progress =
                (std::min)(
                    100U,
                    static_cast<std::uint32_t>(
                        elapsed.count() * 100 /
                        durationMs));

            if (progress >= lastProgress + 10)
            {
                lastProgress = progress;

                BenchmarkState runningState;
                runningState.status =
                    BenchmarkStatus::Running;
                runningState.progressPercent = progress;
                runningState.statusText =
                    "Midiendo frecuencia y capacidad real";
                Publish(runningState);
            }

            if (elapsed.count() >=
                static_cast<std::int64_t>(durationMs))
            {
                break;
            }
        }

        LARGE_INTEGER endCounter{};
        QueryPerformanceCounter(&endCounter);

        for (const auto& work : workItems)
            work->stop.store(true, std::memory_order_release);

        for (std::thread& thread : threads)
            thread.join();

        const double elapsedSeconds =
            frequency.QuadPart > 0
                ? static_cast<double>(
                      endCounter.QuadPart -
                      startCounter.QuadPart) /
                      static_cast<double>(
                          frequency.QuadPart)
                : static_cast<double>(durationMs) / 1000.0;

        CpuBenchmarkProfile profile;
        profile.version = ProfileVersion;
        profile.createdMs = GetTickCount64();
        profile.durationMs = durationMs;
        profile.topologySignature =
            TopologySignature(topology);

        std::unordered_map<
            std::size_t, double> rateByCore;

        for (const auto& work : workItems)
        {
            rateByCore[work->coreIndex] =
                elapsedSeconds > 0.0
                    ? static_cast<double>(
                          work->iterations.load(
                              std::memory_order_acquire)) /
                          elapsedSeconds
                    : 0.0;
        }

        double maximumRate = 0.0;
        double maximumPeak = 0.0;

        for (std::size_t i = 0;
             i < topology.processors.size();
             ++i)
        {
            const TopologyProcessor& processor =
                topology.processors[i];

            ProcessorBenchmark entry;
            entry.group = processor.group;
            entry.logicalIndex = processor.logicalIndex;
            entry.physicalCoreIndex =
                processor.physicalCoreIndex;
            entry.coreClass = processor.coreClass;

            const auto rateIt =
                rateByCore.find(
                    processor.physicalCoreIndex);

            if (rateIt != rateByCore.end())
                entry.workRate = rateIt->second;

            const FrequencyAccumulator& accumulator =
                frequencies[i];

            if (accumulator.samples != 0)
            {
                entry.peakMHz = accumulator.peakMHz;
                entry.averageMHz =
                    accumulator.sumMHz /
                    static_cast<double>(
                        accumulator.samples);
            }
            else if (processor.frequencyAvailable)
            {
                entry.peakMHz =
                    processor.maxMHz > 0.0
                        ? processor.maxMHz
                        : processor.mhzLimitMHz;
                entry.averageMHz = entry.peakMHz;
            }

            maximumRate =
                (std::max)(maximumRate, entry.workRate);
            maximumPeak =
                (std::max)(maximumPeak, entry.peakMHz);

            profile.processors.push_back(entry);
        }

        for (ProcessorBenchmark& entry : profile.processors)
        {
            if (maximumRate > 0.0)
            {
                entry.score +=
                    entry.workRate / maximumRate * 65.0;
            }

            if (maximumPeak > 0.0)
            {
                entry.score +=
                    entry.peakMHz / maximumPeak * 35.0;
            }

            if (maximumRate <= 0.0 && maximumPeak <= 0.0)
            {
                entry.score =
                    entry.coreClass ==
                            CoreClass::Performance
                        ? 100.0
                        : 50.0;
            }
        }

        std::vector<std::size_t> order(
            profile.processors.size());

        for (std::size_t i = 0; i < order.size(); ++i)
            order[i] = i;

        std::sort(
            order.begin(), order.end(),
            [&profile](std::size_t left, std::size_t right)
            {
                const ProcessorBenchmark& a =
                    profile.processors[left];
                const ProcessorBenchmark& b =
                    profile.processors[right];

                if (a.score != b.score)
                    return a.score > b.score;

                if (a.peakMHz != b.peakMHz)
                    return a.peakMHz > b.peakMHz;

                if (a.group != b.group)
                    return a.group < b.group;

                return a.logicalIndex < b.logicalIndex;
            });

        for (std::size_t i = 0; i < order.size(); ++i)
        {
            profile.processors[order[i]].rank =
                static_cast<std::uint16_t>(i + 1);
        }

        const bool saved = Save(profile);
        SetProfile(profile);

        BenchmarkState finished;
        finished.profile = profile;
        finished.progressPercent = 100;
        finished.status =
            cancel_.load()
                ? BenchmarkStatus::Failed
                : BenchmarkStatus::Completed;
        finished.statusText =
            cancel_.load()
                ? "Prueba cancelada"
                : (saved
                       ? "Perfil medido y guardado"
                       : "Perfil medido; no se pudo guardar");

        Publish(finished);
        running_.store(false);
    }
}
