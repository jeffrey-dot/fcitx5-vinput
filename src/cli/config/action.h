#pragma once

#include <functional>

#include "cli/utils/cli_context.h"
#include "cli/utils/formatter.h"

namespace vinput::cli::config {

using CliAction = std::function<int(Formatter &, const CliContext &)>;

}  // namespace vinput::cli::config
