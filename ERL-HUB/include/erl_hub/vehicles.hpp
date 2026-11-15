#pragma once

#include "erl_hub/inventory.hpp"

#include <memory>
#include <string>

namespace ReliefOps {
class PersistenceManager;
namespace Logistics { struct PersistenceAccess; struct DispatchAccountingAccess; }
}

namespace ReliefOps::Logistics {

using Inventory::CargoManifest;
using Inventory::ReliefCargo;

class GPSBeacon {
public:
    GPSBeacon() = default;
    explicit GPSBeacon(GeoPoint position);
    const GeoPoint& position() const noexcept;
    void update(GeoPoint position);

private:
    GeoPoint position_;
};

class EmergencyLifeSupportUnit {
public:
    EmergencyLifeSupportUnit(bool installed = true, unsigned patientCapacity = 2);
    bool installed() const noexcept;
    unsigned patientCapacity() const noexcept;
    void configure(bool installed, unsigned patientCapacity);

private:
    bool installed_{};
    unsigned patientCapacity_{};
};

struct RouteConditions {
    double distanceKm{};
    double roadDamageFactor{};
    double waterDepthM{};
    bool roadOpen{true};
    bool waterwayOpen{true};
    bool airspaceRestricted{};
};

struct RouteEvaluation {
    bool viable{};
    double distanceKm{};
    double estimatedFuelLitres{};
    std::string rationale;
};

class TransportVehicle {
public:
    TransportVehicle(std::string id, double maxPayloadKg, double maximumRangeKm, std::string manifestId = {});
    TransportVehicle(const TransportVehicle&) = delete;
    TransportVehicle& operator=(const TransportVehicle&) = delete;
    virtual ~TransportVehicle();

    const std::string& id() const noexcept;
    double maxPayloadKg() const noexcept;
    double maximumRangeKm() const noexcept;
    double currentPayloadKg() const noexcept;
    double remainingPayloadKg() const noexcept;
    bool available() const noexcept;
    const CargoManifest& manifest() const noexcept;
    void loadCargo(const ReliefCargo& cargo);
    void unloadCargo(const std::string& cargoId, int quantity);
    void completeMission();
    static std::size_t activeVehicleCount() noexcept;
    static double totalDispatchedMetricTonnes() noexcept;

private:
    friend struct PersistenceAccess;
    friend struct DispatchAccountingAccess;
    void startMission();
    static void validateDispatch(double payloadKg);
    static void recordDispatch(double payloadKg);
    void restoreState(bool available, CargoManifest manifest);
    std::string id_;
    double maxPayloadKg_{};
    double maximumRangeKm_{};
    bool available_{true};
    CargoManifest manifest_;
    static std::size_t activeVehicleCount_;
    static double totalDispatchedMetricTonnes_;
};

class ReliefVehicle : public TransportVehicle {
public:
    ReliefVehicle(std::string id, double maxPayloadKg, double maximumRangeKm, GeoPoint position, EmergencyLifeSupportUnit lifeSupport = {});
    ~ReliefVehicle() override = default;

    virtual RouteEvaluation evaluateRoute(const RouteConditions& conditions) const = 0;
    virtual VehicleKind vehicleKind() const noexcept = 0;
    const GPSBeacon& gpsBeacon() const noexcept;
    GPSBeacon& gpsBeacon() noexcept;
    const EmergencyLifeSupportUnit& lifeSupport() const noexcept;
    void updatePosition(GeoPoint position);

private:
    GPSBeacon gpsBeacon_;
    EmergencyLifeSupportUnit lifeSupport_;
};

class GroundConvoy : virtual public ReliefVehicle {
public:
    GroundConvoy(std::string id, double maxPayloadKg, double maximumRangeKm, GeoPoint position, EmergencyLifeSupportUnit lifeSupport = {});
    RouteEvaluation evaluateRoute(const RouteConditions& conditions) const override;
    VehicleKind vehicleKind() const noexcept override;
};

class AerialTransport : virtual public ReliefVehicle {
public:
    AerialTransport(std::string id, double maxPayloadKg, double maximumRangeKm, GeoPoint position, EmergencyLifeSupportUnit lifeSupport = {});
    RouteEvaluation evaluateRoute(const RouteConditions& conditions) const override;
    VehicleKind vehicleKind() const noexcept override;
};

class AmphibiousCraft : virtual public ReliefVehicle {
public:
    AmphibiousCraft(std::string id, double maxPayloadKg, double maximumRangeKm, GeoPoint position, EmergencyLifeSupportUnit lifeSupport = {});
    RouteEvaluation evaluateRoute(const RouteConditions& conditions) const override;
    VehicleKind vehicleKind() const noexcept override;
};

class AmphibiousMedicalUnit final : public GroundConvoy, public AmphibiousCraft {
public:
    AmphibiousMedicalUnit(std::string id, double maxPayloadKg, double maximumRangeKm, GeoPoint position, EmergencyLifeSupportUnit lifeSupport = {});
    RouteEvaluation evaluateRoute(const RouteConditions& conditions) const override;
    VehicleKind vehicleKind() const noexcept override;
};

std::unique_ptr<ReliefVehicle> createVehicle(VehicleKind kind, const std::string& id, double payloadKg, double rangeKm, GeoPoint position, EmergencyLifeSupportUnit lifeSupport = {});

}
