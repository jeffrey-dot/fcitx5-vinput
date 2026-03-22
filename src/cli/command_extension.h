#pragma once

#include "cli/cli_context.h"
#include "cli/formatter.h"

#include <string>

int RunExtensionList(Formatter &fmt, const CliContext &ctx);
int RunExtensionStart(const std::string &name, Formatter &fmt,
                      const CliContext &ctx);
int RunExtensionStop(const std::string &name, Formatter &fmt,
                     const CliContext &ctx);
