#include "AbController.h"

#include "EnergyManager.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <utility>

namespace Optimization
{
    void AbController::Accumulator::Add(
        const FrameProbeSnapshot& snapshot)
    {
        if (!snapshot.active || snapshot.sampleCount == 0 ||
            snapshot.lastUpdateMs == lastUpdateMs)
        {
            return;
        }

        lastUpdateMs = snapshot.lastUpdateMs;

        // FrameProbe publica una ventana movil de 5 s cada segundo. Escalar
        // al segundo evita contar la misma muestra cinco veces.
        const double windowMs =
            snapshot.sampleWindowMs > 0
                ? static_cast<double>(snapshot.sampleWindowMs)
                : 5000.0;
        const double scale =
            (std::min)(1.0, 1000.0 / windowMs);
        const double weight =
            static_cast<double>(snapshot.sampleCount) * scale;

        samples += weight;
        weightedP50 += snapshot.p50Ms * weight;
        weightedP95 += snapshot.p95Ms * weight;
        weightedP99 += snapshot.p99Ms * weight;
        weightedFps += snapshot.fps * weight;
        weightedCpu += snapshot.avgCpuInPresentMs * weight;
        framesOver20Ms +=
            static_cast<double>(snapshot.framesOver20Ms) * scale;
        framesOver25Ms +=
            static_cast<double>(snapshot.framesOver25Ms) * scale;
    }

    FrameMeasurement AbController::Accumulator::Measurement() const
    {
        FrameMeasurement result{};
        result.sampleCount = static_cast<std::uint32_t>(samples);
        result.framesOver20Ms =
            static_cast<std::uint32_t>(framesOver20Ms + 0.5);
        result.framesOver25Ms =
            static_cast<std::uint32_t>(framesOver25Ms + 0.5);
        if (samples == 0)
            return result;

        const double divisor = static_cast<double>(samples);
        result.p50Ms = weightedP50 / divisor;
        result.p95Ms = weightedP95 / divisor;
        result.p99Ms = weightedP99 / divisor;
        result.fps = weightedFps / divisor;
        result.cpuInPresentMs = weightedCpu / divisor;
        return result;
    }

    AbController::AbController(Publisher publisher)
        : publisher_(std::move(publisher))
    {
    }

    AbController::~AbController()
    {
        Stop();
    }

    bool AbController::Start(
        std::uint32_t phaseSeconds,
        bool applyCandidate,
        FrameProbe* probe,
        EnergyManager* energy)
    {
        if (!probe)
            return false;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (started_)
                return false;

            phaseSeconds_ = (std::max)(8U, (std::min)(phaseSeconds, 300U));
            applyCandidate_ = applyCandidate;
            probe_ = probe;
            energy_ = energy;
            elapsed_ = 0;
            baseline_ = Accumulator{};
            candidate_ = Accumulator{};
            result_ = AbResult{};
            phase_ = Phase::Baseline;
            started_ = true;
            result_.running = true;
            result_.phase = "Linea base sin cambios";
            result_.progressPercent = 0;
            result_.verdict = "Midiendo linea base";
        }

        if (energy)
            energy->SetExperimentalProfile(false);

