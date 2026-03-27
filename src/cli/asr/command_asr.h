#pragma once

#include "cli/utils/cli_context.h"
#include "cli/utils/formatter.h"

#include <string>
#include <vector>

int RunAsrList(Formatter &fmt, const CliContext &ctx);
int RunAsrListAvailable(Formatter &fmt, const CliContext &ctx);
int RunAsrAdd(const std::string &name, const std::string &type,
              const std::string &model, const std::string &command,
              const std::vector<std::string> &args,
              const std::vector<std::string> &env_entries, int timeout_ms,
              Formatter &fmt, const CliContext &ctx);
int RunAsrInstall(const std::string &id, Formatter &fmt, const CliContext &ctx);
int RunAsrRemove(const std::string &name, bool force, Formatter &fmt,
                 const CliContext &ctx);
int RunAsrUse(const std::string &name, Formatter &fmt, const CliContext &ctx);
int RunAsrEdit(const std::string &name, Formatter &fmt, const CliContext &ctx);
