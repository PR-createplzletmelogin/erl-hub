#include "erl_hub/vehicles.hpp"
#include "erl_hub/exceptions.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace ReliefOps::Logistics {

std::size_t TransportVehicle::activeVehicleCount_ = 0;
double TransportVehicle::totalDispatchedMetricTonnes_ = 0.0;

GPSBeacon::GPSBeacon(GeoPoint position) : position_(position) { position_.validate(); }
const GeoPoint& GPSBeacon::position() const noexcept { return position_; }
void GPSBeacon::update(GeoPoint position) { position.validate(); position_ = position; }

EmergencyLifeSupportUnit::EmergencyLifeSupportUnit(bool installed, unsigned patientCapacity)
    : installed_(installed), patientCapacity_(patientCapacity) {
    if ((installed_ && patientCapacity_ == 0) || (!installed_ && patientCapacity_ != 0)) throw std::invalid_argument("Life-support installation and patient capacity are inconsistent");
}

bool EmergencyLifeSupportUnit::installed() const noexcept { return installed_; }
unsigned EmergencyLifeSupportUnit::patientCapacity() const noexcept { return patientCapacity_; }
void EmergencyLifeSupportUnit::configure(bool installed, unsigned patientCapacity) {
    if ((installed && patientCapacity == 0) || (!installed && patientCapacity != 0)) throw std::invalid_argument("Life-support installation and patient capacity are inconsistent");
    installed_ = installed;
    patientCapacity_ = patientCapacity;
}

TransportVehicle::TransportVehicle(std::string id, double maxPayloadKg, double maximumRangeKm, std::string manifestId)
    : id_(std::move(id)), maxPayloadKg_(maxPayloadKg), maximumRangeKm_(maximumRangeKm), manifest_(manifestId.empty() ? id_ + "-MANIFEST" : std::move(manifestId)) {
    if (!isValidLabel(id_, 112) || !std::isfinite(maxPayloadKg_) || !std::isfinite(maximumRangeKm_) || maxPayloadKg_ <= 0.0 || maximumRangeKm_ <= 0.0) {
        throw std::invalid_argument("Invalid transport vehicle specification");
    }
    ++activeVehicleCount_;
}

TransportVehicle::~TransportVehicle() {
    if (activeVehicleCount_ > 0) --activeVehicleCount_;
}

const std::string& TransportVehicle::id() const noexcept { return id_; }
double TransportVehicle::maxPayloadKg() const noexcept { return maxPayloadKg_; }
double TransportVehicle::maximumRangeKm() const noexcept { return maximumRangeKm_; }
double TransportVehicle::currentPayloadKg() const noexcept { return manifest_.totalWeightKg(); }
double TransportVehicle::remainingPayloadKg() const noexcept { return std::max(0.0, maxPayloadKg_ - currentPayloadKg()); }
bool TransportVehicle::available() const noexcept { return available_; }
const CargoManifest& TransportVehicle::manifest() const noexcept { return manifest_; }

void TransportVehicle::loadCargo(const ReliefCargo& cargo) {
    if (!available_) throw std::logic_error("Vehicle is not available for loading");
    const double utilization = payloadUtilization(currentPayloadKg(), cargo.totalWeightKg(), maxPayloadKg_);
    if (utilization > 100.0 + 1e-9) throw CargoOverweightException("Cargo exceeds vehicle payload capacity");
    manifest_.add(cargo);
}

void TransportVehicle::unloadCargo(const std::string& cargoId, int quantity) {
    if (!available_) throw std::logic_error("Cargo cannot be unloaded while the vehicle is on a mission");
    manifest_.remove(cargoId, quantity);
}

void TransportVehicle::startMission() {
    if (!available_ || manifest_.empty()) throw std::logic_error("Vehicle must be available and loaded before mission start");
    available_ = false;
}

void TransportVehicle::completeMission() {
    if (available_) throw std::logic_error("Vehicle is not currently on a mission");
    manifest_ = CargoManifest(id_ + "-MANIFEST");
    available_ = true;
}
std::size_t TransportVehicle::activeVehicleCount() noexcept { return activeVehicleCount_; }
double TransportVehicle::totalDispatchedMetricTonnes() noexcept { return totalDispatchedMetricTonnes_; }

