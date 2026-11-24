#pragma once

#include "../RenderRegion/RenderRegion.h"

#include <cstdint>
#include <string>
#include <vector>

class RenderManager
{
public:
    uint32_t CreateRegion(
        const std::string& name,
        float x,
        float y,
        float width,
        float height);

    void DestroyRegion(uint32_t id);

    RenderRegion* GetRegion(uint32_t id);
    const RenderRegion* GetRegion(uint32_t id) const;

    void SetRegionBounds(
        uint32_t id,
        float x,
        float y,
        float width,
        float height);

    void SetRegionVisible(uint32_t id, bool visible);

    void MarkDirty(uint32_t id);
    void MarkAllDirty();

    std::vector<uint32_t> GetDirtyRegions() const;

    void ClearDirty(uint32_t id);

private:
    std::vector<RenderRegion> regions_;

    uint32_t nextId_ = 1;
};