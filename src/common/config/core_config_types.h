#pragma once

#include <map>
#include <string>
#include <variant>
#include <vector>

#include "common/scene/postprocess_scene.h"

namespace vinput::asr {

inline constexpr char kLocalProviderType[] = "local";
inline constexpr char kCommandProviderType[] = "command";

}  // namespace vinput::asr

struct LlmProvider {
  std::string id;
  std::string base_url;
  std::string api_key;
};

struct LlmAdaptor {
  std::string id;
  std::string command;
  std::vector<std::string> args;
  std::map<std::string, std::string> env;
};

struct AsrProviderBase {
  std::string id;
  int timeoutMs = 0;
};

struct LocalAsrProvider : AsrProviderBase {
  std::string model;
  std::string hotwordsFile;
};

struct CommandAsrProvider : AsrProviderBase {
  std::string command;
  std::vector<std::string> args;
  std::map<std::string, std::string> env;
};

using AsrProvider = std::variant<LocalAsrProvider, CommandAsrProvider>;

// Helper to get the id from any AsrProvider variant
inline const std::string &AsrProviderId(const AsrProvider &p) {
  return std::visit([](const AsrProviderBase &base) -> const std::string & {
    return base.id;
  }, p);
}

inline int AsrProviderTimeoutMs(const AsrProvider &p) {
  return std::visit([](const AsrProviderBase &base) { return base.timeoutMs; }, p);
}

inline std::string_view AsrProviderType(const AsrProvider &p) {
  if (std::holds_alternative<LocalAsrProvider>(p))
    return vinput::asr::kLocalProviderType;
  return vinput::asr::kCommandProviderType;
}

struct CoreConfig {
  int version = 0;
  struct Registry {
    std::vector<std::string> baseUrls;
  } registry;
  struct Global {
    std::string defaultLanguage;
    std::string captureDevice;
  } global;

  struct Llm {
    std::vector<LlmProvider> providers;
    std::vector<LlmAdaptor> adaptors;
  } llm;

  struct Asr {
    std::string activeProvider;
    bool normalizeAudio{true};
    struct Vad {
      bool enabled{true};
    } vad;
    std::vector<AsrProvider> providers;
  } asr;

  struct Scenes {
    std::string activeScene;
    std::vector<vinput::scene::Definition> definitions;
  } scenes;
};
