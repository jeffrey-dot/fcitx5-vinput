#include "common/registry/registry_fetch.h"

#include "common/config/core_config.h"
#include "common/registry/registry_cache.h"
#include "common/registry/registry_i18n.h"

namespace vinput::registry {

namespace {

bool RefreshLocaleCache(const CoreConfig &config, const std::string &locale,
                        std::string *error) {
  const auto urls = ResolveRegistryI18nUrls(config, locale);
  if (urls.empty()) {
    if (error) {
      *error = "no i18n URLs configured";
    }
    return false;
  }

  std::string content;
  vinput::download::Options options;
  options.timeout_seconds = 20;
  options.max_bytes = 1024 * 1024;
  return vinput::registry::cache::FetchText(
      urls, vinput::registry::cache::I18nPath(locale), options, &content,
      nullptr, error);
}

void RefreshI18nAfterOnlineFetch(const CoreConfig *config) {
  if (!config) {
    return;
  }

  const std::string preferred_locale = DetectPreferredLocale();
  std::string ignored_error;
  RefreshLocaleCache(*config, preferred_locale, &ignored_error);
  if (preferred_locale != "en_US") {
    RefreshLocaleCache(*config, "en_US", nullptr);
  }
}

} // namespace

bool FetchRegistryText(const CoreConfig *config,
                       const std::vector<std::string> &urls,
                       const std::filesystem::path &cache_path,
                       const vinput::download::Options &options,
                       std::string *content,
                       vinput::download::Result *result,
                       std::string *error) {
  vinput::download::Result local_result;
  const bool ok = vinput::registry::cache::FetchText(
      urls, cache_path, options, content, &local_result, error);
  if (!ok) {
    if (result) {
      *result = std::move(local_result);
    }
    return false;
  }

  if (local_result.resolved_url != cache_path.string()) {
    RefreshI18nAfterOnlineFetch(config);
  }

  if (result) {
    *result = std::move(local_result);
  }
  return true;
}

} // namespace vinput::registry
