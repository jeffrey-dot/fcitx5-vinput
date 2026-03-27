#include "cli/config/asr_actions.h"

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <vector>

#include "cli/utils/cli_helpers.h"
#include "cli/utils/editor_utils.h"
#include "common/config/core_config.h"
#include "common/i18n.h"
#include "common/model_manager.h"
#include "common/utils/string_utils.h"

namespace {

std::string CommandText(const CommandAsrProvider &provider) {
  std::string text = provider.command;
  for (const auto &arg : provider.args) {
    if (!text.empty()) {
      text += " ";
    }
    text += arg;
  }
  return text;
}

const LocalAsrProvider *PreferredLocalProvider(const CoreConfig &config) {
  const AsrProvider *provider = ResolvePreferredLocalAsrProvider(config);
  if (!provider) {
    return nullptr;
  }
  return std::get_if<LocalAsrProvider>(provider);
}

LocalAsrProvider *PreferredLocalProvider(CoreConfig *config) {
  if (!config) {
    return nullptr;
  }
  const AsrProvider *provider = ResolvePreferredLocalAsrProvider(*config);
  if (!provider) {
    return nullptr;
  }
  const std::string providerId = AsrProviderId(*provider);
  for (auto &candidate : config->asr.providers) {
    if (AsrProviderId(candidate) == providerId) {
      return std::get_if<LocalAsrProvider>(&candidate);
    }
  }
  return nullptr;
}

}  // namespace

int RunAsrConfigList(Formatter &fmt, const CliContext &ctx) {
  CoreConfig config = LoadCoreConfig();

  if (ctx.json_output) {
    nlohmann::json providers = nlohmann::json::array();
    for (const auto &provider : config.asr.providers) {
      nlohmann::json entry = {
          {"id", AsrProviderId(provider)},
          {"type", std::string(AsrProviderType(provider))},
          {"active", AsrProviderId(provider) == config.asr.activeProvider},
          {"timeout_ms", AsrProviderTimeoutMs(provider)},
      };

      if (const auto *local = std::get_if<LocalAsrProvider>(&provider)) {
        entry["model"] = local->model;
        entry["hotwords_file"] = local->hotwordsFile;
      } else if (const auto *command = std::get_if<CommandAsrProvider>(&provider)) {
        entry["command"] = command->command;
        entry["args"] = command->args;
        entry["env"] = command->env;
      }
      providers.push_back(std::move(entry));
    }
    fmt.PrintJson(providers);
    return 0;
  }

  std::vector<std::string> headers = {_("ID"), _("TYPE"), _("ACTIVE"),
                                      _("MODEL"), _("COMMAND"),
                                      _("TIMEOUT")};
  std::vector<std::vector<std::string>> rows;
  for (const auto &provider : config.asr.providers) {
    const std::string id = AsrProviderId(provider);
    const std::string type = std::string(AsrProviderType(provider));
    const std::string active =
        id == config.asr.activeProvider ? _("yes") : _("no");
    std::string model = "-";
    std::string command = "-";
    if (const auto *local = std::get_if<LocalAsrProvider>(&provider)) {
      model = local->model.empty() ? _("(not set)") : local->model;
    } else if (const auto *cmd = std::get_if<CommandAsrProvider>(&provider)) {
      command = CommandText(*cmd);
    }
    rows.push_back({id, type, active, model, command,
                    vinput::str::FmtStr("%d ms", AsrProviderTimeoutMs(provider))});
  }
  fmt.PrintTable(headers, rows);
  return 0;
}

int RunAsrConfigRemove(const std::string &id, Formatter &fmt,
                       const CliContext &ctx) {
  (void)ctx;
  CoreConfig config = LoadCoreConfig();
  auto &providers = config.asr.providers;
  auto it = std::find_if(providers.begin(), providers.end(),
                         [&id](const AsrProvider &provider) {
                           return AsrProviderId(provider) == id;
                         });
  if (it == providers.end()) {
    fmt.PrintError(vinput::str::FmtStr(_("ASR provider '%s' not found."), id));
    return 1;
  }

  providers.erase(it);
  if (config.asr.activeProvider == id) {
    config.asr.activeProvider.clear();
  }

  if (!SaveConfigOrFail(config, fmt)) {
    return 1;
  }
  fmt.PrintSuccess(vinput::str::FmtStr(_("ASR provider '%s' removed."), id));
  return 0;
}

int RunAsrConfigUse(const std::string &id, Formatter &fmt,
                    const CliContext &ctx) {
  (void)ctx;
  CoreConfig config = LoadCoreConfig();
  if (ResolveAsrProvider(config, id) == nullptr) {
    fmt.PrintError(vinput::str::FmtStr(_("ASR provider '%s' not found."), id));
    return 1;
  }

  config.asr.activeProvider = id;
  if (!SaveConfigOrFail(config, fmt)) {
    return 1;
  }
  fmt.PrintSuccess(vinput::str::FmtStr(_("Active ASR provider set to '%s'."), id));
  return 0;
}

