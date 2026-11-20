#pragma once

#include "../GpuInfo.h"
#include <vector>

class GpuWddmInfo
{
public:
    static void Update(std::vector<GpuAdapterInfo>& adapters);
};