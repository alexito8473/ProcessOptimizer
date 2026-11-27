
#include "GpuCapabilities.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <dxgi1_2.h>
#include <d3d11.h>
#include <d3d12.h>
#include <wrl/client.h>

#include <vector>
#include <string>
#include "../GpuInfo.h"

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3d12.lib")

using Microsoft::WRL::ComPtr;

namespace
{
    const wchar_t* GetFeatureLevelName(D3D_FEATURE_LEVEL level)
    {
        switch (level)
        {
        case D3D_FEATURE_LEVEL_12_2: return L"12.2";
        case D3D_FEATURE_LEVEL_12_1: return L"12.1";
        case D3D_FEATURE_LEVEL_12_0: return L"12.0";
        case D3D_FEATURE_LEVEL_11_1: return L"11.1";
        case D3D_FEATURE_LEVEL_11_0: return L"11.0";
        case D3D_FEATURE_LEVEL_10_1: return L"10.1";
        case D3D_FEATURE_LEVEL_10_0: return L"10.0";
        case D3D_FEATURE_LEVEL_9_3:  return L"9.3";
        case D3D_FEATURE_LEVEL_9_2:  return L"9.2";
        case D3D_FEATURE_LEVEL_9_1:  return L"9.1";
        default: return L"Desconocido";
        }
    }

    const wchar_t* GetDirect3DVersion(D3D_FEATURE_LEVEL level)
    {
        if (level >= D3D_FEATURE_LEVEL_12_0)
            return L"Direct3D 12";

        if (level >= D3D_FEATURE_LEVEL_11_0)
            return L"Direct3D 11";

        if (level >= D3D_FEATURE_LEVEL_10_0)
            return L"Direct3D 10";

        if (level >= D3D_FEATURE_LEVEL_9_1)
            return L"Direct3D 9";

        return L"Desconocido";
    }

    bool MatchesAdapter(
        IDXGIAdapter1* adapter,
        const GpuAdapterInfo& info)
    {
        DXGI_ADAPTER_DESC1 description{};

        if (FAILED(adapter->GetDesc1(&description)))
            return false;

        return
            description.AdapterLuid.LowPart ==
            static_cast<UINT>(info.luidLowPart) &&
            description.AdapterLuid.HighPart ==
            info.luidHighPart;
    }

    void ReadCapabilities(
        IDXGIAdapter1* adapter,
        GpuAdapterInfo& info)
    {
        const D3D_FEATURE_LEVEL featureLevels[] =
        {
            D3D_FEATURE_LEVEL_12_2,
            D3D_FEATURE_LEVEL_12_1,
            D3D_FEATURE_LEVEL_12_0,
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0,
            D3D_FEATURE_LEVEL_9_3,
            D3D_FEATURE_LEVEL_9_2,
            D3D_FEATURE_LEVEL_9_1
        };

        ComPtr<ID3D11Device> device11;
        ComPtr<ID3D11DeviceContext> context11;

        D3D_FEATURE_LEVEL obtainedLevel{};

        HRESULT result = D3D11CreateDevice(
            adapter,
            D3D_DRIVER_TYPE_UNKNOWN,
            nullptr,
            0,
            featureLevels,
            ARRAYSIZE(featureLevels),
            D3D11_SDK_VERSION,
            &device11,
            &obtainedLevel,
            &context11
        );

        if (result == E_INVALIDARG)
        {
            result = D3D11CreateDevice(
                adapter,
                D3D_DRIVER_TYPE_UNKNOWN,
                nullptr,
                0,
                featureLevels + 1,
                ARRAYSIZE(featureLevels) - 1,
                D3D11_SDK_VERSION,
                &device11,
                &obtainedLevel,
                &context11
            );
        }

        if (SUCCEEDED(result))
        {
            info.d3d11Supported = true;
            info.featureLevel = GetFeatureLevelName(obtainedLevel);
            info.direct3DVersion = GetDirect3DVersion(obtainedLevel);

            // DirectCompute está disponible desde Feature Level 11.0.
            info.directComputeSupported =
                obtainedLevel >= D3D_FEATURE_LEVEL_11_0;
        }

        HRESULT d3d12Result = D3D12CreateDevice(
            adapter,
            D3D_FEATURE_LEVEL_11_0,
            __uuidof(ID3D12Device),
            nullptr
        );

        info.d3d12Supported = SUCCEEDED(d3d12Result);
    }
}

void GpuCapabilities::Update(
    std::vector<GpuAdapterInfo>& adapters)
{
    ComPtr<IDXGIFactory1> factory;

    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
        return;

    for (GpuAdapterInfo& info : adapters)
    {
        info.direct3DVersion.clear();
        info.featureLevel.clear();
        info.d3d11Supported = false;
        info.d3d12Supported = false;
        info.directComputeSupported = false;

        for (UINT index = 0;; ++index)
        {
            ComPtr<IDXGIAdapter1> adapter;

            HRESULT result = factory->EnumAdapters1(
                index,
                &adapter
            );

            if (result == DXGI_ERROR_NOT_FOUND)
                break;

            if (FAILED(result))
                break;

            if (MatchesAdapter(adapter.Get(), info))
            {
                ReadCapabilities(adapter.Get(), info);
                break;
            }
        }
    }
}
