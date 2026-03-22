#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vinput::extension {

enum class Type {
  kAsr,
  kLlm,
  kUnknown,
};

enum class Source {
  kBuiltin,
  kUser,
};

struct Info {
  std::string id;
  std::string name;
  std::string description;
  std::string author;
  std::string version;
  std::vector<std::string> env_entries;
  Type type = Type::kUnknown;
  Source source = Source::kBuiltin;
  std::filesystem::path path;
  bool executable = false;
};

std::string TypeToString(Type type);
std::string SourceToString(Source source);

std::vector<Info> Discover(std::string *error);
std::optional<Info> FindById(std::string_view id, std::optional<Type> type,
                             std::string *error);
std::optional<std::filesystem::path> ResolveCommandPath(
    std::string_view command, std::optional<Type> type, std::string *error);

bool IsRunning(const Info &info);
bool Start(const Info &info, std::string *error);
bool Stop(const Info &info, std::string *error);
std::filesystem::path LogPath(const Info &info);

}  // namespace vinput::extension
