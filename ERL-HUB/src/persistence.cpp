#include "erl_hub/persistence.hpp"
#include "request_access.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <sstream>
#include <map>
#include <stdexcept>
#include <system_error>
#include <type_traits>
#include <set>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace ReliefOps::Logistics {

struct PersistenceAccess {
    static void restore(TransportVehicle& vehicle, bool available, Inventory::CargoManifest manifest) {
        vehicle.restoreState(available, std::move(manifest));
    }
};

}

namespace ReliefOps {
namespace {

constexpr std::array<char, 8> magic{{'E', 'R', 'L', 'H', 'U', 'B', '1', '7'}};
constexpr std::uint32_t formatVersion = 3;
constexpr std::uint32_t minimumSupportedFormatVersion = 1;
constexpr std::uint32_t maximumRecords = 100000;
constexpr std::uint32_t maximumDepotCargoTypes = 4096;
constexpr std::uint32_t maximumManifestItems = 1024;
constexpr std::uint32_t maximumRequestRequirements = 256;
constexpr std::uint32_t maximumStringLength = 4096;
constexpr std::uint64_t maximumPayloadSize = 1ULL << 28;

void atomicReplace(const std::filesystem::path& source, const std::filesystem::path& destination) {
#if defined(_WIN32)
    if (!MoveFileExW(source.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const auto errorCode = static_cast<int>(GetLastError());
        throw std::system_error(std::error_code(errorCode, std::system_category()), "Unable to atomically replace " + destination.string());
    }
#else
    std::error_code error;
    std::filesystem::rename(source, destination, error);
    if (error) throw std::system_error(error, "Unable to atomically replace " + destination.string());
#endif
}

class BinaryWriter {
public:
    explicit BinaryWriter(std::ostream& output) : output_(output) {}

    template <typename T>
    void pod(const T& value) {
        static_assert(std::is_trivially_copyable<T>::value, "Binary field must be trivially copyable");
        reserve(sizeof(T));
        output_.write(reinterpret_cast<const char*>(&value), sizeof(T));
        ensure();
    }

    void string(const std::string& value) {
        if (value.size() > maximumStringLength) throw std::runtime_error("Persistent string exceeds size limit");
        if (value.size() > maximumPayloadSize - bytesWritten_ - sizeof(std::uint32_t)) throw std::runtime_error("Persistent payload exceeds size limit");
        pod(static_cast<std::uint32_t>(value.size()));
        reserve(value.size());
        output_.write(value.data(), static_cast<std::streamsize>(value.size()));
        ensure();
    }

    void cargo(const Inventory::ReliefCargo& item) {
        string(item.id());
        string(item.name());
        pod(static_cast<std::uint8_t>(item.category()));
        pod(static_cast<std::int32_t>(item.quantity()));
        pod(item.unitWeightKg());
        pod(item.unitVolumeM3());
    }

    void ensure() {
        if (!output_) throw std::runtime_error("Failed while writing persistent state");
    }

private:
    void reserve(std::size_t amount) {
        if (amount > maximumPayloadSize - bytesWritten_) throw std::runtime_error("Persistent payload exceeds size limit");
        bytesWritten_ += amount;
    }

    std::ostream& output_;
    std::uint64_t bytesWritten_{};
};

class BinaryReader {
public:
    explicit BinaryReader(std::istream& input) : input_(input) {}

    template <typename T>
    T pod() {
        static_assert(std::is_trivially_copyable<T>::value, "Binary field must be trivially copyable");
        T value{};
        input_.read(reinterpret_cast<char*>(&value), sizeof(T));
        ensure();
        return value;
    }

    std::string string() {
        const auto length = pod<std::uint32_t>();
        if (length > maximumStringLength) throw std::runtime_error("Persistent string length exceeds limit");
        std::string value(length, '\0');
        if (length > 0) input_.read(value.data(), static_cast<std::streamsize>(length));
        ensure();
        return value;
    }

    Inventory::ReliefCargo cargo() {
        auto id = string();
        auto name = string();
        const auto category = pod<std::uint8_t>();
        const auto quantity = pod<std::int32_t>();
        const auto weight = pod<double>();
        const auto volume = pod<double>();
        if (category < static_cast<std::uint8_t>(CargoCategory::Rations) || category > static_cast<std::uint8_t>(CargoCategory::Other)) {
            throw std::runtime_error("Invalid cargo category in persistent state");
        }
        return Inventory::ReliefCargo(std::move(id), std::move(name), static_cast<CargoCategory>(category), quantity, weight, volume);
    }

    void ensure() {
        if (!input_) throw std::runtime_error("Persistent state is truncated or unreadable");
    }

private:
    std::istream& input_;
};

std::uint64_t checksum(const std::string& bytes) {
    std::uint64_t value = 14695981039346656037ULL;
    for (const char rawByte : bytes) {
        const auto byte = static_cast<unsigned char>(rawByte);
        value ^= byte;
        value *= 1099511628211ULL;
    }
    return value;
}

bool validPriority(MedicalPriority value) {
    const auto raw = static_cast<unsigned>(value);
    return raw >= static_cast<unsigned>(MedicalPriority::Critical) && raw <= static_cast<unsigned>(MedicalPriority::Low);
}

void validateState(const ApplicationState& state) {
    if (state.depots.size() > maximumRecords || state.vehicles.size() > maximumRecords || state.requests.size() > maximumRecords || state.dispatches.size() > maximumRecords) {
        throw std::runtime_error("Application state exceeds the persistent record limit");
    }

    std::set<std::string> depotIds;
    std::set<std::string> dispatchIds;
    std::set<std::string> dispatchedRequestIds;
    std::map<std::string, const Inventory::DistributionDepot*> depotsById;
    std::map<std::string, const Logistics::ReliefVehicle*> vehiclesById;
    std::map<std::string, const Inventory::ReliefRequest*> requestsById;
    std::map<std::string, double> payloadByRequest;
    std::map<std::string, std::map<std::string, int>> expectedReservations;
    std::map<std::string, const Logistics::DispatchRecord*> latestDispatchByVehicle;
    double dispatchedMetricTonnes{};

    for (const auto& depot : state.depots) {
        if (!isValidLabel(depot.id(), 128) || !isValidLabel(depot.name(), 512) || !depotIds.insert(depot.id()).second) throw std::runtime_error("Application state contains an invalid or duplicate depot ID");
        depotsById.emplace(depot.id(), &depot);
        const auto items = depot.inventory();
        if (items.size() > maximumDepotCargoTypes) throw std::runtime_error("Depot inventory exceeds the persistent record limit");
        for (const auto& item : items) {
            if (!isValidLabel(item.id(), 128) || !isValidLabel(item.name(), 512) || item.quantity() < 0 || !std::isfinite(item.totalWeightKg()) || !std::isfinite(item.totalVolumeM3())) throw std::runtime_error("Depot inventory contains an invalid cargo record");
        }
    }

    for (const auto& vehicle : state.vehicles) {
        if (!vehicle) throw std::runtime_error("Application state contains a null vehicle");
        if (!isValidLabel(vehicle->id(), 112) || !vehiclesById.emplace(vehicle->id(), vehicle.get()).second) throw std::runtime_error("Application state contains an invalid or duplicate vehicle ID");
        const auto& manifest = vehicle->manifest();
        const auto items = manifest.items();
        if (!isValidLabel(manifest.id(), 128) || items.size() > maximumManifestItems || !std::isfinite(manifest.totalWeightKg()) || !std::isfinite(manifest.totalVolumeM3()) || manifest.totalWeightKg() > vehicle->maxPayloadKg() + 1e-9) {
            throw std::runtime_error("Vehicle manifest is invalid or exceeds payload capacity");
        }
        if ((vehicle->available() && !manifest.empty()) || (!vehicle->available() && manifest.empty())) throw std::runtime_error("Vehicle availability and manifest state are inconsistent");
        const auto patientCapacity = vehicle->lifeSupport().patientCapacity();
        if ((vehicle->lifeSupport().installed() && patientCapacity == 0) || (!vehicle->lifeSupport().installed() && patientCapacity != 0)) throw std::runtime_error("Vehicle life-support state is inconsistent");
        for (const auto& item : items) {
            if (!isValidLabel(item.id(), 128) || !isValidLabel(item.name(), 512) || item.quantity() <= 0 || !std::isfinite(item.totalWeightKg()) || !std::isfinite(item.totalVolumeM3())) throw std::runtime_error("Vehicle manifest contains an invalid cargo record");
        }
    }

    for (const auto& request : state.requests) {
        if (!isValidLabel(request.id(), 128) || !isValidLabel(request.destinationName(), 512) || !validPriority(request.priority()) || request.requirements().empty() || request.requirements().size() > maximumRequestRequirements || !requestsById.emplace(request.id(), &request).second) {
            throw std::runtime_error("Application state contains an invalid or duplicate request");
        }
        std::set<std::string> requirementIds;
        bool anyAllocated = false;
        bool fullyAllocated = true;
        double allocatedPayloadKg{};
        for (const auto& requirement : request.requirements()) {
            const auto& item = requirement.specification;
            if (!isValidLabel(item.id(), 128) || !isValidLabel(item.name(), 512) || item.quantity() != 0 || requirement.requestedQuantity <= 0 || requirement.allocatedQuantity < 0 || requirement.allocatedQuantity > requirement.requestedQuantity || !requirementIds.insert(item.id()).second) {
                throw std::runtime_error("Request contains an invalid or duplicate cargo requirement");
            }
            anyAllocated = anyAllocated || requirement.allocatedQuantity > 0;
            fullyAllocated = fullyAllocated && requirement.allocatedQuantity == requirement.requestedQuantity;
            const double itemPayload = static_cast<double>(requirement.allocatedQuantity) * item.unitWeightKg();
            if (!std::isfinite(itemPayload) || itemPayload > std::numeric_limits<double>::max() - allocatedPayloadKg) throw std::runtime_error("Request allocation payload exceeds numeric range");
            allocatedPayloadKg += itemPayload;
        }
        payloadByRequest.emplace(request.id(), allocatedPayloadKg);
        const auto expectedStatus = !anyAllocated ? RequestStatus::Pending : (fullyAllocated ? RequestStatus::Allocated : RequestStatus::PartiallyAllocated);
        const Inventory::DistributionDepot* sourceDepot = nullptr;
        if (anyAllocated) {
            if (!isValidLabel(request.allocatedDepotId(), 128) || depotIds.find(request.allocatedDepotId()) == depotIds.end()) throw std::runtime_error("Request allocation references an invalid or missing source depot");
            sourceDepot = depotsById.find(request.allocatedDepotId())->second;
            for (const auto& requirement : request.requirements()) {
                if (requirement.allocatedQuantity == 0) continue;
                const auto& depotCargo = sourceDepot->findCargo(requirement.specification.id());
                const auto& requestedCargo = requirement.specification;
                if (!(depotCargo == requestedCargo)) throw std::runtime_error("Request allocation cargo definition does not match its source depot");
            }
        } else if (!request.allocatedDepotId().empty()) {
            throw std::runtime_error("Unallocated request unexpectedly references a source depot");
        }
        if (request.status() == RequestStatus::Dispatched) {
            if (!fullyAllocated) throw std::runtime_error("Dispatched request is not fully allocated");
        } else if (request.status() != expectedStatus) {
            throw std::runtime_error("Request status does not match its allocation quantities");
        }
        for (const auto& requirement : request.requirements()) {
            const int expectedReserved = request.status() == RequestStatus::Dispatched ? 0 : requirement.allocatedQuantity;
            const int actualReserved = sourceDepot ? sourceDepot->reservedQuantityForRequest(request.id(), requirement.specification.id()) : 0;
            if (actualReserved != expectedReserved) throw std::runtime_error("Request allocation and depot reservation records are inconsistent");
            if (expectedReserved > 0) {
                auto& expectedByCargo = expectedReservations[request.allocatedDepotId()];
                auto& total = expectedByCargo[requirement.specification.id()];
                if (total > std::numeric_limits<int>::max() - expectedReserved) throw std::runtime_error("Aggregate depot reservations exceed numeric range");
                total += expectedReserved;
            }
        }
    }

    for (const auto& depot : state.depots) {
        const auto expectedDepot = expectedReservations.find(depot.id());
        for (const auto& item : depot.inventory()) {
            int expectedQuantity{};
            if (expectedDepot != expectedReservations.end()) {
                const auto expected = expectedDepot->second.find(item.id());
                if (expected != expectedDepot->second.end()) expectedQuantity = expected->second;
            }
            if (depot.reservedQuantityOf(item.id()) != expectedQuantity || expectedQuantity > item.quantity()) {
                throw std::runtime_error("Depot reservation totals do not match pending request allocations");
            }
        }
    }

    for (const auto& record : state.dispatches) {
        if (!isValidLabel(record.dispatchId, 128) || !isValidLabel(record.requestId, 128) || !isValidLabel(record.vehicleId, 128) || !isValidLabel(record.manifestId, 128) || !isValidLabel(record.destinationName, 512) || !isValidLabel(record.timestamp, 64) || !std::isfinite(record.payloadKg) || !std::isfinite(record.routeDistanceKm) || record.payloadKg < 0.0 || record.routeDistanceKm < 0.0 || !validPriority(record.priority)) {
            throw std::runtime_error("Application state contains an invalid dispatch record");
        }
        if (!dispatchIds.insert(record.dispatchId).second || !dispatchedRequestIds.insert(record.requestId).second) throw std::runtime_error("Application state contains duplicate dispatch identifiers");
        const double updatedTonnage = dispatchedMetricTonnes + record.payloadKg / 1000.0;
        if (!std::isfinite(updatedTonnage)) throw std::runtime_error("Dispatch history exceeds numeric range");
        dispatchedMetricTonnes = updatedTonnage;
        latestDispatchByVehicle[record.vehicleId] = &record;
        const auto request = requestsById.find(record.requestId);
        const auto requestPayload = payloadByRequest.find(record.requestId);
        const auto vehicle = vehiclesById.find(record.vehicleId);
        if (request == requestsById.end() || requestPayload == payloadByRequest.end() || request->second->status() != RequestStatus::Dispatched || request->second->priority() != record.priority || request->second->destinationName() != record.destinationName || vehicle == vehiclesById.end() || record.payloadKg > vehicle->second->maxPayloadKg() + 1e-9 || record.manifestId != vehicle->second->manifest().id()) {
            throw std::runtime_error("Dispatch record references inconsistent application state");
        }
        const double payloadDifference = std::abs(record.payloadKg - requestPayload->second);
        if (payloadDifference > std::max(1e-6, requestPayload->second * 1e-9)) throw std::runtime_error("Dispatch payload does not match its allocated request quantities");
    }

    for (const auto& entry : requestsById) {
        const bool hasDispatch = dispatchedRequestIds.find(entry.first) != dispatchedRequestIds.end();
        if ((entry.second->status() == RequestStatus::Dispatched) != hasDispatch) throw std::runtime_error("Dispatched request and dispatch history are inconsistent");
    }

    for (const auto& vehicle : state.vehicles) {
        if (vehicle->available()) continue;
        const auto latest = latestDispatchByVehicle.find(vehicle->id());
        if (latest == latestDispatchByVehicle.end()) throw std::runtime_error("An on-mission vehicle has no dispatch history");
        const auto* record = latest->second;
        const double manifestWeight = vehicle->manifest().totalWeightKg();
        const double allowedDifference = std::max(1e-6, manifestWeight * 1e-9);
        if (record->manifestId != vehicle->manifest().id() || std::abs(record->payloadKg - manifestWeight) > allowedDifference) {
            throw std::runtime_error("Active vehicle manifest does not match its latest dispatch record");
        }
    }
}

void writeState(const ApplicationState& state, std::ostream& output) {
    BinaryWriter writer(output);
    writer.pod(static_cast<std::uint32_t>(state.depots.size()));
    for (const auto& depot : state.depots) {
        writer.string(depot.id());
        writer.string(depot.name());
        writer.pod(depot.location().latitude);
        writer.pod(depot.location().longitude);
        const auto items = depot.inventory();
        writer.pod(static_cast<std::uint32_t>(items.size()));
        for (const auto& item : items) writer.cargo(item);
    }

    writer.pod(static_cast<std::uint32_t>(state.vehicles.size()));
    for (const auto& vehicle : state.vehicles) {
        writer.pod(static_cast<std::uint8_t>(vehicle->vehicleKind()));
        writer.string(vehicle->id());
        writer.pod(vehicle->maxPayloadKg());
        writer.pod(vehicle->maximumRangeKm());
        writer.pod(vehicle->gpsBeacon().position().latitude);
        writer.pod(vehicle->gpsBeacon().position().longitude);
        writer.pod(static_cast<std::uint8_t>(vehicle->lifeSupport().installed() ? 1 : 0));
        writer.pod(static_cast<std::uint32_t>(vehicle->lifeSupport().patientCapacity()));
        writer.pod(static_cast<std::uint8_t>(vehicle->available() ? 1 : 0));
        const auto manifestItems = vehicle->manifest().items();
        writer.string(vehicle->manifest().id());
        writer.pod(static_cast<std::uint32_t>(manifestItems.size()));
        for (const auto& item : manifestItems) writer.cargo(item);
    }

    writer.pod(static_cast<std::uint32_t>(state.requests.size()));
    for (const auto& request : state.requests) {
        writer.string(request.id());
        writer.string(request.destinationName());
        writer.pod(request.destination().latitude);
        writer.pod(request.destination().longitude);
        writer.pod(static_cast<std::uint8_t>(request.priority()));
        writer.pod(static_cast<std::uint8_t>(request.status()));
        writer.string(request.allocatedDepotId());
        writer.pod(static_cast<std::uint32_t>(request.requirements().size()));
        for (const auto& requirement : request.requirements()) {
            writer.cargo(requirement.specification);
            writer.pod(static_cast<std::int32_t>(requirement.requestedQuantity));
            writer.pod(static_cast<std::int32_t>(requirement.allocatedQuantity));
        }
    }

    writer.pod(static_cast<std::uint32_t>(state.dispatches.size()));
    for (const auto& dispatch : state.dispatches) {
        writer.string(dispatch.dispatchId);
        writer.string(dispatch.requestId);
        writer.string(dispatch.vehicleId);
        writer.string(dispatch.manifestId);
        writer.string(dispatch.destinationName);
        writer.string(dispatch.timestamp);
        writer.pod(dispatch.payloadKg);
        writer.pod(dispatch.routeDistanceKm);
        writer.pod(static_cast<std::uint8_t>(dispatch.priority));
    }
}

void ensureCount(std::uint32_t count) {
    if (count > maximumRecords) throw std::runtime_error("Persistent record count exceeds safety limit");
}

ApplicationState readState(std::istream& input, std::uint32_t version) {
    BinaryReader reader(input);
    ApplicationState loaded;

    const auto depotCount = reader.pod<std::uint32_t>();
    ensureCount(depotCount);
    loaded.depots.reserve(depotCount);
    for (std::uint32_t i = 0; i < depotCount; ++i) {
        auto id = reader.string();
        auto name = reader.string();
        GeoPoint location{reader.pod<double>(), reader.pod<double>()};
        Inventory::DistributionDepot depot(std::move(id), std::move(name), location);
        const auto cargoCount = reader.pod<std::uint32_t>();
        if (cargoCount > maximumDepotCargoTypes) throw std::runtime_error("Depot cargo type count exceeds safety limit");
        for (std::uint32_t j = 0; j < cargoCount; ++j) depot += reader.cargo();
        loaded.depots.push_back(std::move(depot));
    }

    const auto vehicleCount = reader.pod<std::uint32_t>();
    ensureCount(vehicleCount);
    loaded.vehicles.reserve(vehicleCount);
    for (std::uint32_t i = 0; i < vehicleCount; ++i) {
        const auto kindValue = reader.pod<std::uint8_t>();
        if (kindValue < static_cast<std::uint8_t>(VehicleKind::Ground) || kindValue > static_cast<std::uint8_t>(VehicleKind::AmphibiousMedical)) {
            throw std::runtime_error("Invalid vehicle type in persistent state");
        }
        auto id = reader.string();
        const auto payload = reader.pod<double>();
        const auto range = reader.pod<double>();
        GeoPoint position{reader.pod<double>(), reader.pod<double>()};
        const auto lifeInstalledValue = reader.pod<std::uint8_t>();
        if (lifeInstalledValue > 1) throw std::runtime_error("Invalid life-support flag in persistent state");
        const bool lifeInstalled = lifeInstalledValue == 1;
        const auto patientCapacity = reader.pod<std::uint32_t>();
        if ((!lifeInstalled && patientCapacity != 0) || (lifeInstalled && patientCapacity == 0)) throw std::runtime_error("Invalid life-support capacity in persistent state");
        const auto availableValue = reader.pod<std::uint8_t>();
        if (availableValue > 1) throw std::runtime_error("Invalid vehicle availability flag in persistent state");
        const bool available = availableValue == 1;
        auto manifestId = reader.string();
        const auto itemCount = reader.pod<std::uint32_t>();
        if (itemCount > maximumManifestItems) throw std::runtime_error("Vehicle manifest item count exceeds safety limit");
        auto vehicle = Logistics::createVehicle(static_cast<VehicleKind>(kindValue), id, payload, range, position, Logistics::EmergencyLifeSupportUnit(lifeInstalled, patientCapacity));
        Inventory::CargoManifest manifest(std::move(manifestId), itemCount);
        for (std::uint32_t j = 0; j < itemCount; ++j) manifest.add(reader.cargo());
        Logistics::PersistenceAccess::restore(*vehicle, available, std::move(manifest));
        loaded.vehicles.push_back(std::move(vehicle));
    }

    const auto requestCount = reader.pod<std::uint32_t>();
    ensureCount(requestCount);
    loaded.requests.reserve(requestCount);
    for (std::uint32_t i = 0; i < requestCount; ++i) {
        auto id = reader.string();
        auto destinationName = reader.string();
        GeoPoint destination{reader.pod<double>(), reader.pod<double>()};
        const auto priorityValue = reader.pod<std::uint8_t>();
        const auto statusValue = reader.pod<std::uint8_t>();
        if (priorityValue < 1 || priorityValue > 4 || statusValue < 1 || statusValue > 4) throw std::runtime_error("Invalid request state in persistent data");
        auto allocatedDepotId = version >= 2 ? reader.string() : std::string{};
        Inventory::ReliefRequest request(std::move(id), std::move(destinationName), destination, static_cast<MedicalPriority>(priorityValue));
        const auto requirementCount = reader.pod<std::uint32_t>();
        if (requirementCount > maximumRequestRequirements) throw std::runtime_error("Request requirement count exceeds safety limit");
        std::vector<std::pair<std::string, int>> allocatedQuantities;
        allocatedQuantities.reserve(requirementCount);
        for (std::uint32_t j = 0; j < requirementCount; ++j) {
            auto specification = reader.cargo();
            const auto requested = reader.pod<std::int32_t>();
            const auto allocated = reader.pod<std::int32_t>();
            if (requested <= 0 || allocated < 0 || allocated > requested) throw std::runtime_error("Invalid request allocation in persistent data");
            const auto cargoId = specification.id();
            request.addRequirement(specification, requested);
            allocatedQuantities.emplace_back(cargoId, allocated);
        }
        const bool hasAllocations = std::any_of(allocatedQuantities.begin(), allocatedQuantities.end(), [](const auto& allocation) { return allocation.second > 0; });
        if (version == 1 && hasAllocations) {
            if (loaded.depots.size() != 1) throw std::runtime_error("Legacy state contains allocated requests but multiple depots; allocation source cannot be determined safely");
            allocatedDepotId = loaded.depots.front().id();
        }
        if (hasAllocations && allocatedDepotId.empty()) throw std::runtime_error("Allocated request is missing its source depot");
        if (!hasAllocations && !allocatedDepotId.empty()) throw std::runtime_error("Unallocated request contains an unexpected source depot");
        for (const auto& allocation : allocatedQuantities) {
            if (allocation.second > 0) Logistics::RequestAccess::recordAllocation(request, allocation.first, allocation.second, allocatedDepotId);
        }
        const auto storedStatus = static_cast<RequestStatus>(statusValue);
        if (storedStatus == RequestStatus::Dispatched) Logistics::RequestAccess::markDispatched(request);
        else if (storedStatus != request.status()) throw std::runtime_error("Request status does not match stored allocation quantities");
        loaded.requests.push_back(std::move(request));
    }

    const auto dispatchCount = reader.pod<std::uint32_t>();
    ensureCount(dispatchCount);
    loaded.dispatches.reserve(dispatchCount);
    for (std::uint32_t i = 0; i < dispatchCount; ++i) {
        Logistics::DispatchRecord record;
        record.dispatchId = reader.string();
        record.requestId = reader.string();
        record.vehicleId = reader.string();
        record.manifestId = reader.string();
        record.destinationName = reader.string();
        record.timestamp = reader.string();
        record.payloadKg = reader.pod<double>();
        record.routeDistanceKm = reader.pod<double>();
        const auto priority = reader.pod<std::uint8_t>();
        if (priority < 1 || priority > 4 || record.payloadKg < 0.0 || record.routeDistanceKm < 0.0) throw std::runtime_error("Invalid dispatch history entry");
        record.priority = static_cast<MedicalPriority>(priority);
        loaded.dispatches.push_back(std::move(record));
    }
    for (auto& request : loaded.requests) {
        if (request.status() == RequestStatus::Dispatched || request.allocatedDepotId().empty()) continue;
        const auto source = std::find_if(loaded.depots.begin(), loaded.depots.end(), [&](const auto& depot) { return depot.id() == request.allocatedDepotId(); });
        if (source == loaded.depots.end()) throw std::runtime_error("Allocated request source depot is missing during reservation restoration");
        for (const auto& requirement : request.requirements()) {
            if (requirement.allocatedQuantity <= 0) continue;
            auto allocatedCargo = requirement.specification;
            allocatedCargo.setQuantity(requirement.allocatedQuantity);
            if (version <= 2) *source += allocatedCargo;
            Logistics::RequestAccess::reserveInventory(*source, request.id(), allocatedCargo);
        }
    }

    validateState(loaded);
    return loaded;
}

ApplicationState readStateFile(const std::filesystem::path& file) {
    std::fstream input(file, std::ios::binary | std::ios::in);
    if (!input) throw std::runtime_error("Unable to open persistent state file: " + file.string());

    std::array<char, 8> fileMagic{};
    input.read(fileMagic.data(), static_cast<std::streamsize>(fileMagic.size()));
    std::uint32_t version{};
    std::uint64_t payloadSize{};
    std::uint64_t expectedChecksum{};
    input.read(reinterpret_cast<char*>(&version), sizeof(version));
    input.read(reinterpret_cast<char*>(&payloadSize), sizeof(payloadSize));
    input.read(reinterpret_cast<char*>(&expectedChecksum), sizeof(expectedChecksum));
    if (!input || fileMagic != magic || version < minimumSupportedFormatVersion || version > formatVersion || payloadSize > maximumPayloadSize) {
        throw std::runtime_error("Persistent state header is invalid or unsupported: " + file.string());
    }
    std::string bytes(static_cast<std::size_t>(payloadSize), '\0');
    if (payloadSize > 0) input.read(bytes.data(), static_cast<std::streamsize>(payloadSize));
    if (!input || input.peek() != std::char_traits<char>::eof()) throw std::runtime_error("Persistent state size does not match file contents: " + file.string());
    if (checksum(bytes) != expectedChecksum) throw std::runtime_error("Persistent state checksum mismatch: " + file.string());
    std::istringstream payload(bytes, std::ios::binary | std::ios::in);
    auto loaded = readState(payload, version);
    if (payload.peek() != std::char_traits<char>::eof()) throw std::runtime_error("Persistent state contains unexpected trailing data: " + file.string());
    return loaded;
}

}

void PersistenceManager::save(const ApplicationState& state, const std::filesystem::path& directory) {
    validateState(state);
    std::filesystem::create_directories(directory);
    std::ostringstream payload(std::ios::binary | std::ios::out);
    writeState(state, payload);
    const std::string bytes = payload.str();
    const auto sum = checksum(bytes);
    const auto target = directory / "erl_hub.state";
    const auto temporary = directory / "erl_hub.state.tmp";
    const auto backup = directory / "erl_hub.state.bak";
    const auto backupTemporary = directory / "erl_hub.state.bak.tmp";

    try {
        {
            std::fstream output(temporary, std::ios::binary | std::ios::out | std::ios::trunc);
            if (!output) throw std::runtime_error("Unable to create temporary state file");
            output.write(magic.data(), static_cast<std::streamsize>(magic.size()));
            output.write(reinterpret_cast<const char*>(&formatVersion), sizeof(formatVersion));
            const auto payloadSize = static_cast<std::uint64_t>(bytes.size());
            output.write(reinterpret_cast<const char*>(&payloadSize), sizeof(payloadSize));
            output.write(reinterpret_cast<const char*>(&sum), sizeof(sum));
            output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            output.flush();
            if (!output) throw std::runtime_error("Failed to write temporary state file");
        }

        bool currentSnapshotIsValid = false;
        if (std::filesystem::exists(target)) {
            try {
                static_cast<void>(readStateFile(target));
                currentSnapshotIsValid = true;
            } catch (const std::exception&) {
                currentSnapshotIsValid = false;
            }
        }

        if (currentSnapshotIsValid) {
            std::error_code cleanupError;
            std::filesystem::remove(backupTemporary, cleanupError);
            std::error_code copyError;
            std::filesystem::copy_file(target, backupTemporary, std::filesystem::copy_options::none, copyError);
            if (copyError) throw std::system_error(copyError, "Unable to prepare previous state backup");
            static_cast<void>(readStateFile(backupTemporary));
            atomicReplace(backupTemporary, backup);
        }

        atomicReplace(temporary, target);
    } catch (...) {
        std::error_code cleanupError;
        std::filesystem::remove(temporary, cleanupError);
        cleanupError.clear();
        std::filesystem::remove(backupTemporary, cleanupError);
        throw;
    }
}

bool PersistenceManager::load(ApplicationState& state, const std::filesystem::path& directory) {
    const auto file = directory / "erl_hub.state";
    const auto backup = directory / "erl_hub.state.bak";
    const bool hasPrimary = std::filesystem::exists(file);
    const bool hasBackup = std::filesystem::exists(backup);
    if (!hasPrimary && !hasBackup) return false;

    std::string primaryError;
    if (hasPrimary) {
        try {
            auto loaded = readStateFile(file);
            state = std::move(loaded);
            return true;
        } catch (const std::exception& error) {
            primaryError = error.what();
        }
    }

    if (hasBackup) {
        try {
            auto loaded = readStateFile(backup);
            state = std::move(loaded);
            return true;
        } catch (const std::exception& error) {
            throw std::runtime_error("Primary state could not be loaded (" + (primaryError.empty() ? std::string("missing") : primaryError) + "); backup recovery failed (" + error.what() + ")");
        }
    }

    throw std::runtime_error(primaryError.empty() ? "Persistent state could not be loaded" : primaryError);
}

}
