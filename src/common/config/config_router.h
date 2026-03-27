#pragma once
#include <filesystem>
#include <string>

namespace vinput::config {

// JSON Pointer (RFC 6901) access to core config.
// Example: "/global/capture_device", "/asr/active_provider"
bool GetConfigValue(const std::string& pointer, std::string* value, std::string* error);
bool SetConfigValue(const std::string& pointer, const std::string& value, std::string* error);

// Returns the file path for the given editable config target.
// "core" -> config.json path, "fcitx" -> vinput.conf path
std::filesystem::path GetEditTarget(const std::string& target);

} // namespace vinput::config
