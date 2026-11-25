#pragma once

#include "Protocol.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_set>

namespace IPC
{
    class PipeServer
    {
    public:
        using StateProvider =
            std::function<std::shared_ptr<const State>()>;
        using CommandHandler = std::function<bool(
            Request request,
            const std::vector<std::uint8_t>& payload)>;

        explicit PipeServer(
            StateProvider provider,
            CommandHandler handler = {});

        ~PipeServer();

        PipeServer(const PipeServer&) = delete;
        PipeServer& operator=(const PipeServer&) = delete;

        bool Start();
        void Stop();

    private:
        void AcceptLoop();
        void ServeClient(HANDLE pipe);
        void CloseClient(HANDLE pipe);
        void RegisterClient(HANDLE pipe);
        void UnregisterClient(HANDLE pipe);

        StateProvider provider_;
        CommandHandler handler_;
        std::atomic<bool> running_{false};
        std::atomic<HANDLE> listenPipe_{ INVALID_HANDLE_VALUE };

        std::mutex clientsMutex_;
        std::unordered_set<HANDLE> clients_;
        std::atomic<int> activeClients_{0};

        // Serializar el estado completo cuesta mas que leerlo. Solo se
        // regenera cuando cambio la secuencia, aunque haya varias UI
        // sondeando el pipe.
        mutable std::mutex stateCacheMutex_;
        std::uint64_t cachedSequence_ = 0;
        std::vector<std::uint8_t> cachedPayload_;

        std::thread acceptThread_;
    };

    class PipeClient
    {
    public:
        bool Ping();
        bool GetState(State& state);
        bool SetOptimizerConfig(
            bool enabled,
            Optimization::OptimizerMode mode);
        bool SetForegroundProcess(DWORD processId);
        bool StartBenchmark(
            std::uint32_t durationMs = 4000);

    private:
        bool RoundTrip(
            Request request,
            ResponseType expected,
            const std::vector<std::uint8_t>& requestPayload,
            std::vector<std::uint8_t>& responsePayload);
    };
}
