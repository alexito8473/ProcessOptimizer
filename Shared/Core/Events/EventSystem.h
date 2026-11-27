
#pragma once

#include "Event.h"
#include <string>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>

class EventSystem
{
public:

    // Publica un valor en un evento.
    // Si no existe, lo crea automáticamente.
    template<typename T>
    void Publish(const std::string& name, const T& value)
    {
        auto event = GetOrCreateEvent<T>(name);

        event->Publish(value);
    }

    // Registra un suscriptor.
    template<typename T>
    std::size_t Subscribe(
        const std::string& name,
        typename Event<T>::Callback callback)
    {
        auto event = GetOrCreateEvent<T>(name);

        return event->Subscribe(std::move(callback));
    }

    // Elimina un suscriptor.
    template<typename T>
    void Unsubscribe(
        const std::string& name,
        std::size_t id)
    {
        auto event = GetOrCreateEvent<T>(name);

        event->Unsubscribe(id);
    }

    // Consulta el último valor publicado.
    template<typename T>
    std::optional<T> GetLastValue(const std::string& name)
    {
        auto event = GetOrCreateEvent<T>(name);

        return event->GetLastValue();
    }

private:

    // Nuestro mapa de eventos.
    std::unordered_map<std::string, std::shared_ptr<IEvent>> events_;

    // Protege el acceso al mapa.
    std::mutex mutex_;

    // Busca un evento o lo crea si no existe.
    template<typename T>
    std::shared_ptr<Event<T>> GetOrCreateEvent(
        const std::string& name)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = events_.find(name);

        if (it != events_.end())
        {
            auto event =
                std::dynamic_pointer_cast<Event<T>>(it->second);

            if (!event)
            {
                throw std::logic_error(
                    "El evento ya existe con otro tipo."
                );
            }

            return event;
        }

        auto event = std::make_shared<Event<T>>();

        events_[name] = event;

        return event;
    }
};

