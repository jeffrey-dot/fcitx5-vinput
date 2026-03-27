#pragma once

#include "cli/utils/cli_context.h"
#include "cli/utils/formatter.h"
#include <string>

int RunHotwordGet(Formatter &fmt, const CliContext &ctx);
int RunHotwordSet(const std::string &file_path, Formatter &fmt,
                   const CliContext &ctx);
int RunHotwordClear(Formatter &fmt, const CliContext &ctx);
int RunHotwordEdit(Formatter &fmt, const CliContext &ctx);
