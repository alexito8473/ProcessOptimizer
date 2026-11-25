#pragma once

#include <atomic>
#include <cstdint>

namespace UI::Core
{
    // Cada componente tiene su propio bit de invalidación. Un evento de datos
    // solo marca los componentes que realmente dependen de él.
    enum class UiComponent : std::uint32_t
    {
        OverviewPage   = 1u << 0,
        CpuPage        = 1u << 1,
        MemorySummary  = 1u << 2,
        MemoryChart    = 1u << 3,
        MemoryModules  = 1u << 4,
        GpuPage        = 1u << 5,
        ProcessPage    = 1u << 6,
        OptimizationPage = 1u << 7,

        All = OverviewPage | CpuPage | MemorySummary |
              MemoryChart | MemoryModules | GpuPage |
              ProcessPage | OptimizationPage
    };

    constexpr std::uint32_t Mask(UiComponent component)
    {
        return static_cast<std::uint32_t>(component);
    }

    // Planificador muy ligero estilo Flutter: acumula invalidaciones de
    // varios hilos y las entrega coalescidas en un solo frame.
    class UiScheduler
    {
    public:
        UiScheduler() = default;

        void Invalidate(UiComponent component)
        {
            dirty_.fetch_or(Mask(component), std::memory_order_acq_rel);
        }

        void Invalidate(std::uint32_t mask)
        {
            dirty_.fetch_or(mask, std::memory_order_acq_rel);
        }

        void InvalidateAll()
        {
            dirty_.store(Mask(UiComponent::All), std::memory_order_release);
        }

        std::uint32_t Consume()
        {
            return dirty_.exchange(0, std::memory_order_acq_rel);
        }

        // Consume solo los componentes visibles. Las invalidaciones de otras
        // páginas se conservan para cuando el usuario cambie de página.
        void ConsumePage(std::uint32_t mask)
        {
            dirty_.fetch_and(~mask, std::memory_order_acq_rel);
        }

        bool HasDirty(std::uint32_t mask) const
        {
            return (dirty_.load(std::memory_order_acquire) & mask) != 0;
        }

        bool IsDirty(UiComponent component) const
        {
            return HasDirty(Mask(component));
        }

    private:
        std::atomic<std::uint32_t> dirty_{ Mask(UiComponent::All) };
    };
}
