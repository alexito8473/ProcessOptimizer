#pragma once

#include <cstdint>
#include <string>
#include <vector>
enum class GpuEngineType
{
    Unknown,
    ThreeD,
    Copy,
    VideoDecode,
    VideoEncode,
    VideoProcessing,
    Compute,
    Graphics,
    Overlay
};

struct GpuEngineInfo
{
    GpuEngineType type = GpuEngineType::Unknown;
    std::wstring name;
    double usagePercent = 0.0;
};
struct GpuAdapterInfo
{
    // Identificación
    std::uint32_t index = 0;
    std::wstring name;

    std::uint32_t vendorId = 0;
    std::uint32_t deviceId = 0;
    std::uint32_t subsystemId = 0;
    std::uint32_t revision = 0;

    // Memoria
    std::uint64_t dedicatedVideoMemoryBytes = 0;
    std::uint64_t dedicatedSystemMemoryBytes = 0;
    std::uint64_t sharedSystemMemoryBytes = 0;

    // Identificación del adaptador en Windows
    std::uint32_t luidLowPart = 0;
    std::int32_t luidHighPart = 0;

    bool isSoftware = false;
    // Memoria dedicada
    // Memoria utilizada por el proceso ProcessOptimizer
    std::uint64_t processDedicatedMemoryUsedBytes = 0;
    std::uint64_t processDedicatedMemoryBudgetBytes = 0;
    double processDedicatedMemoryUsagePercent = 0.0;

    std::uint64_t processSharedMemoryUsedBytes = 0;
    std::uint64_t processSharedMemoryBudgetBytes = 0;
    double processSharedMemoryUsagePercent = 0.0;
    // Uso general
    double usagePercent = 0.0;

    // Uso por motor
    std::vector<GpuEngineInfo> engines;

    // Motores principales
    double usage3D = 0.0;
    double usageCopy = 0.0;
    double usageVideoDecode = 0.0;
    double usageVideoEncode = 0.0;
    double usageVideoProcessing = 0.0;
    double usageCompute = 0.0;

    // Información del controlador
    std::wstring driverProvider;
    std::wstring driverVersion;
    std::wstring driverDate;
    std::wstring driverDescription;
    std::wstring deviceLocation;
    bool driverInfoAvailable = false;
    // Capacidades gráficas
    std::wstring direct3DVersion;
    std::wstring featureLevel;

    bool d3d11Supported = false;
    bool d3d12Supported = false;
    bool directComputeSupported = false;
    std::wstring wddmVersion;
    bool wddmInfoAvailable = false;

    // Sensores y frecuencias
    double temperatureCelsius = 0.0;

    std::uint32_t coreClockMHz = 0;
    std::uint32_t memoryClockMHz = 0;

    std::uint32_t fanSpeedPercent = 0;
    std::uint32_t fanSpeedRPM = 0;

    double powerUsageWatts = 0.0;
    double powerLimitWatts = 0.0;

    bool temperatureAvailable = false;
    bool coreClockAvailable = false;
    bool memoryClockAvailable = false;
    bool fanSpeedAvailable = false;
    bool powerUsageAvailable = false;

    // Ubicación física PCI
    std::uint32_t pciBus = 0;
    std::uint32_t pciDevice = 0;
    std::uint32_t pciFunction = 0;

    bool pciLocationAvailable = false;

    // Tipo de adaptador
    bool isIntegrated = false;
    bool adapterTypeAvailable = false;
};

struct GpuSnapshot
{
    std::vector<GpuAdapterInfo> gpus;
};
