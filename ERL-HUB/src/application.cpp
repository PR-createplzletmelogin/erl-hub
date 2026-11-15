#include "erl_hub/application.hpp"
#include "dispatch_accounting_access.hpp"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace ReliefOps {
namespace {

struct RequestPriorityOrder {
    bool operator()(const Inventory::ReliefRequest& left, const Inventory::ReliefRequest& right) const noexcept {
        if (left.priority() != right.priority()) return static_cast<unsigned>(left.priority()) > static_cast<unsigned>(right.priority());
        return left.id() > right.id();
    }
};

template <typename T>
T readNumber(const std::string& prompt, T minimum, T maximum) {
    while (true) {
        std::cout << prompt;
        T value{};
        if (std::cin >> value && value >= minimum && value <= maximum) {
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            return value;
        }
        if (std::cin.eof()) throw std::runtime_error("Input stream closed");
        std::cin.clear();
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        std::cout << "Invalid value. Please try again.\n";
    }
}

std::string readText(const std::string& prompt) {
    while (true) {
        std::cout << prompt;
        std::string value;
        value.reserve(512);
        bool tooLong = false;
        char character{};
        while (std::cin.get(character) && character != '\n') {
            if (value.size() < 512) value.push_back(character);
            else tooLong = true;
        }
        if (std::cin.eof()) throw std::runtime_error("Input stream closed");
        if (!std::cin) throw std::runtime_error("Input stream failed");
        if (tooLong) {
            std::cout << "Input exceeds the 512-character limit.\n";
            continue;
        }
        if (!value.empty()) return value;
        std::cout << "A value is required.\n";
    }
}

MedicalPriority readPriority() {
    const auto value = readNumber<int>("Priority (1 Critical, 2 High, 3 Standard, 4 Low): ", 1, 4);
    return static_cast<MedicalPriority>(value);
}

CargoCategory readCategory() {
    const auto value = readNumber<int>("Category (1 Rations, 2 Medical, 3 Water, 4 Blankets, 5 Other): ", 1, 5);
    return static_cast<CargoCategory>(value);
}

std::string generateId(const std::string& prefix, std::size_t sequence) {
    std::ostringstream value;
    value << prefix << '-' << std::setw(4) << std::setfill('0') << sequence;
    return value.str();
}

}

ReliefApplication::ReliefApplication(std::filesystem::path dataDirectory) : dataDirectory_(std::move(dataDirectory)) {
    std::filesystem::create_directories(dataDirectory_);
    if (!PersistenceManager::load(state_, dataDirectory_)) {
        seedDemonstrationData();
    } else {
        for (const auto& record : state_.dispatches) Logistics::DispatchAccountingAccess::recordDispatch(record.payloadKg);
    }
}

const ApplicationState& ReliefApplication::state() const noexcept { return state_; }

