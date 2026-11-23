
#pragma once

#include <Windows.h>

#include <cstdint>
#include <string>
#include <vector>

enum class ProcessPowerLevel
{
    Unknown,
    VeryLow,
    Low,
    Moderate,
    High,
    VeryHigh
};

struct ProcessInfo
{
    // Identification
    DWORD processId = 0;
    DWORD parentProcessId = 0;

    std::wstring name;
    std::wstring executableName;
    std::wstring executablePath;
    std::wstring publisher;

    // Application information
    std::wstring commandLine;
    std::wstring userName;

    // Process state
    bool isApplication = false;
    bool isResponding = true;
    bool isSuspended = false;
    bool isElevated = false;

    // Architecture
    bool is64Bit = false;

    // CPU
    double cpuUsagePercent = 0.0;
    std::uint64_t cpuTime100ns = 0;

    // Memory
    std::uint64_t workingSetBytes = 0;
    std::uint64_t privateMemoryBytes = 0;
    std::uint64_t sharedMemoryBytes = 0;
    std::uint64_t peakWorkingSetBytes = 0;

    // Disk I/O
    std::uint64_t diskReadBytes = 0;
    std::uint64_t diskWriteBytes = 0;
    double diskReadBytesPerSecond = 0.0;
    double diskWriteBytesPerSecond = 0.0;

    // Network I/O
    std::uint64_t networkReceiveBytes = 0;
    std::uint64_t networkSendBytes = 0;
    double networkReceiveBytesPerSecond = 0.0;
    double networkSendBytesPerSecond = 0.0;

    // GPU
    double gpuUsagePercent = 0.0;
    std::wstring gpuEngine;
    std::uint64_t gpuDedicatedMemoryBytes = 0;
    std::uint64_t gpuSharedMemoryBytes = 0;

    // Energy
    ProcessPowerLevel powerLevel = ProcessPowerLevel::Unknown;
    ProcessPowerLevel powerTrend = ProcessPowerLevel::Unknown;

    // Isolation
    bool isAppContainer = false;
    bool isIsolated = false;

    // Icon
    std::uintptr_t iconHandle = 0;
};

struct ProcessSnapshot
{
    std::vector<ProcessInfo> processes;
};
