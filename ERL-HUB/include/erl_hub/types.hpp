#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace ReliefOps {

inline bool isValidLabel(const std::string& value, std::size_t maximumLength) noexcept {
    if (value.empty() || value.size() > maximumLength) return false;
    for (const char rawCharacter : value) {
        const auto character = static_cast<unsigned char>(rawCharacter);
        if (character < 0x20U || character == 0x7FU) return false;
    }
    return true;
}

struct GeoPoint {
    double latitude{};
    double longitude{};

    void validate() const {
        if (!std::isfinite(latitude) || !std::isfinite(longitude) || latitude < -90.0 || latitude > 90.0 || longitude < -180.0 || longitude > 180.0) {
            throw std::invalid_argument("Invalid geographic coordinates");
        }
    }
};

inline double distanceKm(const GeoPoint& from, const GeoPoint& to) {
    from.validate();
    to.validate();
    constexpr double earthRadiusKm = 6371.0088;
    constexpr double radians = 3.14159265358979323846 / 180.0;
    const double lat1 = from.latitude * radians;
    const double lat2 = to.latitude * radians;
    const double deltaLat = (to.latitude - from.latitude) * radians;
    const double deltaLon = (to.longitude - from.longitude) * radians;
    const double rawA = std::sin(deltaLat / 2.0) * std::sin(deltaLat / 2.0) + std::cos(lat1) * std::cos(lat2) * std::sin(deltaLon / 2.0) * std::sin(deltaLon / 2.0);
    const double a = std::clamp(rawA, 0.0, 1.0);
    return earthRadiusKm * 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
}

inline double payloadUtilization(double currentWeightKg, double additionalWeightKg, double capacityKg) {
    if (!std::isfinite(currentWeightKg) || !std::isfinite(additionalWeightKg) || !std::isfinite(capacityKg) || currentWeightKg < 0.0 || additionalWeightKg < 0.0 || capacityKg <= 0.0) {
        throw std::invalid_argument("Invalid payload calculation inputs");
    }
    const double combinedWeight = currentWeightKg + additionalWeightKg;
    if (!std::isfinite(combinedWeight)) throw std::overflow_error("Payload calculation exceeds numeric range");
    const double utilization = (combinedWeight / capacityKg) * 100.0;
    if (!std::isfinite(utilization)) throw std::overflow_error("Payload utilization exceeds numeric range");
    return utilization;
}

enum class CargoCategory : std::uint8_t {
    Rations = 1,
    Medical = 2,
    Water = 3,
    Blankets = 4,
    Other = 5
};

enum class MedicalPriority : std::uint8_t {
    Critical = 1,
    High = 2,
    Standard = 3,
    Low = 4
};

enum class VehicleKind : std::uint8_t {
    Ground = 1,
    Aerial = 2,
    Amphibious = 3,
    AmphibiousMedical = 4
};

enum class RequestStatus : std::uint8_t {
    Pending = 1,
    PartiallyAllocated = 2,
    Allocated = 3,
    Dispatched = 4
};

std::string toString(CargoCategory value);
std::string toString(MedicalPriority value);
std::string toString(VehicleKind value);
std::string toString(RequestStatus value);

}
