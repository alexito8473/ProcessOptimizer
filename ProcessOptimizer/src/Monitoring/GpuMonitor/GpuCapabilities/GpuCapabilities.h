#pragma once

#include "../GpuInfo.h"
#include <vector>

class GpuCapabilities
{
public:
    static void Update(std::vector<GpuAdapterInfo>& adapters);
};