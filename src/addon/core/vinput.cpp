#include "core/vinput.h"
#include "dbus/notifier_dbus_object.h"
#include "common/config/core_config.h"
#include "common/dbus/dbus_interface.h"
#include "common/i18n.h"
#include "common/utils/file_utils.h"
#include "common/utils/path_utils.h"
#include "common/utils/sandbox.h"
#include "common/scene/postprocess_scene.h"

#include <dbus_public.h>
#include <fcitx-utils/event.h>
#include <fcitx/inputcontext.h>

#include <cstdio>
#include <fstream>
#include <string>

using namespace vinput::dbus;

namespace {
// Auto-install systemd service when running inside a sandbox.
void ensureDaemonServiceInstalled() {
  if (!vinput::sandbox::IsInSandbox())
    return;

  const std::filesystem::path dest = vinput::path::DaemonServiceUnitInstallPath();
  if (dest.empty()) {
    return;
  }

  std::error_code ec_exists;
  bool destExists = std::filesystem::exists(dest, ec_exists);
  if (ec_exists) {
    FCITX_LOG(Error) << "vinput: failed to check existence of " << dest
                     << ": " << ec_exists.message();
    return;
  }
  if (destExists)
    return;

  const auto src = vinput::path::DaemonServiceUnitTemplatePath();
  std::ifstream src_f(src);
  if (!src_f) {
    FCITX_LOG(Error) << "vinput: service file not found at " << src;
    return;
  }

  std::string content((std::istreambuf_iterator<char>(src_f)), {});
  content = vinput::sandbox::RewriteServiceUnit(content);

  std::string file_error;
  if (!vinput::file::EnsureParentDirectory(dest, &file_error)) {
    FCITX_LOG(Error) << "vinput: failed to prepare systemd user dir: "
                     << file_error;
    return;
  }
  if (!vinput::file::AtomicWriteTextFile(dest, content, &file_error)) {
    FCITX_LOG(Error) << "vinput: failed to write service file to " << dest;
    FCITX_LOG(Error) << "vinput: write error: " << file_error;
    return;
  }
  FCITX_LOG(Info) << "vinput: installed vinput-daemon.service to " << dest;

  auto reload_cmd = vinput::sandbox::WrapHostCommand(
      {"systemctl", "--user", "daemon-reload"});
  std::string cmd;
  for (const auto &arg : reload_cmd) {
    if (!cmd.empty()) cmd += ' ';
    cmd += arg;
  }
  int ret = system(cmd.c_str());
  if (ret != 0) {
    FCITX_LOG(Error)
        << "vinput: failed to reload systemd user daemon, return code: "
        << ret;
  }
}
} // namespace

VinputEngine::VinputEngine(fcitx::Instance *instance) : instance_(instance) {
  vinput::i18n::Init();
  ensureDaemonServiceInstalled();
  reloadConfig();

  eventHandlers_.emplace_back(instance_->watchEvent(
      fcitx::EventType::InputContextKeyEvent,
      fcitx::EventWatcherPhase::PreInputMethod,
      [this](fcitx::Event &event) { handleKeyEvent(event); }));

  eventHandlers_.emplace_back(instance_->watchEvent(
      fcitx::EventType::InputContextCreated,
      fcitx::EventWatcherPhase::PreInputMethod,
      [this](fcitx::Event &event) {
        auto &icEvent = static_cast<fcitx::InputContextEvent &>(event);
        auto *ic = icEvent.inputContext();
        ic->setCapabilityFlags(ic->capabilityFlags() |
                               fcitx::CapabilityFlag::SurroundingText);
        rememberInputContext(ic);
      }));

  eventHandlers_.emplace_back(instance_->watchEvent(
      fcitx::EventType::InputContextDestroyed,
      fcitx::EventWatcherPhase::PreInputMethod,
      [this](fcitx::Event &event) {
        auto &icEvent = static_cast<fcitx::InputContextEvent &>(event);
        auto *ic = icEvent.inputContext();
        if (session_ && session_->ic == ic) {
          session_.reset();
        }
        if (status_ic_ == ic) {
          status_ic_ = nullptr;
          stopStatusSyncIfIdle();
        }
        if (last_active_ic_ == ic) {
          last_active_ic_ = nullptr;
        }
        if (scene_menu_ic_ == ic) {
          hideSceneMenu();
        }
        if (asr_menu_ic_ == ic) {
          hideAsrMenu();
        }
        if (result_menu_ic_ == ic) {
          hideResultMenu();
        }
      }));

  auto *dbus_addon = instance_->addonManager().addon("dbus");
  if (dbus_addon) {
    bus_ = dbus_addon->call<fcitx::IDBusModule::bus>();
    notifier_dbus_ = std::make_unique<VinputNotifierDBusObject>(
        [this](const vinput::dbus::ErrorInfo &notification) {
          showDaemonNotification(notification);
        });
    if (!bus_->addObjectVTable(vinput::dbus::kNotifierObjectPath,
                               vinput::dbus::kNotifierInterface,
                               *notifier_dbus_)) {
      FCITX_LOG(Error) << "vinput: failed to register notifier DBus object";
      notifier_dbus_.reset();
    }
    setupDBusWatcher();
  }
}

VinputEngine::~VinputEngine() = default;

void VinputEngine::reloadConfig() {
  settings_ = LoadVinputSettings();
  applySettings();
}

void VinputEngine::save() { SaveVinputSettings(settings_); }

const fcitx::Configuration *VinputEngine::getConfig() const {
  rebuildUiConfig();
  return ui_config_.get();
}

void VinputEngine::setConfig(const fcitx::RawConfig &rawConfig) {
  auto config = std::make_unique<VinputConfig>(settings_);
  config->load(rawConfig, true);
  settings_ = config->settings();
  applySettings();
  SaveVinputSettings(settings_);
}

void VinputEngine::applySettings() {
  trigger_keys_ = settings_.triggerKeys;
  command_keys_ = settings_.commandKeys;
  scene_menu_key_ = settings_.sceneMenuKeys;
  asr_menu_key_ = settings_.asrMenuKeys;
  reloadSceneConfig();
  reloadAsrMenuItems();
}

void VinputEngine::reloadSceneConfig() {
  auto core_config = LoadCoreConfig();
  scene_config_.activeSceneId = core_config.scenes.activeScene;
  scene_config_.scenes = core_config.scenes.definitions;
  active_scene_id_ = scene_config_.activeSceneId;
}

void VinputEngine::rebuildUiConfig() const {
  ui_config_ = std::make_unique<VinputConfig>(settings_);
}

void VinputEngine::rememberInputContext(fcitx::InputContext *ic) {
  if (!ic) {
    return;
  }
  last_active_ic_ = ic;
}

fcitx::InputContext *VinputEngine::resolveFrontendInputContext(
    fcitx::InputContext *fallback_ic) const {
  if (session_) {
    return session_->ic;
  }
  if (status_ic_) {
    return status_ic_;
  }
  if (fallback_ic) {
    return fallback_ic;
  }
  return last_active_ic_;
}

fcitx::AddonInstance *
VinputEngineFactory::create(fcitx::AddonManager *manager) {
  return new VinputEngine(manager->instance());
}

#ifdef VINPUT_FCITX5_CORE_HAVE_ADDON_FACTORY_V2
FCITX_ADDON_FACTORY_V2(vinput, VinputEngineFactory);
#else
FCITX_ADDON_FACTORY(VinputEngineFactory);
#endif
