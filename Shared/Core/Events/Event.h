
#pragma once

#include "IEvent.h"
#include <functional>
#include <unordered_map>
#include <mutex>
#include <optional>
#include <cstddef>
#include <utility>

template<typename T>
class Event : public IEvent
{
public:

    using Callback = std::function<void(const T&)>;

    // Devuelve el tipo de dato de este evento.
    std::type_index GetType() const override
    {
        return typeid(T);
    }

    // Registra un suscriptor.
    std::size_t Subscribe(Callback callback)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        const std::size_t id = nextId_++;

        subscribers_[id] = std::move(callback);

        return id;
    }

    // Elimina un suscriptor.
    void Unsubscribe(std::size_t id)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        subscribers_.erase(id);
    }

    // Publica un nuevo valor.
    void Publish(const T& value)
    {
        std::unordered_map<std::size_t, Callback> callbacks;

        {
            std::lock_guard<std::mutex> lock(mutex_);

            lastValue_ = value;

            callbacks = subscribers_;
        }

        // Ejecutamos los callbacks fuera del mutex.
        for (const auto& [id, callback] : callbacks)
        {
            callback(value);
        }
    }

    // Obtiene el último valor publicado.
    std::optional<T> GetLastValue() const
    {
        std::lock_guard<std::mutex> lock(mutex_);

        return lastValue_;
    }

private:

    mutable std::mutex mutex_;

    std::unordered_map<std::size_t, Callback> subscribers_;

    std::optional<T> lastValue_;

    std::size_t nextId_ = 1;
};
