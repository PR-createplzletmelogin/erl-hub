#include "erl_hub/application.hpp"

#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    try {
        const bool demonstrationMode = argc > 1 && std::string(argv[1]) == "--demo";
        if ((demonstrationMode && argc > 3) || (!demonstrationMode && argc > 2)) {
            std::cerr << "Usage: erl_hub [data-directory] | erl_hub --demo [data-directory]\n";
            return 64;
        }
        const std::filesystem::path dataDirectory = demonstrationMode
            ? (argc > 2 ? argv[2] : "data")
            : (argc > 1 ? argv[1] : "data");
        ReliefOps::ReliefApplication application(dataDirectory);
        if (demonstrationMode) {
            const bool completed = application.runDemonstrationScenario(std::cout);
            if (!completed) {
                std::cout << "Demonstration scenario could not run in the current state. Reset the data directory to restore the sample scenario.\n";
                return 2;
            }
            return 0;
        }
        return application.run();
    } catch (const std::exception& error) {
        std::cerr << "ERL-HUB startup failure: " << error.what() << '\n';
        return 1;
    }
}
