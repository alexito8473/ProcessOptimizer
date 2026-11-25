#define NOMINMAX
#include "ThreadMonitor.h"

#include <TlHelp32.h>

#include <algorithm>

namespace Optimization
{
    namespace
    {
        std::uint64_t Key(DWORD processId, DWORD threadId)
        {
            return
                (static_cast<std::uint64_t>(processId) << 32) |
                static_cast<std::uint64_t>(threadId);
        }

        std::uint64_t FileTimeTo100ns(const FILETIME& value)
        {
            return
                (static_cast<std::uint64_t>(
                    value.dwHighDateTime) << 32) |
                static_cast<std::uint64_t>(
                    value.dwLowDateTime);
        }
    }

    std::vector<ThreadLoad> ThreadMonitor::Sample(
        const std::unordered_set<DWORD>& processIds,
        std::size_t logicalProcessorCount)
    {
        std::vector<ThreadLoad> result;

        const auto now = std::chrono::steady_clock::now();

        double elapsed100ns = 0.0;
        if (hasLastSample_)
        {
            elapsed100ns =
                std::chrono::duration<
                    double, std::ratio<1, 10000000>>(
                    now - lastSample_).count();
        }

        lastSample_ = now;
        hasLastSample_ = true;
        ++sequence_;

        if (processIds.empty())
        {
            previous_.clear();
            return result;
        }

        HANDLE snapshot = CreateToolhelp32Snapshot(
            TH32CS_SNAPTHREAD, 0);

        if (snapshot == INVALID_HANDLE_VALUE)
            return result;

        std::unordered_set<std::uint64_t> seen;

        THREADENTRY32 entry{};
        entry.dwSize = sizeof(entry);

        if (Thread32First(snapshot, &entry))
        {
            do
            {
                if (processIds.find(
                        entry.th32OwnerProcessID) ==
                    processIds.end())
                {
                    continue;
                }

                HANDLE thread = OpenThread(
                    THREAD_QUERY_LIMITED_INFORMATION,
                    FALSE,
                    entry.th32ThreadID);

                if (thread == nullptr)
                    continue;

                FILETIME creation{};
                FILETIME exitTime{};
                FILETIME kernel{};
                FILETIME user{};

                const bool ok = GetThreadTimes(
                    thread, &creation, &exitTime,
                    &kernel, &user) != FALSE;

                CloseHandle(thread);

                if (!ok)
                    continue;

                const std::uint64_t key =
                    Key(
                        entry.th32OwnerProcessID,
                        entry.th32ThreadID);

                seen.insert(key);

                ThreadLoad sample;
                sample.processId = entry.th32OwnerProcessID;
                sample.threadId = entry.th32ThreadID;
                sample.cpuTime100ns =
                    FileTimeTo100ns(kernel) +
                    FileTimeTo100ns(user);

                auto it = previous_.find(key);

                if (it != previous_.end() &&
                    elapsed100ns > 0.0 &&
                    sample.cpuTime100ns >=
                        it->second.cpuTime100ns)
                {
                    const double delta =
                        static_cast<double>(
                            sample.cpuTime100ns -
                            it->second.cpuTime100ns);

                    const double denominator =
                        elapsed100ns *
                        static_cast<double>(
                            std::max<std::size_t>(
                                logicalProcessorCount, 1));

                    sample.usagePercent =
                        denominator > 0.0
                            ? 100.0 * delta / denominator
                            : 0.0;
                }

                previous_[key] = {
                    sample.cpuTime100ns, sequence_ };

                result.push_back(sample);
            }
            while (Thread32Next(snapshot, &entry));
        }

        CloseHandle(snapshot);

        for (auto it = previous_.begin();
             it != previous_.end();)
        {
            if (seen.find(it->first) == seen.end())
                it = previous_.erase(it);
            else
                ++it;
        }

        return result;
    }
}
