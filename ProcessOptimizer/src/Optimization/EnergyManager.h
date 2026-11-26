#pragma once

#include "OptimizationTypes.h"

#include <Windows.h>

#include <functional>
#include <mutex>

namespace Optimization
{
    // Gestion de energia y prioridad de scheduling de GPU.
    // Solo trabaja cuando el perfil experimental esta activado explicitamente.
    // El plan de energia nunca se modifica en sitio: se duplica, se activa y
    // se elimina al desactivar o al apagar el servicio.
    class EnergyManager
    {
    public:
        using Publisher =
            std::function<void(const EnergySnapshot&)>;

        explicit EnergyManager(Publisher publisher);
        ~EnergyManager();

        EnergyManager(const EnergyManager&) = delete;
        EnergyManager& operator=(const EnergyManager&) = delete;

        bool Start();
        void Stop();

        bool SetExperimentalProfile(bool enabled);
        void SetGameProcess(DWORD processId, bool foreground);

        void Refresh();
        EnergySnapshot Snapshot() const;

    private:
        bool ActivateDuplicateScheme();
        void RestoreOriginalScheme();
        bool ReadSchemeValues(EnergySnapshot& snapshot) const;
        void SetGpuPriorityForCurrentProcess();
        void RestoreGpuPriority();
        void Publish();

        Publisher publisher_;
        mutable std::mutex mutex_;

        GUID originalScheme_{};
        GUID duplicateScheme_{};
        bool hasOriginalScheme_ = false;
        bool duplicateActive_ = false;
        bool experimental_ = false;

        DWORD gamePid_ = 0;
        bool gameForeground_ = false;
        bool gpuPriorityApplied_ = false;
        int previousGpuPriority_ = 2; // D3DKMT_SCHEDULINGPRIORITYCLASS_NORMAL
        bool previousGpuPriorityRead_ = false;

        std::uint32_t parkedProcessors_ = 0;
        double minProcessorState_ = 0.0;
        double maxProcessorState_ = 0.0;
        std::int32_t boostMode_ = -1;
        std::string activeSchemeName_ = "Plan actual de Windows";
        std::string gpuPriorityText_ = "Normal";
        std::string status_ = "Sin medir";
        bool available_ = false;
    };
}
