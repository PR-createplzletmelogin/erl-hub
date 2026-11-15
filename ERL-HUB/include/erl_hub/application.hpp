#pragma once

#include "erl_hub/persistence.hpp"

#include <filesystem>
#include <iosfwd>
#include <string>

namespace ReliefOps {

class ReliefApplication {
public:
    explicit ReliefApplication(std::filesystem::path dataDirectory);
    int run();
    void seedDemonstrationData();
    bool runDemonstrationScenario(std::ostream& output);
    const ApplicationState& state() const noexcept;

private:
    void printDashboard(std::ostream& output) const;
    void listDepots(std::ostream& output) const;
    void listVehicles(std::ostream& output) const;
    void listRequests(std::ostream& output) const;
    void listDispatches(std::ostream& output) const;
    void viewManifest() const;
    void createRequest();
    void restockDepot();
    void registerDepot();
    void registerVehicle();
    void dispatchRequestMenu();
    void completeMission();
    void overrideRequestPriority();
    Inventory::DistributionDepot& depotById(const std::string& id);
    Inventory::ReliefRequest& requestById(const std::string& id);
    Logistics::ReliefVehicle& vehicleById(const std::string& id);
    void save();

    std::filesystem::path dataDirectory_;
    ApplicationState state_;
};

}
