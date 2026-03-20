#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "common/asr_defaults.h"
#include "common/postprocess_scene.h"

struct LlmProvider {
  std::string name;
  std::string base_url;
  std::string api_key;
};

struct AsrProvider {
  std::string name{vinput::asr::kDefaultProviderName};
  std::string type{vinput::asr::kBuiltinProviderType};
  std::string model{vinput::asr::kDefaultBuiltinModel};
  std::string command;
  std::vector<std::string> args;
  std::map<std::string, std::string> env;
  int timeoutMs{vinput::asr::kDefaultProviderTimeoutMs};
};

struct CoreConfig {
  std::string captureDevice{"default"};
  std::string modelBaseDir;
  std::string registryUrl{"https://raw.githubusercontent.com/xifan2333/vinput-models/main/registry.json"};

  std::string defaultLanguage{"zh"};

  std::string hotwordsFile;

  struct Llm {
    std::vector<LlmProvider> providers;
  } llm;

  struct Asr {
    std::string activeProvider{vinput::asr::kDefaultProviderName};
    bool normalizeAudio{true};
    struct Vad {
      bool enabled{true};
    } vad;
    std::vector<AsrProvider> providers;
  } asr;

  struct Scenes {
    std::string activeScene{std::string(vinput::scene::kRawSceneId)};
    std::vector<vinput::scene::Definition> definitions;
  } scenes;
};

// API Functions
CoreConfig LoadCoreConfig();
bool SaveCoreConfig(const CoreConfig &config);
std::string GetCoreConfigPath();

void NormalizeCoreConfig(CoreConfig *config);
const LlmProvider *ResolveLlmProvider(const CoreConfig &config,
                                      const std::string &provider_id);
const AsrProvider *ResolveAsrProvider(const CoreConfig &config,
                                      const std::string &provider_id);
const AsrProvider *ResolveActiveAsrProvider(const CoreConfig &config);
const AsrProvider *ResolveActiveBuiltinAsrProvider(const CoreConfig &config);
const AsrProvider *ResolvePreferredBuiltinAsrProvider(const CoreConfig &config);
std::string ResolveActiveBuiltinModel(const CoreConfig &config);
std::string ResolvePreferredBuiltinModel(const CoreConfig &config);
bool SetPreferredBuiltinModel(CoreConfig *config, const std::string &model,
                              std::string *error);
const vinput::scene::Definition *FindCommandScene(const CoreConfig &config);
std::filesystem::path ResolveModelBaseDir(const CoreConfig &config);