void ReliefApplication::seedDemonstrationData() {
    if (!state_.depots.empty() || !state_.vehicles.empty() || !state_.requests.empty()) return;

    Inventory::DistributionDepot depot("DEP-DELHI", "Central Relief Depot", {28.6139, 77.2090});
    depot += Inventory::ReliefCargo("RAT-001", "Emergency ration kit", CargoCategory::Rations, 500, 1.2, 0.003);
    depot += Inventory::ReliefCargo("MED-001", "Trauma medical kit", CargoCategory::Medical, 120, 0.4, 0.001);
    depot += Inventory::ReliefCargo("WAT-001", "Drinking water container", CargoCategory::Water, 250, 5.0, 0.005);
    depot += Inventory::ReliefCargo("BLK-001", "Thermal blanket", CargoCategory::Blankets, 300, 1.5, 0.008);
    state_.depots.push_back(std::move(depot));

    state_.vehicles.push_back(Logistics::createVehicle(VehicleKind::Ground, "GRD-101", 6000.0, 600.0, {28.6139, 77.2090}, Logistics::EmergencyLifeSupportUnit(true, 4)));
    state_.vehicles.push_back(Logistics::createVehicle(VehicleKind::Aerial, "AIR-201", 900.0, 450.0, {28.6139, 77.2090}, Logistics::EmergencyLifeSupportUnit(true, 2)));
    state_.vehicles.push_back(Logistics::createVehicle(VehicleKind::Amphibious, "AMP-301", 2500.0, 300.0, {28.6139, 77.2090}, Logistics::EmergencyLifeSupportUnit(false, 0)));
    state_.vehicles.push_back(Logistics::createVehicle(VehicleKind::AmphibiousMedical, "MED-401", 1800.0, 250.0, {28.6139, 77.2090}, Logistics::EmergencyLifeSupportUnit(true, 6)));

    Inventory::ReliefRequest critical("REQ-0001", "Sector 14 Field Hospital", {28.6200, 77.1000}, MedicalPriority::Critical);
    critical.addRequirement(Inventory::ReliefCargo("MED-001", "Trauma medical kit", CargoCategory::Medical, 0, 0.4, 0.001), 20);
    critical.addRequirement(Inventory::ReliefCargo("WAT-001", "Drinking water container", CargoCategory::Water, 0, 5.0, 0.005), 30);
    state_.requests.push_back(std::move(critical));

    Inventory::ReliefRequest relief("REQ-0002", "North Relief Camp", {28.7500, 77.1500}, MedicalPriority::High);
    relief.addRequirement(Inventory::ReliefCargo("RAT-001", "Emergency ration kit", CargoCategory::Rations, 0, 1.2, 0.003), 100);
    relief.addRequirement(Inventory::ReliefCargo("WAT-001", "Drinking water container", CargoCategory::Water, 0, 5.0, 0.005), 50);
    relief.addRequirement(Inventory::ReliefCargo("BLK-001", "Thermal blanket", CargoCategory::Blankets, 0, 1.5, 0.008), 20);
    state_.requests.push_back(std::move(relief));

    save();
}

void ReliefApplication::save() { PersistenceManager::save(state_, dataDirectory_); }

void ReliefApplication::printDashboard(std::ostream& output) const {
    const auto pending = std::count_if(state_.requests.begin(), state_.requests.end(), [](const auto& request) { return request.status() != RequestStatus::Dispatched; });
    const auto available = std::count_if(state_.vehicles.begin(), state_.vehicles.end(), [](const auto& vehicle) { return vehicle->available(); });
    output << "\n============================================================\n";
    output << " ERL-HUB | EMERGENCY RELIEF LOGISTICS COMMAND CENTER\n";
    output << "============================================================\n";
    output << std::left << std::setw(28) << "Registered depots" << state_.depots.size() << '\n';
    output << std::left << std::setw(28) << "Operational vehicles" << available << " / " << state_.vehicles.size() << '\n';
    output << std::left << std::setw(28) << "Pending requests" << pending << '\n';
    output << std::left << std::setw(28) << "Completed dispatches" << state_.dispatches.size() << '\n';
    output << std::left << std::setw(28) << "Total dispatched tonnage" << std::fixed << std::setprecision(3) << Logistics::TransportVehicle::totalDispatchedMetricTonnes() << " t\n";
    output << "============================================================\n";
}

void ReliefApplication::listDepots(std::ostream& output) const {
    output << "\nDEPOT INVENTORY\n";
    for (const auto& depot : state_.depots) {
        output << "\n" << depot.id() << " | " << depot.name() << " | " << depot.distinctCargoCount() << " cargo types\n";
        output << std::left << std::setw(14) << "CARGO ID" << std::setw(28) << "DESCRIPTION" << std::setw(14) << "CATEGORY"
               << std::right << std::setw(10) << "ON HAND" << std::setw(10) << "RESERVED" << std::setw(11) << "AVAILABLE" << std::setw(14) << "WEIGHT (KG)" << '\n';
        output << Inventory::manifestTableSeparator;
        for (const auto& item : depot.inventory()) {
            output << std::left << std::setw(14) << item.id() << std::setw(28) << item.name() << std::setw(14) << toString(item.category())
                   << std::right << std::setw(10) << depot.onHandQuantityOf(item.id()) << std::setw(10) << depot.reservedQuantityOf(item.id())
                   << std::setw(11) << depot.quantityOf(item.id()) << std::setw(14) << std::fixed << std::setprecision(2) << item.totalWeightKg() << '\n';
        }
    }
}

