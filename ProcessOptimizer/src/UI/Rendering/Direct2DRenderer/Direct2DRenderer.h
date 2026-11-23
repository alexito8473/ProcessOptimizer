
#pragma once

#include <Windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

class Direct2DRenderer
{
public:
    Direct2DRenderer() = default;
    ~Direct2DRenderer();

    Direct2DRenderer(const Direct2DRenderer&) = delete;
    Direct2DRenderer& operator=(const Direct2DRenderer&) = delete;

    bool Initialize(HWND hwnd);
    void BeginFrame();
    void EndFrame();
    void Resize(UINT width, UINT height);
    void Shutdown();

private:
    HWND hwnd_ = nullptr;

    Microsoft::WRL::ComPtr<ID2D1Factory> factory_;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> renderTarget_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;

    bool initialized_ = false;
};
