#pragma once
#include "cli/utils/cli_context.h"
#include "cli/utils/formatter.h"
#include <string>

int RunModelList(bool available, Formatter& fmt, const CliContext& ctx);
int RunModelInstall(const std::string& name, Formatter& fmt, const CliContext& ctx);
int RunModelUse(const std::string& name, Formatter& fmt, const CliContext& ctx);
int RunModelRemove(const std::string& name, bool force, Formatter& fmt, const CliContext& ctx);
int RunModelInfo(const std::string& name, Formatter& fmt, const CliContext& ctx);
