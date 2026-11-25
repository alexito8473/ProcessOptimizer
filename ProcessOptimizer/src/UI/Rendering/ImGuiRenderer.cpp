#include "ImGuiRenderer.h"
#include <imgui.h>
#include "../Rendering/DirectX11/D3D11RegionTexture.h"

#include <d3d11.h>
#include <dxgi.h>

#include <cstddef>

#include <imgui.h>
#include <implot.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

std::uint64_t ImGuiRenderer::HashDrawData(const ImDrawData& drawData)
{
    constexpr std::uint64_t offsetBasis =
        14695981039346656037ull;
    constexpr std::uint64_t prime =
        1099511628211ull;

    std::uint64_t hash = offsetBasis;

    const auto addBytes =
        [&hash](const void* data, std::size_t bytes)
        {
            const auto* ptr =
                static_cast<const unsigned char*>(data);

            for (std::size_t i = 0; i < bytes; ++i)
            {
                hash ^= ptr[i];
                hash *= prime;
            }
        };

    addBytes(
        &drawData.CmdListsCount,
        sizeof(drawData.CmdListsCount));
    addBytes(
        &drawData.DisplayPos,
        sizeof(drawData.DisplayPos));
    addBytes(
        &drawData.DisplaySize,
        sizeof(drawData.DisplaySize));
    addBytes(
        &drawData.FramebufferScale,
        sizeof(drawData.FramebufferScale));

    for (int i = 0; i < drawData.CmdListsCount; ++i)
    {
        const ImDrawList* list = drawData.CmdLists[i];

        if (!list)
            continue;

        addBytes(
            &list->VtxBuffer.Size,
            sizeof(list->VtxBuffer.Size));
        addBytes(
            &list->IdxBuffer.Size,
            sizeof(list->IdxBuffer.Size));

        addBytes(
            list->VtxBuffer.Data,
            static_cast<std::size_t>(
                list->VtxBuffer.Size) * sizeof(ImDrawVert));

        addBytes(
            list->IdxBuffer.Data,
            static_cast<std::size_t>(
                list->IdxBuffer.Size) * sizeof(ImDrawIdx));

        addBytes(
            &list->CmdBuffer.Size,
            sizeof(list->CmdBuffer.Size));

        for (const ImDrawCmd& cmd : list->CmdBuffer)
        {
            addBytes(
                &cmd.ClipRect,
                sizeof(cmd.ClipRect));

            // No usar cmd.GetTexID(): en ImGui 1.92 esa llamada valida
            // que la textura ya haya sido subida por el backend, y aquí
            // todavía no lo ha sido. TexRef es estable y no asserta.
            addBytes(
                &cmd.TexRef,
                sizeof(cmd.TexRef));

            addBytes(
                &cmd.VtxOffset,
                sizeof(cmd.VtxOffset));
            addBytes(
                &cmd.IdxOffset,
                sizeof(cmd.IdxOffset));
            addBytes(
                &cmd.ElemCount,
                sizeof(cmd.ElemCount));
            addBytes(
                &cmd.UserCallback,
                sizeof(cmd.UserCallback));
        }
    }

    return hash;
}

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
ID3D11Device* ImGuiRenderer::GetDevice() const
{
    return device_;
}

ID3D11DeviceContext* ImGuiRenderer::GetDeviceContext() const
{
    return context_;
}

ID3D11DeviceContext* ImGuiRenderer::GetContext() const
{
    return context_;
}

ID3D11RenderTargetView* ImGuiRenderer::GetRenderTarget() const
{
    return renderTarget_;
}

ImGuiContext* ImGuiRenderer::GetImGuiContext() const
{
    return ImGui::GetCurrentContext();
}

bool ImGuiRenderer::IsInitialized() const
{
    return initialized_;
}

std::uint64_t ImGuiRenderer::GetPresentCount() const
{
    return presentCount_;
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
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;

    D3D_FEATURE_LEVEL featureLevel{};

    const D3D_FEATURE_LEVEL levels[] =
    {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_0
    };

    const HRESULT result = D3D11CreateDeviceAndSwapChain(
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
        &context_);

    if (FAILED(result))
        return false;

    return CreateRenderTarget();
}

