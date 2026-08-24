#pragma once

#include "combine_duplicate_furniture/domain.hpp"

#include <filesystem>
#include <string>

namespace cdf {

[[nodiscard]] bool LoadFurnitureCatalog(
    const std::filesystem::path& resources_gpak,
    FurnitureCatalog& catalog,
    std::string& error);

}  // namespace cdf
