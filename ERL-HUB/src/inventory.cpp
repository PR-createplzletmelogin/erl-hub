#include "erl_hub/inventory.hpp"
#include "erl_hub/exceptions.hpp"

#include <algorithm>
#include <iomanip>
#include <istream>
#include <limits>
#include <memory>
#include <ostream>
#include <set>
#include <stdexcept>
#include <utility>

namespace ReliefOps::Inventory {
namespace {
constexpr std::size_t maximumManifestItems = 1024;
constexpr std::size_t maximumRequestRequirements = 256;
constexpr std::size_t maximumDepotCargoTypes = 4096;
}

std::ostream& manifestTableHeader(std::ostream& output) {
    output << std::left << std::setw(14) << "CARGO ID" << std::setw(28) << "DESCRIPTION" << std::setw(14) << "CATEGORY" << std::right << std::setw(10) << "QUANTITY" << std::setw(14) << "WEIGHT (KG)" << '\n';
    return output;
}

std::ostream& manifestTableSeparator(std::ostream& output) {
    output << std::string(80, '-') << '\n';
    return output;
}

ReliefCargo::ReliefCargo(std::string id, std::string name, CargoCategory category, int quantity, double unitWeightKg, double unitVolumeM3)
    : id_(std::move(id)), name_(std::move(name)), category_(category), quantity_(quantity), unitWeightKg_(unitWeightKg), unitVolumeM3_(unitVolumeM3) {
    if (!isValidLabel(id_, 128) || !isValidLabel(name_, 512) || static_cast<unsigned>(category_) < static_cast<unsigned>(CargoCategory::Rations) || static_cast<unsigned>(category_) > static_cast<unsigned>(CargoCategory::Other) || quantity_ < 0 || unitWeightKg_ < 0.0 || unitVolumeM3_ < 0.0 || !std::isfinite(unitWeightKg_) || !std::isfinite(unitVolumeM3_) || !std::isfinite(static_cast<double>(quantity_) * unitWeightKg_) || !std::isfinite(static_cast<double>(quantity_) * unitVolumeM3_)) {
        throw std::invalid_argument("Invalid relief cargo definition");
    }
}

const std::string& ReliefCargo::id() const noexcept { return id_; }
const std::string& ReliefCargo::name() const noexcept { return name_; }
CargoCategory ReliefCargo::category() const noexcept { return category_; }
int ReliefCargo::quantity() const noexcept { return quantity_; }
double ReliefCargo::unitWeightKg() const noexcept { return unitWeightKg_; }
double ReliefCargo::unitVolumeM3() const noexcept { return unitVolumeM3_; }
double ReliefCargo::totalWeightKg() const noexcept { return quantity_ * unitWeightKg_; }
double ReliefCargo::totalVolumeM3() const noexcept { return quantity_ * unitVolumeM3_; }

void ReliefCargo::addUnits(int quantity) {
    if (quantity <= 0 || quantity_ > std::numeric_limits<int>::max() - quantity) {
        throw std::invalid_argument("Cargo restock quantity must be positive and within range");
    }
    const int updatedQuantity = quantity_ + quantity;
    if (!std::isfinite(static_cast<double>(updatedQuantity) * unitWeightKg_) || !std::isfinite(static_cast<double>(updatedQuantity) * unitVolumeM3_)) throw std::overflow_error("Cargo restock exceeds numeric range");
    quantity_ = updatedQuantity;
}

void ReliefCargo::removeUnits(int quantity) {
    if (quantity <= 0 || quantity > quantity_) {
        throw std::invalid_argument("Cargo deduction exceeds available quantity");
    }
    quantity_ -= quantity;
}

void ReliefCargo::setQuantity(int quantity) {
    if (quantity < 0) throw std::invalid_argument("Cargo quantity cannot be negative");
    if (!std::isfinite(static_cast<double>(quantity) * unitWeightKg_) || !std::isfinite(static_cast<double>(quantity) * unitVolumeM3_)) throw std::overflow_error("Cargo quantity exceeds numeric range");
    quantity_ = quantity;
}

bool ReliefCargo::operator==(const ReliefCargo& other) const noexcept {
    return id_ == other.id_ && name_ == other.name_ && category_ == other.category_ && unitWeightKg_ == other.unitWeightKg_ && unitVolumeM3_ == other.unitVolumeM3_;
}

std::ostream& operator<<(std::ostream& output, const ReliefCargo& cargo) {
    output << std::quoted(cargo.id_) << ' ' << std::quoted(cargo.name_) << ' '
           << static_cast<unsigned>(cargo.category_) << ' ' << cargo.quantity_ << ' '
           << std::setprecision(12) << cargo.unitWeightKg_ << ' ' << cargo.unitVolumeM3_;
    return output;
}

std::istream& operator>>(std::istream& input, ReliefCargo& cargo) {
    std::string id;
    std::string name;
    unsigned category{};
    int quantity{};
    double weight{};
    double volume{};
    if (!(input >> std::quoted(id) >> std::quoted(name) >> category >> quantity >> weight >> volume)) {
        return input;
    }
    if (category < static_cast<unsigned>(CargoCategory::Rations) || category > static_cast<unsigned>(CargoCategory::Other)) {
        input.setstate(std::ios::failbit);
        return input;
    }
    try {
        cargo = ReliefCargo(std::move(id), std::move(name), static_cast<CargoCategory>(category), quantity, weight, volume);
    } catch (...) {
        input.setstate(std::ios::failbit);
    }
    return input;
}

CargoManifest::CargoManifest(std::string manifestId, std::size_t initialCapacity)
    : manifestId_(std::move(manifestId)), capacity_(initialCapacity) {
    if (!manifestId_.empty() && !isValidLabel(manifestId_, 128)) throw std::invalid_argument("Invalid manifest ID");
    if (capacity_ > maximumManifestItems) throw std::length_error("Manifest capacity limit exceeded");
    if (capacity_ > 0) {
        items_ = new ReliefCargo[capacity_];
    }
}

CargoManifest::CargoManifest(const CargoManifest& other)
    : manifestId_(other.manifestId_), size_(other.size_), capacity_(other.capacity_) {
    if (capacity_ > 0) {
        std::unique_ptr<ReliefCargo[]> replacement(new ReliefCargo[capacity_]);
        if (size_ > 0) std::copy(other.items_, other.items_ + size_, replacement.get());
        items_ = replacement.release();
    }
}

CargoManifest::CargoManifest(CargoManifest&& other) noexcept
    : manifestId_(std::move(other.manifestId_)), items_(other.items_), size_(other.size_), capacity_(other.capacity_) {
    other.items_ = nullptr;
    other.size_ = 0;
    other.capacity_ = 0;
}

CargoManifest& CargoManifest::operator=(CargoManifest other) noexcept {
    swap(other);
    return *this;
}

CargoManifest::~CargoManifest() { delete[] items_; }

void CargoManifest::swap(CargoManifest& other) noexcept {
    using std::swap;
    swap(manifestId_, other.manifestId_);
    swap(items_, other.items_);
    swap(size_, other.size_);
    swap(capacity_, other.capacity_);
}

const std::string& CargoManifest::id() const noexcept { return manifestId_; }
std::size_t CargoManifest::size() const noexcept { return size_; }
bool CargoManifest::empty() const noexcept { return size_ == 0; }

bool CargoManifest::contains(const std::string& cargoId) const noexcept {
    return find(cargoId) != nullptr;
}

const ReliefCargo* CargoManifest::find(const std::string& cargoId) const noexcept {
    for (std::size_t i = 0; i < size_; ++i) {
        if (items_[i].id() == cargoId) return &items_[i];
    }
    return nullptr;
}

ReliefCargo* CargoManifest::find(const std::string& cargoId) noexcept {
    for (std::size_t i = 0; i < size_; ++i) {
        if (items_[i].id() == cargoId) return &items_[i];
    }
    return nullptr;
}

void CargoManifest::reserve(std::size_t newCapacity) {
    if (newCapacity > maximumManifestItems) throw std::length_error("Manifest capacity limit exceeded");
    if (newCapacity <= capacity_) return;
    std::unique_ptr<ReliefCargo[]> replacement(new ReliefCargo[newCapacity]);
    if (size_ > 0) std::copy(items_, items_ + size_, replacement.get());
    delete[] items_;
    items_ = replacement.release();
    capacity_ = newCapacity;
}

void CargoManifest::add(const ReliefCargo& cargo) {
    if (cargo.quantity() <= 0) throw std::invalid_argument("Manifest cargo quantity must be positive");
    if (auto* existing = find(cargo.id())) {
        if (!(*existing == cargo)) throw std::invalid_argument("Manifest cargo definition does not match existing item");
        existing->addUnits(cargo.quantity());
        return;
    }
    if (size_ == capacity_) {
        if (capacity_ > std::numeric_limits<std::size_t>::max() / 2) throw std::length_error("Manifest capacity limit exceeded");
        reserve(capacity_ == 0 ? 2 : capacity_ * 2);
    }
    items_[size_] = cargo;
    ++size_;
}

void CargoManifest::remove(const std::string& cargoId, int quantity) {
    auto* cargo = find(cargoId);
    if (!cargo) throw std::out_of_range("Cargo item not found in manifest");
    cargo->removeUnits(quantity);
    if (cargo->quantity() == 0) {
        const auto index = static_cast<std::size_t>(cargo - items_);
        for (std::size_t i = index + 1; i < size_; ++i) items_[i - 1] = std::move(items_[i]);
        --size_;
    }
}

double CargoManifest::totalWeightKg() const noexcept {
    double total{};
    for (std::size_t i = 0; i < size_; ++i) total += items_[i].totalWeightKg();
    return total;
}

double CargoManifest::totalVolumeM3() const noexcept {
    double total{};
    for (std::size_t i = 0; i < size_; ++i) total += items_[i].totalVolumeM3();
    return total;
}

std::vector<ReliefCargo> CargoManifest::items() const {
    std::vector<ReliefCargo> result;
    result.reserve(size_);
    for (std::size_t i = 0; i < size_; ++i) result.push_back(items_[i]);
    return result;
}

bool CargoManifest::operator==(const CargoManifest& other) const noexcept {
    return manifestId_ == other.manifestId_;
}

std::ostream& operator<<(std::ostream& output, const CargoManifest& manifest) {
    output << std::quoted(manifest.manifestId_) << ' ' << manifest.size_ << '\n';
    for (std::size_t i = 0; i < manifest.size_; ++i) output << manifest.items_[i] << '\n';
    return output;
}

std::istream& operator>>(std::istream& input, CargoManifest& manifest) {
    std::string id;
    std::size_t count{};
    if (!(input >> std::quoted(id) >> count)) return input;
    if (count > maximumManifestItems) {
        input.setstate(std::ios::failbit);
        return input;
    }
    try {
        CargoManifest parsed(std::move(id), count);
        for (std::size_t i = 0; i < count; ++i) {
            ReliefCargo cargo;
            if (!(input >> cargo)) return input;
            parsed.add(cargo);
        }
        manifest.swap(parsed);
    } catch (...) {
        input.setstate(std::ios::failbit);
    }
    return input;
}

DistributionDepot::DistributionDepot(std::string id, std::string name, GeoPoint location)
    : id_(std::move(id)), name_(std::move(name)), location_(location) {
    if (!isValidLabel(id_, 128) || !isValidLabel(name_, 512)) throw std::invalid_argument("Invalid depot ID or name");
    location_.validate();
}

const std::string& DistributionDepot::id() const noexcept { return id_; }
const std::string& DistributionDepot::name() const noexcept { return name_; }
const GeoPoint& DistributionDepot::location() const noexcept { return location_; }

int DistributionDepot::quantityOf(const std::string& cargoId) const noexcept {
    const auto onHand = onHandQuantityOf(cargoId);
    return onHand - reservedQuantityOf(cargoId);
}

int DistributionDepot::onHandQuantityOf(const std::string& cargoId) const noexcept {
    const auto found = inventory_.find(cargoId);
    return found == inventory_.end() ? 0 : found->second.quantity();
}

int DistributionDepot::reservedQuantityOf(const std::string& cargoId) const noexcept {
    const auto found = reservedTotals_.find(cargoId);
    return found == reservedTotals_.end() ? 0 : found->second;
}

int DistributionDepot::reservedQuantityForRequest(const std::string& requestId, const std::string& cargoId) const noexcept {
    const auto request = reservationsByRequest_.find(requestId);
    if (request == reservationsByRequest_.end()) return 0;
    const auto item = request->second.find(cargoId);
    return item == request->second.end() ? 0 : item->second;
}

const ReliefCargo& DistributionDepot::findCargo(const std::string& cargoId) const {
    const auto found = inventory_.find(cargoId);
    if (found == inventory_.end()) throw std::out_of_range("Cargo item not found in depot");
    return found->second;
}

std::vector<ReliefCargo> DistributionDepot::inventory() const {
    std::vector<ReliefCargo> result;
    result.reserve(inventory_.size());
    for (const auto& entry : inventory_) result.push_back(entry.second);
    std::sort(result.begin(), result.end(), [](const ReliefCargo& left, const ReliefCargo& right) { return left.id() < right.id(); });
    return result;
}

std::size_t DistributionDepot::distinctCargoCount() const noexcept { return inventory_.size(); }

void DistributionDepot::reserveInventory(const std::string& requestId, const ReliefCargo& cargo) {
    if (!isValidLabel(requestId, 128) || cargo.quantity() <= 0) throw std::invalid_argument("A valid request and positive reservation quantity are required");
    const auto stock = inventory_.find(cargo.id());
    if (stock == inventory_.end() || !(stock->second == cargo)) throw std::invalid_argument("Reservation cargo does not match depot stock");
    if (quantityOf(cargo.id()) < cargo.quantity()) throw Logistics::InventoryShortageException("Insufficient unreserved stock for cargo " + cargo.id());

    auto request = reservationsByRequest_.end();
    auto item = std::unordered_map<std::string, int>::iterator{};
    auto total = reservedTotals_.end();
    bool insertedRequest = false;
    bool insertedItem = false;
    bool insertedTotal = false;
    try {
        const auto requestResult = reservationsByRequest_.try_emplace(requestId);
        request = requestResult.first;
        insertedRequest = requestResult.second;
        const auto itemResult = request->second.try_emplace(cargo.id(), 0);
        item = itemResult.first;
        insertedItem = itemResult.second;
        const auto totalResult = reservedTotals_.try_emplace(cargo.id(), 0);
        total = totalResult.first;
        insertedTotal = totalResult.second;
        if (item->second > std::numeric_limits<int>::max() - cargo.quantity() || total->second > std::numeric_limits<int>::max() - cargo.quantity()) {
            throw std::overflow_error("Cargo reservation exceeds numeric range");
        }
        item->second += cargo.quantity();
        total->second += cargo.quantity();
    } catch (...) {
        if (insertedTotal && total != reservedTotals_.end() && total->second == 0) reservedTotals_.erase(total);
        if (request != reservationsByRequest_.end()) {
            if (insertedItem && item != request->second.end() && item->second == 0) request->second.erase(item);
            if (insertedRequest && request->second.empty()) reservationsByRequest_.erase(request);
        }
        throw;
    }
}

void DistributionDepot::releaseInventoryReservation(const std::string& requestId, const std::string& cargoId, int quantity) {
    if (quantity <= 0) throw std::invalid_argument("Reservation release quantity must be positive");
    auto request = reservationsByRequest_.find(requestId);
    if (request == reservationsByRequest_.end()) throw std::logic_error("Request has no depot reservation");
    auto item = request->second.find(cargoId);
    auto total = reservedTotals_.find(cargoId);
    if (item == request->second.end() || total == reservedTotals_.end() || item->second < quantity || total->second < quantity) {
        throw std::logic_error("Reservation release exceeds reserved inventory");
    }
    item->second -= quantity;
    total->second -= quantity;
    if (item->second == 0) request->second.erase(item);
    if (total->second == 0) reservedTotals_.erase(total);
    if (request->second.empty()) reservationsByRequest_.erase(request);
}

void DistributionDepot::consumeInventoryReservations(const std::string& requestId, const std::vector<ReliefCargo>& cargo) {
    auto request = reservationsByRequest_.find(requestId);
    if (request == reservationsByRequest_.end()) throw std::logic_error("Request has no reserved depot inventory");
    std::set<std::string> cargoIds;
    for (const auto& item : cargo) {
        const auto reserved = request->second.find(item.id());
        const auto total = reservedTotals_.find(item.id());
        const auto stock = inventory_.find(item.id());
        if (!cargoIds.insert(item.id()).second || item.quantity() <= 0 || reserved == request->second.end() || reserved->second < item.quantity() || total == reservedTotals_.end() || total->second < item.quantity() || stock == inventory_.end() || !(stock->second == item) || stock->second.quantity() < item.quantity()) {
            throw std::logic_error("Dispatch cargo does not match the request's reserved inventory");
        }
    }
    for (const auto& item : cargo) {
        request->second.find(item.id())->second -= item.quantity();
        reservedTotals_.find(item.id())->second -= item.quantity();
    }
    std::size_t deductedCount{};
    try {
        for (const auto& item : cargo) {
            *this -= item;
            ++deductedCount;
        }
    } catch (...) {
        for (std::size_t index = 0; index < deductedCount; ++index) *this += cargo[index];
        for (const auto& item : cargo) {
            request->second.find(item.id())->second += item.quantity();
            reservedTotals_.find(item.id())->second += item.quantity();
        }
        throw;
    }
    for (const auto& item : cargo) {
        auto reserved = request->second.find(item.id());
        if (reserved->second == 0) request->second.erase(reserved);
        auto total = reservedTotals_.find(item.id());
        if (total->second == 0) reservedTotals_.erase(total);
    }
    if (request->second.empty()) reservationsByRequest_.erase(request);
}

DistributionDepot& DistributionDepot::operator+=(const ReliefCargo& cargo) {
    if (cargo.quantity() <= 0) throw std::invalid_argument("Restock quantity must be positive");
    const auto found = inventory_.find(cargo.id());
    if (found == inventory_.end()) {
        if (inventory_.size() >= maximumDepotCargoTypes) throw std::length_error("Depot cargo type limit exceeded");
        inventory_.emplace(cargo.id(), cargo);
    } else {
        if (!(found->second == cargo)) throw std::invalid_argument("Restock cargo definition conflicts with depot inventory");
        found->second.addUnits(cargo.quantity());
    }
    return *this;
}

DistributionDepot& DistributionDepot::operator-=(const ReliefCargo& cargo) {
    if (cargo.quantity() <= 0) throw std::invalid_argument("Deduction quantity must be positive");
    auto found = inventory_.find(cargo.id());
    if (found == inventory_.end() || quantityOf(cargo.id()) < cargo.quantity()) {
        throw Logistics::InventoryShortageException("Insufficient unreserved depot inventory for cargo " + cargo.id());
    }
    if (!(found->second == cargo)) throw std::invalid_argument("Cargo deduction definition conflicts with depot inventory");
    found->second.removeUnits(cargo.quantity());
    return *this;
}

void IncidentCommander::overrideInventory(DistributionDepot& depot, const ReliefCargo& cargo) {
    if (cargo.quantity() <= 0) throw std::invalid_argument("Emergency inventory override must be positive");
    const auto found = depot.inventory_.find(cargo.id());
    if (found == depot.inventory_.end()) {
        if (depot.inventory_.size() >= maximumDepotCargoTypes) throw std::length_error("Depot cargo type limit exceeded");
        depot.inventory_.emplace(cargo.id(), cargo);
    } else {
        if (!(found->second == cargo)) throw std::invalid_argument("Emergency override cannot redefine an existing cargo identity");
        if (cargo.quantity() < depot.reservedQuantityOf(cargo.id())) throw std::invalid_argument("Emergency override cannot reduce stock below reserved quantities");
        found->second.setQuantity(cargo.quantity());
    }
}

ReliefRequest& IncidentCommander::authorizePriority(ReliefRequest& request, MedicalPriority priority) {
    return request.setPriority(priority);
}

int CargoRequirement::remainingQuantity() const noexcept {
    return requestedQuantity - allocatedQuantity;
}

ReliefRequest::ReliefRequest(std::string id, std::string destinationName, GeoPoint destination, MedicalPriority priority)
    : id_(std::move(id)), destinationName_(std::move(destinationName)), destination_(destination), priority_(priority) {
    if (static_cast<unsigned>(priority_) < static_cast<unsigned>(MedicalPriority::Critical) || static_cast<unsigned>(priority_) > static_cast<unsigned>(MedicalPriority::Low)) throw std::invalid_argument("Invalid medical priority");
    if (!isValidLabel(id_, 128) || !isValidLabel(destinationName_, 512)) throw std::invalid_argument("Invalid request ID or destination");
    destination_.validate();
}

const std::string& ReliefRequest::id() const noexcept { return id_; }
const std::string& ReliefRequest::destinationName() const noexcept { return destinationName_; }
const GeoPoint& ReliefRequest::destination() const noexcept { return destination_; }
MedicalPriority ReliefRequest::priority() const noexcept { return priority_; }
RequestStatus ReliefRequest::status() const noexcept { return status_; }
const std::string& ReliefRequest::allocatedDepotId() const noexcept { return allocatedDepotId_; }
const std::vector<CargoRequirement>& ReliefRequest::requirements() const noexcept { return requirements_; }

int ReliefRequest::remainingQuantity(const std::string& cargoId) const noexcept {
    for (const auto& requirement : requirements_) if (requirement.specification.id() == cargoId) return requirement.remainingQuantity();
    return 0;
}

bool ReliefRequest::fullyAllocated() const noexcept {
    return !requirements_.empty() && std::all_of(requirements_.begin(), requirements_.end(), [](const CargoRequirement& item) { return item.remainingQuantity() == 0; });
}

void ReliefRequest::addRequirement(const ReliefCargo& specification, int quantity) {
    if (status_ != RequestStatus::Pending) throw std::logic_error("Cannot modify a request after allocation has started");
    if (requirements_.size() >= maximumRequestRequirements) throw std::length_error("Request requirement limit exceeded");
    if (quantity <= 0 || specification.id().empty()) throw std::invalid_argument("Requirement quantity must be positive");
    const auto found = std::find_if(requirements_.begin(), requirements_.end(), [&](const CargoRequirement& item) { return item.specification.id() == specification.id(); });
    if (found != requirements_.end()) throw std::invalid_argument("Duplicate cargo requirement in request");
    ReliefCargo normalized = specification;
    normalized.setQuantity(0);
    requirements_.push_back({std::move(normalized), quantity, 0});
}

void ReliefRequest::setDestination(GeoPoint destination) {
    if (status_ != RequestStatus::Pending) throw std::logic_error("Cannot change the destination after allocation has started");
    destination.validate();
    destination_ = destination;
}

ReliefRequest& ReliefRequest::setPriority(MedicalPriority priority) {
    if (status_ == RequestStatus::Dispatched) throw std::logic_error("Cannot change the priority of a dispatched request");
    if (static_cast<unsigned>(priority) < static_cast<unsigned>(MedicalPriority::Critical) || static_cast<unsigned>(priority) > static_cast<unsigned>(MedicalPriority::Low)) throw std::invalid_argument("Invalid medical priority");
    priority_ = priority;
    return *this;
}

ReliefRequest& ReliefRequest::recordAllocation(const std::string& cargoId, int quantity, const std::string& depotId) {
    if (quantity <= 0 || !isValidLabel(depotId, 128)) throw std::invalid_argument("Allocation quantity and source depot are required");
    auto found = std::find_if(requirements_.begin(), requirements_.end(), [&](const CargoRequirement& item) { return item.specification.id() == cargoId; });
    if (found == requirements_.end() || quantity > found->remainingQuantity()) throw std::invalid_argument("Allocation exceeds requested quantity");
    if (!allocatedDepotId_.empty() && allocatedDepotId_ != depotId) throw std::invalid_argument("A request cannot be allocated across multiple depots");
    if (allocatedDepotId_.empty()) allocatedDepotId_ = depotId;
    found->allocatedQuantity += quantity;
    status_ = fullyAllocated() ? RequestStatus::Allocated : RequestStatus::PartiallyAllocated;
    return *this;
}

ReliefRequest& ReliefRequest::releaseAllocation(const std::string& cargoId, int quantity) {
    if (status_ == RequestStatus::Dispatched) throw std::logic_error("Cannot release allocation from a dispatched request");
    if (quantity <= 0) throw std::invalid_argument("Release quantity must be positive");
    auto found = std::find_if(requirements_.begin(), requirements_.end(), [&](const CargoRequirement& item) { return item.specification.id() == cargoId; });
    if (found == requirements_.end() || quantity > found->allocatedQuantity) throw std::invalid_argument("Release exceeds allocated quantity");
    found->allocatedQuantity -= quantity;
    const bool noAllocationsRemain = std::all_of(requirements_.begin(), requirements_.end(), [](const CargoRequirement& item) { return item.allocatedQuantity == 0; });
    if (noAllocationsRemain) allocatedDepotId_.clear();
    status_ = noAllocationsRemain ? RequestStatus::Pending : (fullyAllocated() ? RequestStatus::Allocated : RequestStatus::PartiallyAllocated);
    return *this;
}

ReliefRequest& ReliefRequest::markDispatched() {
    if (status_ == RequestStatus::Dispatched) throw std::logic_error("Request has already been dispatched");
    if (!fullyAllocated() || allocatedDepotId_.empty()) throw std::logic_error("A request must be fully allocated from a depot before dispatch");
    status_ = RequestStatus::Dispatched;
    return *this;
}

}
