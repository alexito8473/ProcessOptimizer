#include "RenderRegion.h"

RenderRegion::RenderRegion(
    uint32_t id,
    const std::string& name,
    float x,
    float y,
    float width,
    float height)
    : id_(id),
    name_(name),
    x_(x),
    y_(y),
    width_(width),
    height_(height)
{
}

uint32_t RenderRegion::GetId() const
{
    return id_;
}

const std::string& RenderRegion::GetName() const
{
    return name_;
}

float RenderRegion::GetX() const
{
    return x_;
}

float RenderRegion::GetY() const
{
    return y_;
}

float RenderRegion::GetWidth() const
{
    return width_;
}

float RenderRegion::GetHeight() const
{
    return height_;
}

bool RenderRegion::IsVisible() const
{
    return visible_;
}

bool RenderRegion::IsDirty() const
{
    return dirty_;
}

void RenderRegion::SetBounds(
    float x,
    float y,
    float width,
    float height)
{
    x_ = x;
    y_ = y;
    width_ = width;
    height_ = height;

    MarkDirty();
}

void RenderRegion::SetVisible(bool visible)
{
    if (visible_ == visible)
        return;

    visible_ = visible;
    MarkDirty();
}

void RenderRegion::MarkDirty()
{
    dirty_ = true;
}

void RenderRegion::ClearDirty()
{
    dirty_ = false;
}