void ReliefApplication::listVehicles(std::ostream& output) const {
    output << "\nVEHICLE FLEET\n";
    output << std::left << std::setw(14) << "VEHICLE ID" << std::setw(26) << "TYPE" << std::right << std::setw(14) << "CAPACITY KG" << std::setw(14) << "LOADED KG" << std::setw(14) << "RANGE KM" << "  STATUS\n";
    output << std::string(96, '-') << '\n';
    std::vector<Logistics::ReliefVehicle*> vehiclePointers;
    vehiclePointers.reserve(state_.vehicles.size());
    for (const auto& vehicle : state_.vehicles) vehiclePointers.push_back(vehicle.get());
    std::for_each(vehiclePointers.begin(), vehiclePointers.end(), [&](const Logistics::ReliefVehicle* vehicle) {
        output << std::left << std::setw(14) << vehicle->id() << std::setw(26) << toString(vehicle->vehicleKind())
               << std::right << std::setw(14) << std::fixed << std::setprecision(1) << vehicle->maxPayloadKg()
               << std::setw(14) << vehicle->currentPayloadKg() << std::setw(14) << vehicle->maximumRangeKm()
               << "  " << (vehicle->available() ? "AVAILABLE" : "ON MISSION") << '\n';
    });
}

void ReliefApplication::listRequests(std::ostream& output) const {
    Logistics::PriorityDispatchQueue<Inventory::ReliefRequest, RequestPriorityOrder> queue;
    std::for_each(state_.requests.begin(), state_.requests.end(), [&](const auto& request) { queue.push(request); });
    output << "\nRELIEF REQUESTS | PRIORITY ORDER\n";
    while (!queue.empty()) {
        const auto request = queue.pop();
        output << request.id() << " | " << request.destinationName() << " | " << toString(request.priority()) << " | " << toString(request.status()) << '\n';
        for (const auto& requirement : request.requirements()) {
            output << "  " << requirement.specification.name() << ": " << requirement.allocatedQuantity << '/' << requirement.requestedQuantity << " allocated\n";
        }
    }
}

void ReliefApplication::viewManifest() const {
    const auto id = readText("Vehicle ID: ");
    const auto found = std::find_if(state_.vehicles.begin(), state_.vehicles.end(), [&](const auto& vehicle) { return vehicle->id() == id; });
    if (found == state_.vehicles.end()) throw std::out_of_range("Vehicle not found: " + id);
    const auto& manifest = (*found)->manifest();
    std::cout << "\nMANIFEST " << manifest.id() << " | Vehicle " << (*found)->id() << '\n';
    std::cout << Inventory::manifestTableHeader << Inventory::manifestTableSeparator;
    for (const auto& item : manifest.items()) {
        std::cout << std::left << std::setw(14) << item.id() << std::setw(28) << item.name() << std::setw(14) << toString(item.category())
                  << std::right << std::setw(10) << item.quantity() << std::setw(14) << std::fixed << std::setprecision(2) << item.totalWeightKg() << '\n';
    }
    std::cout << Inventory::manifestTableSeparator;
    std::cout << "Total payload: " << std::fixed << std::setprecision(2) << manifest.totalWeightKg() << " kg\n";
}

void ReliefApplication::listDispatches(std::ostream& output) const {
    output << "\nDISPATCH HISTORY\n";
    if (state_.dispatches.empty()) {
        output << "No dispatch records available.\n";
        return;
    }
    for (const auto& record : state_.dispatches) {
        output << record.dispatchId << " | Request " << record.requestId << " | Vehicle " << record.vehicleId
               << " | " << record.destinationName << " | " << std::fixed << std::setprecision(2) << record.payloadKg << " kg"
               << " | " << record.routeDistanceKm << " km | " << record.timestamp << '\n';
    }
}

Inventory::DistributionDepot& ReliefApplication::depotById(const std::string& id) {
    const auto found = std::find_if(state_.depots.begin(), state_.depots.end(), [&](const auto& depot) { return depot.id() == id; });
    if (found == state_.depots.end()) throw std::out_of_range("Depot not found: " + id);
    return *found;
}

Inventory::ReliefRequest& ReliefApplication::requestById(const std::string& id) {
    const auto found = std::find_if(state_.requests.begin(), state_.requests.end(), [&](const auto& request) { return request.id() == id; });
    if (found == state_.requests.end()) throw std::out_of_range("Request not found: " + id);
    return *found;
}

Logistics::ReliefVehicle& ReliefApplication::vehicleById(const std::string& id) {
    const auto found = std::find_if(state_.vehicles.begin(), state_.vehicles.end(), [&](const auto& vehicle) { return vehicle->id() == id; });
    if (found == state_.vehicles.end()) throw std::out_of_range("Vehicle not found: " + id);
    return **found;
}

