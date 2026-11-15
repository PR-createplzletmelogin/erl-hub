#pragma once

#include "erl_hub/logistics.hpp"

#include <filesystem>
#include <memory>
#include <vector>

namespace ReliefOps {

struct ApplicationState {
    std::vector<Inventory::DistributionDepot> depots;
    std::vector<std::unique_ptr<Logistics::ReliefVehicle>> vehicles;
    std::vector<Inventory::ReliefRequest> requests;
    std::vector<Logistics::DispatchRecord> dispatches;
};

class PersistenceManager {
public:
    static void save(const ApplicationState& state, const std::filesystem::path& directory);
    static bool load(ApplicationState& state, const std::filesystem::path& directory);
};

}
