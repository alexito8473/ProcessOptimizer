
#include "GraphBuffer.h"

namespace UI::Components::Graph
{
    GraphBuffer::GraphBuffer(std::size_t capacity)
        : capacity_(capacity)
    {
        values_.resize(capacity);
    }

    void GraphBuffer::AddSample(float value)
    {
        if (capacity_ == 0)
            return;

        std::lock_guard<std::mutex> lock(mutex_);

        values_[writeIndex_] = value;

        writeIndex_ = (writeIndex_ + 1) % capacity_;

        if (count_ < capacity_)
            ++count_;
    }

    std::vector<float> GraphBuffer::GetData() const
    {
        std::lock_guard<std::mutex> lock(mutex_);

        std::vector<float> result;
        result.reserve(count_);

        const std::size_t oldestIndex =
            (writeIndex_ + capacity_ - count_) % capacity_;

        for (std::size_t i = 0; i < count_; ++i)
        {
            result.push_back(
                values_[(oldestIndex + i) % capacity_]
            );
        }

        return result;
    }

    std::size_t GraphBuffer::GetCount() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return count_;
    }

    void GraphBuffer::Clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);

        writeIndex_ = 0;
        count_ = 0;
    }
}
