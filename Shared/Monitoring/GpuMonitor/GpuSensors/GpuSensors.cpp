#include "GpuSensors.h"

void GpuSensors::Update(
    std::vector<GpuAdapterInfo>& adapters)
{
    for (GpuAdapterInfo& adapter : adapters)
    {
        adapter.temperatureAvailable = false;
        adapter.coreClockAvailable = false;
        adapter.memoryClockAvailable = false;
        adapter.fanSpeedAvailable = false;
        adapter.powerUsageAvailable = false;
    }
}