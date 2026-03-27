#pragma once
#include "cli/utils/cli_context.h"
#include "cli/utils/formatter.h"
#include <string>

int RunRecordingStart(Formatter& fmt, const CliContext& ctx);
int RunRecordingStop(const std::string& scene_id, Formatter& fmt, const CliContext& ctx);
int RunRecordingToggle(const std::string& scene_id, Formatter& fmt, const CliContext& ctx);
