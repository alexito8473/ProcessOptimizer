#pragma once

#include "../IPC/PipeTransport.h"
#include "../Monitoring/SystemMonitor/SystemMonitor.h"
#include "MonitorState.h"

#include <cstddef>
#include <memory>

namespace Service
{
    class ServiceApp
    {
    public:
        ServiceApp() = default;
        ~ServiceApp();

        ServiceApp(const ServiceApp&) = delete;
        ServiceApp& operator=(const ServiceApp&) = delete;

        bool Start();
        void Stop();

        const MonitorState& State() const
        {
            return state_;
        }

    private:
        EventSystem events_;
        std::shared_ptr<SystemMonitor> monitor_;
        std::unique_ptr<IPC::PipeServer> pipe_;
        MonitorState state_;

        bool subscribed_ = false;
        std::size_t cpuSubscription_ = 0;
        std::size_t memorySubscription_ = 0;
        std::size_t gpuSubscription_ = 0;
        std::size_t processSubscription_ = 0;
    };
}
