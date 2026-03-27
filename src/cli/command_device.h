#pragma once

#include "cli/utils/cli_context.h"
#include "cli/utils/formatter.h"
#include <string>

int RunDeviceList(Formatter &fmt, const CliContext &ctx);
int RunDeviceUse(const std::string &name, Formatter &fmt,
                 const CliContext &ctx);
