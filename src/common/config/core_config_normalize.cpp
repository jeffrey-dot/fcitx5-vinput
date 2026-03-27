#include "common/config/core_config.h"

#include <iostream>
#include <iterator>
#include <set>
#include <utility>

namespace {

vinput::scene::Definition MakeBuiltinScene(std::string_view id) {
  vinput::scene::Definition scene;
  scene.id = std::string(id);
  scene.builtin = true;
  if (id == vinput::scene::kRawSceneId) {
    scene.label = std::string(vinput::scene::kRawSceneLabelKey);
    scene.candidate_count = 0;
  } else if (id == vinput::scene::kCommandSceneId) {
    scene.label = std::string(vinput::scene::kCommandSceneLabelKey);
  }
  vinput::scene::NormalizeDefinition(&scene);
  return scene;
}

template <typename T>
void EraseEmptyEnvKeys(T *entry) {
  for (auto it = entry->env.begin(); it != entry->env.end();) {
    it = it->first.empty() ? entry->env.erase(it) : std::next(it);
  }
}

} // namespace

void NormalizeCoreConfig(CoreConfig *config) {
  if (!config) {
    return;
  }

  {
    std::set<std::string> seen;
    std::vector<std::string> normalized;
    for (const auto &url : config->registry.baseUrls) {
      if (!url.empty() && seen.insert(url).second) {
        normalized.push_back(url);
      }
    }
    config->registry.baseUrls = std::move(normalized);
  }

  {
    std::set<std::string> seen;
    std::vector<LlmProvider> normalized;
    for (auto provider : config->llm.providers) {
      if (provider.id.empty()) {
        std::cerr << "Ignoring LLM provider with empty id\n";
        continue;
      }
      if (!seen.insert(provider.id).second) {
        std::cerr << "Ignoring duplicate LLM provider '" << provider.id << "'\n";
        continue;
      }
      normalized.push_back(std::move(provider));
    }
    config->llm.providers = std::move(normalized);
  }

  {
    std::set<std::string> seen;
    std::vector<LlmAdaptor> normalized;
    for (auto adaptor : config->llm.adaptors) {
      if (adaptor.id.empty()) {
        std::cerr << "Ignoring LLM adaptor with empty id\n";
        continue;
      }
      if (!seen.insert(adaptor.id).second) {
        std::cerr << "Ignoring duplicate LLM adaptor '" << adaptor.id << "'\n";
        continue;
      }
      EraseEmptyEnvKeys(&adaptor);
      normalized.push_back(std::move(adaptor));
    }
    config->llm.adaptors = std::move(normalized);
  }

  {
    std::set<std::string> seen;
    std::vector<AsrProvider> normalized;
    for (auto provider : config->asr.providers) {
      const std::string &id = AsrProviderId(provider);
      if (id.empty()) {
        std::cerr << "Ignoring ASR provider with empty id\n";
        continue;
      }
      if (!seen.insert(id).second) {
        std::cerr << "Ignoring duplicate ASR provider '" << id << "'\n";
        continue;
      }
      if (auto *cmd = std::get_if<CommandAsrProvider>(&provider)) {
        if (cmd->command.empty()) {
          std::cerr << "Ignoring command ASR provider '" << id
                    << "' with empty command\n";
          continue;
        }
        EraseEmptyEnvKeys(cmd);
      }
      normalized.push_back(std::move(provider));
    }
    config->asr.providers = std::move(normalized);
  }

  {
    std::set<std::string> seen;
    std::vector<vinput::scene::Definition> normalized;
    for (auto scene : config->scenes.definitions) {
      vinput::scene::NormalizeDefinition(&scene);
      std::string error;
      if (!vinput::scene::ValidateDefinition(scene, &error)) {
        std::cerr << "Ignoring invalid scene '" << scene.id << "': " << error << "\n";
        continue;
      }
      if (!seen.insert(scene.id).second) {
        std::cerr << "Ignoring duplicate scene id '" << scene.id << "'\n";
        continue;
      }
      normalized.push_back(std::move(scene));
    }

    if (!seen.count(std::string(vinput::scene::kRawSceneId))) {
      normalized.push_back(MakeBuiltinScene(vinput::scene::kRawSceneId));
      seen.insert(std::string(vinput::scene::kRawSceneId));
    }
    if (!seen.count(std::string(vinput::scene::kCommandSceneId))) {
      normalized.push_back(MakeBuiltinScene(vinput::scene::kCommandSceneId));
      seen.insert(std::string(vinput::scene::kCommandSceneId));
    }

    config->scenes.definitions = std::move(normalized);
  }

  if (!config->asr.activeProvider.empty() &&
      ResolveAsrProvider(*config, config->asr.activeProvider) == nullptr) {
    config->asr.activeProvider.clear();
  }

  if (!config->scenes.activeScene.empty() &&
      vinput::scene::Find({config->scenes.activeScene, config->scenes.definitions},
                          config->scenes.activeScene) == nullptr) {
    config->scenes.activeScene.clear();
  }
}