bool ImGuiRenderer::CreateRenderTarget()
{
    ID3D11Texture2D* backBuffer = nullptr;

    HRESULT result = swapChain_->GetBuffer(
        0,
        IID_PPV_ARGS(&backBuffer));

    if (FAILED(result))
        return false;

    result = device_->CreateRenderTargetView(
        backBuffer,
        nullptr,
        &renderTarget_);

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

    // La UI se construye en CPU cada frame: el hover y el scroll responden
    // a 60 Hz. Solo se envía a GPU si el resultado cambió de verdad.
    ImGui::Render();

    ImDrawData* drawData = ImGui::GetDrawData();

    if (!drawData)
        return;

    const std::uint64_t drawHash =
        HashDrawData(*drawData);

    if (drawHash == lastDrawHash_)
        return;

    // Tras un Resize fallido el RTV puede ser nulo: no enviar a Present.
    if (!renderTarget_ || !swapChain_)
        return;

    lastDrawHash_ = drawHash;

    context_->OMSetRenderTargets(
        1,
        &renderTarget_,
        nullptr);

    ImGui_ImplDX11_RenderDrawData(drawData);

    swapChain_->Present(1, 0);

    ++presentCount_;
}

void ImGuiRenderer::Resize(UINT width, UINT height)
{
    if (!initialized_ || width == 0 || height == 0)
        return;

    // El backbuffer se recrea: hay que forzar un Present aunque el hash
    // de draw data no haya cambiado.
    lastDrawHash_ = 0;

    if (regionActive_)
        EndRegion();

    context_->OMSetRenderTargets(0, nullptr, nullptr);

    const HRESULT result = swapChain_->ResizeBuffers(
        0,
        width,
        height,
        DXGI_FORMAT_UNKNOWN,
        0);

    // El RTV antiguo apunta al backbuffer anterior: liberarlo solo
    // cuando ResizeBuffers ha tenido exito. Si falla, recrear desde el
    // backbuffer que siga vivo para no dejar la ventana en negro.
    if (FAILED(result))
    {
        if (renderTarget_)
        {
            renderTarget_->Release();
            renderTarget_ = nullptr;
        }

        CreateRenderTarget();
        return;
    }

    if (renderTarget_)
    {
        renderTarget_->Release();
        renderTarget_ = nullptr;
    }

    CreateRenderTarget();
}

bool ImGuiRenderer::BeginRegion(
    D3D11RegionTexture& region,
    const float clearColor[4])
{
    if (!initialized_ || regionActive_)
        return false;

    ID3D11RenderTargetView* regionTarget =
        region.GetRenderTarget();

    if (!regionTarget)
        return false;

    // Guardar el render target que estaba activo.
    context_->OMGetRenderTargets(
        1,
        &previousRenderTarget_,
        &previousDepthStencil_);

    // Guardar viewport actual.
    previousViewportCount_ =
        D3D11_VIEWPORT_AND_SCISSORRECT_MAX_INDEX + 1;

    context_->RSGetViewports(
        &previousViewportCount_,
        previousViewports_);

    // Activar el render target de la región.
    context_->OMSetRenderTargets(
        1,
        &regionTarget,
        nullptr);

    D3D11_VIEWPORT viewport{};

    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = static_cast<float>(region.GetWidth());
    viewport.Height = static_cast<float>(region.GetHeight());
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    context_->RSSetViewports(1, &viewport);

    context_->ClearRenderTargetView(
        regionTarget,
        clearColor);

    regionActive_ = true;

    return true;
}

void ImGuiRenderer::EndRegion()
{
    if (!regionActive_)
        return;

    RestoreRenderTarget();

    regionActive_ = false;
}

void ImGuiRenderer::RestoreRenderTarget()
{
    context_->OMSetRenderTargets(
        1,
        &previousRenderTarget_,
        previousDepthStencil_);

    if (previousViewportCount_ > 0)
    {
        context_->RSSetViewports(
            previousViewportCount_,
            previousViewports_);
    }

    if (previousRenderTarget_)
    {
        previousRenderTarget_->Release();
        previousRenderTarget_ = nullptr;
    }

    if (previousDepthStencil_)
    {
        previousDepthStencil_->Release();
        previousDepthStencil_ = nullptr;
    }

    previousViewportCount_ = 0;
}

void ImGuiRenderer::Shutdown()
{
    initialized_ = false;
    lastDrawHash_ = 0;
    presentCount_ = 0;

    if (regionActive_)
        EndRegion();

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