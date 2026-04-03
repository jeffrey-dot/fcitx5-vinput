#include "pages/control/control_page.h"

#include <QDesktopServices>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QUrl>
#include <QVBoxLayout>

#include "common/utils/sandbox.h"
#include "dialogs/asr_provider_dialog.h"
#include "utils/gui_helpers.h"

#include "common/audio/pipewire_device.h"
#include "gui/utils/config_manager.h"
#include "gui/utils/i18n_cache.h"
#include "cli/runtime/dbus_client.h"
#include "cli/runtime/systemd_client.h"

namespace vinput::gui {

namespace {

bool ReloadAsrBackend(std::string *error = nullptr) {
  vinput::cli::DbusClient dbus;
  std::string daemon_error;
  if (!dbus.IsDaemonRunning(&daemon_error)) {
    if (error) {
      *error = daemon_error;
    }
    return daemon_error.empty();
  }
  return dbus.ReloadAsrBackend(error);
}

}  // namespace

ControlPage::ControlPage(QWidget *parent) : QWidget(parent) {
  auto *layout = new QVBoxLayout(this);

  auto *formLayout = new QFormLayout();
  comboDevice_ = new QComboBox();
  comboDevice_->setEditable(false);
  formLayout->addRow(tr("Capture Device:"), comboDevice_);
  layout->addLayout(formLayout);

  layout->addSpacing(20);

  // ASR Providers section
  auto *asrFrame = new QFrame();
  asrFrame->setFrameShape(QFrame::StyledPanel);
  auto *asrLayout = new QVBoxLayout(asrFrame);

  auto *asrTitle = new QLabel(tr("<b>ASR Providers</b>"));
  asrLayout->addWidget(asrTitle);

  auto *asrListLayout = new QHBoxLayout();
  listAsrProviders_ = new QListWidget();
  asrListLayout->addWidget(listAsrProviders_);

  auto *asrBtnLayout = new QVBoxLayout();
  btnAsrEdit_ = new QPushButton(tr("Edit"));
  btnAsrRemove_ = new QPushButton(tr("Remove"));
  btnAsrSetActive_ = new QPushButton(tr("Activate"));
  asrBtnLayout->addWidget(btnAsrEdit_);
  asrBtnLayout->addWidget(btnAsrRemove_);
  asrBtnLayout->addWidget(btnAsrSetActive_);
  asrBtnLayout->addStretch();
  asrListLayout->addLayout(asrBtnLayout);
  asrLayout->addLayout(asrListLayout);
  layout->addWidget(asrFrame);

  connect(btnAsrEdit_, &QPushButton::clicked, this, &ControlPage::onAsrEdit);
  connect(btnAsrRemove_, &QPushButton::clicked, this,
          &ControlPage::onAsrRemove);
  connect(btnAsrSetActive_, &QPushButton::clicked, this,
          &ControlPage::onAsrSetActive);
  connect(listAsrProviders_, &QListWidget::currentItemChanged, this,
          [this](QListWidgetItem *, QListWidgetItem *) { updateAsrButtons(); });
  btnAsrEdit_->setEnabled(false);
  btnAsrRemove_->setEnabled(false);
  btnAsrSetActive_->setEnabled(false);

  connect(&I18nCache::Get(), &I18nCache::mapUpdated, this,
          &ControlPage::refreshAsrList);

  // Daemon section
  auto *daemonFrame = new QFrame();
  daemonFrame->setFrameShape(QFrame::StyledPanel);
  auto *daemonLayout = new QVBoxLayout(daemonFrame);

  auto *statusLayout = new QHBoxLayout();
  statusLayout->addWidget(new QLabel(tr("Daemon Status:")));
  lblDaemonStatus_ = new QLabel(tr("Unknown"));
  QFont boldFont = lblDaemonStatus_->font();
  boldFont.setBold(true);
  lblDaemonStatus_->setFont(boldFont);
  statusLayout->addWidget(lblDaemonStatus_);
  statusLayout->addStretch();
  daemonLayout->addLayout(statusLayout);

  auto *btnLayout = new QHBoxLayout();
  btnDaemonStart_ = new QPushButton(tr("Start"));
  btnDaemonStop_ = new QPushButton(tr("Stop"));
  btnDaemonRestart_ = new QPushButton(tr("Restart"));
  btnLayout->addWidget(btnDaemonStart_);
  btnLayout->addWidget(btnDaemonStop_);
  btnLayout->addWidget(btnDaemonRestart_);
  btnLayout->addStretch();
  daemonLayout->addLayout(btnLayout);
  layout->addWidget(daemonFrame);

  connect(btnDaemonStart_, &QPushButton::clicked, this,
          &ControlPage::onDaemonStart);
  connect(btnDaemonStop_, &QPushButton::clicked, this,
          &ControlPage::onDaemonStop);
  connect(btnDaemonRestart_, &QPushButton::clicked, this,
          &ControlPage::onDaemonRestart);

  layout->addStretch();

  daemonRefreshTimer_ = new QTimer(this);
  connect(daemonRefreshTimer_, &QTimer::timeout, this,
          &ControlPage::refreshDaemonStatus);
  daemonRefreshTimer_->start(2000);

  QTimer::singleShot(0, this, &ControlPage::refreshDaemonStatus);
  QTimer::singleShot(0, this, &ControlPage::checkSandboxPermissions);
}

void ControlPage::reload() {
  comboDevice_->clear();
  comboDevice_->addItem("default", "default");

  const auto devices = vinput::pw::EnumerateAudioSources();
  CoreConfig config = ConfigManager::Get().Load();
  QString activeDevice = QString::fromStdString(config.global.captureDevice);

  for (const auto& dev : devices) {
    QString name = QString::fromStdString(dev.name);
    QString desc = QString::fromStdString(dev.description);
    QString label = desc.isEmpty() ? name : QString("%1 - %2").arg(name, desc);
    comboDevice_->addItem(label, name);
    if (name == activeDevice) {
      comboDevice_->setCurrentIndex(comboDevice_->count() - 1);
    }
  }
  if (comboDevice_->currentIndex() <= 0) {
    comboDevice_->setCurrentIndex(0);
  }

  refreshAsrList();
}

QString ControlPage::currentDevice() const {
  QString val = comboDevice_->currentData().toString();
  if (val.isEmpty())
    val = comboDevice_->currentText();
  return val;
}

void ControlPage::refreshAsrList() {
  listAsrProviders_->clear();
  CoreConfig config = ConfigManager::Get().Load();
  
  auto i18n_map = I18nCache::Get().GetMap();

  for (const auto& provider : config.asr.providers) {
    QString id = QString::fromStdString(AsrProviderId(provider));
    QString type = QString::fromStdString(std::string(AsrProviderType(provider)));
    bool active = (id.toStdString() == config.asr.activeProvider);
    
    std::string id_str = id.toStdString();
    QString title = QString::fromStdString(
        vinput::registry::LookupI18n(i18n_map, id_str + ".title", id_str));

    QString display = title + " [" + type + "]";
    if (const auto* local = std::get_if<LocalAsrProvider>(&provider)) {
      QString model = QString::fromStdString(local->model);
      if (!model.isEmpty()) {
         model = QString::fromStdString(
             vinput::registry::LookupI18n(i18n_map, local->model + ".title", local->model));
      }
      display += " · " + (model.isEmpty() ? GuiTranslate("(not set)") : model);
    } else if (const auto* command = std::get_if<CommandAsrProvider>(&provider)) {
      display += " · " + QString::fromStdString(command->command);
    }
    if (active) {
      display += GuiTranslate(" *");
    }

    auto *item = new QListWidgetItem(display, listAsrProviders_);
    item->setData(Qt::UserRole, id);
    item->setData(Qt::UserRole + 1, type);
  }
}

void ControlPage::updateAsrButtons() {
  auto *item = listAsrProviders_->currentItem();
  if (!item) {
    btnAsrEdit_->setEnabled(false);
    btnAsrRemove_->setEnabled(false);
    btnAsrSetActive_->setEnabled(false);
    return;
  }
  const bool is_local = item->data(Qt::UserRole + 1).toString() == "local";
  btnAsrEdit_->setEnabled(!is_local);
  btnAsrRemove_->setEnabled(!is_local);
  btnAsrSetActive_->setEnabled(true);
}

void ControlPage::onAsrEdit() {
  auto *item = listAsrProviders_->currentItem();
  if (!item) return;
  QString provider_id = item->data(Qt::UserRole).toString();

  CoreConfig config = ConfigManager::Get().Load();
  AsrProviderData current;
  bool found = false;
  
  for (const auto& provider : config.asr.providers) {
    if (AsrProviderId(provider) == provider_id.toStdString()) {
      current.id = AsrProviderId(provider);
      current.type = std::string(AsrProviderType(provider));
      current.timeout_ms = AsrProviderTimeoutMs(provider);
      if (const auto* local = std::get_if<LocalAsrProvider>(&provider)) {
        current.model = local->model;
      } else if (const auto* cmd = std::get_if<CommandAsrProvider>(&provider)) {
        current.command = cmd->command;
        current.args = cmd->args;
        current.env = cmd->env;
      }
      found = true;
      break;
    }
  }
  
  if (!found) return;

  AsrProviderData updated;
  if (!ShowAsrProviderDialog(this, tr("Edit ASR Provider"), &current, &updated)) {
    return;
  }

  // Remove old
  auto it = std::remove_if(config.asr.providers.begin(), config.asr.providers.end(),
                           [&](const AsrProvider& p) { return AsrProviderId(p) == current.id; });
                           
  config.asr.providers.erase(it, config.asr.providers.end());
  
  // Add new
  if (updated.type == "local") {
     LocalAsrProvider p;
     p.model = updated.model;
     p.timeoutMs = updated.timeout_ms;
     config.asr.providers.push_back(p);
  } else {
     CommandAsrProvider p;
     p.id = updated.id;
     p.command = updated.command;
     p.args = updated.args;
     p.env = updated.env;
     p.timeoutMs = updated.timeout_ms;
     config.asr.providers.push_back(p);
  }
  
  std::string err;
  if (!ConfigManager::Get().Save(config)) {
     QMessageBox::critical(this, tr("Error"), tr("Failed to save config."));
     return;
  }
  if (!ReloadAsrBackend(&err)) {
     QMessageBox::warning(this, tr("Warning"),
                          tr("Config saved, but failed to reload ASR backend: %1")
                              .arg(QString::fromStdString(err)));
  }
  
  refreshAsrList();
  emit configChanged();
}

void ControlPage::onAsrRemove() {
  auto *item = listAsrProviders_->currentItem();
  if (!item) return;

  QString provider_id = item->data(Qt::UserRole).toString();
  auto response = QMessageBox::question(
      this, tr("Confirm"),
      tr("Are you sure you want to remove ASR provider '%1'?").arg(provider_id));
  if (response != QMessageBox::Yes) return;

  CoreConfig config = ConfigManager::Get().Load();
  auto it = std::remove_if(config.asr.providers.begin(), config.asr.providers.end(),
                           [&](const AsrProvider& p) { return AsrProviderId(p) == provider_id.toStdString(); });
  if (it != config.asr.providers.end()) {
      if (std::holds_alternative<LocalAsrProvider>(*it)) {
          QMessageBox::warning(this, tr("Error"), tr("The local ASR provider cannot be removed."));
          return;
      }
      config.asr.providers.erase(it, config.asr.providers.end());
      if (config.asr.activeProvider == provider_id.toStdString()) {
          config.asr.activeProvider.clear();
      }
      
      if (!ConfigManager::Get().Save(config)) {
          QMessageBox::critical(this, tr("Error"), tr("Failed to save config."));
          return;
      }
      std::string err;
      if (!ReloadAsrBackend(&err)) {
          QMessageBox::warning(
              this, tr("Warning"),
              tr("Config saved, but failed to reload ASR backend: %1")
                  .arg(QString::fromStdString(err)));
      }
      refreshAsrList();
      emit configChanged();
  }
}

void ControlPage::onAsrSetActive() {
  auto *item = listAsrProviders_->currentItem();
  if (!item) return;

  QString provider_id = item->data(Qt::UserRole).toString();
  
  CoreConfig config = ConfigManager::Get().Load();
  config.asr.activeProvider = provider_id.toStdString();
  if (!ConfigManager::Get().Save(config)) {
      QMessageBox::critical(this, tr("Error"), tr("Failed to save config."));
      return;
  }
  std::string err;
  if (!ReloadAsrBackend(&err)) {
      QMessageBox::warning(this, tr("Warning"),
                           tr("Config saved, but failed to reload ASR backend: %1")
                               .arg(QString::fromStdString(err)));
  }
  
  refreshAsrList();
  emit configChanged();
}

void ControlPage::refreshDaemonStatus() {
  vinput::cli::DbusClient dbus;
  std::string err;
  if (!dbus.IsDaemonRunning(&err)) {
      lblDaemonStatus_->setText(tr("Stopped"));
      btnDaemonStart_->setEnabled(true);
      btnDaemonStop_->setEnabled(false);
      btnDaemonRestart_->setEnabled(false);
      return;
  }
  std::string status;
  if (!dbus.GetDaemonStatus(&status, &err)) {
      lblDaemonStatus_->setText(tr("Running (Status Error: %1)").arg(QString::fromStdString(err)));
  } else {
      lblDaemonStatus_->setText(tr("Running: %1").arg(QString::fromStdString(status)));
  }
  btnDaemonStart_->setEnabled(false);
  btnDaemonStop_->setEnabled(true);
  btnDaemonRestart_->setEnabled(true);
}

void ControlPage::onDaemonStart() {
  btnDaemonStart_->setEnabled(false);
  const auto result = vinput::cli::SystemctlStartWithDiagnostics();
  if (!result.ok()) {
      vinput::cli::NotifyDaemonNotification(result.notification);
      QMessageBox::critical(this, tr("Error"),
                            QString::fromStdString(result.failure_message));
  }
  refreshDaemonStatus();
}

void ControlPage::onDaemonStop() {
  btnDaemonStop_->setEnabled(false);
  vinput::cli::SystemctlStop();
}

void ControlPage::onDaemonRestart() {
  btnDaemonRestart_->setEnabled(false);
  btnDaemonStop_->setEnabled(false);
  const auto result = vinput::cli::SystemctlRestartWithDiagnostics();
  if (!result.ok()) {
      vinput::cli::NotifyDaemonNotification(result.notification);
      QMessageBox::critical(this, tr("Error"),
                            QString::fromStdString(result.failure_message));
  }
  refreshDaemonStatus();
}

void ControlPage::checkSandboxPermissions() {
  auto missing = vinput::sandbox::MissingSandboxPermissions();
  if (missing.empty())
    return;

  QMessageBox msg(this);
  msg.setIcon(QMessageBox::Warning);
  msg.setWindowTitle(tr("Additional Install Required"));
  msg.setText(tr("Vinput requires additional Flatpak permissions.\n"
                 "Please follow the instructions."));
  if (msg.exec() == QMessageBox::Ok) {
    QDesktopServices::openUrl(
        QUrl("https://github.com/xifan2333/fcitx5-vinput#Flatpak"));
  }
}

}  // namespace vinput::gui
