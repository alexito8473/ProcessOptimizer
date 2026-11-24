#pragma once

#include <string>
#include <cstdint>

class RenderRegion
{
public:
    RenderRegion(
        uint32_t id,
        const std::string& name,
        float x,
        float y,
        float width,
        float height);

    uint32_t GetId() const;
    const std::string& GetName() const;

    float GetX() const;
    float GetY() const;
    float GetWidth() const;
    float GetHeight() const;

    bool IsVisible() const;
    bool IsDirty() const;

    void SetBounds(
        float x,
        float y,
        float width,
        float height);

    void SetVisible(bool visible);
    void MarkDirty();
    void ClearDirty();

private:
    uint32_t id_;
    std::string name_;

    float x_;
    float y_;
    float width_;
    float height_;

    bool visible_ = true;
    bool dirty_ = true;
};