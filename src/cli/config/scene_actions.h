#pragma once

#include <string>

#include "cli/utils/cli_context.h"
#include "cli/utils/formatter.h"

int RunSceneConfigList(Formatter &fmt, const CliContext &ctx);
int RunSceneConfigAdd(const std::string &id, const std::string &label,
                      const std::string &prompt,
                      const std::string &provider_id,
                      const std::string &model, int candidate_count,
                      int timeout_ms, Formatter &fmt,
                      const CliContext &ctx);
int RunSceneConfigUse(const std::string &id, Formatter &fmt,
                      const CliContext &ctx);
int RunSceneConfigRemove(const std::string &id, bool force, Formatter &fmt,
                         const CliContext &ctx);
