
#pragma once

#include "../GpuInfo.h"

class GpuEnumerator
{
public:
    static bool Enumerate(
        std::vector<GpuAdapterInfo>& adapters
    );
};
