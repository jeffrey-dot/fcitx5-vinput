#pragma once
#include <filesystem>
#include <string_view>

namespace vinput::path {
std::string_view DaemonServiceUnitName();
std::string_view CliExecutableName();
std::filesystem::path CliExecutablePath();
std::filesystem::path DaemonExecutablePath();
std::filesystem::path DaemonServiceUnitInstallPath();
std::filesystem::path DaemonServiceUnitTemplatePath();
std::filesystem::path ExpandUserPath(std::string_view path);
std::filesystem::path DefaultModelBaseDir();
std::filesystem::path CoreConfigPath();
std::filesystem::path FcitxAddonConfigPath();
std::filesystem::path RegistryCacheDir();
bool IsInsideFlatpak();
std::filesystem::path FlatpakInfoPath();
std::filesystem::path UserSystemdUnitDir();
std::filesystem::path ManagedAsrProviderDir();
std::filesystem::path ManagedLlmAdaptorDir();
std::filesystem::path AdaptorRuntimeDir();
} // namespace vinput::path
