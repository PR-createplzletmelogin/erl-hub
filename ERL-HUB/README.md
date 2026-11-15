# ERL-HUB

**Emergency Relief Logistics Hub**  
A C++17 console application for coordinating supplies, vehicles, and dispatch operations after a major disaster.

## Overview

ERL-HUB models the work of a regional relief distribution centre following an earthquake. Coordinators can maintain depot stock, register relief requests, allocate supplies, review available vehicles, assess route conditions, and dispatch vehicles to specified destinations.

The application is a local simulation. It uses sample data on first launch and stores operational records in local binary files so that information can be restored when the application is started again.

## Main features

- **Depot inventory:** Register depots, view stock, and add supplies.
- **Relief requests:** Create requests with a destination, required cargo, and medical priority.
- **Supply allocation:** Allocate supplies using either a volume limit or an authorised medical-priority threshold. Allocations reserve stock so that the same items cannot be promised to multiple requests.
- **Vehicle fleet:** Register and manage ground, aerial, amphibious, and amphibious medical vehicles.
- **Route assessment:** Evaluate a proposed journey using its distance, road condition, water depth, road and waterway availability, and airspace restrictions.
- **Dispatch management:** Check vehicle availability and payload capacity, load the manifest, record dispatch details, and complete a mission when a vehicle returns.
- **Incident Commander action:** Change a request's priority through the designated override operation.
- **Persistent records:** Save and restore inventory, requests, vehicles, manifests, and dispatch history using binary file streams.
- **Dashboard and history:** Review depot, fleet, request, and dispatch information from the console menu.

## C++ concepts used

The project was developed to demonstrate the topics covered in the CIC-211 Object-Oriented Programming Using C++ course.

- Namespaces: `ReliefOps::Inventory` and `ReliefOps::Logistics`.
- Function overloading: two `allocateSupply()` overloads support volume-based and priority-based allocation.
- References and inline utilities: used for inventory operations and distance/payload calculations.
- Friend access: `IncidentCommander` provides the defined emergency override operations.
- Dynamic memory management: `CargoManifest` manages its own dynamically allocated cargo storage and implements copy and move operations.
- Operator overloading: `+=` and `-=` for depot stock operations, `==` for cargo and manifest comparisons, and `<<` / `>>` for stream input and output.
- Inheritance and polymorphism: an abstract `ReliefVehicle` interface is implemented by the vehicle types.
- Virtual inheritance: `GroundConvoy` and `AmphibiousCraft` virtually inherit from `ReliefVehicle`; `AmphibiousMedicalUnit` combines the two paths.
- Composition: vehicles contain `GPSBeacon` and `EmergencyLifeSupportUnit` objects.
- Exceptions: custom exceptions represent conditions such as overweight cargo, fuel depletion, damaged routes, and inventory shortages.
- Templates and STL: `PriorityDispatchQueue<T>`, vectors, unordered maps, algorithms, and a request-priority functor are used in the application.
- File handling: binary streams are used to save and restore application state.

## Project structure

```text
ERL-HUB/
├── CMakeLists.txt
├── include/
│   └── erl_hub/
│       ├── application.hpp
│       ├── exceptions.hpp
│       ├── inventory.hpp
│       ├── logistics.hpp
│       ├── persistence.hpp
│       ├── types.hpp
│       └── vehicles.hpp
└── src/
    ├── application.cpp
    ├── dispatch_accounting_access.hpp
    ├── inventory.cpp
    ├── logistics.cpp
    ├── main.cpp
    ├── persistence.cpp
    ├── request_access.hpp
    ├── types.cpp
    └── vehicles.cpp
```

## Requirements

- CMake 3.16 or later
- A C++17-compatible compiler (GCC, Clang, or MSVC)

## Build

Run these commands from the project root:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

## Run

Start the interactive application on Linux or macOS:

```bash
./build/erl_hub
```

On Windows with a Visual Studio CMake generator, run:

```powershell
.\build\Release\erl_hub.exe
```

The application creates a `data` directory for saved records by default. A different data directory can be supplied as an argument:

```bash
./build/erl_hub ./my-data
```

## Demonstration mode

The application includes a sample dispatch scenario. Run it with a separate, fresh data directory:

```bash
./build/erl_hub --demo ./demo-data
```

The demonstration uses the sample depot, emergency request, and aerial vehicle. Since it saves the completed dispatch, use a new or empty data directory when you want to run the demonstration again.

## Scope

ERL-HUB is an educational C++ simulation. Route conditions are entered by the operator and assessed by the application; the project does not connect to live GPS devices, mapping services, emergency agencies, or real vehicles.

## Course context

**Course:** Object-Oriented Programming Using C++  
**Paper code:** CIC-211  
**Project:** Post-Earthquake Multi-Agency Emergency Relief Logistics Hub (ERL-HUB)
