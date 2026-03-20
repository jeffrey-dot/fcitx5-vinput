#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace vinput::scene {

constexpr int kMinCandidateCount = 0;
constexpr int kMaxCandidateCount = 9;
constexpr int kDefaultCandidateCount = 1;
constexpr int kDefaultTimeoutMs = 4000;
constexpr std::string_view kRawSceneId = "__raw__";
constexpr std::string_view kCommandSceneId = "__command__";
inline constexpr char kBuiltinCommandScenePrompt[] =
    "Execute the voice command on the given text. "
    "The command may contain speech recognition errors; infer the intent.";

struct Definition {
  std::string id;
  std::string label;
  std::string prompt;
  std::string provider_id;
  std::string model;
  int candidate_count = kDefaultCandidateCount;
  int timeout_ms = kDefaultTimeoutMs;
  bool builtin = false;
};

struct Config {
  std::string activeSceneId;
  std::vector<Definition> scenes;
};

int NormalizeCandidateCount(int candidate_count);
bool IsBuiltinSceneId(std::string_view scene_id);
void NormalizeDefinition(Definition *scene);
bool ValidateDefinition(const Definition &scene, std::string *error,
                        bool require_id = true);

const Definition *Find(const Config &config, std::string_view scene_id);
const Definition &Resolve(const Config &config, std::string_view scene_id);
std::string DisplayLabel(const Definition &scene);

bool AddScene(Config *config, const Definition &def, std::string *error);
bool UpdateScene(Config *config, const std::string &id, const Definition &def,
                 std::string *error);
bool RemoveScene(Config *config, const std::string &id, bool force,
                 std::string *error);
bool SetActiveScene(Config *config, const std::string &id, std::string *error);

} // namespace vinput::scene
