#pragma once

#include "cli/utils/cli_context.h"
#include "cli/utils/formatter.h"

#include <string>

int RunAdaptorList(Formatter &fmt, const CliContext &ctx);
int RunAdaptorListAvailable(Formatter &fmt, const CliContext &ctx);
int RunAdaptorInstall(const std::string &id, Formatter &fmt,
                      const CliContext &ctx);
int RunAdaptorStart(const std::string &name, Formatter &fmt,
                    const CliContext &ctx);
int RunAdaptorStop(const std::string &name, Formatter &fmt,
                   const CliContext &ctx);
