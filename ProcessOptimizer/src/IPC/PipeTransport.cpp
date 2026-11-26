#include "PipeTransport.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <sddl.h>

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

        // El servicio corre como LocalSystem. Sin una DACL explicita, el
        // pipe hereda una lista que puede denegar el acceso a la UI y al
        // tray cuando se ejecutan como usuario normal.
        PSECURITY_DESCRIPTOR CreatePipeSecurityDescriptor()
        {
            PSECURITY_DESCRIPTOR descriptor = nullptr;

            ConvertStringSecurityDescriptorToSecurityDescriptorW(
                L"D:P"
                L"(A;;GA;;;SY)"  // SYSTEM
                L"(A;;GA;;;BA)"  // Administradores
                L"(A;;GA;;;AU)"  // Usuarios autenticados
                L"(A;;GA;;;BU)"  // Usuarios
                L"(A;;GA;;;IU)", // Usuarios interactivos
                SDDL_REVISION_1,
                &descriptor,
                nullptr);

            return descriptor;
        }
    }

    PipeServer::PipeServer(
        StateProvider provider,
        CommandHandler handler)
        : provider_(std::move(provider)),
          handler_(std::move(handler))
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

        HANDLE listen = listenPipe_.exchange(
            INVALID_HANDLE_VALUE);

        if (listen != INVALID_HANDLE_VALUE)
        {
            // AcceptLoop es el propietario del pipe de escucha y lo
            // cierra al despertar. Aqui solo se cancela la espera.
            CancelIoEx(listen, nullptr);
        }

        // Cada ServeClient es el unico propietario de su handle: aqui
        // solo se cancela la lectura bloqueante. Cerrarlo desde Stop
        // provocaba un doble CloseHandle y cierre de handles reutilizados.
        {
            std::lock_guard<std::mutex> lock(clientsMutex_);
            for (HANDLE pipe : clients_)
                CancelIoEx(pipe, nullptr);
        }

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
            PSECURITY_DESCRIPTOR security =
                CreatePipeSecurityDescriptor();

            SECURITY_ATTRIBUTES securityAttributes{};
            securityAttributes.nLength =
                sizeof(SECURITY_ATTRIBUTES);
            securityAttributes.lpSecurityDescriptor =
                security;
            securityAttributes.bInheritHandle = FALSE;

            HANDLE pipe = CreateNamedPipeW(
                PipeName,
                PIPE_ACCESS_DUPLEX,
                PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE |
                    PIPE_WAIT,
                PIPE_UNLIMITED_INSTANCES,
                PipeBufferSize,
                PipeBufferSize,
                0,
                &securityAttributes);

            if (security != nullptr)
                LocalFree(security);

            if (pipe == INVALID_HANDLE_VALUE)
            {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(200));
                continue;
            }

            listenPipe_ = pipe;

            if (!running_.load())
            {
                listenPipe_.exchange(INVALID_HANDLE_VALUE);
                CloseHandle(pipe);
                break;
            }

            BOOL connected = ConnectNamedPipe(pipe, nullptr);
            if (!connected &&
                GetLastError() != ERROR_PIPE_CONNECTED)
            {
                // Unico propietario del handle: el propio AcceptLoop.
                listenPipe_.exchange(INVALID_HANDLE_VALUE);
                CloseHandle(pipe);

                if (running_.load())
                {
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(100));
                }
                continue;
            }

            listenPipe_.exchange(INVALID_HANDLE_VALUE);

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
        std::uint32_t requestSize = 0;

        bool ok = ReadExact(
                      pipe, &rawRequest, sizeof(rawRequest)) &&
                  ReadExact(
                      pipe, &requestSize, sizeof(requestSize));

        constexpr std::uint32_t MaxRequestSize =
            1024 * 1024;

        if (!ok || requestSize > MaxRequestSize)
        {
            CloseClient(pipe);
            return;
        }

        std::vector<std::uint8_t> requestPayload(
            requestSize);

        if (requestSize != 0 &&
            !ReadExact(
                pipe,
                requestPayload.data(),
                requestSize))
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
            const auto state = provider_();

            if (!state)
            {
                header.type =
                    static_cast<std::uint32_t>(
                        ResponseType::Error);
                break;
            }

            {
                std::lock_guard<std::mutex> lock(
                    stateCacheMutex_);

                if (cachedPayload_.empty() ||
                    cachedSequence_ != state->sequence)
                {
                    cachedPayload_ = Serialize(*state);
                    cachedSequence_ = state->sequence;
                }

                payload = cachedPayload_;
            }

            header.sequence = state->sequence;
            header.timestampMs = state->timestampMs;
            header.type =
                static_cast<std::uint32_t>(
                    ResponseType::State);
            break;
        }

        case Request::SetOptimizerConfig:
        case Request::SetForegroundProcess:
        case Request::StartBenchmark:
        case Request::StartAb:
        case Request::StopAb:
        case Request::SetExperimentalProfile:
        case Request::SetEtwStutterProbe:
        case Request::SetContainment:
        {
            const bool handled =
                handler_ &&
                handler_(request, requestPayload);

            header.type =
                static_cast<std::uint32_t>(
                    handled
                        ? ResponseType::Ack
                        : ResponseType::Error);
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

        ok = WriteExact(
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
        std::vector<std::uint8_t> requestPayload;
        std::vector<std::uint8_t> responsePayload;
        return RoundTrip(
            Request::Ping,
            ResponseType::Pong,
            requestPayload,
            responsePayload);
    }

    bool PipeClient::GetState(State& state)
    {
        std::vector<std::uint8_t> requestPayload;
        std::vector<std::uint8_t> responsePayload;

        if (!RoundTrip(
                Request::GetState,
                ResponseType::State,
                requestPayload,
                responsePayload))
        {
            return false;
        }

        return Deserialize(
            responsePayload.data(),
            responsePayload.size(),
            state);
    }

    bool PipeClient::SetOptimizerConfig(
        bool enabled,
        Optimization::OptimizerMode mode)
    {
        OptimizerConfigCommand command;
        command.enabled = enabled;
        command.mode = mode;

        std::vector<std::uint8_t> responsePayload;

        return RoundTrip(
            Request::SetOptimizerConfig,
            ResponseType::Ack,
            Serialize(command),
            responsePayload);
    }

    bool PipeClient::SetForegroundProcess(DWORD processId)
    {
        ForegroundProcessCommand command;
        command.processId = processId;

        std::vector<std::uint8_t> responsePayload;

        return RoundTrip(
            Request::SetForegroundProcess,
            ResponseType::Ack,
            Serialize(command),
            responsePayload);
    }

    bool PipeClient::StartBenchmark(
        std::uint32_t durationMs)
    {
        BenchmarkCommand command;
        command.durationMs = durationMs;

        std::vector<std::uint8_t> responsePayload;

        return RoundTrip(
            Request::StartBenchmark,
            ResponseType::Ack,
            Serialize(command),
            responsePayload);
    }

    bool PipeClient::StartAb(
        std::uint32_t phaseSeconds,
        bool applyCandidate)
    {
        AbCommand command;
        command.phaseSeconds = phaseSeconds;
        command.applyCandidate = applyCandidate;

        std::vector<std::uint8_t> responsePayload;
        return RoundTrip(
            Request::StartAb,
            ResponseType::Ack,
            Serialize(command),
            responsePayload);
    }

    bool PipeClient::StopAb()
    {
        std::vector<std::uint8_t> requestPayload;
        std::vector<std::uint8_t> responsePayload;
        return RoundTrip(
            Request::StopAb,
            ResponseType::Ack,
            requestPayload,
            responsePayload);
    }

    bool PipeClient::SetExperimentalProfile(bool enabled)
    {
        ToggleCommand command;
        command.enabled = enabled;

        std::vector<std::uint8_t> responsePayload;
        return RoundTrip(
            Request::SetExperimentalProfile,
            ResponseType::Ack,
            Serialize(command),
            responsePayload);
    }

    bool PipeClient::SetEtwStutterProbe(bool enabled)
    {
        ToggleCommand command;
        command.enabled = enabled;

        std::vector<std::uint8_t> responsePayload;
        return RoundTrip(
            Request::SetEtwStutterProbe,
            ResponseType::Ack,
            Serialize(command),
            responsePayload);
    }

    bool PipeClient::SetContainment(bool enabled)
    {
        ToggleCommand command;
        command.enabled = enabled;

        std::vector<std::uint8_t> responsePayload;
        return RoundTrip(
            Request::SetContainment,
            ResponseType::Ack,
            Serialize(command),
            responsePayload);
    }

    bool PipeClient::RoundTrip(
        Request request,
        ResponseType expected,
        const std::vector<std::uint8_t>& requestPayload,
        std::vector<std::uint8_t>& responsePayload)
    {
        lastIssue_ = ConnectionIssue::None;
        remoteVersion_ = 0;
        lastError_ = ERROR_SUCCESS;

        HANDLE pipe = CreateFileW(
            PipeName,
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr);

        if (pipe == INVALID_HANDLE_VALUE)
        {
            lastError_ = GetLastError();
            lastIssue_ =
                lastError_ == ERROR_FILE_NOT_FOUND
                    ? ConnectionIssue::NotInstalled
                    : ConnectionIssue::Other;
            return false;
        }

        DWORD mode = PIPE_READMODE_MESSAGE;
        SetNamedPipeHandleState(
            pipe, &mode, nullptr, nullptr);

        const std::uint32_t rawRequest =
            static_cast<std::uint32_t>(request);
        const std::uint32_t rawRequestSize =
            static_cast<std::uint32_t>(
                requestPayload.size());

        bool ok =
            WriteExact(
                pipe, &rawRequest, sizeof(rawRequest)) &&
            WriteExact(
                pipe, &rawRequestSize,
                sizeof(rawRequestSize));

        if (ok && rawRequestSize != 0)
        {
            ok = WriteExact(
                pipe,
                requestPayload.data(),
                static_cast<DWORD>(
                    requestPayload.size()));
        }

        ResponseHeader header{};
        if (ok)
        {
            ok = ReadExact(
                pipe, &header, sizeof(header));
        }
        if (!ok)
            lastError_ = GetLastError();

        if (ok &&
            header.magic == ProtocolMagic &&
            header.version != ProtocolVersion)
        {
            remoteVersion_ = header.version;
            lastIssue_ = ConnectionIssue::VersionMismatch;
        }

        if (ok &&
            (header.magic != ProtocolMagic ||
             header.version != ProtocolVersion ||
             header.type !=
                 static_cast<std::uint32_t>(expected)))
        {
            ok = false;
        }

        if (ok &&
            header.payloadSize >
                static_cast<std::uint32_t>(PipeBufferSize))
        {
            ok = false;
        }

        if (ok && header.payloadSize != 0)
        {
            responsePayload.resize(
                header.payloadSize);
            ok = ReadExact(
                pipe, responsePayload.data(),
                static_cast<DWORD>(
                    responsePayload.size()));
            if (!ok)
                lastError_ = GetLastError();
        }
        else
        {
            responsePayload.clear();
        }

        if (!ok && lastIssue_ == ConnectionIssue::None)
        {
            lastIssue_ =
                lastError_ == ERROR_SEM_TIMEOUT
                    ? ConnectionIssue::Timeout
                    : ConnectionIssue::Other;
        }

        CloseHandle(pipe);
        return ok;
    }
}
