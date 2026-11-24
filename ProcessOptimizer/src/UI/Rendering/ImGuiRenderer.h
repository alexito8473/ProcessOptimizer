#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <imgui.h>

#include <cstdint>

class D3D11RegionTexture;

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

    ID3D11Device* GetDevice() const;
    ID3D11DeviceContext* GetContext() const;
    ID3D11DeviceContext* GetDeviceContext() const;
    ID3D11RenderTargetView* GetRenderTarget() const;

    ImGuiContext* GetImGuiContext() const;

    bool IsInitialized() const;

    std::uint64_t GetPresentCount() const;

    bool BeginRegion(
        D3D11RegionTexture& region,
        const float clearColor[4]);

    void EndRegion();

    void Shutdown();

private:
    bool CreateDevice(HWND hwnd);
    bool CreateRenderTarget();
    void RestoreRenderTarget();

    static std::uint64_t HashDrawData(const ImDrawData& drawData);

private:
    HWND hwnd_ = nullptr;

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    IDXGISwapChain* swapChain_ = nullptr;
    ID3D11RenderTargetView* renderTarget_ = nullptr;

    ID3D11RenderTargetView* previousRenderTarget_ = nullptr;
    ID3D11DepthStencilView* previousDepthStencil_ = nullptr;

    D3D11_VIEWPORT previousViewports[
        D3D11_VIEWPORT_AND_SCISSORRECT_MAX_INDEX]{};

        UINT previousViewportCount_ = 0;

        bool regionActive_ = false;
        bool initialized_ = false;
        bool imguiContextCreated_ = false;
        bool implotContextCreated_ = false;
        bool win32BackendInitialized_ = false;
        bool dx11BackendInitialized_ = false;

        std::uint64_t lastDrawHash_ = 0;
        std::uint64_t presentCount_ = 0;

        D3D11_VIEWPORT previousViewports_[
            D3D11_VIEWPORT_AND_SCISSORRECT_MAX_INDEX]{};
};