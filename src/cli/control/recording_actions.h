#pragma once

#include <string>

#include "cli/utils/cli_context.h"
#include "cli/utils/formatter.h"

int RunRecordingControlStart(Formatter &fmt, const CliContext &ctx);
int RunRecordingControlStop(const std::string &scene_id, Formatter &fmt,
                            const CliContext &ctx);
int RunRecordingControlToggle(const std::string &scene_id, Formatter &fmt,
                              const CliContext &ctx);
