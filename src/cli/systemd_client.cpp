#include "cli/systemd_client.h"
#include "common/utils/path_utils.h"
#include <array>
#include <sys/wait.h>
#include <unistd.h>
#include <string>
#include <vector>

namespace vinput::cli {

namespace {

std::vector<std::string> BuildCommand(const std::vector<std::string>& args) {
    if (vinput::path::IsInsideFlatpak()) {
        std::vector<std::string> actual_args = {"flatpak-spawn", "--host"};
        actual_args.insert(actual_args.end(), args.begin(), args.end());
        return actual_args;
    }
    return args;
}

std::vector<char*> BuildExecArgs(std::vector<std::string>& args) {
    std::vector<char*> exec_args;
    exec_args.reserve(args.size() + 1);
    for (auto& arg : args) {
        exec_args.push_back(arg.data());
    }
    exec_args.push_back(nullptr);
    return exec_args;
}

std::vector<std::string> BuildJournalctlCommand(bool follow, int lines) {
    std::vector<std::string> args;
    const std::string lines_str = std::to_string(lines);
    if (vinput::path::IsInsideFlatpak()) {
        args = {"journalctl", "--user", "-t", "flatpak", "--grep", "vinput",
                "-n", lines_str};
    } else {
        args = {"journalctl", "--user-unit",
                std::string(vinput::path::DaemonServiceUnitName()), "-n",
                lines_str};
    }
    if (follow) {
        args.push_back("-f");
    }
    return args;
}

static int RunCommand(const std::vector<std::string>& args) {
    auto actual_args = BuildCommand(args);
    auto exec_args = BuildExecArgs(actual_args);
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        execvp(exec_args[0], exec_args.data());
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static int RunCommandCapture(const std::vector<std::string>& args,
                             std::string* output) {
    int pipefd[2];
    if (pipe(pipefd) < 0) return -1;

    auto actual_args = BuildCommand(args);
    auto exec_args = BuildExecArgs(actual_args);

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        execvp(exec_args[0], exec_args.data());
        _exit(127);
    }

    close(pipefd[1]);
    std::string buffer;
    std::array<char, 4096> chunk{};
    ssize_t count;
    while ((count = read(pipefd[0], chunk.data(), chunk.size())) > 0) {
        buffer.append(chunk.data(), static_cast<size_t>(count));
    }
    close(pipefd[0]);

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return -1;
    if (output) {
        *output = std::move(buffer);
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

} // namespace

int SystemctlStart() {
    return RunCommand({"systemctl", "--user", "start",
                       std::string(vinput::path::DaemonServiceUnitName())});
}

int SystemctlStop() {
    return RunCommand({"systemctl", "--user", "stop",
                       std::string(vinput::path::DaemonServiceUnitName())});
}

int SystemctlRestart() {
    return RunCommand({"systemctl", "--user", "restart",
                       std::string(vinput::path::DaemonServiceUnitName())});
}

int JournalctlLogs(bool follow, int lines) {
    return RunCommand(BuildJournalctlCommand(follow, lines));
}

std::string JournalctlLogsText(int lines) {
    std::string output;
    if (RunCommandCapture(BuildJournalctlCommand(false, lines), &output) < 0) {
        return {};
    }
    return output;
}

} // namespace vinput::cli
