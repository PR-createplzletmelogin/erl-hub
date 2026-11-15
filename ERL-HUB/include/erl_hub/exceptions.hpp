#pragma once

#include <stdexcept>
#include <string>

namespace ReliefOps::Logistics {

class CargoOverweightException : public std::runtime_error {
public:
    explicit CargoOverweightException(const std::string& message) : std::runtime_error(message) {}
};

class FuelDepletionException : public std::runtime_error {
public:
    explicit FuelDepletionException(const std::string& message) : std::runtime_error(message) {}
};

class DamagedCorridorException : public std::runtime_error {
public:
    explicit DamagedCorridorException(const std::string& message) : std::runtime_error(message) {}
};

class InventoryShortageException : public std::runtime_error {
public:
    explicit InventoryShortageException(const std::string& message) : std::runtime_error(message) {}
};

class InvalidDispatchStateException : public std::logic_error {
public:
    explicit InvalidDispatchStateException(const std::string& message) : std::logic_error(message) {}
};

}
