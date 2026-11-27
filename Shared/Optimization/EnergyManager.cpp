#include "EnergyManager.h"
#include <PowrProf.h>
#include <d3dkmthk.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace Optimization
{
    namespace
    {
        // Subgrupo y valores documentados de planes de energia de Windows.
        const GUID ProcessorSettingsSubgroup =
            { 0x54533251, 0x82be, 0x4824,
              { 0x96, 0xc1, 0x47, 0xb6, 0x0b, 0x74, 0x0d, 0x00 } };
        const GUID MinProcessorState =
            { 0x893dee8e, 0x2bef, 0x41e0,
              { 0x89, 0xc6, 0xb5, 0x5d, 0x09, 0x29, 0x96, 0x4c } };
        const GUID MaxProcessorState =
            { 0xbc5038f7, 0x23e0, 0x4960,
              { 0x96, 0xda, 0x33, 0xab, 0xaf, 0x59, 0x35, 0xec } };
        const GUID ProcessorBoostMode =
            { 0xbe337238, 0x0d82, 0x4146,
              { 0xa9, 0x60, 0x4f, 0x37, 0x49, 0xd4, 0x70, 0xc7 } };
        // NO_SUBGROUP_GUID se declara en winnt.h; se replica aqui para no
        // depender de Uuid.lib en todos los proyectos.
        const GUID NoSubgroup =
            { 0xfea3413e, 0x7e05, 0x4911,
              { 0x9a, 0x71, 0x70, 0x03, 0x31, 0xf1, 0xc2, 0x94 } };

        using SetGpuClassFn =
            NTSTATUS(APIENTRY*)(HANDLE, D3DKMT_SCHEDULINGPRIORITYCLASS);
        using GetGpuClassFn =
            NTSTATUS(APIENTRY*)(HANDLE, D3DKMT_SCHEDULINGPRIORITYCLASS*);

        const char* PriorityText(int value)
        {
            switch (value)
            {
            case 0: return "Idle";
            case 1: return "Below normal";
            case 2: return "Normal";
            case 3: return "Above normal";
            case 4: return "High";
            case 5: return "Realtime";
            default: return "Desconocida";
            }
        }

        std::string GuidToString(const GUID& guid)
        {
            char buffer[64]{};
            std::snprintf(
                buffer, sizeof(buffer),
                "%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
                guid.Data1, guid.Data2, guid.Data3,
                guid.Data4[0], guid.Data4[1],
                guid.Data4[2], guid.Data4[3], guid.Data4[4],
                guid.Data4[5], guid.Data4[6], guid.Data4[7]);
            return buffer;
        }

        std::string FriendlySchemeName(const GUID& scheme)
        {
            DWORD size = 0;
            const DWORD probe = PowerReadFriendlyName(
                nullptr, &scheme, &NoSubgroup, nullptr, nullptr, &size);
            if ((probe != ERROR_SUCCESS &&
                 probe != ERROR_INSUFFICIENT_BUFFER &&
                 probe != ERROR_MORE_DATA) ||
                size == 0)
            {
                return GuidToString(scheme);
            }

            std::vector<wchar_t> buffer(
                size / sizeof(wchar_t) + 1, L'\0');
            DWORD actual = size;
            if (PowerReadFriendlyName(
                    nullptr, &scheme, &NoSubgroup, nullptr,
                    reinterpret_cast<PUCHAR>(buffer.data()),
                    &actual) != ERROR_SUCCESS)
            {
                return GuidToString(scheme);
            }

            std::wstring wide(
                reinterpret_cast<const wchar_t*>(buffer.data()),
                actual / sizeof(wchar_t));
            const std::size_t nul = wide.find(L'\0');
            if (nul != std::wstring::npos)
                wide.resize(nul);

            const int bytes = WideCharToMultiByte(
                CP_UTF8, 0, wide.data(),
                static_cast<int>(wide.size()), nullptr, 0,
                nullptr, nullptr);
            if (bytes <= 0)
                return GuidToString(scheme);

            std::string result(static_cast<std::size_t>(bytes), '\0');
            WideCharToMultiByte(
                CP_UTF8, 0, wide.data(),
                static_cast<int>(wide.size()), result.data(), bytes,
                nullptr, nullptr);
            return result;
        }
    }

    EnergyManager::EnergyManager(Publisher publisher)
        : publisher_(std::move(publisher))
    {
    }

    EnergyManager::~EnergyManager()
    {
        Stop();
    }

    bool EnergyManager::Start()
    {
        GUID* active = nullptr;
        if (PowerGetActiveScheme(nullptr, &active) != ERROR_SUCCESS)
            return false;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            originalScheme_ = *active;
            hasOriginalScheme_ = true;
            activeSchemeName_ = FriendlySchemeName(originalScheme_);
        }
        LocalFree(active);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            available_ = true;
            status_ = "Plan actual de Windows sin cambios";
        }
        Refresh();
        return true;
    }

    void EnergyManager::Stop()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            RestoreGpuPriority();

            if (duplicateActive_ || hasOriginalScheme_)
                RestoreOriginalScheme();

            available_ = false;
            status_ = "Servicio detenido: energia restaurada";
        }
        Publish();
    }

    bool EnergyManager::SetExperimentalProfile(bool enabled)
    {
        bool ok = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!available_)
            {
                status_ = "No se pudo leer el plan de energia activo";
            }
            else if (enabled == experimental_)
            {
                ok = true;
            }
            else if (enabled && !ActivateDuplicateScheme())
            {
                status_ = "No se pudo duplicar el plan de energia";
            }
            else
            {
                // Restaurar la GPU antes de perder la marca de perfil:
                // de lo contrario el juego se queda en Above normal.
                if (!enabled)
                    RestoreGpuPriority();

                experimental_ = enabled;
                if (!enabled)
                    RestoreOriginalScheme();

                status_ = enabled
                    ? "Perfil experimental: plan duplicado activo"
                    : "Perfil experimental desactivado: plan original activo";
                SetGpuPriorityForCurrentProcess();
                ok = true;
            }
        }

        Refresh();
        return ok;
    }

    void EnergyManager::SetGameProcess(DWORD processId, bool foreground)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (processId == gamePid_ && foreground == gameForeground_)
                return;

            RestoreGpuPriority();
            gamePid_ = processId;
            gameForeground_ = foreground;
            SetGpuPriorityForCurrentProcess();
        }
        Publish();
    }

    void EnergyManager::Refresh()
    {
        EnergySnapshot snapshot{};
        bool hasPower = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            hasPower = available_;
            snapshot.available = available_;
            snapshot.experimentalProfile = experimental_;
            snapshot.parkedProcessors = parkedProcessors_;
            snapshot.gpuPriority = gpuPriorityText_;
            snapshot.status = status_;
            snapshot.customSchemeActive = duplicateActive_;
            snapshot.activeSchemeName = activeSchemeName_;
        }

        if (hasPower && ReadSchemeValues(snapshot))
        {
            GUID* active = nullptr;
            std::string schemeName;
            if (PowerGetActiveScheme(nullptr, &active) == ERROR_SUCCESS &&
                active != nullptr)
            {
                schemeName = FriendlySchemeName(*active);
                if (duplicateActive_)
                    schemeName += " (ProcessOptimizer)";
                LocalFree(active);
            }

            std::lock_guard<std::mutex> lock(mutex_);
            snapshot.available = true;
            parkedProcessors_ = snapshot.parkedProcessors;
            minProcessorState_ = snapshot.minProcessorState;
            maxProcessorState_ = snapshot.maxProcessorState;
            boostMode_ = snapshot.boostMode;
            if (!schemeName.empty())
                activeSchemeName_ = std::move(schemeName);
        }

        Publish();
    }

    EnergySnapshot EnergyManager::Snapshot() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        EnergySnapshot snapshot{};
        snapshot.available = available_;
        snapshot.customSchemeActive = duplicateActive_;
        snapshot.activeSchemeName = activeSchemeName_;
        snapshot.parkedProcessors = parkedProcessors_;
        snapshot.minProcessorState = minProcessorState_;
        snapshot.maxProcessorState = maxProcessorState_;
        snapshot.boostMode = boostMode_;
        snapshot.gpuPriority = gpuPriorityText_;
        snapshot.experimentalProfile = experimental_;
        snapshot.status = status_;
        return snapshot;
    }

    bool EnergyManager::ActivateDuplicateScheme()
    {
        if (duplicateActive_)
            return true;
        if (!hasOriginalScheme_)
            return false;

        GUID* duplicate = nullptr;
        const DWORD result = PowerDuplicateScheme(
            nullptr, &originalScheme_, &duplicate);
        if (result != ERROR_SUCCESS || duplicate == nullptr)
            return false;

        duplicateScheme_ = *duplicate;
        LocalFree(duplicate);

        const DWORD minimum = 5;
        const DWORD maximum = 100;
        const DWORD boost = 1; // Habilitado, sin modo agresivo.

        if (PowerWriteACValueIndex(
                nullptr, &duplicateScheme_, &ProcessorSettingsSubgroup,
                &MinProcessorState, minimum) != ERROR_SUCCESS ||
            PowerWriteACValueIndex(
                nullptr, &duplicateScheme_, &ProcessorSettingsSubgroup,
                &MaxProcessorState, maximum) != ERROR_SUCCESS ||
            PowerWriteACValueIndex(
                nullptr, &duplicateScheme_, &ProcessorSettingsSubgroup,
                &ProcessorBoostMode, boost) != ERROR_SUCCESS ||
            PowerSetActiveScheme(nullptr, &duplicateScheme_) != ERROR_SUCCESS)
        {
            PowerDeleteScheme(nullptr, &duplicateScheme_);
            return false;
        }

        duplicateActive_ = true;
        return true;
    }

    void EnergyManager::RestoreOriginalScheme()
    {
        if (duplicateActive_)
        {
            PowerSetActiveScheme(nullptr, &originalScheme_);
            PowerDeleteScheme(nullptr, &duplicateScheme_);
            duplicateActive_ = false;
        }
    }

    bool EnergyManager::ReadSchemeValues(EnergySnapshot& snapshot) const
    {
        GUID* active = nullptr;
        if (PowerGetActiveScheme(nullptr, &active) != ERROR_SUCCESS)
            return false;

        const GUID* scheme = active;
        std::lock_guard<std::mutex> lock(mutex_);
        if (duplicateActive_)
            scheme = &duplicateScheme_;

        DWORD value = 0;
        if (PowerReadACValueIndex(
                nullptr, scheme, &ProcessorSettingsSubgroup,
                &MinProcessorState, &value) == ERROR_SUCCESS)
        {
            snapshot.minProcessorState = static_cast<double>(value);
        }
        value = 0;
        if (PowerReadACValueIndex(
                nullptr, scheme, &ProcessorSettingsSubgroup,
                &MaxProcessorState, &value) == ERROR_SUCCESS)
        {
            snapshot.maxProcessorState = static_cast<double>(value);
        }
        value = 0;
        if (PowerReadACValueIndex(
                nullptr, scheme, &ProcessorSettingsSubgroup,
                &ProcessorBoostMode, &value) == ERROR_SUCCESS)
        {
            snapshot.boostMode = static_cast<std::int32_t>(value);
        }

        LocalFree(active);
        return true;
    }

    void EnergyManager::SetGpuPriorityForCurrentProcess()
    {
        if (!experimental_ || !gameForeground_ || gamePid_ == 0)
        {
            gpuPriorityText_ = "Normal";
            return;
        }

        HMODULE gdi = LoadLibraryW(L"gdi32.dll");
        if (!gdi)
        {
            gpuPriorityText_ = "No disponible";
            return;
        }

        auto setter = reinterpret_cast<SetGpuClassFn>(
            GetProcAddress(
                gdi, "D3DKMTSetProcessSchedulingPriorityClass"));
        auto getter = reinterpret_cast<GetGpuClassFn>(
            GetProcAddress(
                gdi, "D3DKMTGetProcessSchedulingPriorityClass"));

        HANDLE process = OpenProcess(
            PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_SET_INFORMATION,
            FALSE, gamePid_);
        if (setter && process)
        {
            D3DKMT_SCHEDULINGPRIORITYCLASS previous =
                D3DKMT_SCHEDULINGPRIORITYCLASS_NORMAL;
            if (getter && getter(process, &previous) == 0)
            {
                previousGpuPriority_ = static_cast<int>(previous);
                previousGpuPriorityRead_ = true;
            }
            else
            {
                previousGpuPriorityRead_ = false;
            }

            // Nunca REALTIME: puede perjudicar a DWM y al codificador.
            const NTSTATUS status = setter(
                process,
                D3DKMT_SCHEDULINGPRIORITYCLASS_ABOVE_NORMAL);
            gpuPriorityApplied_ = status == 0;
            gpuPriorityText_ = gpuPriorityApplied_
                ? "Above normal (experimental)"
                : "No se pudo aplicar";
        }

        if (process)
            CloseHandle(process);
        FreeLibrary(gdi);
    }

    void EnergyManager::RestoreGpuPriority()
    {
        if (!gpuPriorityApplied_ || gamePid_ == 0)
            return;

        HMODULE gdi = LoadLibraryW(L"gdi32.dll");
        if (gdi)
        {
            auto setter = reinterpret_cast<SetGpuClassFn>(
                GetProcAddress(
                    gdi, "D3DKMTSetProcessSchedulingPriorityClass"));
            HANDLE process = OpenProcess(
                PROCESS_SET_INFORMATION, FALSE, gamePid_);
            if (setter && process)
            {
                const int value = previousGpuPriorityRead_
                    ? previousGpuPriority_
                    : static_cast<int>(
                        D3DKMT_SCHEDULINGPRIORITYCLASS_NORMAL);
                setter(
                    process,
                    static_cast<D3DKMT_SCHEDULINGPRIORITYCLASS>(value));
            }
            if (process)
                CloseHandle(process);
            FreeLibrary(gdi);
        }

        gpuPriorityApplied_ = false;
        gpuPriorityText_ = "Normal";
    }

    void EnergyManager::Publish()
    {
        if (!publisher_)
            return;

        EnergySnapshot snapshot{};
        {
            std::lock_guard<std::mutex> lock(mutex_);
            snapshot.available = available_;
            snapshot.customSchemeActive = duplicateActive_;
            snapshot.activeSchemeName = activeSchemeName_;
            snapshot.parkedProcessors = parkedProcessors_;
            snapshot.minProcessorState = minProcessorState_;
            snapshot.maxProcessorState = maxProcessorState_;
            snapshot.boostMode = boostMode_;
            snapshot.gpuPriority = gpuPriorityText_;
            snapshot.experimentalProfile = experimental_;
            snapshot.status = status_;
        }
        publisher_(snapshot);
    }
}