void TransportVehicle::validateDispatch(double payloadKg) {
    if (!std::isfinite(payloadKg) || payloadKg < 0.0) throw std::invalid_argument("Invalid dispatch payload");
    const double updated = totalDispatchedMetricTonnes_ + payloadKg / 1000.0;
    if (!std::isfinite(updated)) throw std::overflow_error("Dispatched tonnage total exceeds numeric range");
}

void TransportVehicle::recordDispatch(double payloadKg) {
    validateDispatch(payloadKg);
    totalDispatchedMetricTonnes_ += payloadKg / 1000.0;
}

void TransportVehicle::restoreState(bool available, CargoManifest manifest) {
    if (available && !manifest.empty()) throw std::invalid_argument("An available vehicle cannot have a loaded manifest");
    if (manifest.totalWeightKg() > maxPayloadKg_ + 1e-9) throw CargoOverweightException("Restored manifest exceeds vehicle payload capacity");
    manifest_ = std::move(manifest);
    available_ = available;
}

ReliefVehicle::ReliefVehicle(std::string id, double maxPayloadKg, double maximumRangeKm, GeoPoint position, EmergencyLifeSupportUnit lifeSupport)
    : TransportVehicle(std::move(id), maxPayloadKg, maximumRangeKm), gpsBeacon_(position), lifeSupport_(lifeSupport) {}

const GPSBeacon& ReliefVehicle::gpsBeacon() const noexcept { return gpsBeacon_; }
GPSBeacon& ReliefVehicle::gpsBeacon() noexcept { return gpsBeacon_; }
const EmergencyLifeSupportUnit& ReliefVehicle::lifeSupport() const noexcept { return lifeSupport_; }
void ReliefVehicle::updatePosition(GeoPoint position) { gpsBeacon_.update(position); }

GroundConvoy::GroundConvoy(std::string id, double maxPayloadKg, double maximumRangeKm, GeoPoint position, EmergencyLifeSupportUnit lifeSupport)
    : ReliefVehicle(std::move(id), maxPayloadKg, maximumRangeKm, position, lifeSupport) {}

RouteEvaluation GroundConvoy::evaluateRoute(const RouteConditions& conditions) const {
    if (!std::isfinite(conditions.distanceKm) || !std::isfinite(conditions.roadDamageFactor) || conditions.distanceKm < 0.0 || conditions.roadDamageFactor < 0.0 || conditions.roadDamageFactor > 1.0) {
        throw std::invalid_argument("Invalid ground route conditions");
    }
    if (!conditions.roadOpen) return {false, conditions.distanceKm, 0.0, "Road corridor is closed"};
    const double damageMultiplier = 1.0 + conditions.roadDamageFactor * 1.8;
    const double effectiveDistance = conditions.distanceKm * damageMultiplier;
    const double fuel = effectiveDistance * (0.32 + currentPayloadKg() / std::max(1.0, maxPayloadKg()) * 0.12);
    if (!std::isfinite(effectiveDistance) || !std::isfinite(fuel)) throw std::overflow_error("Ground route calculation exceeds numeric range");
    const bool viable = effectiveDistance <= maximumRangeKm() && currentPayloadKg() <= maxPayloadKg();
    return {viable, effectiveDistance, fuel, viable ? "Road corridor is serviceable" : "Ground vehicle range or payload limit exceeded"};
}

VehicleKind GroundConvoy::vehicleKind() const noexcept { return VehicleKind::Ground; }

AerialTransport::AerialTransport(std::string id, double maxPayloadKg, double maximumRangeKm, GeoPoint position, EmergencyLifeSupportUnit lifeSupport)
    : ReliefVehicle(std::move(id), maxPayloadKg, maximumRangeKm, position, lifeSupport) {}

RouteEvaluation AerialTransport::evaluateRoute(const RouteConditions& conditions) const {
    if (!std::isfinite(conditions.distanceKm) || conditions.distanceKm < 0.0) throw std::invalid_argument("Invalid aerial route distance");
    if (conditions.airspaceRestricted) return {false, conditions.distanceKm, 0.0, "Airspace restriction prevents flight"};
    const double fuel = conditions.distanceKm * (1.15 + currentPayloadKg() / std::max(1.0, maxPayloadKg()) * 0.35);
    if (!std::isfinite(fuel)) throw std::overflow_error("Aerial route calculation exceeds numeric range");
    const bool viable = conditions.distanceKm <= maximumRangeKm() && currentPayloadKg() <= maxPayloadKg();
    return {viable, conditions.distanceKm, fuel, viable ? "Aerial corridor is available" : "Aircraft range or payload limit exceeded"};
}

