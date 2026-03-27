#include "cli/command_asr.h"

#include "cli/cli_helpers.h"
#include "cli/editor_utils.h"
#include "cli/systemd_client.h"
#include "common/core_config.h"
#include "common/i18n.h"
#include "common/path_utils.h"
#include "common/registry_i18n.h"
#include "common/script_resource.h"
#include "common/string_utils.h"

#include <algorithm>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <system_error>

namespace {

bool IsLocalProvider(const AsrProvider &provider) {
  return provider.type == vinput::asr::kLocalProviderType;
}

bool IsCommandProvider(const AsrProvider &provider) {
  return provider.type == vinput::asr::kCommandProviderType;
}

std::string JoinCommand(const AsrProvider &provider) {
  std::string text = provider.command;
  for (const auto &arg : provider.args) {
    if (!text.empty()) {
      text += " ";
    }
    text += arg;
  }
  return text;
}

bool ParseEnvEntry(const std::string &entry, std::string *key,
                   std::string *value) {
  if (!key || !value) {
    return false;
  }
  const std::size_t pos = entry.find('=');
  if (pos == std::string::npos || pos == 0) {
    return false;
  }
  *key = entry.substr(0, pos);
  *value = entry.substr(pos + 1);
  return true;
}

std::filesystem::path ResolveExistingFilePath(const std::string &candidate) {
  if (candidate.empty()) {
    return {};
  }

  std::filesystem::path path = vinput::path::ExpandUserPath(candidate);
  if (path.empty()) {
    return {};
  }
  if (path.is_relative()) {
    std::error_code ec;
    const std::filesystem::path cwd = std::filesystem::current_path(ec);
    if (ec) {
      return {};
    }
    path = cwd / path;
  }

  std::error_code ec;
  if (!std::filesystem::exists(path, ec) || ec) {
    return {};
  }
  if (!std::filesystem::is_regular_file(path, ec) || ec) {
    return {};
  }
  return path;
}

std::filesystem::path ResolveEditableScriptPath(const AsrProvider &provider) {
  if (!IsCommandProvider(provider)) {
    return {};
  }

  if (provider.command.find('/') != std::string::npos ||
      provider.command.rfind(".", 0) == 0 ||
      provider.command.rfind("~", 0) == 0) {
    if (auto path = ResolveExistingFilePath(provider.command); !path.empty()) {
      return path;
    }
  }

  for (const auto &arg : provider.args) {
    if (auto path = ResolveExistingFilePath(arg); !path.empty()) {
      return path;
    }
  }

  return {};
}

void RestartDaemonAfterAsrChange(Formatter &fmt) {
  const int restart_result = vinput::cli::SystemctlRestart();
  if (restart_result != 0) {
    fmt.PrintWarning(vinput::str::FmtStr(
        _("ASR config updated, but daemon restart failed (exit code: %d)."),
        restart_result));
    fmt.PrintInfo(_("Restart the daemon manually to apply the new provider."));
    return;
  }
  fmt.PrintInfo(_("Daemon restarted to apply ASR changes."));
}

}  // namespace

int RunAsrList(Formatter &fmt, const CliContext &ctx) {
  auto config = LoadCoreConfig();
  NormalizeCoreConfig(&config);

  if (ctx.json_output) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto &provider : config.asr.providers) {
      arr.push_back({{"name", provider.name},
                     {"type", provider.type},
                     {"active", provider.name == config.asr.activeProvider},
                     {"model", provider.model},
                     {"command", provider.command},
                     {"args", provider.args},
                     {"env", provider.env},
                     {"timeout_ms", provider.timeoutMs}});
    }
    fmt.PrintJson(arr);
    return 0;
  }

  std::vector<std::string> headers = {_("NAME"), _("TYPE"), _("ACTIVE"),
                                      _("MODEL"), _("COMMAND"),
                                      _("TIMEOUT")};
  std::vector<std::vector<std::string>> rows;
  for (const auto &provider : config.asr.providers) {
    std::string model_display = "-";
    if (IsLocalProvider(provider)) {
      model_display = provider.model.empty() ? _("(not set)") : provider.model;
    }
    rows.push_back(
        {provider.name,
         provider.type,
         provider.name == config.asr.activeProvider ? _("yes") : _("no"),
         model_display,
         IsCommandProvider(provider) ? JoinCommand(provider) : "-",
         vinput::str::FmtStr("%d ms", provider.timeoutMs)});
  }
  fmt.PrintTable(headers, rows);
  return 0;
}

