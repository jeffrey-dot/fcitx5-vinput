#pragma once
#include "cli/utils/cli_context.h"
#include "cli/utils/formatter.h"
#include <string>

int RunModelList(bool available, Formatter& fmt, const CliContext& ctx);
int RunModelInstall(const std::string& id, Formatter& fmt, const CliContext& ctx);
int RunModelUse(const std::string& id, Formatter& fmt, const CliContext& ctx);
int RunModelRemove(const std::string& id, bool force, Formatter& fmt, const CliContext& ctx);
int RunModelInfo(const std::string& id, Formatter& fmt, const CliContext& ctx);
