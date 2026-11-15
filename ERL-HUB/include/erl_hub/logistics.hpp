#pragma once

#include "erl_hub/inventory.hpp"
#include "erl_hub/vehicles.hpp"

#include <algorithm>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace ReliefOps::Logistics {

template <typename T, typename Compare = std::less<T>>
class PriorityDispatchQueue {
public:
    explicit PriorityDispatchQueue(Compare compare = Compare{}) : compare_(std::move(compare)) {}
    void push(T value) {
        values_.push_back(std::move(value));
        std::push_heap(values_.begin(), values_.end(), compare_);
    }
    const T& top() const {
        if (values_.empty()) throw std::out_of_range("Priority queue is empty");
        return values_.front();
    }
    T pop() {
        if (values_.empty()) throw std::out_of_range("Priority queue is empty");
        std::pop_heap(values_.begin(), values_.end(), compare_);
        T result = std::move(values_.back());
        values_.pop_back();
        return result;
    }
    bool empty() const noexcept { return values_.empty(); }
    std::size_t size() const noexcept { return values_.size(); }
    void clear() noexcept { values_.clear(); }
private:
    std::vector<T> values_;
    Compare compare_;
};

struct AllocationResult {
    bool accepted{};
    bool complete{};
    double allocatedVolumeM3{};
    std::vector<Inventory::ReliefCargo> allocatedCargo;
    std::string message;
};

AllocationResult allocateSupply(Inventory::DistributionDepot& depot, Inventory::ReliefRequest& request, double volumeLimitM3);
AllocationResult allocateSupply(Inventory::DistributionDepot& depot, Inventory::ReliefRequest& request, MedicalPriority maximumPriority);

struct DispatchRecord {
    std::string dispatchId;
    std::string requestId;
    std::string vehicleId;
    std::string manifestId;
    std::string destinationName;
    std::string timestamp;
    double payloadKg{};
    double routeDistanceKm{};
    MedicalPriority priority{MedicalPriority::Standard};
};

struct DispatchOutcome {
    bool success{};
    RouteEvaluation route;
    std::vector<Inventory::ReliefCargo> cargo;
    std::string message;
};

DispatchOutcome dispatchRequest(Inventory::DistributionDepot& depot, Inventory::ReliefRequest& request, ReliefVehicle* vehicle, const RouteConditions& conditions, MedicalPriority maximumPriority, const std::string& dispatchId);
std::string currentTimestamp();

}
