#include "erl_hub/types.hpp"

namespace ReliefOps {

std::string toString(CargoCategory value) {
    switch (value) {
        case CargoCategory::Rations: return "Rations";
        case CargoCategory::Medical: return "Medical";
        case CargoCategory::Water: return "Water";
        case CargoCategory::Blankets: return "Blankets";
        case CargoCategory::Other: return "Other";
    }
    return "Unknown";
}

std::string toString(MedicalPriority value) {
    switch (value) {
        case MedicalPriority::Critical: return "Critical";
        case MedicalPriority::High: return "High";
        case MedicalPriority::Standard: return "Standard";
        case MedicalPriority::Low: return "Low";
    }
    return "Unknown";
}

std::string toString(VehicleKind value) {
    switch (value) {
        case VehicleKind::Ground: return "Ground Convoy";
        case VehicleKind::Aerial: return "Aerial Transport";
        case VehicleKind::Amphibious: return "Amphibious Craft";
        case VehicleKind::AmphibiousMedical: return "Amphibious Medical Unit";
    }
    return "Unknown";
}

std::string toString(RequestStatus value) {
    switch (value) {
        case RequestStatus::Pending: return "Pending";
        case RequestStatus::PartiallyAllocated: return "Partially Allocated";
        case RequestStatus::Allocated: return "Allocated";
        case RequestStatus::Dispatched: return "Dispatched";
    }
    return "Unknown";
}

}