void ReliefApplication::registerDepot() {
    const auto id = readText("Depot ID: ");
    if (std::any_of(state_.depots.begin(), state_.depots.end(), [&](const auto& depot) { return depot.id() == id; })) throw std::invalid_argument("Depot ID already exists");
    const auto name = readText("Depot name: ");
    const auto latitude = readNumber<double>("Depot latitude: ", -90.0, 90.0);
    const auto longitude = readNumber<double>("Depot longitude: ", -180.0, 180.0);
    state_.depots.emplace_back(id, name, GeoPoint{latitude, longitude});
    save();
    std::cout << "Distribution depot registered successfully.\n";
}

void ReliefApplication::restockDepot() {
    if (state_.depots.empty()) throw std::runtime_error("No depots are registered");
    listDepots(std::cout);
    const auto depotId = readText("Depot ID: ");
    const auto cargoId = readText("Cargo ID: ");
    const auto name = readText("Cargo description: ");
    const auto category = readCategory();
    const auto quantity = readNumber<int>("Quantity to add: ", 1, 1000000);
    const auto unitWeight = readNumber<double>("Unit weight in kg: ", 0.0, 1000000.0);
    const auto unitVolume = readNumber<double>("Unit volume in cubic metres: ", 0.0, 1000000.0);
    depotById(depotId) += Inventory::ReliefCargo(cargoId, name, category, quantity, unitWeight, unitVolume);
    save();
    std::cout << "Inventory updated successfully.\n";
}

void ReliefApplication::createRequest() {
    if (state_.depots.empty()) throw std::runtime_error("Register a distribution depot before creating a relief request");
    listDepots(std::cout);
    auto& catalogDepot = depotById(readText("Select depot for the cargo catalogue: "));
    const auto id = readText("Request ID: ");
    const auto destinationName = readText("Destination or receiving agency: ");
    const auto latitude = readNumber<double>("Destination latitude: ", -90.0, 90.0);
    const auto longitude = readNumber<double>("Destination longitude: ", -180.0, 180.0);
    const auto priority = readPriority();
    Inventory::ReliefRequest request(id, destinationName, {latitude, longitude}, priority);
    const auto count = readNumber<int>("Number of cargo requirements: ", 1, 30);
    for (int i = 0; i < count; ++i) {
        std::cout << "Requirement " << i + 1 << '\n';
        const auto cargoId = readText("Cargo ID from selected depot: ");
        const auto quantity = readNumber<int>("Required quantity: ", 1, 1000000);
        request.addRequirement(catalogDepot.findCargo(cargoId), quantity);
    }
    if (std::any_of(state_.requests.begin(), state_.requests.end(), [&](const auto& existing) { return existing.id() == id; })) throw std::invalid_argument("Request ID already exists");
    state_.requests.push_back(std::move(request));
    save();
    std::cout << "Emergency request registered.\n";
}

void ReliefApplication::registerVehicle() {
    const auto id = readText("Vehicle ID: ");
    if (std::any_of(state_.vehicles.begin(), state_.vehicles.end(), [&](const auto& vehicle) { return vehicle->id() == id; })) throw std::invalid_argument("Vehicle ID already exists");
    std::cout << "Vehicle type: 1 Ground, 2 Aerial, 3 Amphibious, 4 Amphibious medical\n";
    const auto kind = static_cast<VehicleKind>(readNumber<int>("Selection: ", 1, 4));
    const auto capacity = readNumber<double>("Maximum payload in kg: ", 1.0, 1000000.0);
    const auto range = readNumber<double>("Maximum range in km: ", 1.0, 1000000.0);
    const auto latitude = readNumber<double>("Current latitude: ", -90.0, 90.0);
    const auto longitude = readNumber<double>("Current longitude: ", -180.0, 180.0);
    const bool medical = readNumber<int>("Install emergency life support? (1 yes, 0 no): ", 0, 1) == 1;
    const auto patientCapacity = medical ? readNumber<unsigned>("Patient capacity: ", 1U, 1000U) : 0U;
    state_.vehicles.push_back(Logistics::createVehicle(kind, id, capacity, range, {latitude, longitude}, Logistics::EmergencyLifeSupportUnit(medical, patientCapacity)));
    save();
    std::cout << "Vehicle registered successfully.\n";
}

