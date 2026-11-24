#pragma once

#include "Protocol.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_set>

namespace IPC
{
    class PipeServer
    {
    public:
        using StateProvider = std::function<State()>;

        explicit PipeServer(StateProvider provider);
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
        std::atomic<bool> running_{false};
        HANDLE listenPipe_ = INVALID_HANDLE_VALUE;

        std::mutex clientsMutex_;
        std::unordered_set<HANDLE> clients_;
        std::atomic<int> activeClients_{0};

        std::thread acceptThread_;
    };

    class PipeClient
    {
    public:
        bool Ping();
        bool GetState(State& state);

    private:
        bool RoundTrip(
            Request request,
            ResponseType expected,
            std::vector<std::uint8_t>& payload);
    };
}
