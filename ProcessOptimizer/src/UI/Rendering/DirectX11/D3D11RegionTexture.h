
#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>

class D3D11RegionTexture
{
public:
    D3D11RegionTexture() = default;
    ~D3D11RegionTexture();

    D3D11RegionTexture(const D3D11RegionTexture&) = delete;
    D3D11RegionTexture& operator=(const D3D11RegionTexture&) = delete;

    bool Initialize(
        ID3D11Device* device,
        uint32_t width,
        uint32_t height);

    bool Resize(
        uint32_t width,
        uint32_t height);

    void Clear(
        ID3D11DeviceContext* context,
        const float color[4]);

    void Shutdown();

    ID3D11RenderTargetView* GetRenderTarget() const;
    ID3D11ShaderResourceView* GetShaderResource() const;

    uint32_t GetWidth() const;
    uint32_t GetHeight() const;

    bool IsInitialized() const;

private:
    bool CreateTexture(
        uint32_t width,
        uint32_t height);

private:
    Microsoft::WRL::ComPtr<ID3D11Device> device_;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> renderTarget_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> shaderResource_;

    uint32_t width_ = 0;
    uint32_t height_ = 0;
};
