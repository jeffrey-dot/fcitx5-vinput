#include "cli/command_recording.h"
#include "cli/cli_helpers.h"
#include "cli/dbus_client.h"
#include "common/core_config.h"
#include "common/i18n.h"
#include "common/string_utils.h"
#include <nlohmann/json.hpp>
#include <string>

namespace {

bool EnsureDaemon(vinput::cli::DbusClient& dbus, Formatter& fmt, const CliContext& ctx) {
    std::string err;
    if (!dbus.IsDaemonRunning(&err)) {
        std::string msg = err.empty() ? _("Daemon is not running.") : err;
        if (ctx.json_output) {
            fmt.PrintJson({{"ok", false}, {"error", msg}});
        } else {
            fmt.PrintError(msg);
        }
        return false;
    }
    return true;
}

} // namespace

int RunRecordingStart(Formatter& fmt, const CliContext& ctx) {
    vinput::cli::DbusClient dbus;
    if (!EnsureDaemon(dbus, fmt, ctx)) return 1;

    std::string err;
    if (!dbus.StartRecording(&err)) {
        if (ctx.json_output) {
            fmt.PrintJson({{"ok", false}, {"error", err}});
        } else {
            fmt.PrintError(err);
        }
        return 1;
    }

    if (ctx.json_output) {
        fmt.PrintJson({{"ok", true}, {"action", "start"}});
    } else {
        fmt.PrintInfo(_("Recording started."));
    }
    return 0;
}

int RunRecordingStop(const std::string& scene_id, Formatter& fmt, const CliContext& ctx) {
    vinput::cli::DbusClient dbus;
    if (!EnsureDaemon(dbus, fmt, ctx)) return 1;

    std::string resolved = ResolveSceneId(scene_id);
    if (resolved.empty()) {
        std::string msg = _("No active scene configured. Use 'vinput scene use <ID>' to set one.");
        if (ctx.json_output) {
            fmt.PrintJson({{"ok", false}, {"error", msg}});
        } else {
            fmt.PrintError(msg);
        }
        return 1;
    }

    std::string err;
    if (!dbus.StopRecording(resolved, &err)) {
        if (ctx.json_output) {
            fmt.PrintJson({{"ok", false}, {"error", err}});
        } else {
            fmt.PrintError(err);
        }
        return 1;
    }

    if (ctx.json_output) {
        fmt.PrintJson({{"ok", true}, {"action", "stop"}, {"scene", resolved}});
    } else {
        fmt.PrintInfo(vinput::str::FmtStr(_("Recording stopped (scene: %s)."), resolved.c_str()));
    }
    return 0;
}

int RunRecordingToggle(const std::string& scene_id, Formatter& fmt, const CliContext& ctx) {
    vinput::cli::DbusClient dbus;
    if (!EnsureDaemon(dbus, fmt, ctx)) return 1;

    // Optimistic start: try StartRecording first, handle failure based on state
    std::string err;
    if (dbus.StartRecording(&err)) {
        if (ctx.json_output) {
            fmt.PrintJson({{"ok", true}, {"action", "start"}});
        } else {
            fmt.PrintInfo(_("Recording started."));
        }
        return 0;
    }

    // Start failed — check daemon status to decide next action
    std::string status;
    std::string status_err;
    if (!dbus.GetDaemonStatus(&status, &status_err)) {
        // Can't even get status, report original start error
        if (ctx.json_output) {
            fmt.PrintJson({{"ok", false}, {"error", err}});
        } else {
            fmt.PrintError(err);
        }
        return 1;
    }

    if (status == "recording") {
        // Was already recording — stop it
        std::string resolved = ResolveSceneId(scene_id);
        if (resolved.empty()) {
            std::string msg = _("No active scene configured. Use 'vinput scene use <ID>' to set one.");
            if (ctx.json_output) {
                fmt.PrintJson({{"ok", false}, {"error", msg}});
            } else {
                fmt.PrintError(msg);
            }
            return 1;
        }

        std::string stop_err;
        if (!dbus.StopRecording(resolved, &stop_err)) {
            if (ctx.json_output) {
                fmt.PrintJson({{"ok", false}, {"error", stop_err}});
            } else {
                fmt.PrintError(stop_err);
            }
            return 1;
        }

        if (ctx.json_output) {
            fmt.PrintJson({{"ok", true}, {"action", "stop"}, {"scene", resolved}});
        } else {
            fmt.PrintInfo(vinput::str::FmtStr(_("Recording stopped (scene: %s)."), resolved.c_str()));
        }
        return 0;
    }

    // Busy states
    if (status == "inferring" || status == "postprocessing") {
        std::string msg = vinput::str::FmtStr(_("Daemon is busy (status: %s)."), status.c_str());
        if (ctx.json_output) {
            fmt.PrintJson({{"ok", false}, {"error", msg}});
        } else {
            fmt.PrintError(msg);
        }
        return 1;
    }

    if (status == "error") {
        std::string msg = _("Daemon is in error state.");
        if (ctx.json_output) {
            fmt.PrintJson({{"ok", false}, {"error", msg}});
        } else {
            fmt.PrintError(msg);
        }
        return 1;
    }

    // Unknown state — pass through original start error
    if (ctx.json_output) {
        fmt.PrintJson({{"ok", false}, {"error", err}});
    } else {
        fmt.PrintError(err);
    }
    return 1;
}
