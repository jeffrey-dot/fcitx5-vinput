#include "cli/systemd_client.h"
#include "common/path_utils.h"
#include <sys/wait.h>
#include <unistd.h>
#include <string>
#include <vector>

namespace vinput::cli {

static int RunCommand(const std::vector<const char*>& args) {
    // Add "flatpak-spawn --host" prefix if inside flatpak
    std::vector<const char*> actual_args;
    if (vinput::path::isInsideFlatpak()) {
        actual_args = {"flatpak-spawn", "--host"};
        for (const auto* a : args) actual_args.push_back(a);
    } else {
        actual_args = args;
    }

    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        execvp(actual_args[0], const_cast<char* const*>(actual_args.data()));
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

int SystemctlStart() {
    return RunCommand({"systemctl", "--user", "start", kServiceUnit, nullptr});
}

int SystemctlStop() {
    return RunCommand({"systemctl", "--user", "stop", kServiceUnit, nullptr});
}

int SystemctlRestart() {
    return RunCommand({"systemctl", "--user", "restart", kServiceUnit, nullptr});
}

int JournalctlLogs(bool follow, int lines) {
    std::string lines_str = std::to_string(lines);
    if (follow) {
        return RunCommand({"journalctl", "--user-unit", kServiceUnit,
                           "-n", lines_str.c_str(), "-f", nullptr});
    }
    return RunCommand({"journalctl", "--user-unit", kServiceUnit,
                       "-n", lines_str.c_str(), nullptr});
}

} // namespace vinput::cli
