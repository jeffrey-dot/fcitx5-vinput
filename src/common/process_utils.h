#pragma once

#include <cstddef>
#include <map>
#include <span>
#include <string>
#include <vector>

#include "common/asr_defaults.h"

namespace vinput::process {

struct CommandSpec {
  std::string command;
  std::vector<std::string> args;
  std::map<std::string, std::string> env;
  int timeout_ms = vinput::asr::kDefaultProviderTimeoutMs;
};

struct CommandResult {
  int exit_code = -1;
  bool timed_out = false;
  bool launch_failed = false;
  std::string stdout_text;
  std::string stderr_text;
};

CommandResult RunCommandWithInput(const CommandSpec &spec,
                                  std::span<const std::byte> input);

}  // namespace vinput::process
