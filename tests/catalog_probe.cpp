#include "combine_duplicate_furniture/catalog.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: cdf_catalog_probe <resources.gpak>\n";
        return EXIT_FAILURE;
    }
    cdf::FurnitureCatalog catalog;
    std::string error;
    if (!cdf::LoadFurnitureCatalog(
            std::filesystem::path(argv[1]), catalog, error)) {
        std::cerr << error << '\n';
        return EXIT_FAILURE;
    }
    const auto fridge = catalog.find("set_90s_frige");
    if (fridge == catalog.end() || !fridge->second.can_be_rare) {
        std::cerr << "set_90s_frige is missing or cannot be rare\n";
        return EXIT_FAILURE;
    }
    const auto rare = fridge->second.attributes.Rare();
    if (fridge->second.attributes.comfort != 1.0 ||
        fridge->second.attributes.stimulation != 1.0 ||
        rare.comfort != 2.0 || rare.stimulation != 2.0) {
        std::cerr << "set_90s_frige attributes are unexpected\n";
        return EXIT_FAILURE;
    }
    std::cout << "catalog definitions=" << catalog.size()
              << " fridge='" << fridge->second.display_name
              << "' comfort=+1->+2 stimulation=+1->+2\n";
    return EXIT_SUCCESS;
}
