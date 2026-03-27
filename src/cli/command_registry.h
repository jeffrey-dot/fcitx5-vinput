#pragma once

#include "cli/utils/cli_context.h"
#include "cli/utils/formatter.h"

int RunRegistryStatus(Formatter &fmt, const CliContext &ctx);
int RunRegistrySync(Formatter &fmt, const CliContext &ctx);
