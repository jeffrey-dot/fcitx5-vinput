#include "common/config/config_router.h"

#include <nlohmann/json.hpp>

#include "common/config/core_config.h"
#include "common/utils/file_utils.h"
#include "common/utils/path_utils.h"

namespace vinput::config {

using json = nlohmann::ordered_json;

bool GetConfigValue(const std::string &pointer, std::string *value,
                    std::string *error) {
  CoreConfig config = LoadCoreConfig();
  NormalizeCoreConfig(&config);
  json j = config;

  try {
    const json &node = j.at(json::json_pointer(pointer));
    if (value) {
      *value = node.is_string() ? node.get<std::string>() : node.dump();
    }
    return true;
  } catch (const std::exception &e) {
    if (error) {
      *error = std::string("Config path not found: ") + e.what();
    }
    return false;
  }
}

bool SetConfigValue(const std::string &pointer, const std::string &value,
                    std::string *error) {
  CoreConfig config = LoadCoreConfig();
  NormalizeCoreConfig(&config);
  json j = config;

  json parsed_value;
  try {
    parsed_value = json::parse(value);
  } catch (const json::parse_error &) {
    parsed_value = value;
  }

  try {
    j.at(json::json_pointer(pointer)) = std::move(parsed_value);
  } catch (const std::exception &e) {
    if (error) {
      *error = std::string("Config path not found: ") + e.what();
    }
    return false;
  }

  try {
    CoreConfig updated = j.get<CoreConfig>();
    if (!SaveCoreConfig(updated)) {
      if (error) {
        std::string validation_error;
        NormalizeCoreConfig(&updated);
        if (ValidateCoreConfig(updated, &validation_error) &&
            validation_error.empty()) {
          *error = "Failed to save config.";
        } else {
          *error = validation_error;
        }
      }
      return false;
    }
    return true;
  } catch (const std::exception &e) {
    if (error) {
      *error = std::string("Invalid value: ") + e.what();
    }
    return false;
  }
}

std::filesystem::path GetEditTarget(const std::string &target) {
  std::filesystem::path path;
  if (target == "core") {
    path = vinput::path::CoreConfigPath();
  } else {
    path = vinput::path::FcitxAddonConfigPath();
  }

  std::filesystem::path resolved;
  if (vinput::file::ResolveSymlinkPath(path, &resolved, nullptr)) {
    return resolved;
  }
  return path;
}

} // namespace vinput::config