        Publish();
        return true;
    }

    void AbController::Stop()
    {
        bool shouldPublish = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!started_)
                return;

            if (phase_ != Phase::Done && energy_)
                energy_->SetExperimentalProfile(false);

            started_ = false;
            phase_ = Phase::Idle;
            probe_ = nullptr;
            energy_ = nullptr;
            result_.running = false;
            shouldPublish = true;
        }
        if (shouldPublish)
            Publish();
    }

    void AbController::Tick()
    {
        bool finish = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!started_ || !probe_ || phase_ == Phase::Done)
                return;

            const FrameProbeSnapshot frame = probe_->Snapshot();
            if (phase_ == Phase::Baseline)
                baseline_.Add(frame);
            else if (phase_ == Phase::Candidate)
                candidate_.Add(frame);

            ++elapsed_;
            const std::uint64_t totalTicks =
                static_cast<std::uint64_t>(phaseSeconds_) * 2;
            const std::uint64_t progress =
                (static_cast<std::uint64_t>(elapsed_) * 100) / totalTicks;
            result_.progressPercent = static_cast<std::uint32_t>(
                (std::min)(std::uint64_t{ 100 }, progress));

            // 60 s de linea base + 60 s de candidato, no 30 + 30.
            if (elapsed_ >= phaseSeconds_ && phase_ == Phase::Baseline)
            {
                phase_ = Phase::Candidate;
                result_.phase = "Candidato con perfil experimental";
                if (applyCandidate_ && energy_)
                    energy_->SetExperimentalProfile(true);
            }
            else if (elapsed_ >= totalTicks)
            {
                finish = true;
            }
        }

        if (finish)
            Finish();
        else
            Publish();
    }

    AbResult AbController::Snapshot() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return result_;
    }

    void AbController::Finish()
    {
        bool keep = false;
        bool applyCandidate = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);

            applyCandidate = applyCandidate_;
            result_.baseline = baseline_.Measurement();
            result_.candidate = candidate_.Measurement();
            result_.running = false;
            result_.completed = true;
            result_.progressPercent = 100;
            result_.phase = "Completado";
            phase_ = Phase::Done;
            started_ = false;

            if (result_.baseline.sampleCount < 30 ||
                result_.candidate.sampleCount < 30)
            {
                result_.verdict =
                    "Datos insuficientes: conserva la configuracion anterior";
                result_.kept = false;
            }
            else
            {
                result_.p95DeltaPercent =
                    (result_.candidate.p95Ms -
                     result_.baseline.p95Ms) /
                    (result_.baseline.p95Ms > 0.0
                        ? result_.baseline.p95Ms : 1.0) * 100.0;
                result_.p99DeltaPercent =
                    (result_.candidate.p99Ms -
                     result_.baseline.p99Ms) /
                    (result_.baseline.p99Ms > 0.0
                        ? result_.baseline.p99Ms : 1.0) * 100.0;

                const bool p95Better = result_.p95DeltaPercent <= -2.0;
                const bool p99Safe = result_.p99DeltaPercent <= 1.0;
                const bool badFramesSafe =
                    result_.candidate.framesOver25Ms <=
                    result_.baseline.framesOver25Ms;
                const bool cpuSafe =
                    result_.candidate.cpuInPresentMs <=
                    result_.baseline.cpuInPresentMs * 1.05 + 0.5;

                keep = applyCandidate &&
                       p95Better && p99Safe && badFramesSafe && cpuSafe;
                result_.kept = keep;
                if (!applyCandidate)
                {
                    result_.verdict =
                        "Solo medicion: candidato no aplicado";
                }
                else
                {
                    result_.verdict = keep
                        ? "A/B positivo: mejora p95 sin regresion "
                          "(hasta detener el servicio)"
                        : "A/B negativo: se restauró la configuracion";
                }
            }
        }

        if (energy_)
            energy_->SetExperimentalProfile(keep);

        WriteCsv();
        Publish();
    }

    void AbController::WriteCsv()
    {
        wchar_t programData[MAX_PATH]{};
        const DWORD size = GetEnvironmentVariableW(
            L"ProgramData", programData,
            sizeof(programData) / sizeof(programData[0]));
        if (size == 0 || size >= sizeof(programData) / sizeof(programData[0]))
            return;

        wchar_t directory[MAX_PATH]{};
        std::swprintf(
            directory, sizeof(directory) / sizeof(directory[0]),
            L"%s\\ProcessOptimizer", programData);
        CreateDirectoryW(directory, nullptr);

        wchar_t path[MAX_PATH]{};
        std::swprintf(
            path, sizeof(path) / sizeof(path[0]),
            L"%s\\ab-result-%llu.csv",
            directory, static_cast<unsigned long long>(GetTickCount64()));

        FILE* file = nullptr;
        if (_wfopen_s(&file, path, L"w, ccs=UTF-8") != 0 || !file)
            return;

        AbResult result = Snapshot();
        std::fwprintf(
            file,
            L"phase;samples;p50Ms;p95Ms;p99Ms;fps;over20;over25;"
            L"cpuInPresentMs\n"
            L"baseline;%u;%.3f;%.3f;%.3f;%.3f;%u;%u;%.3f\n"
            L"candidate;%u;%.3f;%.3f;%.3f;%.3f;%u;%u;%.3f\n"
            L"verdict;%hs\n",
            result.baseline.sampleCount, result.baseline.p50Ms,
            result.baseline.p95Ms, result.baseline.p99Ms,
            result.baseline.fps, result.baseline.framesOver20Ms,
            result.baseline.framesOver25Ms, result.baseline.cpuInPresentMs,
            result.candidate.sampleCount, result.candidate.p50Ms,
            result.candidate.p95Ms, result.candidate.p99Ms,
            result.candidate.fps, result.candidate.framesOver20Ms,
            result.candidate.framesOver25Ms, result.candidate.cpuInPresentMs,
            result.verdict.c_str());
        std::fclose(file);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            result_.csvPath = std::filesystem::path(path).string();
        }
    }

    void AbController::Publish()
    {
        if (!publisher_)
            return;
        publisher_(Snapshot());
    }
}
