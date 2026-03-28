#pragma once

#include <string>

#include "cli/utils/cli_context.h"
#include "cli/utils/formatter.h"

int RunAsrConfigList(Formatter &fmt, const CliContext &ctx);
int RunAsrConfigRemove(const std::string &id, Formatter &fmt,
                       const CliContext &ctx);
int RunAsrConfigUse(const std::string &id, Formatter &fmt,
                    const CliContext &ctx);
int RunAsrConfigListModels(bool available, Formatter &fmt,
                           const CliContext &ctx);
int RunAsrConfigInstallModel(const std::string &selector, Formatter &fmt,
                             const CliContext &ctx);
int RunAsrConfigUseModel(const std::string &selector, Formatter &fmt,
                         const CliContext &ctx);
int RunAsrConfigModelInfo(const std::string &selector, Formatter &fmt,
                          const CliContext &ctx);
int RunAsrConfigGetHotword(Formatter &fmt, const CliContext &ctx);
int RunAsrConfigSetHotword(const std::string &path, Formatter &fmt,
                           const CliContext &ctx);
int RunAsrConfigClearHotword(Formatter &fmt, const CliContext &ctx);
int RunAsrConfigEditHotword(Formatter &fmt, const CliContext &ctx);
