#include "erl_hub/logistics.hpp"
#include "erl_hub/exceptions.hpp"
#include "request_access.hpp"
#include "dispatch_accounting_access.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace ReliefOps::Logistics {
namespace {

AllocationResult executeAllocation(Inventory::DistributionDepot& depot, Inventory::ReliefRequest& request, double volumeLimitM3, bool enforceVolume) {
    if (!std::isfinite(volumeLimitM3) || (enforceVolume && volumeLimitM3 <= 0.0)) {
        throw std::invalid_argument("Allocation volume limit must be positive and finite");
    }
    if (request.status() == RequestStatus::Dispatched) return {false, false, 0.0, {}, "Request has already been dispatched"};
    if (!request.allocatedDepotId().empty() && request.allocatedDepotId() != depot.id()) return {false, false, 0.0, {}, "Existing allocations belong to a different depot"};

    struct PlannedItem {
        std::string id;
        int quantity;
        Inventory::ReliefCargo cargo;
    };

    std::vector<PlannedItem> plan;
    double remainingVolume = volumeLimitM3;
    double allocatedVolume{};

    for (const auto& requirement : request.requirements()) {
        const int remaining = requirement.remainingQuantity();
        if (remaining <= 0) continue;
        const auto available = depot.quantityOf(requirement.specification.id());
        if (available <= 0) continue;
        const auto& stockDefinition = depot.findCargo(requirement.specification.id());
        if (!(stockDefinition == requirement.specification)) {
            throw std::invalid_argument("Request cargo definition does not match depot stock: " + requirement.specification.id());
        }
        int quantity = std::min(remaining, available);
        if (enforceVolume && requirement.specification.unitVolumeM3() > 0.0) {
            const double volumeBoundValue = std::floor((remainingVolume + 1e-9) / requirement.specification.unitVolumeM3());
            const int volumeBound = volumeBoundValue >= static_cast<double>(remaining) ? remaining : std::max(0, static_cast<int>(volumeBoundValue));
            quantity = std::min(quantity, volumeBound);
        }
        if (quantity <= 0) continue;
        auto item = requirement.specification;
        item.setQuantity(quantity);
        plan.push_back({item.id(), quantity, item});
        const double itemVolume = item.totalVolumeM3();
        if (!std::isfinite(itemVolume) || itemVolume > std::numeric_limits<double>::max() - allocatedVolume) throw std::overflow_error("Allocated cargo volume exceeds numeric range");
        allocatedVolume += itemVolume;
        if (enforceVolume) remainingVolume = std::max(0.0, remainingVolume - itemVolume);
    }

    if (plan.empty()) return {false, false, 0.0, {}, "No requested supplies are currently available within the allocation constraints"};

    std::vector<Inventory::ReliefCargo> allocated;
    allocated.reserve(plan.size());
    for (const auto& item : plan) allocated.push_back(item.cargo);

    std::size_t reservedCount{};
    std::size_t recordedCount{};
    try {
        for (const auto& item : plan) {
            RequestAccess::reserveInventory(depot, request.id(), item.cargo);
            ++reservedCount;
        }
        for (const auto& item : plan) {
            RequestAccess::recordAllocation(request, item.id, item.quantity, depot.id());
            ++recordedCount;
        }
    } catch (...) {
        for (std::size_t i = 0; i < recordedCount; ++i) RequestAccess::releaseAllocation(request, plan[i].id, plan[i].quantity);
        for (std::size_t i = 0; i < reservedCount; ++i) RequestAccess::releaseInventoryReservation(depot, request.id(), plan[i].id, plan[i].quantity);
        throw;
    }

    const bool complete = request.fullyAllocated();
    return {true, complete, allocatedVolume, std::move(allocated), complete ? "All requested supplies allocated" : "Partial allocation completed; additional stock is required"};
}

}

AllocationResult allocateSupply(Inventory::DistributionDepot& depot, Inventory::ReliefRequest& request, double volumeLimitM3) {
    return executeAllocation(depot, request, volumeLimitM3, true);
}

AllocationResult allocateSupply(Inventory::DistributionDepot& depot, Inventory::ReliefRequest& request, MedicalPriority maximumPriority) {
    const auto threshold = static_cast<unsigned>(maximumPriority);
    if (threshold < static_cast<unsigned>(MedicalPriority::Critical) || threshold > static_cast<unsigned>(MedicalPriority::Low)) throw std::invalid_argument("Invalid allocation priority threshold");
    if (static_cast<unsigned>(request.priority()) > threshold) {
        return {false, false, 0.0, {}, "Request priority is below the authorized allocation threshold"};
    }
    return executeAllocation(depot, request, std::numeric_limits<double>::max(), false);
}

std::string currentTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm timeInfo{};
#if defined(_WIN32)
    localtime_s(&timeInfo, &time);
#else
    localtime_r(&time, &timeInfo);
