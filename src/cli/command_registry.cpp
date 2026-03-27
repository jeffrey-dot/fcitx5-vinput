#include "cli/command_registry.h"

#include <filesystem>
#include <nlohmann/json.hpp>

#include "common/config/core_config.h"
#include "common/i18n.h"
#include "common/utils/string_utils.h"
#include "common/utils/path_utils.h"
#include "common/registry_cache.h"
#include "common/registry_i18n.h"
#include "common/script_resource.h"
#include "common/model_repository.h"

namespace {

nlohmann::json CacheStatusObject(const std::filesystem::path &path) {
  std::error_code ec;
  const bool exists = std::filesystem::exists(path, ec) && !ec;
  nlohmann::json obj = {{"path", path.string()}, {"exists", exists}};
  if (exists) {
    obj["size_bytes"] = std::filesystem::file_size(path, ec);
  }
  return obj;
}

} // namespace

int RunRegistryStatus(Formatter &fmt, const CliContext &ctx) {
  auto config = LoadCoreConfig();
  NormalizeCoreConfig(&config);

  nlohmann::json i18n = nlohmann::json::array();
  const auto preferred_locale = vinput::registry::DetectPreferredLocale();
  for (const auto &locale :
       std::vector<std::string>{preferred_locale, std::string("en_US")}) {
    auto obj = CacheStatusObject(vinput::registry::cache::I18nPath(locale));
    obj["locale"] = locale;
    i18n.push_back(std::move(obj));
  }

  nlohmann::json status = {
      {"cache_dir", vinput::path::RegistryCacheDir().string()},
      {"models", CacheStatusObject(vinput::registry::cache::ModelRegistryPath())},
      {"asr_providers", CacheStatusObject(vinput::registry::cache::AsrProviderRegistryPath())},
      {"llm_adaptors", CacheStatusObject(vinput::registry::cache::LlmAdaptorRegistryPath())},
      {"i18n", i18n},
  };

  if (ctx.json_output) {
    fmt.PrintJson(status);
    return 0;
  }

  fmt.PrintKeyValue(_("Cache Dir"), vinput::path::RegistryCacheDir().string());

  std::vector<std::string> headers = {_("KIND"), _("STATUS"), _("SIZE"), _("PATH")};
  std::vector<std::vector<std::string>> rows;
  const auto add_row = [&rows](const std::string &label, const nlohmann::json &obj) {
    const bool exists = obj.value("exists", false);
    const auto size = exists ? vinput::str::FormatSize(obj.value("size_bytes", uint64_t{0}))
                             : "-";
    rows.push_back({label, exists ? _("cached") : _("missing"), size,
                    obj.value("path", "")});
  };
  add_row(_("models"), status.at("models"));
  add_row(_("asr_providers"), status.at("asr_providers"));
  add_row(_("llm_adaptors"), status.at("llm_adaptors"));
  for (const auto &item : status.at("i18n")) {
    add_row(std::string("i18n:") + item.value("locale", ""), item);
  }
  fmt.PrintTable(headers, rows);
  return 0;
}

int RunRegistrySync(Formatter &fmt, const CliContext &ctx) {
  auto config = LoadCoreConfig();
  NormalizeCoreConfig(&config);

  std::string error;

  ModelRepository repo(ResolveModelBaseDir(config).string());
  if (const auto urls = ResolveModelRegistryUrls(config); !urls.empty()) {
    repo.FetchRegistry(urls, &error);
    if (!error.empty()) {
      fmt.PrintError(error);
      return 1;
    }
  }

  if (const auto urls = ResolveAsrProviderRegistryUrls(config); !urls.empty()) {
    vinput::script::FetchRegistry(vinput::script::Kind::kAsrProvider, urls, &error);
    if (!error.empty()) {
      fmt.PrintError(error);
      return 1;
    }
  }

  if (const auto urls = ResolveLlmAdaptorRegistryUrls(config); !urls.empty()) {
    vinput::script::FetchRegistry(vinput::script::Kind::kLlmAdaptor, urls, &error);
    if (!error.empty()) {
      fmt.PrintError(error);
      return 1;
    }
  }

  const auto preferred_locale = vinput::registry::DetectPreferredLocale();
  for (const auto &locale :
       std::vector<std::string>{preferred_locale, std::string("en_US")}) {
    const auto urls = ResolveRegistryI18nUrls(config, locale);
    if (urls.empty()) {
      continue;
    }
    vinput::registry::FetchI18nMap(locale, urls, &error);
    if (!error.empty()) {
      fmt.PrintError(error);
      return 1;
    }
  }

  if (!ctx.json_output) {
    fmt.PrintSuccess(_("Registry cache synchronized."));
  } else {
    fmt.PrintJson({{"status", "ok"}});
  }
  return 0;
}
