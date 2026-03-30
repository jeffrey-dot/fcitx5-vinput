#include "common/config/core_config.h"

#include <set>
#include <string_view>

#include "common/utils/path_utils.h"

const LlmProvider *ResolveLlmProvider(const CoreConfig &config,
                                      const std::string &provider_id) {
  if (provider_id.empty()) {
    return nullptr;
  }
  for (const auto &provider : config.llm.providers) {
    if (provider.id == provider_id) {
      return &provider;
    }
  }
  return nullptr;
}

const LlmAdapter *ResolveLlmAdapter(const CoreConfig &config,
                                    const std::string &adapter_id) {
  if (adapter_id.empty()) {
    return nullptr;
  }
  for (const auto &adapter : config.llm.adapters) {
    if (adapter.id == adapter_id) {
      return &adapter;
    }
  }
  return nullptr;
}

const AsrProvider *ResolveAsrProvider(const CoreConfig &config,
                                      const std::string &provider_id) {
  if (provider_id.empty()) {
    return nullptr;
  }
  for (const auto &provider : config.asr.providers) {
    if (AsrProviderId(provider) == provider_id) {
      return &provider;
    }
  }
  return nullptr;
}

const AsrProvider *ResolveActiveAsrProvider(const CoreConfig &config) {
  return ResolveAsrProvider(config, config.asr.activeProvider);
}

const AsrProvider *ResolveActiveLocalAsrProvider(const CoreConfig &config) {
  const AsrProvider *provider = ResolveActiveAsrProvider(config);
  if (!provider || !std::holds_alternative<LocalAsrProvider>(*provider)) {
    return nullptr;
  }
  return provider;
}

const AsrProvider *ResolvePreferredLocalAsrProvider(const CoreConfig &config) {
  if (const AsrProvider *provider = ResolveActiveLocalAsrProvider(config)) {
    return provider;
  }
  for (const auto &provider : config.asr.providers) {
    if (std::holds_alternative<LocalAsrProvider>(provider)) {
      return &provider;
    }
  }
  return nullptr;
}

std::string ResolveActiveLocalModel(const CoreConfig &config) {
  const AsrProvider *provider = ResolveActiveLocalAsrProvider(config);
  if (!provider) {
    return {};
  }
  return std::get<LocalAsrProvider>(*provider).model;
}

std::string ResolvePreferredLocalModel(const CoreConfig &config) {
  const AsrProvider *provider = ResolvePreferredLocalAsrProvider(config);
  if (!provider) {
    return {};
  }
  return std::get<LocalAsrProvider>(*provider).model;
}

namespace {

std::vector<std::string> ResolveRegistryUrlsForPath(
    const CoreConfig &config, std::string_view suffix) {
  std::vector<std::string> urls;
  std::set<std::string> seen;
  for (const auto &baseUrl : config.registry.baseUrls) {
    if (baseUrl.empty()) {
      continue;
    }
    std::string url = baseUrl;
    while (!url.empty() && url.back() == '/') {
      url.pop_back();
    }
    url += "/";
    url += suffix;
    if (seen.insert(url).second) {
      urls.push_back(std::move(url));
    }
  }
  return urls;
}

} // namespace

std::vector<std::string> ResolveModelRegistryUrls(const CoreConfig &config) {
  return ResolveRegistryUrlsForPath(config, "registry/models.json");
}

std::vector<std::string> ResolveAsrProviderRegistryUrls(const CoreConfig &config) {
  return ResolveRegistryUrlsForPath(config, "registry/providers.json");
}

std::vector<std::string> ResolveLlmAdapterRegistryUrls(const CoreConfig &config) {
  return ResolveRegistryUrlsForPath(config, "registry/adapters.json");
}

std::vector<std::string> ResolveRegistryI18nUrls(const CoreConfig &config,
                                                 const std::string &locale) {
  return ResolveRegistryUrlsForPath(config, "i18n/" + locale + ".json");
}

bool SetPreferredLocalModel(CoreConfig *config, const std::string &model,
                            std::string *error) {
  if (!config) {
    if (error) {
      *error = "Config is null.";
    }
    return false;
  }

  const AsrProvider *provider = ResolvePreferredLocalAsrProvider(*config);
  if (!provider) {
    if (error) {
      *error = "No local ASR provider configured.";
    }
    return false;
  }

  const std::string providerId = AsrProviderId(*provider);
  for (auto &candidate : config->asr.providers) {
    if (AsrProviderId(candidate) == providerId) {
      if (auto *local = std::get_if<LocalAsrProvider>(&candidate)) {
        local->model = model;
        return true;
      }
    }
  }

  if (error) {
    *error = "Local ASR provider not found.";
  }
  return false;
}

const vinput::scene::Definition *FindCommandScene(const CoreConfig &config) {
  for (const auto &scene : config.scenes.definitions) {
    if (scene.id == vinput::scene::kCommandSceneId) {
      return &scene;
    }
  }
  return nullptr;
}

std::filesystem::path ResolveModelBaseDir(const CoreConfig &) {
  return vinput::path::DefaultModelBaseDir();
}
