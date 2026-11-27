#pragma once

#include "../GpuInfo.h"
#include <vector>

class GpuDriverInfo
{
public:
    static void Update(std::vector<GpuAdapterInfo>& adapters);
};