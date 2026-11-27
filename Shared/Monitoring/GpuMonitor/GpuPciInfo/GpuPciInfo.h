#pragma once
#include <vector>
#include "../GpuInfo.h"

class GpuPciInfo
{
public:
    static void Update(std::vector<GpuAdapterInfo>& adapters);
};