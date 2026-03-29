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
#include "utils/cli_runner.h"
#include "utils/gui_helpers.h"

namespace vinput::gui {

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
  // Reload device combo — async
  comboDevice_->clear();
  comboDevice_->addItem("default", "default");

  RunVinputJsonAsync(
      {"device", "list"}, this,
      [this](bool ok, const QJsonDocument &doc, const QString &) {
        if (!ok || !doc.isArray())
          return;
        for (const auto &v : doc.array()) {
          if (!v.isObject())
            continue;
          QJsonObject obj = v.toObject();
          QString name = obj.value("name").toString();
          if (name.isEmpty())
            continue;
          QString desc = obj.value("description").toString();
          QString label =
              desc.isEmpty() ? name : QString("%1 - %2").arg(name, desc);
          bool active = obj.value("active").toBool(false);
          comboDevice_->addItem(label, name);
          if (active) {
            comboDevice_->setCurrentIndex(comboDevice_->count() - 1);
          }
        }
        if (comboDevice_->currentIndex() <= 0) {
          comboDevice_->setCurrentIndex(0);
        }
      });

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

  RunVinputJsonAsync(
      {"provider", "list"}, this,
      [this](bool ok, const QJsonDocument &doc, const QString &) {
        if (!ok || !doc.isArray())
          return;

        for (const auto &v : doc.array()) {
          if (!v.isObject())
            continue;
          QJsonObject obj = v.toObject();
          QString id = obj.value("id").toString();
          QString type = obj.value("type").toString();
          bool active = obj.value("active").toBool(false);

          QString display = id + " [" + type + "]";
          if (type == "local") {
            QString model = obj.value("model").toString();
            display += " · " +
                       (model.isEmpty() ? GuiTranslate("(not set)") : model);
          } else if (!obj.value("command").toString().isEmpty()) {
            display += " · " + obj.value("command").toString();
          }
          if (active) {
            display += GuiTranslate(" *");
          }

          auto *item = new QListWidgetItem(display, listAsrProviders_);
          item->setData(Qt::UserRole, id);
          item->setData(Qt::UserRole + 1, type);
        }
      });
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
  if (!item)
    return;

  QString provider_id = item->data(Qt::UserRole).toString();

  // Load current data from CLI
  QJsonDocument doc;
  if (!RunVinputJson({"provider", "list"}, &doc) || !doc.isArray()) {
    return;
  }

  AsrProviderData current;
  bool found = false;
  for (const auto &v : doc.array()) {
    QJsonObject obj = v.toObject();
    if (obj.value("id").toString() == provider_id) {
      current.id = provider_id.toStdString();
      current.type = obj.value("type").toString().toStdString();
      current.timeout_ms = obj.value("timeout_ms").toInt(15000);
      current.model = obj.value("model").toString().toStdString();
      current.command = obj.value("command").toString().toStdString();
      for (const auto &a : obj.value("args").toArray()) {
        current.args.push_back(a.toString().toStdString());
      }
      QJsonObject envObj = obj.value("env").toObject();
      for (auto it = envObj.begin(); it != envObj.end(); ++it) {
        current.env[it.key().toStdString()] = it.value().toString().toStdString();
      }
      found = true;
      break;
    }
  }
  if (!found)
    return;

  AsrProviderData updated;
  if (!ShowAsrProviderDialog(this, tr("Edit ASR Provider"), &current,
                             &updated)) {
    return;
  }

  // Remove old, add new via CLI
  QString error;
  RunVinputCommand({"provider", "remove", provider_id}, &error);

