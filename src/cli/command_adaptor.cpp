#include "cli/command_adaptor.h"

#include <nlohmann/json.hpp>

#include "cli/dbus_client.h"
#include "common/adaptor_manager.h"
#include "common/config/core_config.h"
#include "common/i18n.h"
#include "common/registry_i18n.h"
#include "common/script_resource.h"
#include "common/utils/string_utils.h"

namespace {

std::string RunningState(const LlmAdaptor &adaptor) {
  return vinput::adaptor::IsRunning(adaptor.id) ? "running" : "stopped";
}

}  // namespace

int RunAdaptorList(Formatter &fmt, const CliContext &ctx) {
  auto config = LoadCoreConfig();
  NormalizeCoreConfig(&config);

  if (ctx.json_output) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto &adaptor : config.llm.adaptors) {
      arr.push_back({
          {"id", adaptor.id},
          {"command", adaptor.command},
          {"args", adaptor.args},
          {"env", adaptor.env},
          {"state", RunningState(adaptor)},
      });
    }
    fmt.PrintJson(arr);
    return 0;
  }

  std::vector<std::string> headers = {_("ID"), _("STATE"), _("COMMAND")};
  std::vector<std::vector<std::string>> rows;
  for (const auto &adaptor : config.llm.adaptors) {
    rows.push_back({adaptor.id,
                    RunningState(adaptor),
                    adaptor.command.empty() ? _("(not set)")
                                            : adaptor.command});
  }
  fmt.PrintTable(headers, rows);
  return 0;
}

int RunAdaptorListAvailable(Formatter &fmt, const CliContext &ctx) {
  auto config = LoadCoreConfig();
  NormalizeCoreConfig(&config);

  const auto registry_urls = ResolveLlmAdaptorRegistryUrls(config);
  if (registry_urls.empty()) {
    fmt.PrintError(
        _("No LLM adaptor registry base URLs configured. Edit config.json and set registry.base_urls."));
    return 1;
  }

  std::string error;
  const auto entries = vinput::script::FetchRegistry(
      vinput::script::Kind::kLlmAdaptor, registry_urls, &error);
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
                     {"status", ResolveLlmAdaptor(config, entry.id) ? "installed"
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
         ResolveLlmAdaptor(config, entry.id) ? _("installed")
                                             : _("available")});
  }
  fmt.PrintTable(headers, rows);
  return 0;
}

int RunAdaptorInstall(const std::string &id, Formatter &fmt,
                      const CliContext &ctx) {
  (void)ctx;
  auto config = LoadCoreConfig();
  NormalizeCoreConfig(&config);

  const auto registry_urls = ResolveLlmAdaptorRegistryUrls(config);
  if (registry_urls.empty()) {
    fmt.PrintError(
        _("No LLM adaptor registry base URLs configured. Edit config.json and set registry.base_urls."));
    return 1;
  }

  std::string error;
  const auto entries = vinput::script::FetchRegistry(
      vinput::script::Kind::kLlmAdaptor, registry_urls, &error);
  if (!error.empty()) {
    fmt.PrintError(error);
    return 1;
  }

  const auto it = std::find_if(
      entries.begin(), entries.end(),
      [&id](const vinput::script::RegistryEntry &entry) { return entry.id == id; });
  if (it == entries.end()) {
    fmt.PrintError(
        vinput::str::FmtStr(_("Adaptor '%s' not found in registry."), id));
    return 1;
  }

  std::filesystem::path script_path;
  if (!vinput::script::DownloadScript(*it, vinput::script::Kind::kLlmAdaptor,
                                      &script_path, &error)) {
    fmt.PrintError(error);
    return 1;
  }
  if (!vinput::script::MaterializeLlmAdaptor(&config, *it, script_path,
                                             &error)) {
    fmt.PrintError(error);
    return 1;
  }
  NormalizeCoreConfig(&config);
  if (!SaveCoreConfig(config)) {
    fmt.PrintError(_("Failed to save configuration."));
    return 1;
  }

  fmt.PrintSuccess(
      vinput::str::FmtStr(_("Adaptor '%s' synchronized to local config."), id));
  fmt.PrintInfo(
      vinput::str::FmtStr(_("Local script path: %s"), script_path.string()));
  return 0;
}

int RunAdaptorStart(const std::string &name, Formatter &fmt,
                    const CliContext &ctx) {
  (void)ctx;
  std::string error;
  vinput::cli::DbusClient dbus;
  if (!dbus.StartAdaptor(name, &error)) {
    fmt.PrintError(error);
    return 1;
  }
  fmt.PrintSuccess(vinput::str::FmtStr(_("Adaptor '%s' started."), name));
  return 0;
}

int RunAdaptorStop(const std::string &name, Formatter &fmt,
                   const CliContext &ctx) {
  (void)ctx;
  std::string error;
  vinput::cli::DbusClient dbus;
  if (!dbus.StopAdaptor(name, &error)) {
    fmt.PrintError(error);
    return 1;
  }
  fmt.PrintSuccess(vinput::str::FmtStr(_("Adaptor '%s' stopped."), name));
  return 0;
}
