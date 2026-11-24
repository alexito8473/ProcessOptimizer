
#include "D3D11RegionTexture.h"
#include <algorithm>

D3D11RegionTexture::~D3D11RegionTexture()
{
    Shutdown();
}

bool D3D11RegionTexture::Initialize(
    ID3D11Device* device,
    uint32_t width,
    uint32_t height)
{
    if (!device || width == 0 || height == 0)
        return false;

    Shutdown();

    device_ = device;

    if (!CreateTexture(width, height))
    {
        Shutdown();
        return false;
    }

    return true;
}

bool D3D11RegionTexture::CreateTexture(
    uint32_t width,
    uint32_t height)
{
    if (!device_ || width == 0 || height == 0)
        return false;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> newTexture;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> newRenderTarget;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> newShaderResource;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags =
        D3D11_BIND_RENDER_TARGET |
        D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;

    HRESULT result = device_->CreateTexture2D(
        &desc,
        nullptr,
        newTexture.GetAddressOf());

    if (FAILED(result))
        return false;

    result = device_->CreateRenderTargetView(
        newTexture.Get(),
        nullptr,
        newRenderTarget.GetAddressOf());

    if (FAILED(result))
        return false;

    result = device_->CreateShaderResourceView(
        newTexture.Get(),
        nullptr,
        newShaderResource.GetAddressOf());

    if (FAILED(result))
        return false;

    // Sustituir los recursos anteriores solo cuando todo se haya creado.
    texture_ = (std::move)(newTexture);
    renderTarget_ = (std::move)(newRenderTarget);
    shaderResource_ = (std::move)(newShaderResource);

    width_ = width;
    height_ = height;

    return true;
}

bool D3D11RegionTexture::Resize(
    uint32_t width,
    uint32_t height)
{
    if (!device_ || width == 0 || height == 0)
        return false;

    if (width_ == width && height_ == height)
        return true;

    return CreateTexture(width, height);
}

void D3D11RegionTexture::Clear(
    ID3D11DeviceContext* context,
    const float color[4])
{
    if (!context || !renderTarget_ || !color)
        return;

    context->ClearRenderTargetView(
        renderTarget_.Get(),
        color);
}

void D3D11RegionTexture::Shutdown()
{
    shaderResource_.Reset();
    renderTarget_.Reset();
    texture_.Reset();
    device_.Reset();

    width_ = 0;
    height_ = 0;
}

ID3D11RenderTargetView*
D3D11RegionTexture::GetRenderTarget() const
{
    return renderTarget_.Get();
}

ID3D11ShaderResourceView*
D3D11RegionTexture::GetShaderResource() const
{
    return shaderResource_.Get();
}

uint32_t D3D11RegionTexture::GetWidth() const
{
    return width_;
}

uint32_t D3D11RegionTexture::GetHeight() const
{
    return height_;
}

bool D3D11RegionTexture::IsInitialized() const
{
    return device_ != nullptr &&
        texture_ != nullptr &&
        renderTarget_ != nullptr &&
        shaderResource_ != nullptr;
}
