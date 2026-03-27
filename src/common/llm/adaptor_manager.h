#pragma once

#include <filesystem>
#include <sys/types.h>
#include <string>
#include <string_view>

struct CoreConfig;
struct LlmAdaptor;
namespace vinput::process {
struct CommandSpec;
}

namespace vinput::adaptor {

vinput::process::CommandSpec BuildCommandSpec(const LlmAdaptor &adaptor);
std::filesystem::path ResolveWorkingDir(const LlmAdaptor &adaptor);
std::filesystem::path PidPath(std::string_view adaptor_id);
bool WritePidFile(std::string_view adaptor_id, pid_t pid, std::string *error);
void RemovePidFile(std::string_view adaptor_id);
bool IsRunning(std::string_view adaptor_id);
bool Stop(std::string_view adaptor_id, std::string *error);

}  // namespace vinput::adaptor
