#include "RenderManager.h"

uint32_t RenderManager::CreateRegion(
    const std::string& name,
    float x,
    float y,
    float width,
    float height)
{
    const uint32_t id = nextId_++;

    regions_.emplace_back(
        id,
        name,
        x,
        y,
        width,
        height);

    return id;
}

void RenderManager::DestroyRegion(uint32_t id)
{
    for (auto it = regions_.begin(); it != regions_.end(); ++it)
    {
        if (it->GetId() == id)
        {
            regions_.erase(it);
            return;
        }
    }
}

RenderRegion* RenderManager::GetRegion(uint32_t id)
{
    for (auto& region : regions_)
    {
        if (region.GetId() == id)
            return &region;
    }

    return nullptr;
}

const RenderRegion* RenderManager::GetRegion(uint32_t id) const
{
    for (const auto& region : regions_)
    {
        if (region.GetId() == id)
            return &region;
    }

    return nullptr;
}

void RenderManager::SetRegionBounds(
    uint32_t id,
    float x,
    float y,
    float width,
    float height)
{
    RenderRegion* region = GetRegion(id);

    if (!region)
        return;

    region->SetBounds(
        x,
        y,
        width,
        height);
}

void RenderManager::SetRegionVisible(uint32_t id, bool visible)
{
    RenderRegion* region = GetRegion(id);

    if (!region)
        return;

    region->SetVisible(visible);
}

void RenderManager::MarkDirty(uint32_t id)
{
    RenderRegion* region = GetRegion(id);

    if (!region)
        return;

    region->MarkDirty();
}

void RenderManager::MarkAllDirty()
{
    for (auto& region : regions_)
        region.MarkDirty();
}

std::vector<uint32_t> RenderManager::GetDirtyRegions() const
{
    std::vector<uint32_t> dirtyRegions;

    for (const auto& region : regions_)
    {
        if (region.IsDirty() && region.IsVisible())
            dirtyRegions.push_back(region.GetId());
    }

    return dirtyRegions;
}

void RenderManager::ClearDirty(uint32_t id)
{
    RenderRegion* region = GetRegion(id);

    if (!region)
        return;

    region->ClearDirty();
}