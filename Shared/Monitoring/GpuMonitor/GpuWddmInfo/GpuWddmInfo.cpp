
#include "GpuWddmInfo.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <d3dkmthk.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <string>
#include <vector>
#include <sstream>

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;

namespace
{
    bool GetAdapterLuid(
        IDXGIAdapter1* adapter,
        LUID& luid)
    {
        DXGI_ADAPTER_DESC1 description{};

        if (FAILED(adapter->GetDesc1(&description)))
            return false;

        luid = description.AdapterLuid;
        return true;
    }

    std::wstring GetWddmVersion(
        const D3DKMT_DRIVERVERSION& version)
    {
        switch (version)
        {
        case KMT_DRIVERVERSION_WDDM_1_0:
            return L"1.0";

        case KMT_DRIVERVERSION_WDDM_1_1:
            return L"1.1";

        case KMT_DRIVERVERSION_WDDM_1_2:
            return L"1.2";

        case KMT_DRIVERVERSION_WDDM_1_3:
            return L"1.3";

        case KMT_DRIVERVERSION_WDDM_2_0:
            return L"2.0";

        case KMT_DRIVERVERSION_WDDM_2_1:
            return L"2.1";

        case KMT_DRIVERVERSION_WDDM_2_2:
            return L"2.2";

        case KMT_DRIVERVERSION_WDDM_2_3:
            return L"2.3";

        case KMT_DRIVERVERSION_WDDM_2_4:
            return L"2.4";

        case KMT_DRIVERVERSION_WDDM_2_5:
            return L"2.5";

        case KMT_DRIVERVERSION_WDDM_2_6:
            return L"2.6";

        case KMT_DRIVERVERSION_WDDM_2_7:
            return L"2.7";

        case KMT_DRIVERVERSION_WDDM_2_8:
            return L"2.8";

        case KMT_DRIVERVERSION_WDDM_2_9:
            return L"2.9";

        case KMT_DRIVERVERSION_WDDM_3_0:
            return L"3.0";

        case KMT_DRIVERVERSION_WDDM_3_1:
            return L"3.1";

        case KMT_DRIVERVERSION_WDDM_3_2:
            return L"3.2";

        default:
            return L"Desconocido";
        }
    }

    bool QueryWddmVersion(
        const LUID& luid,
        std::wstring& version)
    {
        D3DKMT_OPENADAPTERFROMLUID openAdapter{};
        openAdapter.AdapterLuid = luid;

        NTSTATUS status = D3DKMTOpenAdapterFromLuid(
            &openAdapter
        );

        if (status != 0)
            return false;

        D3DKMT_QUERYADAPTERINFO queryInfo{};
        D3DKMT_DRIVERVERSION driverVersion{};

        queryInfo.hAdapter = openAdapter.hAdapter;
        queryInfo.Type = KMTQAITYPE_DRIVERVERSION;
        queryInfo.pPrivateDriverData = &driverVersion;
        queryInfo.PrivateDriverDataSize =
            sizeof(driverVersion);

        status = D3DKMTQueryAdapterInfo(&queryInfo);

        D3DKMT_CLOSEADAPTER closeAdapter{};
        closeAdapter.hAdapter = openAdapter.hAdapter;

        D3DKMTCloseAdapter(&closeAdapter);

        if (status != 0)
            return false;

        version = GetWddmVersion(driverVersion);
        return version != L"Desconocido";
    }
}

void GpuWddmInfo::Update(
    std::vector<GpuAdapterInfo>& adapters)
{
    ComPtr<IDXGIFactory1> factory;

    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
        return;

    for (GpuAdapterInfo& info : adapters)
    {
        info.wddmVersion.clear();
        info.wddmInfoAvailable = false;

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

            LUID luid{};

            if (!GetAdapterLuid(adapter.Get(), luid))
                continue;

            if (luid.LowPart !=
                static_cast<UINT>(info.luidLowPart) ||
                luid.HighPart != info.luidHighPart)
            {
                continue;
            }

            info.wddmInfoAvailable =
                QueryWddmVersion(luid, info.wddmVersion);

            break;
        }
    }
}
