
#pragma once

#include "../GpuInfo.h"
#include <dxgi1_4.h>
#include <wrl/client.h>

class GpuMemory
{
public:
    static bool Update(
        Microsoft::WRL::ComPtr<IDXGIAdapter1>& adapter,
        GpuAdapterInfo& info
    );
};
