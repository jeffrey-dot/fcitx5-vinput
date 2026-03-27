#include "cli/config/config_actions.h"

#include <cstdio>
#include <iostream>
#include <string>

#include "cli/utils/editor_utils.h"
#include "common/config/config_router.h"
#include "common/i18n.h"

int RunConfigDomainGet(const std::string &path, Formatter &fmt,
                       const CliContext &ctx) {
  std::string value, error;
  if (!vinput::config::GetConfigValue(path, &value, &error)) {
    fmt.PrintError(error);
    return 1;
  }
  if (ctx.json_output) {
    fmt.PrintJson({{"path", path}, {"value", value}});
  } else {
    std::puts(value.c_str());
  }
  return 0;
}

int RunConfigDomainSet(const std::string &path, const std::string &value_arg,
                       bool from_stdin, Formatter &fmt,
                       const CliContext &ctx) {
  (void)ctx;
  std::string value = value_arg;
  if (from_stdin) {
    std::string line, all;
    while (std::getline(std::cin, line)) {
      if (!all.empty())
        all += '\n';
      all += line;
    }
    value = all;
  }
  std::string error;
  if (!vinput::config::SetConfigValue(path, value, &error)) {
    fmt.PrintError(error);
    return 1;
  }
  fmt.PrintSuccess(_("Config value set."));
  return 0;
}

int RunConfigDomainEdit(const std::string &target, Formatter &fmt,
                        const CliContext &ctx) {
  (void)ctx;
  if (target != "core" && target != "fcitx") {
    fmt.PrintError(_("Unsupported config target. Use 'core' or 'fcitx'."));
    return 1;
  }
  auto file_path = vinput::config::GetEditTarget(target);
  return OpenInEditor(file_path);
}