int RunAsrConfigListModels(Formatter &fmt, const CliContext &ctx) {
  CoreConfig config = LoadCoreConfig();
  ModelManager manager(ResolveModelBaseDir(config).string());
  const std::string activeModel = ResolvePreferredLocalModel(config);
  const auto models = manager.ListDetailed(activeModel);

  if (ctx.json_output) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto &model : models) {
      std::string status = "installed";
      if (model.state == ModelState::Active) {
        status = "active";
      } else if (model.state == ModelState::Broken) {
        status = "broken";
      }
      arr.push_back({
          {"id", model.name},
          {"model_type", model.model_type},
          {"language", model.language},
          {"supports_hotwords", model.supports_hotwords},
          {"size_bytes", model.size_bytes},
          {"size", vinput::str::FormatSize(model.size_bytes)},
          {"status", status},
      });
    }
    fmt.PrintJson(arr);
    return 0;
  }

  std::vector<std::string> headers = {_("ID"), _("TYPE"), _("LANGUAGE"),
                                      _("SIZE"), _("HOTWORDS"), _("STATUS")};
  std::vector<std::vector<std::string>> rows;
  for (const auto &model : models) {
    std::string status = _("Installed");
    if (model.state == ModelState::Active) {
      status = std::string("[*] ") + _("Active");
    } else if (model.state == ModelState::Broken) {
      status = std::string("[!] ") + _("Broken");
    }
    rows.push_back({model.name, model.model_type, model.language,
                    vinput::str::FormatSize(model.size_bytes),
                    model.supports_hotwords ? _("yes") : _("no"), status});
  }
  fmt.PrintTable(headers, rows);
  return 0;
}

int RunAsrConfigUseModel(const std::string &id, Formatter &fmt,
                         const CliContext &ctx) {
  (void)ctx;
  CoreConfig config = LoadCoreConfig();
  ModelManager manager(ResolveModelBaseDir(config).string());

  std::string error;
  if (!manager.Validate(id, &error)) {
    fmt.PrintError(vinput::str::FmtStr(_("Model '%s' is not valid: %s"), id, error));
    return 1;
  }
  if (!SetPreferredLocalModel(&config, id, &error)) {
    fmt.PrintError(error);
    return 1;
  }
  if (!SaveConfigOrFail(config, fmt)) {
    return 1;
  }

  fmt.PrintSuccess(vinput::str::FmtStr(_("Active local model set to '%s'."), id));
  return 0;
}

int RunAsrConfigModelInfo(const std::string &id, Formatter &fmt,
                          const CliContext &ctx) {
  ModelManager manager("", id);
  std::string error;
  if (!manager.Validate(id, &error)) {
    fmt.PrintError(vinput::str::FmtStr(_("Model '%s' is not valid: %s"), id, error));
    return 1;
  }

  const ModelInfo info = manager.GetModelInfo(&error);
  if (!error.empty()) {
    fmt.PrintError(error);
    return 1;
  }

  nlohmann::json obj;
  obj["id"] = id;
  obj["model_type"] = info.model_type;
  obj["files"] = info.files;
  obj["params"] = info.params;

  if (ctx.json_output) {
    fmt.PrintJson(obj);
    return 0;
  }

  std::vector<std::string> headers = {_("FIELD"), _("VALUE")};
  std::vector<std::vector<std::string>> rows = {
      {"id", id},
      {"model_type", info.model_type},
  };
  for (const auto &[key, value] : info.params) {
    rows.push_back({std::string("param.") + key, value});
  }
  for (const auto &[key, value] : info.files) {
    rows.push_back({std::string("file.") + key, value});
  }
  fmt.PrintTable(headers, rows);
  return 0;
}

int RunAsrConfigGetHotword(Formatter &fmt, const CliContext &ctx) {
  CoreConfig config = LoadCoreConfig();
  const auto *provider = PreferredLocalProvider(config);
  const std::string hotwordPath = provider ? provider->hotwordsFile : "";

  if (ctx.json_output) {
    fmt.PrintJson({{"hotwords_file", hotwordPath}});
    return 0;
  }

  if (hotwordPath.empty()) {
    fmt.PrintInfo(_("No hotwords file configured."));
  } else {
    fmt.PrintInfo(hotwordPath);
  }
  return 0;
}

int RunAsrConfigSetHotword(const std::string &path, Formatter &fmt,
                           const CliContext &ctx) {
  (void)ctx;
  CoreConfig config = LoadCoreConfig();
  auto *provider = PreferredLocalProvider(&config);
  if (!provider) {
    fmt.PrintError(_("No local ASR provider configured."));
    return 1;
  }
  provider->hotwordsFile = path;
  if (!SaveConfigOrFail(config, fmt)) {
    return 1;
  }
  fmt.PrintSuccess(_("Hotwords file path saved."));
  return 0;
}

int RunAsrConfigClearHotword(Formatter &fmt, const CliContext &ctx) {
  (void)ctx;
  CoreConfig config = LoadCoreConfig();
  auto *provider = PreferredLocalProvider(&config);
  if (!provider) {
    fmt.PrintError(_("No local ASR provider configured."));
    return 1;
  }
  provider->hotwordsFile.clear();
  if (!SaveConfigOrFail(config, fmt)) {
    return 1;
  }
  fmt.PrintSuccess(_("Hotwords file path cleared."));
  return 0;
}

int RunAsrConfigEditHotword(Formatter &fmt, const CliContext &ctx) {
  (void)ctx;
  CoreConfig config = LoadCoreConfig();
  const auto *provider = PreferredLocalProvider(config);
  const std::string hotwordPath = provider ? provider->hotwordsFile : "";
  if (hotwordPath.empty()) {
    fmt.PrintError(_("No hotwords file configured. Use 'asr set-hotword <path>' first."));
    return 1;
  }

  const int result = OpenInEditor(hotwordPath);
  if (result != 0) {
    fmt.PrintError(_("Editor exited with error."));
    return result;
  }
  fmt.PrintSuccess(_("Hotwords file updated."));
  return 0;
}
