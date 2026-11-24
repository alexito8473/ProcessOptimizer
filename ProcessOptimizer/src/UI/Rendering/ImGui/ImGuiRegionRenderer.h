#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "imgui.h"
#include <implot.h>

class ImGuiRegionRenderer
{
public:
    using DrawCallback = std::function<void()>;

    ImGuiRegionRenderer() = default;
    ~ImGuiRegionRenderer();

    ImGuiRegionRenderer(const ImGuiRegionRenderer&) = delete;
    ImGuiRegionRenderer& operator=(const ImGuiRegionRenderer&) = delete;

    bool Initialize(
        ID3D11Device* device,
        ID3D11DeviceContext* deviceContext,
        ImGuiContext* mainContext);

    bool CreateRegion(const std::string& id);

    bool ResizeRegion(
        const std::string& id,
        int width,
        int height);

    // Renderiza únicamente esta región.
    // mainIO debe ser el IO del contexto ImGui principal.
    bool RenderRegion(
        const std::string& id,
        int width,
        int height,
        ImGuiIO& mainIO,
        ImVec2 regionPosition,
        bool keyboardFocused,
        float deltaTime,
        const DrawCallback& drawCallback);

    ID3D11ShaderResourceView* GetTexture(
        const std::string& id) const;

    void DestroyRegion(const std::string& id);
    void Shutdown();

private:
    struct Region
    {
        ImGuiContext* imguiContext = nullptr;
        ImPlotContext* implotContext = nullptr;

        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> renderTarget;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> shaderResource;

        int width = 0;
        int height = 0;
        bool dirty = true;
    };

    bool CreateTexture(Region& region, int width, int height);

    void CopyInput(
        ImGuiIO& source,
        ImGuiIO& destination,
        ImVec2 regionPosition,
        bool keyboardFocused);

    void RenderToTexture(Region& region);

private:
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> deviceContext_;

    ImGuiContext* mainContext_ = nullptr;

    std::unordered_map<std::string, std::unique_ptr<Region>> regions_;

    bool initialized_ = false;
};