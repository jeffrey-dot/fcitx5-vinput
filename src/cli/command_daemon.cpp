#include "cli/command_daemon.h"
#include "cli/dbus_client.h"
#include "cli/systemd_client.h"
#include "common/error_info.h"
#include "common/i18n.h"
#include "common/utils/string_utils.h"

#include <cctype>
#include <algorithm>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace {

std::string Trim(std::string_view text) {
    size_t begin = 0;
    while (begin < text.size() &&
           std::isspace(static_cast<unsigned char>(text[begin]))) {
        begin++;
    }
    size_t end = text.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        end--;
    }
    return std::string(text.substr(begin, end - begin));
}

std::vector<std::string> SplitLines(std::string_view text) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        lines.push_back(std::string(text.substr(start, end - start)));
        start = end + 1;
    }
    return lines;
}

std::string StripJournalPrefix(std::string line) {
    auto pos = line.find("]: ");
    if (pos != std::string::npos) {
        line.erase(0, pos + 3);
    } else if ((pos = line.find(": ")) != std::string::npos) {
        line.erase(0, pos + 2);
    }
    if (line.rfind("vinput-daemon: ", 0) == 0) {
        line.erase(0, std::string("vinput-daemon: ").size());
    } else if (line.rfind("vinput: ", 0) == 0) {
        line.erase(0, std::string("vinput: ").size());
    }
    return Trim(line);
}

bool IsActionableFailureLine(const std::string& line) {
    if (line.empty()) {
        return false;
    }
    if (line.find("ASR provider=") != std::string::npos ||
        line.find("running with ASR disabled") != std::string::npos ||
        line.find("recording started") != std::string::npos ||
        line.find("phase ->") != std::string::npos ||
        line.find("negotiated format") != std::string::npos) {
        return false;
    }
    return line.find("Missing ") != std::string::npos ||
           line.find("missing ") != std::string::npos ||
           line.find("not found") != std::string::npos ||
           line.find("failed") != std::string::npos ||
           line.find("error") != std::string::npos;
}

std::vector<std::string> ExtractDaemonFailureReasons(const std::string& logs) {
    std::string fallback;
    std::vector<std::string> candidates;
    std::unordered_set<std::string> seen;

    auto lines = SplitLines(logs);
    for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
        std::string line = StripJournalPrefix(*it);
        if (line.empty()) {
            continue;
        }
        if (fallback.empty()) {
            fallback = line;
        }
        if (!IsActionableFailureLine(line) || !seen.insert(line).second) {
            continue;
        }
        candidates.push_back(std::move(line));
        if (candidates.size() >= 3) {
            break;
        }
    }

    std::reverse(candidates.begin(), candidates.end());
    if (candidates.empty() && !fallback.empty()) {
        candidates.push_back(std::move(fallback));
    }
    return candidates;
}

void NotifyDaemonFailure(const vinput::dbus::ErrorInfo& error) {
    if (error.empty()) {
        return;
    }

    vinput::cli::DbusClient dbus;
    dbus.NotifyError(error);
}

std::string BuildDaemonFailureMessage(const char* fmt, int exit_code) {
    std::string message = vinput::str::FmtStr(fmt, exit_code);
    std::string logs = vinput::cli::JournalctlLogsText(20);
    if (!logs.empty()) {
        message += "\n\n";
        message += logs;
    }
    return message;
}

vinput::dbus::ErrorInfo BuildDaemonFailureNotification(
    const char* fallback_code, const char* fmt, int exit_code) {
    std::string logs = vinput::cli::JournalctlLogsText(20);
    auto reasons = ExtractDaemonFailureReasons(logs);
    vinput::dbus::ErrorInfo fallback_error;
    for (auto it = reasons.rbegin(); it != reasons.rend(); ++it) {
        auto error = vinput::dbus::ClassifyErrorText(*it);
        if (error.empty()) {
            continue;
        }
        if (fallback_error.empty()) {
            fallback_error = error;
        }
        if (error.code != vinput::dbus::kErrorCodeUnknown) {
            return error;
        }
    }
    if (!fallback_error.empty()) {
        return fallback_error;
    }
    return vinput::dbus::MakeErrorInfo(
        fallback_code, {}, std::to_string(exit_code),
        vinput::str::FmtStr(fmt, exit_code));
}

} // namespace

int RunDaemonStart(Formatter& fmt, const CliContext& ctx) {
    (void)ctx;
    int r = vinput::cli::SystemctlStart();
    if (r == 0) {
        fmt.PrintSuccess(_("Daemon started."));
        return 0;
    }
    const auto message =
        BuildDaemonFailureMessage(_("systemctl start failed (exit code: %d)"), r);
    fmt.PrintError(message);
    NotifyDaemonFailure(BuildDaemonFailureNotification(
        vinput::dbus::kErrorCodeDaemonStartFailed,
        _("systemctl start failed (exit code: %d)"), r));
    return 1;
}

int RunDaemonStop(Formatter& fmt, const CliContext& ctx) {
    (void)ctx;
    int r = vinput::cli::SystemctlStop();
    if (r == 0) {
        fmt.PrintSuccess(_("Daemon stopped."));
        return 0;
    }
    fmt.PrintError(vinput::str::FmtStr(_("systemctl stop failed (exit code: %d)"), r));
    return 1;
}

int RunDaemonRestart(Formatter& fmt, const CliContext& ctx) {
    (void)ctx;
    int r = vinput::cli::SystemctlRestart();
    if (r == 0) {
        fmt.PrintSuccess(_("Daemon restarted."));
        return 0;
    }
    const auto message =
        BuildDaemonFailureMessage(_("systemctl restart failed (exit code: %d)"), r);
    fmt.PrintError(message);
    NotifyDaemonFailure(BuildDaemonFailureNotification(
        vinput::dbus::kErrorCodeDaemonRestartFailed,
        _("systemctl restart failed (exit code: %d)"), r));
    return 1;
}

int RunDaemonLogs(bool follow, int lines, Formatter& fmt, const CliContext& ctx) {
    (void)fmt;
    (void)ctx;
    return vinput::cli::JournalctlLogs(follow, lines);
}
