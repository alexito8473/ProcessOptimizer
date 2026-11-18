#pragma once
#include <typeindex>
// Interfaz común para poder almacenar diferentes Event<T>
// dentro del mismo mapa.
class IEvent
{
public:
    virtual ~IEvent() = default;

    virtual std::type_index GetType() const = 0;
};