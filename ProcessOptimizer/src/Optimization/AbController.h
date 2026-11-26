#pragma once

#include "OptimizationTypes.h"

#include "FrameProbe.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

namespace Optimization
{
    class EnergyManager;

    // Prueba A/B de 60 s + 60 s. Solo acumula agregados de FrameProbe,
    // nunca muestras por frame a traves de IPC.
    class AbController
    {
    public:
        using Publisher = std::function<void(const AbResult&)>;

        explicit AbController(Publisher publisher);
        ~AbController();

        AbController(const AbController&) = delete;
        AbController& operator=(const AbController&) = delete;

        bool Start(
            std::uint32_t phaseSeconds,
            bool applyCandidate,
            FrameProbe* probe,
            EnergyManager* energy);
        void Stop();
        void Tick();
        AbResult Snapshot() const;

    private:
        struct Accumulator
        {
            std::uint64_t samples = 0;
            double weightedP50 = 0.0;
            double weightedP95 = 0.0;
            double weightedP99 = 0.0;
            double weightedFps = 0.0;
            double weightedCpu = 0.0;
            double framesOver20Ms = 0.0;
            double framesOver25Ms = 0.0;
            std::uint64_t lastUpdateMs = 0;

            void Add(const FrameProbeSnapshot& snapshot);
            FrameMeasurement Measurement() const;
        };

        enum class Phase
        {
            Idle,
            Baseline,
            Candidate,
            Done
        };

        void SetPhase(Phase phase, const std::string& text);
        void Finish();
        void WriteCsv();
        void Publish();

        Publisher publisher_;
        mutable std::mutex mutex_;

        Phase phase_ = Phase::Idle;
        std::uint32_t phaseSeconds_ = 60;
        std::uint32_t elapsed_ = 0;
        bool applyCandidate_ = true;
        bool started_ = false;
        FrameProbe* probe_ = nullptr;
        EnergyManager* energy_ = nullptr;
        Accumulator baseline_;
        Accumulator candidate_;
        AbResult result_;
    };
}
