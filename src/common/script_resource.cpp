#include "common/script_resource.h"

#include <algorithm>
#include <filesystem>
#include <system_error>

#include <nlohmann/json.hpp>

#include "common/config/core_config.h"
#include "common/utils/downloader.h"
#include "common/utils/path_utils.h"
#include "common/registry_cache.h"

namespace vinput::script {

namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;

std::vector<RegistryEntry> ParseRegistryJson(const std::string &content,
                                             std::string *error) {
  std::vector<RegistryEntry> entries;
  try {
    const json j = json::parse(content);
    if (!j.is_object()) {
      if (error) {
        *error = "script registry JSON is not an object";
      }
      return {};
    }
    if (!j.contains("items") || !j.at("items").is_array()) {
      if (error) {
        *error = "script registry JSON is missing array field 'items'";
      }
      return {};
    }

    for (const auto &item : j.at("items")) {
      RegistryEntry entry;
      entry.id = item.value("id", "");
      entry.command = item.value("command", "");
      entry.readme_url = item.value("readme_url", "");
      if (item.contains("script_urls") && item.at("script_urls").is_array()) {
        for (const auto &value : item.at("script_urls")) {
          if (value.is_string()) {
            const auto url = value.get<std::string>();
            if (!url.empty()) {
              entry.script_urls.push_back(url);
            }
          }
        }
      }
      if (item.contains("envs") && item.at("envs").is_array()) {
        for (const auto &value : item.at("envs")) {
          if (!value.is_object()) {
            continue;
          }
          EnvSpec env;
          env.name = value.value("name", "");
          env.required = value.value("required", false);
          if (!env.name.empty()) {
            entry.envs.push_back(std::move(env));
          }
        }
      }
      if (!entry.id.empty() && !entry.command.empty() &&
          !entry.script_urls.empty()) {
        entries.push_back(std::move(entry));
      }
    }
  } catch (const std::exception &ex) {
    if (error) {
      *error = std::string("failed to parse script registry JSON: ") + ex.what();
    }
    return {};
  }

  if (error) {
    error->clear();
  }
  return entries;
}

bool EnsureExecutable(const fs::path &path, std::string *error) {
  std::error_code ec;
  fs::permissions(path,
                  fs::perms::owner_exec | fs::perms::group_exec |
                      fs::perms::others_exec,
                  fs::perm_options::add, ec);
  if (ec) {
    if (error) {
      *error = "failed to mark script executable: " + ec.message();
    }
    return false;
  }
  return true;
}

void FillDefaultEnvMap(const std::vector<EnvSpec> &envs,
                       std::map<std::string, std::string> *target) {
  if (!target) {
    return;
  }
  for (const auto &env : envs) {
    target->try_emplace(env.name, "");
  }
}

bool IsManagedScriptPath(const fs::path &expected, const std::vector<std::string> &args) {
  if (args.size() != 1) {
    return false;
  }
  return fs::path(args.front()).lexically_normal() == expected.lexically_normal();
}

} // namespace

std::vector<RegistryEntry> FetchRegistry(Kind kind,
                                         const std::vector<std::string> &urls,
                                         std::string *error,
                                         std::string *resolved_registry_url) {
  if (urls.empty()) {
    if (error) {
      *error = "no script registry URLs configured";
    }
    return {};
  }

  std::string content;
  vinput::download::Options options;
  options.timeout_seconds = 30;
  options.max_bytes = 4 * 1024 * 1024;
  vinput::download::Result result;
  if (!vinput::registry::cache::FetchText(
          urls,
          kind == Kind::kAsrProvider
              ? vinput::registry::cache::AsrProviderRegistryPath()
              : vinput::registry::cache::LlmAdaptorRegistryPath(),
          options, &content, &result, error)) {
    if (resolved_registry_url) {
      resolved_registry_url->clear();
    }
    return {};
  }

  if (resolved_registry_url) {
    *resolved_registry_url = result.resolved_url;
  }
  return ParseRegistryJson(content, error);
}

