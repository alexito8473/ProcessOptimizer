#pragma once

#include "../IPC/PipeTransport.h"

#include <Windows.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace Tray
{
    class TrayApp
    {
    public:
        bool Run(HINSTANCE instance);

    private:
        static LRESULT CALLBACK WindowProc(
            HWND hwnd, UINT message,
            WPARAM wParam, LPARAM lParam);
        static LRESULT CALLBACK PopupProc(
            HWND hwnd, UINT message,
            WPARAM wParam, LPARAM lParam);

        void PollLoop();
        void UpdateTooltip();
        void ShowPopup();
        void HidePopup();
        void ShowMenu(POINT point);
        void LaunchUi() const;

        HINSTANCE instance_ = nullptr;
        HWND hwnd_ = nullptr;
        HWND popup_ = nullptr;
        HICON icon_ = nullptr;
        NOTIFYICONDATAW notifyData_{};

        std::atomic<bool> running_{ false };
        std::thread pollThread_;

        mutable std::mutex stateMutex_;
        IPC::State state_;
        bool connected_ = false;
        std::wstring popupText_;
    };
}
