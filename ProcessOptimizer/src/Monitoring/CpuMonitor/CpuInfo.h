
#pragma once

#include <Windows.h>

#include <cstdint>
#include <string>
#include <vector>

struct CpuPowerInformation
{
    ULONG Number;
    ULONG MaxMhz;
    ULONG CurrentMhz;
    ULONG MhzLimit;
    ULONG MaxIdleState;
    ULONG CurrentIdleState;
};

// ------------------------------------------------------------
// Procesadores lógicos y núcleos
// ------------------------------------------------------------

struct LogicalProcessor
{
    WORD group = 0;
    BYTE logicalIndex = 0;
    double usagePercent = 0.0;
};

struct CpuCore
{
    std::vector<std::size_t> logicalProcessorIndices;
};

struct LogicalProcessorInfo
{
    WORD processorGroup = 0;
    BYTE logicalProcessorIndex = 0;

    std::size_t physicalCoreIndex = 0;

    double usagePercent = 0.0;

    // Frecuencia estimada.
    double estimatedFrequencyMHz = 0.0;

    // Frecuencia efectiva medida por hardware.
    double effectiveFrequencyMHz = 0.0;

    // Rendimiento relativo de Windows.
    double performancePercent = 0.0;
};

struct PhysicalCoreInfo
{
    std::size_t coreIndex = 0;
    std::vector<std::size_t> logicalProcessorIndices;
};

// ------------------------------------------------------------
// Identificación del procesador
// ------------------------------------------------------------

struct CpuProcessorInfo
{
    std::string manufacturer;
    std::string brand;
    std::string architecture;

    DWORD logicalProcessorCount = 0;
    DWORD physicalCoreCount = 0;
    DWORD socketCount = 0;
};

// ------------------------------------------------------------
// Caché
// ------------------------------------------------------------

struct CpuCacheInfo
{
    BYTE level = 0;
    DWORD sizeKB = 0;
    DWORD lineSize = 0;
    BYTE associativity = 0;
};

// ------------------------------------------------------------
// Frecuencias
// ------------------------------------------------------------

struct CpuFrequencyInfo
{
    DWORD baseMHz = 0;

    // Frecuencia estimada mediante contadores de Windows.
    double estimatedMHz = 0.0;

    // Frecuencia efectiva medida mediante hardware.
    double effectiveMHz = 0.0;

    // Porcentaje de rendimiento relativo de Windows.
    double performancePercent = 0.0;

    // Porcentaje de frecuencia máxima informado por Windows.
    double maximumFrequencyPercent = 0.0;
};

// ------------------------------------------------------------
// Virtualización
// ------------------------------------------------------------

struct CpuVirtualizationInfo
{
    bool supported = false;
    bool firmwareEnabled = false;
    bool hypervisorPresent = false;
};

// ------------------------------------------------------------
// Procesos
// ------------------------------------------------------------

struct CpuProcessInfo
{
    DWORD processId = 0;
    std::wstring name;

    DWORD threadCount = 0;
    DWORD handleCount = 0;

    double cpuUsagePercent = 0.0;
};

// ------------------------------------------------------------
// Estadísticas globales
// ------------------------------------------------------------

struct CpuSystemActivity
{
    DWORD processCount = 0;
    DWORD threadCount = 0;
    DWORD handleCount = 0;

    DWORD contextSwitches = 0;
    DWORD interrupts = 0;
};

// ------------------------------------------------------------
// Fotografía completa de CPU
// ------------------------------------------------------------

struct CpuSnapshot
{
    double totalUsagePercent = 0.0;

    CpuProcessorInfo processor;
    CpuFrequencyInfo frequency;
    CpuVirtualizationInfo virtualization;

    std::vector<CpuCacheInfo> caches;

    std::vector<PhysicalCoreInfo> physicalCores;
    std::vector<LogicalProcessorInfo> logicalProcessors;

    std::vector<CpuProcessInfo> processes;

    CpuSystemActivity activity;
};