void ReliefApplication::dispatchRequestMenu() {
    listDepots(std::cout);
    listRequests(std::cout);
    listVehicles(std::cout);
    auto& depot = depotById(readText("Depot ID: "));
    auto& request = requestById(readText("Request ID: "));

    Logistics::RouteConditions conditions;
    conditions.distanceKm = readNumber<double>("Estimated route distance in km: ", 0.1, 100000.0);
    conditions.roadOpen = readNumber<int>("Road corridor open? (1 yes, 0 no): ", 0, 1) == 1;
    conditions.roadDamageFactor = readNumber<double>("Road damage factor (0.0 to 1.0): ", 0.0, 1.0);
    conditions.waterwayOpen = readNumber<int>("Waterway open? (1 yes, 0 no): ", 0, 1) == 1;
    conditions.waterDepthM = readNumber<double>("Water depth in metres: ", 0.0, 20.0);
    conditions.airspaceRestricted = readNumber<int>("Airspace restricted? (1 yes, 0 no): ", 0, 1) == 1;

    std::vector<Logistics::ReliefVehicle*> vehiclePointers;
    vehiclePointers.reserve(state_.vehicles.size());
    for (const auto& vehicle : state_.vehicles) {
        if (vehicle->available()) vehiclePointers.push_back(vehicle.get());
    }
    if (vehiclePointers.empty()) throw std::runtime_error("No vehicles are currently available for dispatch");
    std::cout << "\nROUTE EVALUATION | AVAILABLE VEHICLE FLEET\n";
    for (const auto* candidate : vehiclePointers) {
        const auto route = candidate->evaluateRoute(conditions);
        std::cout << std::left << std::setw(14) << candidate->id() << std::setw(27) << toString(candidate->vehicleKind())
                  << (route.viable ? "FEASIBLE" : "UNAVAILABLE") << " | " << route.rationale << '\n';
    }

    const auto vehicleId = readText("Select vehicle ID: ");
    auto& vehicle = vehicleById(vehicleId);
    const auto threshold = readPriority();
    const auto dispatchId = generateId("DSP", state_.dispatches.size() + 1);
    const auto outcome = Logistics::dispatchRequest(depot, request, &vehicle, conditions, threshold, dispatchId);
    std::cout << outcome.message << '\n';
    if (outcome.success) {
        Logistics::DispatchRecord record;
        record.dispatchId = dispatchId;
        record.requestId = request.id();
        record.vehicleId = vehicle.id();
        record.manifestId = vehicle.manifest().id();
        record.destinationName = request.destinationName();
        record.timestamp = Logistics::currentTimestamp();
        record.payloadKg = vehicle.currentPayloadKg();
        record.routeDistanceKm = outcome.route.distanceKm;
        record.priority = request.priority();
        state_.dispatches.push_back(std::move(record));
        std::cout << "Manifest " << vehicle.manifest().id() << " loaded with " << vehicle.manifest().size() << " cargo types.\n";
        std::cout << "Estimated fuel: " << std::fixed << std::setprecision(2) << outcome.route.estimatedFuelLitres << " L\n";
        save();
    }
}

void ReliefApplication::completeMission() {
    const auto id = readText("Vehicle ID returning from mission: ");
    auto& vehicle = vehicleById(id);
    vehicle.completeMission();
    save();
    std::cout << "Vehicle marked available and manifest cleared.\n";
}

void ReliefApplication::overrideRequestPriority() {
    listRequests(std::cout);
    auto& request = requestById(readText("Request ID for triage override: "));
    const auto previousPriority = request.priority();
    Inventory::IncidentCommander::authorizePriority(request, readPriority());
    save();
    std::cout << "Incident Commander updated request " << request.id() << " from " << toString(previousPriority)
              << " to " << toString(request.priority()) << ".\n";
}

