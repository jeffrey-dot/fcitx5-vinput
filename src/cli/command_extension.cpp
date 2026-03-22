#include "cli/command_extension.h"

#include <nlohmann/json.hpp>

#include "common/extension_manager.h"
#include "common/i18n.h"
#include "common/string_utils.h"

namespace {

std::string RunningState(const vinput::extension::Info &info) {
  if (info.type != vinput::extension::Type::kLlm) {
    return "-";
  }
  return vinput::extension::IsRunning(info) ? "running" : "stopped";
}

}  // namespace

int RunExtensionList(Formatter &fmt, const CliContext &ctx) {
  std::string error;
  const auto extensions = vinput::extension::Discover(&error);
  if (!error.empty()) {
    fmt.PrintError(error);
    return 1;
  }

  if (ctx.json_output) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto &info : extensions) {
      arr.push_back({
          {"id", info.id},
          {"name", info.name},
          {"type", vinput::extension::TypeToString(info.type)},
          {"source", vinput::extension::SourceToString(info.source)},
          {"description", info.description},
          {"author", info.author},
          {"version", info.version},
          {"path", info.path.string()},
          {"executable", info.executable},
          {"state", RunningState(info)},
      });
    }
    fmt.PrintJson(arr);
    return 0;
  }

  std::vector<std::string> headers = {_("ID"), _("TYPE"), _("SOURCE"),
                                      _("STATE"), _("DESCRIPTION")};
  std::vector<std::vector<std::string>> rows;
  for (const auto &info : extensions) {
    rows.push_back({info.id,
                    vinput::extension::TypeToString(info.type),
                    vinput::extension::SourceToString(info.source),
                    RunningState(info),
                    info.description.empty() ? info.name : info.description});
  }
  fmt.PrintTable(headers, rows);
  return 0;
}

int RunExtensionStart(const std::string &name, Formatter &fmt,
                      const CliContext &ctx) {
  (void)ctx;
  std::string error;
  auto info = vinput::extension::FindById(name, vinput::extension::Type::kLlm,
                                          &error);
  if (!info.has_value()) {
    fmt.PrintError(error);
    return 1;
  }
  if (!vinput::extension::Start(*info, &error)) {
    fmt.PrintError(error);
    return 1;
  }
  fmt.PrintSuccess(vinput::str::FmtStr(_("Extension '%s' started."), name));
  fmt.PrintInfo(vinput::str::FmtStr(_("Log file: %s"),
                                    vinput::extension::LogPath(*info).string()));
  return 0;
}

int RunExtensionStop(const std::string &name, Formatter &fmt,
                     const CliContext &ctx) {
  (void)ctx;
  std::string error;
  auto info = vinput::extension::FindById(name, vinput::extension::Type::kLlm,
                                          &error);
  if (!info.has_value()) {
    fmt.PrintError(error);
    return 1;
  }
  if (!vinput::extension::Stop(*info, &error)) {
    fmt.PrintError(error);
    return 1;
  }
  fmt.PrintSuccess(vinput::str::FmtStr(_("Extension '%s' stopped."), name));
  return 0;
}