int RunAsrListAvailable(Formatter &fmt, const CliContext &ctx) {
  auto config = LoadCoreConfig();
  NormalizeCoreConfig(&config);

  const auto registry_urls = ResolveAsrProviderRegistryUrls(config);
  if (registry_urls.empty()) {
    fmt.PrintError(
        _("No ASR provider registry base URLs configured. Edit config.json and set registry.base_urls."));
    return 1;
  }

  std::string error;
  const auto entries = vinput::script::FetchRegistry(
      vinput::script::Kind::kAsrProvider, registry_urls, &error);
  if (!error.empty()) {
    fmt.PrintError(error);
    return 1;
  }

  const auto locale = vinput::registry::DetectPreferredLocale();
  const auto i18n_map = vinput::registry::FetchMergedI18nMap(config, locale);

  if (ctx.json_output) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto &entry : entries) {
      nlohmann::json envs = nlohmann::json::array();
      for (const auto &env : entry.envs) {
        envs.push_back({{"name", env.name}, {"required", env.required}});
      }
      arr.push_back({{"id", entry.id},
                     {"title", vinput::registry::LookupI18n(
                                   i18n_map, entry.id + ".title", entry.id)},
                     {"description", vinput::registry::LookupI18n(
                                         i18n_map, entry.id + ".description", "")},
                     {"command", entry.command},
                     {"readme_url", entry.readme_url},
                     {"envs", envs},
                     {"status", ResolveAsrProvider(config, entry.id) ? "installed"
                                                                      : "available"}});
    }
    fmt.PrintJson(arr);
    return 0;
  }

  std::vector<std::string> headers = {_("ID"), _("TITLE"), _("COMMAND"),
                                      _("STATUS")};
  std::vector<std::vector<std::string>> rows;
  for (const auto &entry : entries) {
    rows.push_back(
        {entry.id,
         vinput::registry::LookupI18n(i18n_map, entry.id + ".title", entry.id),
         entry.command,
         ResolveAsrProvider(config, entry.id) ? _("installed")
                                              : _("available")});
  }
  fmt.PrintTable(headers, rows);
  return 0;
}

int RunAsrAdd(const std::string &name, const std::string &type,
              const std::string &model, const std::string &command,
              const std::vector<std::string> &args,
              const std::vector<std::string> &env_entries, int timeout_ms,
              Formatter &fmt, const CliContext &ctx) {
  (void)ctx;
  if (timeout_ms <= 0) {
    fmt.PrintError(_("Timeout must be greater than zero."));
    return 1;
  }

  auto config = LoadCoreConfig();
  NormalizeCoreConfig(&config);
  if (ResolveAsrProvider(config, name)) {
    fmt.PrintError(
        vinput::str::FmtStr(_("ASR provider '%s' already exists."), name));
    return 1;
  }

  AsrProvider provider;
  provider.name = name;
  provider.type = type;
  provider.timeoutMs = timeout_ms;

  if (type == vinput::asr::kLocalProviderType) {
    if (!command.empty() || !args.empty() || !env_entries.empty()) {
      fmt.PrintError(
          _("Local ASR providers do not accept command or env fields."));
      return 1;
    }
    provider.model = model;
  } else if (type == vinput::asr::kCommandProviderType) {
    if (!model.empty()) {
      fmt.PrintError(_("Command ASR providers do not accept a model field."));
      return 1;
    }
    if (command.empty()) {
      fmt.PrintError(_("Command ASR providers require --command."));
      return 1;
    }

    provider.model.clear();
    provider.command = command;
    provider.args = args;
    for (const auto &entry : env_entries) {
      std::string key;
      std::string value;
      if (!ParseEnvEntry(entry, &key, &value)) {
        fmt.PrintError(vinput::str::FmtStr(
            _("Invalid env entry '%s'. Use KEY=VALUE."), entry));
        return 1;
      }
      provider.env[key] = value;
    }
  } else {
    fmt.PrintError(vinput::str::FmtStr(
        _("Unsupported ASR provider type '%s'."), type));
    return 1;
  }

  config.asr.providers.push_back(std::move(provider));
  NormalizeCoreConfig(&config);
  if (!SaveConfigOrFail(config, fmt)) {
    return 1;
  }

  fmt.PrintSuccess(
      vinput::str::FmtStr(_("ASR provider '%s' added."), name));
  fmt.PrintInfo(
      vinput::str::FmtStr(_("Run `vinput asr use %s` to activate"), name));
  RestartDaemonAfterAsrChange(fmt);
  return 0;
}

