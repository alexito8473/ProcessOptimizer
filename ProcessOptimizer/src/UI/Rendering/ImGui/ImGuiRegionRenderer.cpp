#include "ImGuiRegionRenderer.h"
#include <algorithm>

#include "implot.h"
#include "imgui_impl_dx11.h"

ImGuiRegionRenderer::~ImGuiRegionRenderer()
{
    Shutdown();
}

bool ImGuiRegionRenderer::Initialize(
    ID3D11Device* device,
    ID3D11DeviceContext* deviceContext,
    ImGuiContext* mainContext)
{
    if (!device || !deviceContext || !mainContext)
        return false;

    Shutdown();

    device_ = device;
    deviceContext_ = deviceContext;
    mainContext_ = mainContext;

    initialized_ = true;
    return true;
}

bool ImGuiRegionRenderer::CreateRegion(const std::string& id)
{
    if (!initialized_ || id.empty())
        return false;

    if (regions_.find(id) != regions_.end())
        return true;

    auto region = std::make_unique<Region>();

    region->imguiContext = ImGui::CreateContext();

    if (!region->imguiContext)
        return false;

    ImGui::SetCurrentContext(region->imguiContext);

    // Cada contexto ImGui necesita su propio contexto ImPlot.
    region->implotContext = ImPlot::CreateContext();

    // Copiar el estilo del contexto principal.
    ImGui::SetCurrentContext(mainContext_);
    ImGuiStyle mainStyle = ImGui::GetStyle();

    ImGui::SetCurrentContext(region->imguiContext);
    ImGui::GetStyle() = mainStyle;

    ImGuiIO& io = ImGui::GetIO();

    io.BackendPlatformName = "ProcessOptimizer_Region";
    io.BackendRendererName = "imgui_impl_dx11";

    // El backend DX11 se inicializa para este contexto independiente.
    if (!ImGui_ImplDX11_Init(device_.Get(), deviceContext_.Get()))
    {
        ImPlot::DestroyContext(region->implotContext);
        ImGui::DestroyContext(region->imguiContext);

        ImGui::SetCurrentContext(mainContext_);
        return false;
    }

    ImGui::SetCurrentContext(mainContext_);

    regions_.emplace(id, std::move(region));
    return true;
}

bool ImGuiRegionRenderer::ResizeRegion(
    const std::string& id,
    int width,
    int height)
{
    auto it = regions_.find(id);

    if (it == regions_.end())
        return false;

    width = (std::max)(width, 1);
    height = (std::max)(height, 1);

    Region& region = *it->second;

    if (region.width == width && region.height == height)
        return true;

    if (!CreateTexture(region, width, height))
        return false;

    region.dirty = true;
    return true;
}

bool ImGuiRegionRenderer::CreateTexture(
    Region& region,
    int width,
    int height)
{
    region.renderTarget.Reset();
    region.shaderResource.Reset();
    region.texture.Reset();

    D3D11_TEXTURE2D_DESC textureDesc = {};
    textureDesc.Width = static_cast<UINT>(width);
    textureDesc.Height = static_cast<UINT>(height);
    textureDesc.MipLevels = 1;
    textureDesc.ArraySize = 1;
    textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Usage = D3D11_USAGE_DEFAULT;
    textureDesc.BindFlags =
        D3D11_BIND_RENDER_TARGET |
        D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = device_->CreateTexture2D(
        &textureDesc,
        nullptr,
        region.texture.GetAddressOf());

    if (FAILED(hr))
        return false;

    hr = device_->CreateRenderTargetView(
        region.texture.Get(),
        nullptr,
        region.renderTarget.GetAddressOf());

    if (FAILED(hr))
        return false;

    hr = device_->CreateShaderResourceView(
        region.texture.Get(),
        nullptr,
        region.shaderResource.GetAddressOf());

    if (FAILED(hr))
        return false;

    region.width = width;
    region.height = height;

    return true;
}

void ImGuiRegionRenderer::CopyInput(
    ImGuiIO& source,
    ImGuiIO& destination,
    ImVec2 regionPosition,
    bool keyboardFocused)
{
    // Transformar coordenadas de la ventana a coordenadas de la región.
    destination.AddMousePosEvent(
        source.MousePos.x - regionPosition.x,
        source.MousePos.y - regionPosition.y);

    for (int button = 0; button < 5; ++button)
    {
        destination.AddMouseButtonEvent(
            button,
            source.MouseDown[button]);
    }

    destination.AddMouseWheelEvent(
        source.MouseWheelH,
        source.MouseWheel);

    // Solo la región enfocada recibe teclado.
    if (keyboardFocused)
    {
        for (int key = ImGuiKey_NamedKey_BEGIN;
            key < ImGuiKey_NamedKey_END;
            ++key)
        {
            destination.AddKeyEvent(
                static_cast<ImGuiKey>(key),
                source.KeysData[key].Down);
        }

        destination.AddKeyEvent(ImGuiMod_Ctrl, source.KeyCtrl);
        destination.AddKeyEvent(ImGuiMod_Shift, source.KeyShift);
        destination.AddKeyEvent(ImGuiMod_Alt, source.KeyAlt);
        destination.AddKeyEvent(ImGuiMod_Super, source.KeySuper);

        for (int i = 0; i < source.InputQueueCharacters.Size; ++i)
        {
            destination.AddInputCharacter(
                source.InputQueueCharacters[i]);
        }
    }
    else
    {
        destination.AddKeyEvent(ImGuiMod_Ctrl, false);
        destination.AddKeyEvent(ImGuiMod_Shift, false);
        destination.AddKeyEvent(ImGuiMod_Alt, false);
        destination.AddKeyEvent(ImGuiMod_Super, false);
    }
}

