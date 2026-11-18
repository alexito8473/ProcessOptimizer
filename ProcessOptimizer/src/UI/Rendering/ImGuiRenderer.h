
#pragma once

#include <Windows.h>
#include <d3d11.h>

class ImGuiRenderer
{
public:
    ImGuiRenderer() = default;
    ~ImGuiRenderer();

    ImGuiRenderer(const ImGuiRenderer&) = delete;
    ImGuiRenderer& operator=(const ImGuiRenderer&) = delete;

    bool Initialize(HWND hwnd);

    void BeginFrame();
    void EndFrame();

    void Resize(UINT width, UINT height);

    void Shutdown();

private:
    bool CreateDevice(HWND hwnd);
    bool CreateRenderTarget();

    HWND hwnd_ = nullptr;

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    IDXGISwapChain* swapChain_ = nullptr;
    ID3D11RenderTargetView* renderTarget_ = nullptr;

    bool initialized_ = false;
    bool imguiContextCreated_ = false;
    bool implotContextCreated_ = false;
    bool win32BackendInitialized_ = false;
    bool dx11BackendInitialized_ = false;
};
