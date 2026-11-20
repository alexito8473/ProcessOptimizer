
#pragma once

#include <vector>
#include <cstddef>
#include <mutex>

namespace UI::Components::Graph
{
    class GraphBuffer
    {
    public:
        explicit GraphBuffer(std::size_t capacity);

        void AddSample(float value);

        std::vector<float> GetData() const;

        std::size_t GetCount() const;

        void Clear();

    private:
        mutable std::mutex mutex_;

        std::vector<float> values_;

        std::size_t capacity_ = 0;
        std::size_t writeIndex_ = 0;
        std::size_t count_ = 0;
    };
}