void ImGuiRegionRenderer::RenderToTexture(Region& region)
{
    ID3D11RenderTargetView* oldRTV = nullptr;
    ID3D11DepthStencilView* oldDSV = nullptr;

    deviceContext_->OMGetRenderTargets(
        1,
        &oldRTV,
        &oldDSV);

    UINT viewportCount = 1;
    D3D11_VIEWPORT oldViewport = {};

    deviceContext_->RSGetViewports(
        &viewportCount,
        &oldViewport);

    ID3D11RenderTargetView* target =
        region.renderTarget.Get();

    deviceContext_->OMSetRenderTargets(
        1,
        &target,
        nullptr);

    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(region.width);
    viewport.Height = static_cast<float>(region.height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    deviceContext_->RSSetViewports(1, &viewport);

    const float clearColor[4] = {
        0.055f,
        0.060f,
        0.070f,
        1.0f
    };

    deviceContext_->ClearRenderTargetView(
        region.renderTarget.Get(),
        clearColor);

    ImDrawData* drawData = ImGui::GetDrawData();

    if (drawData)
    {
        ImGui_ImplDX11_RenderDrawData(drawData);
    }

    // Restaurar el render target anterior.
    deviceContext_->OMSetRenderTargets(
        1,
        &oldRTV,
        oldDSV);

    if (viewportCount > 0)
    {
        deviceContext_->RSSetViewports(
            1,
            &oldViewport);
    }

    if (oldRTV)
        oldRTV->Release();

    if (oldDSV)
        oldDSV->Release();
}

bool ImGuiRegionRenderer::RenderRegion(
    const std::string& id,
    int width,
    int height,
    ImGuiIO& mainIO,
    ImVec2 regionPosition,
    bool keyboardFocused,
    float deltaTime,
    const DrawCallback& drawCallback)
{
    auto it = regions_.find(id);

    if (it == regions_.end())
        return false;

    Region& region = *it->second;

    if (!ResizeRegion(id, width, height))
        return false;

    ImGuiContext* previousContext = ImGui::GetCurrentContext();

    ImGui::SetCurrentContext(region.imguiContext);

    ImGuiIO& regionIO = ImGui::GetIO();

    regionIO.DisplaySize = ImVec2(
        static_cast<float>(region.width),
        static_cast<float>(region.height));

    regionIO.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    regionIO.DeltaTime = (std::max)(deltaTime, 1.0f / 1000.0f);

    CopyInput(
        mainIO,
        regionIO,
        regionPosition,
        keyboardFocused);

    ImGui_ImplDX11_NewFrame();
    ImGui::NewFrame();

    if (drawCallback)
        drawCallback();

    ImGui::Render();

    RenderToTexture(region);

    ImGui::SetCurrentContext(previousContext);

    region.dirty = false;
    return true;
}

ID3D11ShaderResourceView* ImGuiRegionRenderer::GetTexture(
    const std::string& id) const
{
    auto it = regions_.find(id);

    if (it == regions_.end())
        return nullptr;

    return it->second->shaderResource.Get();
}

void ImGuiRegionRenderer::DestroyRegion(const std::string& id)
{
    auto it = regions_.find(id);

    if (it == regions_.end())
        return;

    Region& region = *it->second;

    if (region.imguiContext)
    {
        ImGui::SetCurrentContext(region.imguiContext);

        ImGui_ImplDX11_Shutdown();

        if (region.implotContext)
            ImPlot::DestroyContext(region.implotContext);

        ImGui::DestroyContext(region.imguiContext);
    }

    ImGui::SetCurrentContext(mainContext_);

    regions_.erase(it);
}

void ImGuiRegionRenderer::Shutdown()
{
    for (auto& entry : regions_)
    {
        Region& region = *entry.second;

        if (region.imguiContext)
        {
            ImGui::SetCurrentContext(region.imguiContext);

            ImGui_ImplDX11_Shutdown();

            if (region.implotContext)
                ImPlot::DestroyContext(region.implotContext);

            ImGui::DestroyContext(region.imguiContext);
        }
    }

    regions_.clear();

    if (mainContext_)
        ImGui::SetCurrentContext(mainContext_);

    deviceContext_.Reset();
    device_.Reset();

    mainContext_ = nullptr;
    initialized_ = false;
}