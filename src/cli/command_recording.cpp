#include "cli/command_recording.h"
#include "cli/utils/cli_helpers.h"
#include "cli/dbus_client.h"
#include "common/dbus_interface.h"
#include "common/i18n.h"
#include "common/utils/string_utils.h"
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
        nlohmann::json j = {{"ok", true}, {"action", "stop"}};
        if (!resolved.empty()) j["scene"] = resolved;
        fmt.PrintJson(j);
    } else {
        if (resolved.empty()) {
            fmt.PrintInfo(_("Recording stopped."));
        } else {
            fmt.PrintInfo(vinput::str::FmtStr(_("Recording stopped (scene: %s)."), resolved.c_str()));
        }
    }
    return 0;
}

int RunRecordingToggle(const std::string& scene_id, Formatter& fmt, const CliContext& ctx) {
    vinput::cli::DbusClient dbus;
    if (!EnsureDaemon(dbus, fmt, ctx)) return 1;

    // Query current status first to decide action
    std::string status;
    std::string status_err;
    if (!dbus.GetDaemonStatus(&status, &status_err)) {
        if (ctx.json_output) {
            fmt.PrintJson({{"ok", false}, {"error", status_err}});
        } else {
            fmt.PrintError(status_err);
        }
        return 1;
    }

    if (status == vinput::dbus::kStatusRecording) {
        // Currently recording — stop it
        std::string resolved = ResolveSceneId(scene_id);

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
            nlohmann::json j = {{"ok", true}, {"action", "stop"}};
            if (!resolved.empty()) j["scene"] = resolved;
            fmt.PrintJson(j);
        } else {
            if (resolved.empty()) {
                fmt.PrintInfo(_("Recording stopped."));
            } else {
                fmt.PrintInfo(vinput::str::FmtStr(_("Recording stopped (scene: %s)."), resolved.c_str()));
            }
        }
        return 0;
    }

    if (status == vinput::dbus::kStatusInferring ||
        status == vinput::dbus::kStatusPostprocessing) {
        std::string msg = vinput::str::FmtStr(_("Daemon is busy (status: %s)."), status.c_str());
        if (ctx.json_output) {
            fmt.PrintJson({{"ok", false}, {"error", msg}});
        } else {
            fmt.PrintError(msg);
        }
        return 1;
    }

    if (status == vinput::dbus::kStatusError) {
        std::string msg = _("Daemon is in error state.");
        if (ctx.json_output) {
            fmt.PrintJson({{"ok", false}, {"error", msg}});
        } else {
            fmt.PrintError(msg);
        }
        return 1;
    }

    // Idle — start recording
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
