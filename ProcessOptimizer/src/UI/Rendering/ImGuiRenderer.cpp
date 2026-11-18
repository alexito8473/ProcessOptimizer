
#include "ImGuiRenderer.h"

#include <d3d11.h>
#include <dxgi.h>

#include <imgui.h>
#include <implot.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

ImGuiRenderer::~ImGuiRenderer()
{
    Shutdown();
}

bool ImGuiRenderer::Initialize(HWND hwnd)
{
    if (initialized_)
        return true;

    hwnd_ = hwnd;

    if (!CreateDevice(hwnd))
    {
        Shutdown();
        return false;
    }

    IMGUI_CHECKVERSION();

    ImGui::CreateContext();
    imguiContextCreated_ = true;

    ImPlot::CreateContext();
    implotContextCreated_ = true;

    ImGui::StyleColorsDark();

    if (!ImGui_ImplWin32_Init(hwnd))
    {
        Shutdown();
        return false;
    }

    win32BackendInitialized_ = true;

    if (!ImGui_ImplDX11_Init(device_, context_))
    {
        Shutdown();
        return false;
    }

    dx11BackendInitialized_ = true;

    initialized_ = true;

    return true;
}

bool ImGuiRenderer::CreateDevice(HWND hwnd)
{
    DXGI_SWAP_CHAIN_DESC desc{};

    desc.BufferCount = 2;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = hwnd;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL featureLevel{};

    const D3D_FEATURE_LEVEL levels[] =
    {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_0
    };

    HRESULT result = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        levels,
        ARRAYSIZE(levels),
        D3D11_SDK_VERSION,
        &desc,
        &swapChain_,
        &device_,
        &featureLevel,
        &context_
    );

    if (FAILED(result))
        return false;

    return CreateRenderTarget();
}

bool ImGuiRenderer::CreateRenderTarget()
{
    ID3D11Texture2D* backBuffer = nullptr;

    HRESULT result = swapChain_->GetBuffer(
        0,
        IID_PPV_ARGS(&backBuffer)
    );

    if (FAILED(result))
        return false;

    result = device_->CreateRenderTargetView(
        backBuffer,
        nullptr,
        &renderTarget_
    );

    backBuffer->Release();

    return SUCCEEDED(result);
}

void ImGuiRenderer::BeginFrame()
{
    if (!initialized_)
        return;

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();

    ImGui::NewFrame();
}

void ImGuiRenderer::EndFrame()
{
    if (!initialized_)
        return;

    ImGui::Render();

    const float clearColor[] =
    {
        0.055f,
        0.065f,
        0.085f,
        1.0f
    };

    context_->OMSetRenderTargets(
        1,
        &renderTarget_,
        nullptr
    );

    context_->ClearRenderTargetView(
        renderTarget_,
        clearColor
    );

    ImGui_ImplDX11_RenderDrawData(
        ImGui::GetDrawData()
    );

    swapChain_->Present(1, 0);
}

void ImGuiRenderer::Resize(UINT width, UINT height)
{
    if (!initialized_ || width == 0 || height == 0)
        return;

    if (renderTarget_)
    {
        renderTarget_->Release();
        renderTarget_ = nullptr;
    }

    HRESULT result = swapChain_->ResizeBuffers(
        0,
        width,
        height,
        DXGI_FORMAT_UNKNOWN,
        0
    );

    if (FAILED(result))
        return;

    CreateRenderTarget();
}

void ImGuiRenderer::Shutdown()
{
    initialized_ = false;

    if (dx11BackendInitialized_)
    {
        ImGui_ImplDX11_Shutdown();
        dx11BackendInitialized_ = false;
    }

    if (win32BackendInitialized_)
    {
        ImGui_ImplWin32_Shutdown();
        win32BackendInitialized_ = false;
    }

    if (implotContextCreated_)
    {
        ImPlot::DestroyContext();
        implotContextCreated_ = false;
    }

    if (imguiContextCreated_)
    {
        ImGui::DestroyContext();
        imguiContextCreated_ = false;
    }

    if (renderTarget_)
    {
        renderTarget_->Release();
        renderTarget_ = nullptr;
    }

    if (swapChain_)
    {
        swapChain_->Release();
        swapChain_ = nullptr;
    }

    if (context_)
    {
        context_->Release();
        context_ = nullptr;
    }

    if (device_)
    {
        device_->Release();
        device_ = nullptr;
    }

    hwnd_ = nullptr;
}