VehicleKind AerialTransport::vehicleKind() const noexcept { return VehicleKind::Aerial; }

AmphibiousCraft::AmphibiousCraft(std::string id, double maxPayloadKg, double maximumRangeKm, GeoPoint position, EmergencyLifeSupportUnit lifeSupport)
    : ReliefVehicle(std::move(id), maxPayloadKg, maximumRangeKm, position, lifeSupport) {}

RouteEvaluation AmphibiousCraft::evaluateRoute(const RouteConditions& conditions) const {
    if (!std::isfinite(conditions.distanceKm) || !std::isfinite(conditions.waterDepthM) || conditions.distanceKm < 0.0 || conditions.waterDepthM < 0.0) {
        throw std::invalid_argument("Invalid amphibious route conditions");
    }
    if (!conditions.waterwayOpen || conditions.waterDepthM < 0.3 || conditions.waterDepthM > 4.5) {
        return {false, conditions.distanceKm, 0.0, "Waterway is closed or outside navigable depth"};
    }
    const double fuel = conditions.distanceKm * (0.48 + currentPayloadKg() / std::max(1.0, maxPayloadKg()) * 0.18);
    if (!std::isfinite(fuel)) throw std::overflow_error("Amphibious route calculation exceeds numeric range");
    const bool viable = conditions.distanceKm <= maximumRangeKm() && currentPayloadKg() <= maxPayloadKg();
    return {viable, conditions.distanceKm, fuel, viable ? "Water corridor is navigable" : "Craft range or payload limit exceeded"};
}

VehicleKind AmphibiousCraft::vehicleKind() const noexcept { return VehicleKind::Amphibious; }

AmphibiousMedicalUnit::AmphibiousMedicalUnit(std::string id, double maxPayloadKg, double maximumRangeKm, GeoPoint position, EmergencyLifeSupportUnit lifeSupport)
    : ReliefVehicle(id, maxPayloadKg, maximumRangeKm, position, lifeSupport),
      GroundConvoy(id, maxPayloadKg, maximumRangeKm, position, lifeSupport),
      AmphibiousCraft(std::move(id), maxPayloadKg, maximumRangeKm, position, lifeSupport) {}

RouteEvaluation AmphibiousMedicalUnit::evaluateRoute(const RouteConditions& conditions) const {
    const auto road = GroundConvoy::evaluateRoute(conditions);
    const auto water = AmphibiousCraft::evaluateRoute(conditions);
    if (road.viable && water.viable) return road.estimatedFuelLitres <= water.estimatedFuelLitres ? road : water;
    if (road.viable) return road;
    if (water.viable) return water;
    return {false, std::min(road.distanceKm, water.distanceKm), 0.0, "Neither ground nor water route is viable: " + road.rationale + "; " + water.rationale};
}

VehicleKind AmphibiousMedicalUnit::vehicleKind() const noexcept { return VehicleKind::AmphibiousMedical; }

std::unique_ptr<ReliefVehicle> createVehicle(VehicleKind kind, const std::string& id, double payloadKg, double rangeKm, GeoPoint position, EmergencyLifeSupportUnit lifeSupport) {
    switch (kind) {
        case VehicleKind::Ground: return std::make_unique<GroundConvoy>(id, payloadKg, rangeKm, position, lifeSupport);
        case VehicleKind::Aerial: return std::make_unique<AerialTransport>(id, payloadKg, rangeKm, position, lifeSupport);
        case VehicleKind::Amphibious: return std::make_unique<AmphibiousCraft>(id, payloadKg, rangeKm, position, lifeSupport);
        case VehicleKind::AmphibiousMedical: return std::make_unique<AmphibiousMedicalUnit>(id, payloadKg, rangeKm, position, lifeSupport);
    }
    throw std::invalid_argument("Unsupported vehicle type");
}

}