std::filesystem::path DefaultLocalScriptPath(Kind kind, std::string_view id) {
  const fs::path base = kind == Kind::kAsrProvider ? vinput::path::UserAsrProviderDir()
                                                   : vinput::path::UserLlmAdaptorDir();
  return base / (std::string(id) + ".py");
}

bool DownloadScript(const RegistryEntry &entry, Kind kind,
                    std::filesystem::path *local_path, std::string *error,
                    std::string *resolved_script_url) {
  const fs::path path = DefaultLocalScriptPath(kind, entry.id);
  vinput::download::Options options;
  options.timeout_seconds = 120;
  vinput::download::Result result;
  if (!vinput::download::DownloadFile(entry.script_urls, path, options, &result)) {
    if (resolved_script_url) {
      resolved_script_url->clear();
    }
    if (error) {
      *error = result.error.empty() ? "failed to download script"
                                    : result.error;
    }
    return false;
  }
  if (!EnsureExecutable(path, error)) {
    return false;
  }
  if (resolved_script_url) {
    *resolved_script_url = result.resolved_url;
  }
  if (local_path) {
    *local_path = path;
  }
  return true;
}

bool MaterializeAsrProvider(CoreConfig *config, const RegistryEntry &entry,
                            const fs::path &script_path, std::string *error) {
  if (!config) {
    if (error) {
      *error = "config is null";
    }
    return false;
  }

  auto it = std::find_if(config->asr.providers.begin(), config->asr.providers.end(),
                         [&entry](const AsrProvider &provider) {
                           return provider.name == entry.id;
                         });
  const fs::path managed_path = DefaultLocalScriptPath(Kind::kAsrProvider, entry.id);

  if (it == config->asr.providers.end()) {
    CommandAsrProvider provider;
    provider.id = entry.id;
    provider.timeoutMs = 60000;
    provider.command = entry.command;
    provider.args = {script_path.string()};
    FillDefaultEnvMap(entry.envs, &provider.env);
    config->asr.providers.push_back(std::move(provider));
  } else {
    if (!IsManagedScriptPath(managed_path, std::visit([](const AsrProviderBase &b) -> const std::vector<std::string> * {
      if (auto *cmd = dynamic_cast<const CommandAsrProvider *>(&b)) return &cmd->args;
      return nullptr;
    }, *it))) {
      if (error) {
        *error = "refusing to overwrite user-defined ASR provider: " + entry.id;
      }
      return false;
    }
    it->type = vinput::asr::kCommandProviderType;
    it->model.clear();
    it->command = entry.command;
    it->args = {script_path.string()};
    if (it->timeoutMs <= 0) {
      it->timeoutMs = 60000;
    }
    FillDefaultEnvMap(entry.envs, &it->env);
  }

  if (error) {
    error->clear();
  }
  return true;
}

bool MaterializeLlmAdaptor(CoreConfig *config, const RegistryEntry &entry,
                           const fs::path &script_path, std::string *error) {
  if (!config) {
    if (error) {
      *error = "config is null";
    }
    return false;
  }

  auto it = std::find_if(config->llm.adaptors.begin(), config->llm.adaptors.end(),
                         [&entry](const LlmAdaptor &adaptor) {
                           return adaptor.id == entry.id;
                         });
  const fs::path managed_path = DefaultLocalScriptPath(Kind::kLlmAdaptor, entry.id);

  if (it == config->llm.adaptors.end()) {
    LlmAdaptor adaptor;
    adaptor.id = entry.id;
    adaptor.command = entry.command;
    adaptor.args = {script_path.string()};
    FillDefaultEnvMap(entry.envs, &adaptor.env);
    config->llm.adaptors.push_back(std::move(adaptor));
  } else {
    if (!IsManagedScriptPath(managed_path, it->args)) {
      if (error) {
        *error = "refusing to overwrite user-defined adaptor: " + entry.id;
      }
      return false;
    }
    it->command = entry.command;
    it->args = {script_path.string()};
    FillDefaultEnvMap(entry.envs, &it->env);
  }

  if (error) {
    error->clear();
  }
  return true;
}

} // namespace vinput::script
