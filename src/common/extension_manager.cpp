#include "common/extension_manager.h"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <fstream>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

#include "config.h"
#include "common/path_utils.h"

namespace vinput::extension {

namespace fs = std::filesystem;

namespace {

struct SearchRoot {
  fs::path path;
  Source source = Source::kBuiltin;
  Type type = Type::kUnknown;
};

std::string MakeKey(std::string_view id, Type type) {
  return std::string(TypeToString(type)) + ":" + std::string(id);
}

Type ParseType(std::string_view text) {
  if (text == "asr") {
    return Type::kAsr;
  }
  if (text == "llm") {
    return Type::kLlm;
  }
  return Type::kUnknown;
}

fs::path BuiltinExtensionDir() {
  const fs::path installed = VINPUT_EXTENSION_INSTALL_DIR;
  std::error_code ec;
  if (fs::exists(installed, ec) && !ec) {
    return installed;
  }
  return fs::path(VINPUT_EXTENSION_SOURCE_DIR);
}

std::vector<SearchRoot> BuildSearchRoots() {
  const fs::path builtin_root = BuiltinExtensionDir();
  const fs::path user_root = vinput::path::UserExtensionDir();
  return {
      {builtin_root / "asr", Source::kBuiltin, Type::kAsr},
      {builtin_root / "llm", Source::kBuiltin, Type::kLlm},
      {user_root / "asr", Source::kUser, Type::kAsr},
      {user_root / "llm", Source::kUser, Type::kLlm},
  };
}

bool IsExecutableFile(const fs::path &path) {
  std::error_code ec;
  const auto status = fs::status(path, ec);
  if (ec || status.type() != fs::file_type::regular) {
    return false;
  }
  const auto perms = status.permissions();
  using perms_t = fs::perms;
  return (perms & perms_t::owner_exec) != perms_t::none ||
         (perms & perms_t::group_exec) != perms_t::none ||
         (perms & perms_t::others_exec) != perms_t::none;
}

bool ParseMetadataBlock(const fs::path &path, Type fallback_type, Source source,
                        Info *info) {
  if (!info) {
    return false;
  }

  std::ifstream file(path);
  if (!file.is_open()) {
    return false;
  }

  bool in_block = false;
  bool found_block = false;
  std::string line;
  const std::regex field_re(R"(^\s*#\s*@(\w+)\s+(.+?)\s*$)");

  info->id = path.stem().string();
  info->name = info->id;
  info->type = fallback_type;
  info->source = source;
  info->path = path;
  info->executable = IsExecutableFile(path);

  while (std::getline(file, line)) {
    if (!in_block) {
      if (line == "# ==vinput-extension==") {
        in_block = true;
        found_block = true;
      }
      continue;
    }

    if (line == "# ==/vinput-extension==") {
      break;
    }

    std::smatch match;
    if (!std::regex_match(line, match, field_re)) {
      continue;
    }

    const std::string key = match[1].str();
    const std::string value = match[2].str();
    if (key == "name") {
      info->name = value;
    } else if (key == "type") {
      info->type = ParseType(value);
    } else if (key == "description") {
      info->description = value;
    } else if (key == "author") {
      info->author = value;
    } else if (key == "version") {
      info->version = value;
    } else if (key == "env") {
      info->env_entries.push_back(value);
    }
  }

  return found_block && info->type != Type::kUnknown;
}

pid_t ReadPid(const Info &info) {
  std::ifstream file(vinput::path::ExtensionRuntimeDir() / (info.id + ".pid"));
  pid_t pid = -1;
  file >> pid;
  return pid;
}

bool ProcessExists(pid_t pid) {
  if (pid <= 0) {
    return false;
  }
  return kill(pid, 0) == 0 || errno == EPERM;
}

std::string ReadLastLogLine(const Info &info) {
  std::ifstream file(LogPath(info));
  if (!file.is_open()) {
    return {};
  }
  std::string line;
  std::string last;
  while (std::getline(file, line)) {
    if (!line.empty()) {
      last = line;
    }
  }
  return last;
}

}  // namespace

std::string TypeToString(Type type) {
  switch (type) {
  case Type::kAsr:
    return "asr";
  case Type::kLlm:
    return "llm";
  default:
    return "unknown";
  }
}

std::string SourceToString(Source source) {
  switch (source) {
  case Source::kBuiltin:
    return "builtin";
  case Source::kUser:
    return "user";
  default:
    return "unknown";
  }
}

std::vector<Info> Discover(std::string *error) {
  std::vector<Info> results;
  std::set<std::string> seen_keys;

  for (const auto &root : BuildSearchRoots()) {
    std::error_code ec;
    if (!fs::exists(root.path, ec) || ec || !fs::is_directory(root.path, ec)) {
      continue;
    }

    for (const auto &entry : fs::directory_iterator(root.path, ec)) {
      if (ec) {
        if (error) {
          *error = "failed to scan extension directory: " + root.path.string();
        }
        return {};
      }
      if (!entry.is_regular_file()) {
        continue;
      }

      Info info;
      if (!ParseMetadataBlock(entry.path(), root.type, root.source, &info)) {
        continue;
      }

      const std::string key = MakeKey(info.id, info.type);
      if (root.source == Source::kUser) {
        auto it = std::find_if(results.begin(), results.end(),
                               [&](const Info &existing) {
                                 return MakeKey(existing.id, existing.type) ==
                                        key;
                               });
        if (it != results.end()) {
          *it = std::move(info);
          continue;
        }
      } else if (seen_keys.count(key) > 0) {
        continue;
      }

      seen_keys.insert(key);
      results.push_back(std::move(info));
    }
  }

  if (error) {
    error->clear();
  }
  std::sort(results.begin(), results.end(),
            [](const Info &a, const Info &b) {
              if (a.type != b.type) {
                return TypeToString(a.type) < TypeToString(b.type);
              }
              return a.id < b.id;
            });
  return results;
}

std::optional<Info> FindById(std::string_view id, std::optional<Type> type,
                             std::string *error) {
  const auto extensions = Discover(error);
  if (error && !error->empty()) {
    return std::nullopt;
  }

  std::optional<Info> found;
  for (const auto &info : extensions) {
    if (info.id != id) {
      continue;
    }
    if (type.has_value() && info.type != *type) {
      continue;
    }
    found = info;
    break;
  }

  if (!found && error) {
    *error = "extension not found: " + std::string(id);
  }
  return found;
}

std::optional<fs::path> ResolveCommandPath(std::string_view command,
                                           std::optional<Type> type,
                                           std::string *error) {
  if (command.empty()) {
    if (error) {
      *error = "extension command is empty";
    }
    return std::nullopt;
  }

  const auto info = FindById(command, type, error);
  if (!info.has_value()) {
    return std::nullopt;
  }
  if (!info->executable) {
    if (error) {
      *error = "extension is not executable: " + info->path.string();
    }
    return std::nullopt;
  }
  if (error) {
    error->clear();
  }
  return info->path;
}

bool IsRunning(const Info &info) {
  if (info.type != Type::kLlm) {
    return false;
  }
  const pid_t pid = ReadPid(info);
  return ProcessExists(pid);
}

fs::path LogPath(const Info &info) {
  return vinput::path::ExtensionRuntimeDir() / (info.id + ".log");
}

bool Start(const Info &info, std::string *error) {
  if (info.type != Type::kLlm) {
    if (error) {
      *error = "only llm extensions support start/stop";
    }
    return false;
  }
  if (!info.executable) {
    if (error) {
      *error = "extension is not executable: " + info.path.string();
    }
    return false;
  }
  if (IsRunning(info)) {
    if (error) {
      *error = "extension is already running: " + info.id;
    }
    return false;
  }

  std::error_code ec;
  const fs::path runtime_dir = vinput::path::ExtensionRuntimeDir();
  fs::create_directories(runtime_dir, ec);
  if (ec) {
    if (error) {
      *error = "failed to create runtime directory: " + ec.message();
    }
    return false;
  }

  const fs::path log_path = LogPath(info);
  int log_fd = open(log_path.c_str(), O_CREAT | O_WRONLY | O_APPEND, 0644);
  if (log_fd < 0) {
    if (error) {
      *error = "failed to open log file: " + log_path.string();
    }
    return false;
  }

  pid_t pid = fork();
  if (pid < 0) {
    close(log_fd);
    if (error) {
      *error = "fork failed";
    }
    return false;
  }

  if (pid == 0) {
    const int devnull = open("/dev/null", O_RDONLY);
    if (devnull >= 0) {
      dup2(devnull, STDIN_FILENO);
      close(devnull);
    }
    dup2(log_fd, STDOUT_FILENO);
    dup2(log_fd, STDERR_FILENO);
    close(log_fd);
    setsid();
    chdir(info.path.parent_path().c_str());
    execl(info.path.c_str(), info.path.c_str(), static_cast<char *>(nullptr));
    _exit(127);
  }

  close(log_fd);
  usleep(250000);
  int status = 0;
  const pid_t wait_result = waitpid(pid, &status, WNOHANG);
  if (wait_result == pid) {
    if (error) {
      std::string message = "extension exited immediately";
      const std::string detail = ReadLastLogLine(info);
      if (!detail.empty()) {
        message += ": " + detail;
      }
      *error = message;
    }
    return false;
  }

  std::ofstream pid_file(runtime_dir / (info.id + ".pid"),
                         std::ios::out | std::ios::trunc);
  pid_file << pid;
  if (error) {
    error->clear();
  }
  return true;
}

bool Stop(const Info &info, std::string *error) {
  if (info.type != Type::kLlm) {
    if (error) {
      *error = "only llm extensions support start/stop";
    }
    return false;
  }

  const fs::path pid_path = vinput::path::ExtensionRuntimeDir() / (info.id + ".pid");
  const pid_t pid = ReadPid(info);
  if (!ProcessExists(pid)) {
    std::error_code rm_ec;
    fs::remove(pid_path, rm_ec);
    if (error) {
      *error = "extension is not running: " + info.id;
    }
    return false;
  }

  kill(pid, SIGTERM);
  for (int i = 0; i < 20; ++i) {
    if (!ProcessExists(pid)) {
      std::error_code rm_ec;
      fs::remove(pid_path, rm_ec);
      if (error) {
        error->clear();
      }
      return true;
    }
    usleep(100000);
  }

  kill(pid, SIGKILL);
  std::error_code rm_ec;
  fs::remove(pid_path, rm_ec);
  if (error) {
    error->clear();
  }
  return true;
}

}  // namespace vinput::extension
