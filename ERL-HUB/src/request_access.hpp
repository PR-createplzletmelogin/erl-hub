#pragma once

#include "erl_hub/inventory.hpp"

namespace ReliefOps::Logistics {

struct RequestAccess {
    static void reserveInventory(Inventory::DistributionDepot& depot, const std::string& requestId, const Inventory::ReliefCargo& cargo) {
        depot.reserveInventory(requestId, cargo);
    }

    static void releaseInventoryReservation(Inventory::DistributionDepot& depot, const std::string& requestId, const std::string& cargoId, int quantity) {
        depot.releaseInventoryReservation(requestId, cargoId, quantity);
    }

    static void consumeInventoryReservations(Inventory::DistributionDepot& depot, const std::string& requestId, const std::vector<Inventory::ReliefCargo>& cargo) {
        depot.consumeInventoryReservations(requestId, cargo);
    }

    static Inventory::ReliefRequest& recordAllocation(Inventory::ReliefRequest& request, const std::string& cargoId, int quantity, const std::string& depotId) {
        return request.recordAllocation(cargoId, quantity, depotId);
    }

    static Inventory::ReliefRequest& releaseAllocation(Inventory::ReliefRequest& request, const std::string& cargoId, int quantity) {
        return request.releaseAllocation(cargoId, quantity);
    }

    static Inventory::ReliefRequest& markDispatched(Inventory::ReliefRequest& request) {
        return request.markDispatched();
    }
};

}
