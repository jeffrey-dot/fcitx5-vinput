#pragma once

#include <filesystem>
#include <string>
#include <vector>

struct CoreConfig;

namespace vinput::script {

enum class Kind {
  kAsrProvider,
  kLlmAdaptor,
};

struct EnvSpec {
  std::string name;
  bool required = false;
};

struct RegistryEntry {
  std::string id;
  std::string command;
  std::vector<std::string> script_urls;
  std::string readme_url;
  std::vector<EnvSpec> envs;
};

std::vector<RegistryEntry> FetchRegistry(Kind kind,
                                         const std::vector<std::string> &urls,
                                         std::string *error,
                                         std::string *resolved_registry_url = nullptr);
std::vector<RegistryEntry> FetchRegistry(const CoreConfig &config, Kind kind,
                                         const std::vector<std::string> &urls,
                                         std::string *error,
                                         std::string *resolved_registry_url = nullptr);
std::filesystem::path DefaultLocalScriptPath(Kind kind, std::string_view id);
bool DownloadScript(const RegistryEntry &entry, Kind kind,
                    std::filesystem::path *local_path, std::string *error,
                    std::string *resolved_script_url = nullptr);
bool MaterializeAsrProvider(CoreConfig *config, const RegistryEntry &entry,
                            const std::filesystem::path &script_path,
                            std::string *error);
bool MaterializeLlmAdaptor(CoreConfig *config, const RegistryEntry &entry,
                           const std::filesystem::path &script_path,
                           std::string *error);

} // namespace vinput::script
