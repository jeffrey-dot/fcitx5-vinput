#include "common/config/core_config.h"

#include "config.h"

#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

using json = nlohmann::ordered_json;

namespace {

std::filesystem::path BundledDefaultCoreConfigPath() {
  const std::filesystem::path source = VINPUT_DEFAULT_CORE_CONFIG_SOURCE_PATH;
  std::error_code ec;
  if (std::filesystem::exists(source, ec) && !ec) {
    return source;
  }
  return std::filesystem::path(VINPUT_DEFAULT_CORE_CONFIG_INSTALL_PATH);
}

} // namespace

bool LoadBundledDefaultCoreConfig(CoreConfig *config, std::string *error) {
  if (!config) {
    if (error) {
      *error = "Config is null.";
    }
    return false;
  }

  const auto path = BundledDefaultCoreConfigPath();
  std::ifstream file(path);
  if (!file.is_open()) {
    if (error) {
      *error = "Failed to open bundled default config: " + path.string();
    }
    return false;
  }

  try {
    json bundled_json;
    file >> bundled_json;
    from_json(bundled_json, *config);
    if (error) {
      error->clear();
    }
    return true;
  } catch (const std::exception &e) {
    if (error) {
      *error = "Failed to parse bundled default config: " + std::string(e.what());
    }
    return false;
  }
}
