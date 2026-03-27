#pragma once

#include <string>

namespace vinput::cli {

int SystemctlStart();
int SystemctlStop();
int SystemctlRestart();
int JournalctlLogs(bool follow, int lines);
std::string JournalctlLogsText(int lines);

} // namespace vinput::cli
