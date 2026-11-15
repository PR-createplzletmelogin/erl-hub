#pragma once

#include "erl_hub/types.hpp"

#include <iosfwd>
#include <string>
#include <unordered_map>
#include <vector>

namespace ReliefOps::Logistics { struct RequestAccess; }

namespace ReliefOps::Inventory {

class IncidentCommander;

std::ostream& manifestTableHeader(std::ostream& output);
std::ostream& manifestTableSeparator(std::ostream& output);

class ReliefCargo {
public:
    ReliefCargo() = default;
    ReliefCargo(std::string id, std::string name, CargoCategory category, int quantity, double unitWeightKg, double unitVolumeM3);

    const std::string& id() const noexcept;
    const std::string& name() const noexcept;
    CargoCategory category() const noexcept;
    int quantity() const noexcept;
    double unitWeightKg() const noexcept;
    double unitVolumeM3() const noexcept;
    double totalWeightKg() const noexcept;
    double totalVolumeM3() const noexcept;
    void addUnits(int quantity);
    void removeUnits(int quantity);
    void setQuantity(int quantity);
    bool operator==(const ReliefCargo& other) const noexcept;

    friend std::ostream& operator<<(std::ostream& output, const ReliefCargo& cargo);
    friend std::istream& operator>>(std::istream& input, ReliefCargo& cargo);

private:
    std::string id_;
    std::string name_;
    CargoCategory category_{CargoCategory::Other};
    int quantity_{};
    double unitWeightKg_{};
    double unitVolumeM3_{};
};

class CargoManifest {
public:
    explicit CargoManifest(std::string manifestId = {}, std::size_t initialCapacity = 4);
    CargoManifest(const CargoManifest& other);
    CargoManifest(CargoManifest&& other) noexcept;
    CargoManifest& operator=(CargoManifest other) noexcept;
    ~CargoManifest();

    void swap(CargoManifest& other) noexcept;
    const std::string& id() const noexcept;
    std::size_t size() const noexcept;
    bool empty() const noexcept;
    bool contains(const std::string& cargoId) const noexcept;
    const ReliefCargo* find(const std::string& cargoId) const noexcept;
    ReliefCargo* find(const std::string& cargoId) noexcept;
    void add(const ReliefCargo& cargo);
    void remove(const std::string& cargoId, int quantity);
    double totalWeightKg() const noexcept;
    double totalVolumeM3() const noexcept;
    std::vector<ReliefCargo> items() const;

    bool operator==(const CargoManifest& other) const noexcept;
    friend std::ostream& operator<<(std::ostream& output, const CargoManifest& manifest);
    friend std::istream& operator>>(std::istream& input, CargoManifest& manifest);

private:
    void reserve(std::size_t newCapacity);
    std::string manifestId_;
    ReliefCargo* items_{};
    std::size_t size_{};
    std::size_t capacity_{};
};

class DistributionDepot {
public:
    DistributionDepot(std::string id, std::string name, GeoPoint location);

    const std::string& id() const noexcept;
    const std::string& name() const noexcept;
    const GeoPoint& location() const noexcept;
    int quantityOf(const std::string& cargoId) const noexcept;
    int onHandQuantityOf(const std::string& cargoId) const noexcept;
    int reservedQuantityOf(const std::string& cargoId) const noexcept;
    int reservedQuantityForRequest(const std::string& requestId, const std::string& cargoId) const noexcept;
    const ReliefCargo& findCargo(const std::string& cargoId) const;
    std::vector<ReliefCargo> inventory() const;
    std::size_t distinctCargoCount() const noexcept;

    DistributionDepot& operator+=(const ReliefCargo& cargo);
    DistributionDepot& operator-=(const ReliefCargo& cargo);

private:
    friend class IncidentCommander;
    friend struct Logistics::RequestAccess;
    void reserveInventory(const std::string& requestId, const ReliefCargo& cargo);
    void releaseInventoryReservation(const std::string& requestId, const std::string& cargoId, int quantity);
    void consumeInventoryReservations(const std::string& requestId, const std::vector<ReliefCargo>& cargo);
    std::string id_;
    std::string name_;
    GeoPoint location_;
    std::unordered_map<std::string, ReliefCargo> inventory_;
    std::unordered_map<std::string, std::unordered_map<std::string, int>> reservationsByRequest_;
    std::unordered_map<std::string, int> reservedTotals_;
};

class IncidentCommander {
public:
    static void overrideInventory(DistributionDepot& depot, const ReliefCargo& cargo);
    static class ReliefRequest& authorizePriority(class ReliefRequest& request, MedicalPriority priority);
};

struct CargoRequirement {
    ReliefCargo specification;
    int requestedQuantity{};
    int allocatedQuantity{};

    int remainingQuantity() const noexcept;
};

class ReliefRequest {
public:
    ReliefRequest(std::string id, std::string destinationName, GeoPoint destination, MedicalPriority priority);

    const std::string& id() const noexcept;
    const std::string& destinationName() const noexcept;
    const GeoPoint& destination() const noexcept;
    MedicalPriority priority() const noexcept;
    RequestStatus status() const noexcept;
    const std::string& allocatedDepotId() const noexcept;
    const std::vector<CargoRequirement>& requirements() const noexcept;
    int remainingQuantity(const std::string& cargoId) const noexcept;
    bool fullyAllocated() const noexcept;
    void addRequirement(const ReliefCargo& specification, int quantity);
    void setDestination(GeoPoint destination);

private:
    friend class IncidentCommander;
    friend struct Logistics::RequestAccess;
    ReliefRequest& recordAllocation(const std::string& cargoId, int quantity, const std::string& depotId);
    ReliefRequest& releaseAllocation(const std::string& cargoId, int quantity);
    ReliefRequest& markDispatched();
    ReliefRequest& setPriority(MedicalPriority priority);
    std::string id_;
    std::string destinationName_;
    GeoPoint destination_;
    MedicalPriority priority_{MedicalPriority::Standard};
    RequestStatus status_{RequestStatus::Pending};
    std::string allocatedDepotId_;
    std::vector<CargoRequirement> requirements_;
};

}
