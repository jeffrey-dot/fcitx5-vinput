#pragma once

#include <CLI/CLI.hpp>

#include "cli/config/action.h"

namespace vinput::cli::config {

void RegisterConfigCli(CLI::App &app, CliAction *action);

}  // namespace vinput::cli::config