bool ReliefApplication::runDemonstrationScenario(std::ostream& output) {
    if (state_.depots.empty() || state_.vehicles.empty() || state_.requests.empty()) return false;
    auto depotIt = std::find_if(state_.depots.begin(), state_.depots.end(), [](const auto& depot) { return depot.id() == "DEP-DELHI"; });
    auto requestIt = std::find_if(state_.requests.begin(), state_.requests.end(), [](const auto& request) { return request.id() == "REQ-0001"; });
    auto vehicleIt = std::find_if(state_.vehicles.begin(), state_.vehicles.end(), [](const auto& vehicle) { return vehicle->id() == "AIR-201"; });
    if (depotIt == state_.depots.end() || requestIt == state_.requests.end() || vehicleIt == state_.vehicles.end()) return false;
    if (requestIt->status() == RequestStatus::Dispatched || !(*vehicleIt)->available()) return false;

    Logistics::RouteConditions conditions;
    conditions.distanceKm = distanceKm(depotIt->location(), requestIt->destination());
    const auto dispatchId = generateId("DSP", state_.dispatches.size() + 1);
    const auto outcome = Logistics::dispatchRequest(*depotIt, *requestIt, vehicleIt->get(), conditions, MedicalPriority::Critical, dispatchId);
    output << outcome.message << '\n';
    if (!outcome.success) return false;
    Logistics::DispatchRecord record;
    record.dispatchId = dispatchId;
    record.requestId = requestIt->id();
    record.vehicleId = (*vehicleIt)->id();
    record.manifestId = (*vehicleIt)->manifest().id();
    record.destinationName = requestIt->destinationName();
    record.timestamp = Logistics::currentTimestamp();
    record.payloadKg = (*vehicleIt)->currentPayloadKg();
    record.routeDistanceKm = outcome.route.distanceKm;
    record.priority = requestIt->priority();
    state_.dispatches.push_back(std::move(record));
    save();
    output << "Vehicle: " << (*vehicleIt)->id() << '\n';
    output << "Destination: " << requestIt->destinationName() << '\n';
    output << "Payload: " << std::fixed << std::setprecision(2) << (*vehicleIt)->currentPayloadKg() << " kg\n";
    output << "Route distance: " << outcome.route.distanceKm << " km\n";
    output << "Estimated fuel: " << outcome.route.estimatedFuelLitres << " L\n";
    return true;
}

int ReliefApplication::run() {
    while (true) {
        printDashboard(std::cout);
        std::cout << "1. View depot inventory\n"
                  << "2. Restock depot\n"
                  << "3. View vehicle fleet\n"
                  << "4. Register vehicle\n"
                  << "5. Create emergency request\n"
                  << "6. View priority queue\n"
                  << "7. Allocate supplies by volume\n"
                  << "8. Allocate supplies by medical priority\n"
                  << "9. Dispatch fully allocated request\n"
                  << "10. View dispatch history\n"
                  << "11. Complete vehicle mission\n"
                  << "12. Save records\n"
                  << "13. View vehicle manifest\n"
                  << "14. Register distribution depot\n"
                  << "15. Incident commander priority override\n"
                  << "0. Save and exit\n";
        const auto choice = readNumber<int>("Select operation: ", 0, 15);
        try {
            if (choice == 0) {
                save();
                return 0;
            }
            if (choice == 1) listDepots(std::cout);
            else if (choice == 2) restockDepot();
            else if (choice == 3) listVehicles(std::cout);
            else if (choice == 4) registerVehicle();
            else if (choice == 5) createRequest();
            else if (choice == 6) listRequests(std::cout);
            else if (choice == 7 || choice == 8) {
                listDepots(std::cout);
                listRequests(std::cout);
                auto& depot = depotById(readText("Depot ID: "));
                auto& request = requestById(readText("Request ID: "));
                Logistics::AllocationResult result;
                if (choice == 7) {
                    const auto volume = readNumber<double>("Maximum allocation volume in cubic metres: ", 0.001, 1000000.0);
                    result = Logistics::allocateSupply(depot, request, volume);
                } else {
                    result = Logistics::allocateSupply(depot, request, readPriority());
                }
                std::cout << result.message << " | Allocated volume: " << std::fixed << std::setprecision(3) << result.allocatedVolumeM3 << " m3\n";
                save();
            } else if (choice == 9) dispatchRequestMenu();
            else if (choice == 10) listDispatches(std::cout);
            else if (choice == 11) completeMission();
            else if (choice == 12) {
                save();
                std::cout << "Persistent records saved.\n";
            }
            else if (choice == 13) viewManifest();
            else if (choice == 14) registerDepot();
            else if (choice == 15) overrideRequestPriority();
        } catch (const std::exception& error) {
            std::cout << "Operation rejected: " << error.what() << '\n';
        }
    }
}

}
