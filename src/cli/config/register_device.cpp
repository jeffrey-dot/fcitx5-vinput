#include <memory>

#include <CLI/CLI.hpp>

#include "cli/command_device.h"
#include "cli/config/action.h"
#include "common/i18n.h"

namespace vinput::cli::config {

void RegisterDeviceCommands(CLI::App &app, CliAction *action) {
  auto *device = app.add_subcommand("device", _("Manage capture devices"));
  device->require_subcommand(1);

  auto *list = device->add_subcommand("list", _("List available audio input devices"));
  list->alias("ls");
  list->callback([action]() {
    *action = [](Formatter &fmt, const CliContext &ctx) {
      return RunDeviceList(fmt, ctx);
    };
  });

  auto id = std::make_shared<std::string>();
  auto *use = device->add_subcommand("use", _("Set active capture device"));
  use->add_option("id", *id, _("Device id or 'default'"))->required();
  use->callback([action, id]() {
    *action = [id](Formatter &fmt, const CliContext &ctx) {
      return RunDeviceUse(*id, fmt, ctx);
    };
  });
}

}  // namespace vinput::cli::config
