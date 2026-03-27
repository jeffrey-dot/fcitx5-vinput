#pragma once

#include <string>

#include "cli/utils/cli_context.h"
#include "cli/utils/formatter.h"

int RunLlmConfigList(Formatter &fmt, const CliContext &ctx);
int RunLlmConfigAdd(const std::string &id, const std::string &baseUrl,
                    const std::string &apiKey, Formatter &fmt,
                    const CliContext &ctx);
int RunLlmConfigRemove(const std::string &id, Formatter &fmt,
                       const CliContext &ctx);
