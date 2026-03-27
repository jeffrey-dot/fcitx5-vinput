#include "cli/command_hotword.h"
#include "cli/cli_helpers.h"
#include "cli/editor_utils.h"
#include "common/i18n.h"
#include "common/core_config.h"

namespace {

const AsrProvider *PreferredLocalProvider(const CoreConfig &config) {
  return ResolvePreferredLocalAsrProvider(config);
}

AsrProvider *PreferredLocalProvider(CoreConfig *config) {
  if (!config) {
    return nullptr;
  }
  const auto *provider = ResolvePreferredLocalAsrProvider(*config);
  if (!provider) {
    return nullptr;
  }
  for (auto &candidate : config->asr.providers) {
    if (candidate.name == provider->name) {
      return &candidate;
    }
  }
  return nullptr;
}

} // namespace

int RunHotwordGet(Formatter &fmt, const CliContext &ctx) {
  (void)ctx;
  CoreConfig config = LoadCoreConfig();
  const auto *provider = PreferredLocalProvider(config);
  const std::string hotwords_file = provider ? provider->hotwordsFile : "";

  if (ctx.json_output) {
    nlohmann::json obj;
    obj["hotwords_file"] = hotwords_file;
    fmt.PrintJson(obj);
    return 0;
  }

  if (hotwords_file.empty()) {
    fmt.PrintInfo(_("No hotwords file configured."));
  } else {
    fmt.PrintInfo(hotwords_file.c_str());
  }
  return 0;
}

int RunHotwordSet(const std::string &file_path, Formatter &fmt,
                   const CliContext &ctx) {
  (void)ctx;
  CoreConfig config = LoadCoreConfig();
  auto *provider = PreferredLocalProvider(&config);
  if (!provider) {
    fmt.PrintError(_("No local ASR provider configured."));
    return 1;
  }
  provider->hotwordsFile = file_path;
  if (!SaveConfigOrFail(config, fmt)) return 1;
  fmt.PrintSuccess(_("Hotwords file path saved."));
  return 0;
}

int RunHotwordClear(Formatter &fmt, const CliContext &ctx) {
  (void)ctx;
  CoreConfig config = LoadCoreConfig();
  auto *provider = PreferredLocalProvider(&config);
  if (!provider) {
    fmt.PrintError(_("No local ASR provider configured."));
    return 1;
  }
  provider->hotwordsFile.clear();
  if (!SaveConfigOrFail(config, fmt)) return 1;
  fmt.PrintSuccess(_("Hotwords file path cleared."));
  return 0;
}

int RunHotwordEdit(Formatter &fmt, const CliContext &ctx) {
  (void)ctx;
  CoreConfig config = LoadCoreConfig();
  const auto *provider = PreferredLocalProvider(config);
  const std::string hotwords_file = provider ? provider->hotwordsFile : "";

  if (hotwords_file.empty()) {
    fmt.PrintError(_("No hotwords file configured. Use 'hotword set <path>' first."));
    return 1;
  }

  int ret = OpenInEditor(hotwords_file);
  if (ret != 0) {
    fmt.PrintError(_("Editor exited with error."));
    return ret;
  }

  fmt.PrintSuccess(_("Hotwords file updated."));
  return 0;
}
