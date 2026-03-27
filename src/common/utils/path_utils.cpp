#include "common/utils/path_utils.h"
#include <cstdlib>
#include <sys/stat.h>

namespace vinput::path {

namespace {

constexpr std::string_view kDaemonServiceUnitName = "vinput-daemon.service";
constexpr std::string_view kCliExecutableName = "vinput";
constexpr std::string_view kDaemonExecutableName = "vinput-daemon";

std::filesystem::path FlatpakAddonRootDir() {
  return std::filesystem::path("/app") / "addons" / "Vinput";
}

std::filesystem::path FlatpakBundledSystemdUnitPath(std::string_view unit_name) {
  return FlatpakAddonRootDir() / "share" / "systemd" / "user" /
         std::filesystem::path(unit_name);
}

std::filesystem::path FlatpakBundledExecutablePath(std::string_view name) {
  return FlatpakAddonRootDir() / "bin" / std::filesystem::path(name);
}

std::filesystem::path UserSystemdUnitPath(std::string_view unit_name) {
  return vinput::path::UserSystemdUnitDir() / std::filesystem::path(unit_name);
}

std::filesystem::path XdgConfigHome() {
  const char *xdg = std::getenv("XDG_CONFIG_HOME");
  if (xdg && xdg[0] != '\0') {
    return std::filesystem::path(xdg);
  }
  const char *home = std::getenv("HOME");
  if (!home || home[0] == '\0') {
    return {};
  }
  return std::filesystem::path(home) / ".config";
}

std::filesystem::path XdgDataHome() {
  const char *xdg = std::getenv("XDG_DATA_HOME");
  if (xdg && xdg[0] != '\0') {
    return std::filesystem::path(xdg);
  }
  const char *home = std::getenv("HOME");
  if (!home || home[0] == '\0') {
    return {};
  }
  return std::filesystem::path(home) / ".local" / "share";
}

std::filesystem::path XdgCacheHome() {
  const char *xdg = std::getenv("XDG_CACHE_HOME");
  if (xdg && xdg[0] != '\0') {
    return std::filesystem::path(xdg);
  }
  const char *home = std::getenv("HOME");
  if (!home || home[0] == '\0') {
    return {};
  }
  return std::filesystem::path(home) / ".cache";
}

std::filesystem::path VinputConfigDir() { return XdgConfigHome() / "vinput"; }
std::filesystem::path VinputDataDir() { return XdgDataHome() / "vinput"; }
std::filesystem::path VinputCacheDir() { return XdgCacheHome() / "vinput"; }

} // namespace

std::string_view DaemonServiceUnitName() { return kDaemonServiceUnitName; }

std::string_view CliExecutableName() { return kCliExecutableName; }

std::filesystem::path CliExecutablePath() {
  if (IsInsideFlatpak()) {
    return FlatpakBundledExecutablePath(kCliExecutableName);
  }
  return std::filesystem::path(kCliExecutableName);
}

std::filesystem::path DaemonExecutablePath() {
  if (IsInsideFlatpak()) {
    return FlatpakBundledExecutablePath(kDaemonExecutableName);
  }
  return std::filesystem::path(kDaemonExecutableName);
}

std::filesystem::path DaemonServiceUnitInstallPath() {
  return UserSystemdUnitPath(DaemonServiceUnitName());
}

std::filesystem::path DaemonServiceUnitTemplatePath() {
  if (IsInsideFlatpak()) {
    return FlatpakBundledSystemdUnitPath(DaemonServiceUnitName());
  }
  return {};
}

std::filesystem::path ExpandUserPath(std::string_view path) {
  if (path.empty() || path[0] != '~') {
    return std::filesystem::path(path);
  }
  const char *home = std::getenv("HOME");
  if (!home || home[0] == '\0')
    return {};
  return std::filesystem::path(home) /
         std::filesystem::path(
             path.substr(path.size() > 1 && path[1] == '/' ? 2 : 1));
}

std::filesystem::path DefaultModelBaseDir() {
  return VinputDataDir() / "models";
}

std::filesystem::path CoreConfigPath() {
  return VinputConfigDir() / "config.json";
}

std::filesystem::path FcitxAddonConfigPath() {
  const char *xdg = std::getenv("XDG_CONFIG_HOME");
  if (xdg && xdg[0] != '\0') {
    return std::filesystem::path(xdg) / "fcitx5" / "conf" / "vinput.conf";
  }
  const char *home = std::getenv("HOME");
  if (!home || home[0] == '\0') {
    return {};
  }
  return std::filesystem::path(home) / ".config" / "fcitx5" / "conf" /
         "vinput.conf";
}

std::filesystem::path RegistryCacheDir() {
  return VinputCacheDir() / "registry";
}

std::filesystem::path FlatpakInfoPath() {
  return std::filesystem::path("/.flatpak-info");
}

std::filesystem::path UserSystemdUnitDir() {
  return XdgConfigHome() / "systemd" / "user";
}

std::filesystem::path ManagedAsrProviderDir() {
  return VinputDataDir() / "asr-providers";
}

std::filesystem::path ManagedLlmAdaptorDir() {
  return VinputDataDir() / "llm-adaptors";
}

std::filesystem::path AdaptorRuntimeDir() {
  const char *xdg_runtime = std::getenv("XDG_RUNTIME_DIR");
  if (xdg_runtime && xdg_runtime[0] != '\0') {
    return std::filesystem::path(xdg_runtime) / "vinput" / "adaptors";
  }

  const char *tmpdir = std::getenv("TMPDIR");
  std::filesystem::path base =
      (tmpdir && tmpdir[0] != '\0') ? std::filesystem::path(tmpdir)
                                    : std::filesystem::path("/tmp");
  return base / "vinput" / "adaptors";
}

bool IsInsideFlatpak() {
  struct stat st;
  return stat(FlatpakInfoPath().c_str(), &st) == 0;
}

} // namespace vinput::path
