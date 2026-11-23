
#include "Direct2DRenderer.h"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

Direct2DRenderer::~Direct2DRenderer()
{
    Shutdown();
}

bool Direct2DRenderer::Initialize(HWND hwnd)
{
    if (initialized_)
        return true;

    if (!hwnd)
        return false;

    hwnd_ = hwnd;

    HRESULT result = D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED,
        factory_.GetAddressOf()
    );

    if (FAILED(result))
    {
        Shutdown();
        return false;
    }

    RECT rect{};

    if (!GetClientRect(hwnd_, &rect))
    {
        Shutdown();
        return false;
    }

    const D2D1_SIZE_U size = D2D1::SizeU(
        static_cast<UINT32>(rect.right - rect.left),
        static_cast<UINT32>(rect.bottom - rect.top)
    );

    result = factory_->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(),
        D2D1::HwndRenderTargetProperties(
            hwnd_,
            size
        ),
        renderTarget_.GetAddressOf()
    );

    if (FAILED(result))
    {
        Shutdown();
        return false;
    }

    result = renderTarget_->CreateSolidColorBrush(
        D2D1::ColorF(
            0.055f,
            0.065f,
            0.085f,
            1.0f
        ),
        brush_.GetAddressOf()
    );

    if (FAILED(result))
    {
        Shutdown();
        return false;
    }

    initialized_ = true;

    return true;
}

void Direct2DRenderer::BeginFrame()
{
    if (!initialized_)
        return;

    renderTarget_->BeginDraw();

    renderTarget_->Clear(
        D2D1::ColorF(
            0.055f,
            0.065f,
            0.085f,
            1.0f
        )
    );
}

void Direct2DRenderer::EndFrame()
{
    if (!initialized_)
        return;

    const HRESULT result = renderTarget_->EndDraw();

    if (result == D2DERR_RECREATE_TARGET)
    {
        brush_.Reset();
        renderTarget_.Reset();

        initialized_ = false;
        Initialize(hwnd_);
    }
}

void Direct2DRenderer::Resize(UINT width, UINT height)
{
    if (!initialized_ || width == 0 || height == 0)
        return;

    const HRESULT result = renderTarget_->Resize(
        D2D1::SizeU(width, height)
    );

    if (FAILED(result))
    {
        brush_.Reset();
        renderTarget_.Reset();
        initialized_ = false;
    }
}

void Direct2DRenderer::Shutdown()
{
    initialized_ = false;

    brush_.Reset();
    renderTarget_.Reset();
    factory_.Reset();

    hwnd_ = nullptr;
}
