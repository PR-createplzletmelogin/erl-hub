#pragma once

#include "erl_hub/vehicles.hpp"

namespace ReliefOps::Logistics {

struct DispatchAccountingAccess {
    static void validateDispatch(double payloadKg) {
        TransportVehicle::validateDispatch(payloadKg);
    }

    static void recordDispatch(double payloadKg) {
        TransportVehicle::recordDispatch(payloadKg);
    }

    static void startMission(TransportVehicle& vehicle) {
        vehicle.startMission();
    }
};

}
