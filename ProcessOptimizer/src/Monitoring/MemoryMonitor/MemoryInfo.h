
#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Información individual de cada módulo de memoria RAM.
struct MemoryModuleInfo
{
    bool isPopulated = false;

    std::wstring deviceLocator;
    std::wstring bankLabel;

    std::wstring manufacturer;
    std::wstring partNumber;
    std::wstring serialNumber;

    std::uint64_t capacityBytes = 0;

    // Velocidades expresadas en MT/s.
    std::uint32_t speed = 0;
    std::uint32_t configuredSpeed = 0;

    // Tipo de memoria: DDR3, DDR4, DDR5, etc.
    std::uint16_t memoryType = 0;
};

// Información general de la memoria RAM.
struct MemorySnapshot
{
    // Memoria física
    std::uint64_t totalPhysicalBytes = 0;
    std::uint64_t usedPhysicalBytes = 0;
    std::uint64_t availablePhysicalBytes = 0;

    double usagePercent = 0.0;

    // Memoria comprometida
    std::uint64_t committedBytes = 0;
    std::uint64_t commitLimitBytes = 0;

    // Memoria en caché
    std::uint64_t cachedBytes = 0;

    // Pools del sistema
    std::uint64_t pagedPoolBytes = 0;
    std::uint64_t nonPagedPoolBytes = 0;

    // Información de los módulos
    std::uint32_t totalSlots = 0;
    std::uint32_t usedSlots = 0;

    std::vector<MemoryModuleInfo> modules;

    // Velocidad de actualización de la información física.
    bool moduleInformationAvailable = false;
};
