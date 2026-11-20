#pragma once

#include "../GpuInfo.h"
#include <vector>

class GpuSensors
{
public:
    static void Update(std::vector<GpuAdapterInfo>& adapters);
};