  // Re-add with new config
  QJsonObject provObj;
  provObj["id"] = QString::fromStdString(updated.id);
  provObj["type"] = QString::fromStdString(updated.type);
  provObj["timeout_ms"] = updated.timeout_ms;
  if (updated.type == "local") {
    provObj["model"] = QString::fromStdString(updated.model);
  } else {
    provObj["command"] = QString::fromStdString(updated.command);
    QJsonArray argsArr;
    for (const auto &a : updated.args)
      argsArr.append(QString::fromStdString(a));
    provObj["args"] = argsArr;
    QJsonObject envObj;
    for (const auto &[k, v] : updated.env)
      envObj[QString::fromStdString(k)] = QString::fromStdString(v);
    provObj["env"] = envObj;
  }
  if (!RunVinputCommand(
          QStringList{"config", "set", "/asr/providers/-",
           QString::fromUtf8(
               QJsonDocument(provObj).toJson(QJsonDocument::Compact))},
          &error)) {
    QMessageBox::critical(this, tr("Error"), error);
    return;
  }

  RestartDaemon();
  refreshAsrList();
  emit configChanged();
}

void ControlPage::onAsrRemove() {
  auto *item = listAsrProviders_->currentItem();
  if (!item)
    return;

  QString provider_id = item->data(Qt::UserRole).toString();
  auto response = QMessageBox::question(
      this, tr("Confirm"),
      tr("Are you sure you want to remove ASR provider '%1'?")
          .arg(provider_id));
  if (response != QMessageBox::Yes)
    return;

  QString error;
  if (!RunVinputCommand({"provider", "remove", provider_id}, &error)) {
    QMessageBox::warning(this, tr("Error"), error);
    return;
  }

  RestartDaemon();
  refreshAsrList();
  emit configChanged();
}

void ControlPage::onAsrSetActive() {
  auto *item = listAsrProviders_->currentItem();
  if (!item)
    return;

  QString provider_id = item->data(Qt::UserRole).toString();
  QString error;
  if (!RunVinputCommand({"provider", "use", provider_id}, &error)) {
    QMessageBox::warning(this, tr("Error"), error);
    return;
  }

  RestartDaemon();
  refreshAsrList();
  emit configChanged();
}

void ControlPage::refreshDaemonStatus() {
  RunVinputJsonAsync(
      {"daemon", "status"}, this,
      [this](bool ok, const QJsonDocument &doc, const QString &err) {
        if (!ok || !doc.isObject()) {
          lblDaemonStatus_->setText(tr("Error: %1").arg(err));
          lblDaemonStatus_->setStyleSheet("color: red;");
          btnDaemonStart_->setEnabled(true);
          btnDaemonStop_->setEnabled(false);
          btnDaemonRestart_->setEnabled(false);
          return;
        }

        QJsonObject obj = doc.object();
        bool running = obj.value("running").toBool();

        if (!running) {
          lblDaemonStatus_->setText(tr("Stopped"));
          lblDaemonStatus_->setStyleSheet("color: gray;");
          btnDaemonStart_->setEnabled(true);
          btnDaemonStop_->setEnabled(false);
          btnDaemonRestart_->setEnabled(false);
          return;
        }

        QString status = obj.value("status").toString();
        if (status.isEmpty()) {
          QString runtime_err = obj.value("error").toString();
          lblDaemonStatus_->setText(
              tr("Running (Status Error: %1)").arg(runtime_err));
          lblDaemonStatus_->setStyleSheet("color: orange;");
        } else {
          lblDaemonStatus_->setText(tr("Running: %1").arg(status));
          lblDaemonStatus_->setStyleSheet("color: green;");
        }

        btnDaemonStart_->setEnabled(false);
        btnDaemonStop_->setEnabled(true);
        btnDaemonRestart_->setEnabled(true);
      });
}

void ControlPage::onDaemonStart() {
  btnDaemonStart_->setEnabled(false);
  StartVinputDetached({"daemon", "start"});
}

void ControlPage::onDaemonStop() {
  btnDaemonStop_->setEnabled(false);
  StartVinputDetached({"daemon", "stop"});
}

void ControlPage::onDaemonRestart() {
  btnDaemonRestart_->setEnabled(false);
  btnDaemonStop_->setEnabled(false);
  StartVinputDetached({"daemon", "restart"});
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