#endif
    std::ostringstream output;
    output << std::put_time(&timeInfo, "%Y-%m-%d %H:%M:%S");
    return output.str();
}

DispatchOutcome dispatchRequest(Inventory::DistributionDepot& depot, Inventory::ReliefRequest& request, ReliefVehicle* vehicle, const RouteConditions& conditions, MedicalPriority maximumPriority, const std::string& dispatchId) {
    if (!isValidLabel(dispatchId, 128)) throw std::invalid_argument("Dispatch ID is invalid");
    if (!vehicle) throw std::invalid_argument("A vehicle pointer is required for dispatch");
    if (static_cast<unsigned>(maximumPriority) < static_cast<unsigned>(MedicalPriority::Critical) || static_cast<unsigned>(maximumPriority) > static_cast<unsigned>(MedicalPriority::Low)) throw std::invalid_argument("Invalid dispatch priority threshold");
    if (static_cast<unsigned>(request.priority()) > static_cast<unsigned>(maximumPriority)) return {false, {}, {}, "Request priority is below the authorized dispatch threshold"};
    if (!request.allocatedDepotId().empty() && request.allocatedDepotId() != depot.id()) return {false, {}, {}, "Request allocations belong to a different depot"};
    if (!vehicle->available()) throw InvalidDispatchStateException("Selected vehicle is unavailable");
    if (!vehicle->manifest().empty()) throw InvalidDispatchStateException("Selected vehicle must have an empty manifest before dispatch");
    if (request.status() == RequestStatus::Dispatched) throw InvalidDispatchStateException("Request has already been dispatched");

    AllocationResult newAllocation;
    bool hasNewAllocation = false;
    if (!request.fullyAllocated()) {
        newAllocation = allocateSupply(depot, request, maximumPriority);
        if (!newAllocation.accepted) return {false, {}, {}, newAllocation.message};
        hasNewAllocation = true;
        if (!request.fullyAllocated()) {
            for (const auto& cargo : newAllocation.allocatedCargo) {
                RequestAccess::releaseInventoryReservation(depot, request.id(), cargo.id(), cargo.quantity());
                RequestAccess::releaseAllocation(request, cargo.id(), cargo.quantity());
            }
            return {false, {}, {}, "Dispatch requires the complete request to be allocated first"};
        }
    }

    std::vector<Inventory::ReliefCargo> manifestCargo;
    RouteEvaluation route;

    auto rollbackNewAllocation = [&]() {
        if (!hasNewAllocation) return;
        for (const auto& cargo : newAllocation.allocatedCargo) {
            RequestAccess::releaseInventoryReservation(depot, request.id(), cargo.id(), cargo.quantity());
            RequestAccess::releaseAllocation(request, cargo.id(), cargo.quantity());
        }
    };

    auto unloadLoadedCargo = [&]() {
        for (const auto& cargo : manifestCargo) {
            if (vehicle->manifest().contains(cargo.id())) {
                const auto* loaded = vehicle->manifest().find(cargo.id());
                const int amount = std::min(cargo.quantity(), loaded ? loaded->quantity() : 0);
                if (amount > 0) vehicle->unloadCargo(cargo.id(), amount);
            }
        }
    };

    try {
        manifestCargo.reserve(request.requirements().size());
        for (const auto& requirement : request.requirements()) {
            if (requirement.allocatedQuantity <= 0) continue;
            auto cargo = requirement.specification;
            cargo.setQuantity(requirement.allocatedQuantity);
            manifestCargo.push_back(std::move(cargo));
        }
        for (const auto& cargo : manifestCargo) vehicle->loadCargo(cargo);
        route = vehicle->evaluateRoute(conditions);
        if (!route.viable) {
            if (route.rationale.find("closed") != std::string::npos || route.rationale.find("navigable") != std::string::npos || route.rationale.find("restriction") != std::string::npos) {
                throw DamagedCorridorException(route.rationale);
            }
            throw FuelDepletionException(route.rationale);
        }
    } catch (const std::exception& error) {
        unloadLoadedCargo();
        rollbackNewAllocation();
        return {false, {}, {}, std::string("Dispatch rolled back: ") + error.what()};
    }

    const double payloadKg = vehicle->currentPayloadKg();
    try {
        DispatchAccountingAccess::validateDispatch(payloadKg);
        RequestAccess::consumeInventoryReservations(depot, request.id(), manifestCargo);
        DispatchAccountingAccess::recordDispatch(payloadKg);
        RequestAccess::markDispatched(request);
        DispatchAccountingAccess::startMission(*vehicle);
    } catch (const std::exception& error) {
        unloadLoadedCargo();
        rollbackNewAllocation();
        return {false, {}, {}, std::string("Dispatch rolled back: ") + error.what()};
    }
    return {true, route, std::move(manifestCargo), "Dispatch " + dispatchId + " completed successfully"};
}

}
