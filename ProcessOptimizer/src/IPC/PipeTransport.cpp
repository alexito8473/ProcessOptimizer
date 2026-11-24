#include "PipeTransport.h"

#include <algorithm>
#include <chrono>
#include <cstdio>

namespace IPC
{
    namespace
    {
        constexpr DWORD PipeBufferSize = 512 * 1024;

        bool ReadExact(HANDLE pipe, void* data, DWORD size)
        {
            auto* bytes = static_cast<std::uint8_t*>(data);
            DWORD total = 0;

            while (total < size)
            {
                DWORD read = 0;
                if (!ReadFile(
                        pipe, bytes + total, size - total,
                        &read, nullptr))
                {
                    return false;
                }

                if (read == 0)
                    return false;

                total += read;
            }

            return true;
        }

        bool WriteExact(HANDLE pipe, const void* data, DWORD size)
        {
            const auto* bytes =
                static_cast<const std::uint8_t*>(data);
            DWORD total = 0;

            while (total < size)
            {
                DWORD written = 0;
                if (!WriteFile(
                        pipe, bytes + total, size - total,
                        &written, nullptr))
                {
                    return false;
                }

                if (written == 0)
                    return false;

                total += written;
            }

            return true;
        }

        std::uint64_t NowMs()
        {
            return static_cast<std::uint64_t>(
                GetTickCount64());
        }
    }

    PipeServer::PipeServer(StateProvider provider)
        : provider_(std::move(provider))
    {
    }

    PipeServer::~PipeServer()
    {
        Stop();
    }

    bool PipeServer::Start()
    {
        bool expected = false;
        if (!running_.compare_exchange_strong(
                expected, true))
        {
            return false;
        }

        acceptThread_ = std::thread(&PipeServer::AcceptLoop, this);
        return true;
    }

    void PipeServer::Stop()
    {
        if (!running_.exchange(false))
            return;

        HANDLE listen = listenPipe_;
        if (listen != INVALID_HANDLE_VALUE)
        {
            CloseHandle(listen);
            listenPipe_ = INVALID_HANDLE_VALUE;
        }

        std::vector<HANDLE> openClients;
        {
            std::lock_guard<std::mutex> lock(clientsMutex_);
            openClients.assign(
                clients_.begin(), clients_.end());
            clients_.clear();
        }

        // Cerrar el pipe fuerza el retorno de ReadFile en los hilos
        // de cliente que estuvieran esperando una peticion.
        for (HANDLE pipe : openClients)
            CloseHandle(pipe);

        if (acceptThread_.joinable())
            acceptThread_.join();

        for (int i = 0; i < 100 && activeClients_.load() > 0; ++i)
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(20));
        }
    }

    void PipeServer::RegisterClient(HANDLE pipe)
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        clients_.insert(pipe);
    }

    void PipeServer::UnregisterClient(HANDLE pipe)
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        clients_.erase(pipe);
    }

    void PipeServer::CloseClient(HANDLE pipe)
    {
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
        UnregisterClient(pipe);
        --activeClients_;
    }

    void PipeServer::AcceptLoop()
    {
        while (running_.load())
        {
            HANDLE pipe = CreateNamedPipeW(
                PipeName,
                PIPE_ACCESS_DUPLEX,
                PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE |
                    PIPE_WAIT,
                PIPE_UNLIMITED_INSTANCES,
                PipeBufferSize,
                PipeBufferSize,
                0,
                nullptr);

            if (pipe == INVALID_HANDLE_VALUE)
            {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(200));
                continue;
            }

            listenPipe_ = pipe;

            BOOL connected = ConnectNamedPipe(pipe, nullptr);
            if (!connected &&
                GetLastError() != ERROR_PIPE_CONNECTED)
            {
                CloseHandle(pipe);
                listenPipe_ = INVALID_HANDLE_VALUE;

                if (running_.load())
                {
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(100));
                }
                continue;
            }

            listenPipe_ = INVALID_HANDLE_VALUE;

            if (!running_.load())
            {
                CloseHandle(pipe);
                break;
            }

            RegisterClient(pipe);
            ++activeClients_;

            std::thread(&PipeServer::ServeClient, this, pipe)
                .detach();
        }
    }

    void PipeServer::ServeClient(HANDLE pipe)
    {
        std::uint32_t rawRequest = 0;

        if (!ReadExact(
                pipe, &rawRequest, sizeof(rawRequest)))
        {
            CloseClient(pipe);
            return;
        }

        const auto request =
            static_cast<Request>(rawRequest);

        ResponseHeader header{};
        header.sequence = 0;
        header.timestampMs = NowMs();

        std::vector<std::uint8_t> payload;

        switch (request)
        {
        case Request::Ping:
            header.type =
                static_cast<std::uint32_t>(
                    ResponseType::Pong);
            break;

        case Request::GetState:
        {
            const State state = provider_();
            payload = Serialize(state);
            header.sequence = state.sequence;
            header.timestampMs = state.timestampMs;
            header.type =
                static_cast<std::uint32_t>(
                    ResponseType::State);
            break;
        }

        default:
            header.type =
                static_cast<std::uint32_t>(
                    ResponseType::Error);
            break;
        }

        header.payloadSize =
            static_cast<std::uint32_t>(payload.size());

        bool ok = WriteExact(
                      pipe, &header, sizeof(header)) &&
                  (payload.empty() ||
                   WriteExact(
                       pipe, payload.data(),
                       static_cast<DWORD>(payload.size())));

        if (ok)
            FlushFileBuffers(pipe);

        CloseClient(pipe);
    }

    bool PipeClient::Ping()
    {
        std::vector<std::uint8_t> payload;
        return RoundTrip(
            Request::Ping, ResponseType::Pong, payload);
    }

    bool PipeClient::GetState(State& state)
    {
        std::vector<std::uint8_t> payload;
        if (!RoundTrip(
                Request::GetState,
                ResponseType::State,
                payload))
        {
            return false;
        }

        return Deserialize(
            payload.data(), payload.size(), state);
    }

    bool PipeClient::RoundTrip(
        Request request,
        ResponseType expected,
        std::vector<std::uint8_t>& payload)
    {
        HANDLE pipe = CreateFileW(
            PipeName,
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr);

        if (pipe == INVALID_HANDLE_VALUE)
            return false;

        DWORD mode = PIPE_READMODE_MESSAGE;
        SetNamedPipeHandleState(
            pipe, &mode, nullptr, nullptr);

        const std::uint32_t rawRequest =
            static_cast<std::uint32_t>(request);

        bool ok = WriteExact(
            pipe, &rawRequest, sizeof(rawRequest));

        ResponseHeader header{};
        if (ok)
        {
            ok = ReadExact(
                pipe, &header, sizeof(header));
        }

        if (ok &&
            (header.magic != ProtocolMagic ||
             header.version != ProtocolVersion ||
             header.type !=
                 static_cast<std::uint32_t>(expected)))
        {
            ok = false;
        }

        if (ok && header.payloadSize != 0)
        {
            payload.resize(header.payloadSize);
            ok = ReadExact(
                pipe, payload.data(),
                static_cast<DWORD>(payload.size()));
        }
        else
        {
            payload.clear();
        }

        CloseHandle(pipe);
        return ok;
    }
}