int RunAsrInstall(const std::string &id, Formatter &fmt, const CliContext &ctx) {
  (void)ctx;
  auto config = LoadCoreConfig();
  NormalizeCoreConfig(&config);

  const auto registry_urls = ResolveAsrProviderRegistryUrls(config);
  if (registry_urls.empty()) {
    fmt.PrintError(
        _("No ASR provider registry base URLs configured. Edit config.json and set registry.base_urls."));
    return 1;
  }

  std::string error;
  const auto entries = vinput::script::FetchRegistry(
      vinput::script::Kind::kAsrProvider, registry_urls, &error);
  if (!error.empty()) {
    fmt.PrintError(error);
    return 1;
  }

  const auto it = std::find_if(
      entries.begin(), entries.end(),
      [&id](const vinput::script::RegistryEntry &entry) { return entry.id == id; });
  if (it == entries.end()) {
    fmt.PrintError(vinput::str::FmtStr(
        _("ASR provider '%s' not found in registry."), id));
    return 1;
  }

  std::filesystem::path script_path;
  if (!vinput::script::DownloadScript(*it, vinput::script::Kind::kAsrProvider,
                                      &script_path, &error)) {
    fmt.PrintError(error);
    return 1;
  }
  if (!vinput::script::MaterializeAsrProvider(&config, *it, script_path,
                                              &error)) {
    fmt.PrintError(error);
    return 1;
  }
  NormalizeCoreConfig(&config);
  if (!SaveConfigOrFail(config, fmt)) {
    return 1;
  }

  fmt.PrintSuccess(vinput::str::FmtStr(
      _("ASR provider '%s' synchronized to local config."), id));
  fmt.PrintInfo(
      vinput::str::FmtStr(_("Local script path: %s"), script_path.string()));
  RestartDaemonAfterAsrChange(fmt);
  return 0;
}

int RunAsrRemove(const std::string &name, bool force, Formatter &fmt,
                 const CliContext &ctx) {
  (void)ctx;
  auto config = LoadCoreConfig();
  NormalizeCoreConfig(&config);

  auto &providers = config.asr.providers;
  auto it = std::find_if(providers.begin(), providers.end(),
                         [&name](const AsrProvider &provider) {
                           return provider.name == name;
                         });
  if (it == providers.end()) {
    fmt.PrintError(
        vinput::str::FmtStr(_("ASR provider '%s' not found."), name));
    return 1;
  }

  const bool removing_active = name == config.asr.activeProvider;
  if (removing_active && !force) {
    fmt.PrintError(vinput::str::FmtStr(
        _("Cannot remove active ASR provider '%s'. Use --force to override."),
        name));
    return 1;
  }

  providers.erase(it);
  if (removing_active) {
    config.asr.activeProvider.clear();
  }
  NormalizeCoreConfig(&config);

  if (!SaveConfigOrFail(config, fmt)) {
    return 1;
  }

  if (removing_active) {
    RestartDaemonAfterAsrChange(fmt);
    fmt.PrintSuccess(vinput::str::FmtStr(
        _("ASR provider '%s' removed. Active provider is now '%s'."), name,
        config.asr.activeProvider));
    return 0;
  }

  RestartDaemonAfterAsrChange(fmt);
  fmt.PrintSuccess(
      vinput::str::FmtStr(_("ASR provider '%s' removed."), name));
  return 0;
}

int RunAsrUse(const std::string &name, Formatter &fmt, const CliContext &ctx) {
  (void)ctx;
  auto config = LoadCoreConfig();
  NormalizeCoreConfig(&config);

  if (!ResolveAsrProvider(config, name)) {
    fmt.PrintError(
        vinput::str::FmtStr(_("ASR provider '%s' not found."), name));
    return 1;
  }

  config.asr.activeProvider = name;
  if (!SaveConfigOrFail(config, fmt)) {
    return 1;
  }

  const int restart_result = vinput::cli::SystemctlRestart();
  if (restart_result != 0) {
    fmt.PrintWarning(vinput::str::FmtStr(
        _("Active ASR provider set to '%s', but daemon restart failed (exit code: %d)."),
        name, restart_result));
    fmt.PrintInfo(_("Restart the daemon manually to apply the new provider."));
    return 1;
  }

  fmt.PrintSuccess(vinput::str::FmtStr(
      _("Active ASR provider set to '%s'. Daemon restarted."), name));
  return 0;
}

int RunAsrEdit(const std::string &name, Formatter &fmt, const CliContext &ctx) {
  (void)ctx;
  auto config = LoadCoreConfig();
  NormalizeCoreConfig(&config);

  const AsrProvider *provider = ResolveAsrProvider(config, name);
  if (!provider) {
    fmt.PrintError(
        vinput::str::FmtStr(_("ASR provider '%s' not found."), name));
    return 1;
  }
  if (!IsCommandProvider(*provider)) {
    fmt.PrintError(vinput::str::FmtStr(
        _("ASR provider '%s' is not a command provider and cannot be edited."),
        name));
    return 1;
  }

  const std::filesystem::path script_path = ResolveEditableScriptPath(*provider);
  if (script_path.empty()) {
    fmt.PrintError(vinput::str::FmtStr(
        _("ASR provider '%s' does not reference an editable script file."),
        name));
    return 1;
  }

  const int ret = OpenInEditor(script_path);
  if (ret != 0) {
    fmt.PrintError(_("Editor exited with error."));
    return ret;
  }

  RestartDaemonAfterAsrChange(fmt);
  fmt.PrintSuccess(vinput::str::FmtStr(
      _("Updated ASR provider script: %s"), script_path.string()));
  return 0;
}
