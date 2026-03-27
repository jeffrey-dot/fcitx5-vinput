#include "cli/config/register.h"

#include <CLI/CLI.hpp>

#include "cli/config/action.h"

namespace vinput::cli::config {

void RegisterDeviceCommands(CLI::App &app, CliAction *action);
void RegisterAsrCommands(CLI::App &app, CliAction *action);
void RegisterLlmCommands(CLI::App &app, CliAction *action);
void RegisterSceneCommands(CLI::App &app, CliAction *action);
void RegisterConfigCommands(CLI::App &app, CliAction *action);
void RegisterInitCommands(CLI::App &app, CliAction *action);

void RegisterConfigCli(CLI::App &app, CliAction *action) {
  RegisterInitCommands(app, action);
  RegisterDeviceCommands(app, action);
  RegisterAsrCommands(app, action);
  RegisterLlmCommands(app, action);
  RegisterSceneCommands(app, action);
  RegisterConfigCommands(app, action);
}

}  // namespace vinput::cli::config
