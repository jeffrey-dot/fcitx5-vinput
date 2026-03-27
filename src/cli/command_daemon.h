#pragma once
#include "cli/utils/cli_context.h"
#include "cli/utils/formatter.h"

int RunDaemonStart(Formatter& fmt, const CliContext& ctx);
int RunDaemonStop(Formatter& fmt, const CliContext& ctx);
int RunDaemonRestart(Formatter& fmt, const CliContext& ctx);
int RunDaemonLogs(bool follow, int lines, Formatter& fmt, const CliContext& ctx);
