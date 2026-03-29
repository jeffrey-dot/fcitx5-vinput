#include "pages/llm/llm_page.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>

#include "dialogs/adapter_dialog.h"
#include "utils/cli_runner.h"
#include "utils/gui_helpers.h"

namespace vinput::gui {

namespace {

constexpr int kDefaultTimeoutMs = 30000;
constexpr int kDefaultCandidateCount = 3;
constexpr int kMinCandidateCount = 1;
constexpr int kMaxCandidateCount = 10;

QString SceneLabelForGui(const QJsonObject &scene) {
  QString label = scene.value("label").toString();
  QString id = scene.value("id").toString();
  if (label == "raw" || (label.isEmpty() && id == "raw"))
    return GuiTranslate("Raw");
  if (label == "command" || (label.isEmpty() && id == "command"))
    return GuiTranslate("Command");
  if (!label.isEmpty())
    return label;
  return id;
}

}  // namespace

LlmPage::LlmPage(QWidget *parent) : QWidget(parent) {
  auto *layout = new QVBoxLayout(this);

  layout->addWidget(new QLabel(tr("<b>Providers</b>")));
  auto *listLayout = new QHBoxLayout();
  listProviders_ = new QListWidget();
  listLayout->addWidget(listProviders_);

  auto *btnLayout = new QVBoxLayout();
  btnLlmAdd_ = new QPushButton(tr("Add"));
  btnLlmEdit_ = new QPushButton(tr("Edit"));
  btnLlmRemove_ = new QPushButton(tr("Remove"));
  btnLayout->addWidget(btnLlmAdd_);
  btnLayout->addWidget(btnLlmEdit_);
  btnLayout->addWidget(btnLlmRemove_);
  btnLayout->addStretch();
  listLayout->addLayout(btnLayout);
  layout->addLayout(listLayout);

  auto *hint = new QLabel(tr(
      "LLM adapters are local OpenAI-compatible bridge processes. Install them "
      "from the Resources page, then manage runtime here."));
  hint->setWordWrap(true);
  layout->addWidget(hint);

  auto *adapterLayout = new QHBoxLayout();
  listAdapters_ = new QListWidget();
  adapterLayout->addWidget(listAdapters_);

  auto *adapterBtnLayout = new QVBoxLayout();
  btnAdapterEdit_ = new QPushButton(tr("Edit"));
  btnAdapterStart_ = new QPushButton(tr("Start"));
  btnAdapterStop_ = new QPushButton(tr("Stop"));
  btnAdapterRefresh_ = new QPushButton(tr("Refresh"));
  adapterBtnLayout->addWidget(btnAdapterEdit_);
  adapterBtnLayout->addWidget(btnAdapterStart_);
  adapterBtnLayout->addWidget(btnAdapterStop_);
  adapterBtnLayout->addWidget(btnAdapterRefresh_);
  adapterBtnLayout->addStretch();
  adapterLayout->addLayout(adapterBtnLayout);

  layout->addWidget(new QLabel(tr("<b>Installed Adapters</b>")));
  layout->addLayout(adapterLayout);

  // Scenes section
  layout->addWidget(new QLabel(tr("<b>Scenes</b>")));
  auto *sceneLayout = new QHBoxLayout();
  listScenes_ = new QListWidget();
  sceneLayout->addWidget(listScenes_);

  auto *sceneBtnLayout = new QVBoxLayout();
  btnSceneAdd_ = new QPushButton(tr("Add"));
  btnSceneEdit_ = new QPushButton(tr("Edit"));
  btnSceneRemove_ = new QPushButton(tr("Remove"));
  btnSceneSetActive_ = new QPushButton(tr("Set Active"));
  sceneBtnLayout->addWidget(btnSceneAdd_);
  sceneBtnLayout->addWidget(btnSceneEdit_);
  sceneBtnLayout->addWidget(btnSceneRemove_);
  sceneBtnLayout->addWidget(btnSceneSetActive_);
  sceneBtnLayout->addStretch();
  sceneLayout->addLayout(sceneBtnLayout);
  layout->addLayout(sceneLayout);

  connect(btnLlmAdd_, &QPushButton::clicked, this, &LlmPage::onLlmAdd);
  connect(btnLlmEdit_, &QPushButton::clicked, this, &LlmPage::onLlmEdit);
  connect(btnLlmRemove_, &QPushButton::clicked, this, &LlmPage::onLlmRemove);
  connect(btnAdapterEdit_, &QPushButton::clicked, this,
          &LlmPage::onAdapterEdit);
  connect(btnAdapterStart_, &QPushButton::clicked, this,
          &LlmPage::onAdapterStart);
  connect(btnAdapterStop_, &QPushButton::clicked, this,
          &LlmPage::onAdapterStop);
  connect(btnAdapterRefresh_, &QPushButton::clicked, this,
          &LlmPage::refreshAdapterList);
  connect(btnSceneAdd_, &QPushButton::clicked, this, &LlmPage::onSceneAdd);
  connect(btnSceneEdit_, &QPushButton::clicked, this, &LlmPage::onSceneEdit);
  connect(btnSceneRemove_, &QPushButton::clicked, this,
          &LlmPage::onSceneRemove);
  connect(btnSceneSetActive_, &QPushButton::clicked, this,
          &LlmPage::onSceneSetActive);

  QTimer::singleShot(0, this, &LlmPage::refreshAdapterList);
  QTimer::singleShot(0, this, &LlmPage::refreshLlmList);
  QTimer::singleShot(0, this, &LlmPage::refreshSceneList);
}

void LlmPage::reload() {
  refreshLlmList();
  refreshAdapterList();
  refreshSceneList();
}

void LlmPage::refreshLlmList() {
  listProviders_->clear();

  QJsonDocument doc;
  if (!RunVinputJson({"llm", "list"}, &doc) || !doc.isArray()) {
    return;
  }

  for (const auto &v : doc.array()) {
    if (!v.isObject())
      continue;
    QJsonObject obj = v.toObject();
    QString name = obj.value("id").toString();
    QString base_url = obj.value("base_url").toString();
    QString display = QString("%1 @ %2").arg(name, base_url);

    auto *item = new QListWidgetItem(display, listProviders_);
    item->setData(Qt::UserRole, name);
  }
}

void LlmPage::refreshAdapterList() {
  listAdapters_->clear();

  QJsonDocument doc;
  if (!RunVinputJson({"adapter", "list"}, &doc) || !doc.isArray()) {
    return;
  }

  for (const auto &v : doc.array()) {
    if (!v.isObject())
      continue;
    QJsonObject obj = v.toObject();
    QString id = obj.value("id").toString();
    bool running = obj.value("running").toBool(false);

    QString display = id + " · " +
                      (running ? GuiTranslate("running")
                               : GuiTranslate("stopped"));
    QString command = obj.value("command").toString();
    if (!command.isEmpty()) {
      display += " · " + command;
    }

    auto *item = new QListWidgetItem(display, listAdapters_);
    item->setData(Qt::UserRole, id);
    item->setData(Qt::UserRole + 1, running);

    // Build tooltip
    QString tooltip;
    if (!command.isEmpty()) {
      tooltip += "\n" + tr("Command: %1").arg(command);
    }
    QJsonArray argsArr = obj.value("args").toArray();
    if (!argsArr.isEmpty()) {
      QStringList argsList;
      for (const auto &a : argsArr)
        argsList << a.toString();
      tooltip += "\n" + tr("Args: %1").arg(argsList.join(" "));
    }
    QJsonObject envObj = obj.value("env").toObject();
    if (!envObj.isEmpty()) {
      QStringList envList;
      for (auto it = envObj.begin(); it != envObj.end(); ++it)
        envList << it.key() + "=" + it.value().toString();
      tooltip += "\n" + tr("Env: %1").arg(envList.join(" "));
    }
    item->setToolTip(tooltip.trimmed());
  }
}

void LlmPage::onLlmAdd() {
  QDialog dialog(this);
  dialog.setWindowTitle(tr("Add LLM Provider"));

  auto *form = new QFormLayout();
  auto *editName = new QLineEdit();
  auto *editBaseUrl = new QLineEdit();
  auto *editApiKey = new QLineEdit();
  editApiKey->setEchoMode(QLineEdit::Password);

  form->addRow(tr("Name:"), editName);
  form->addRow(tr("Base URL:"), editBaseUrl);
  form->addRow(tr("API Key:"), editApiKey);

  auto *buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  auto *dlgLayout = new QVBoxLayout(&dialog);
  dlgLayout->addLayout(form);
  dlgLayout->addWidget(buttons);

  if (dialog.exec() != QDialog::Accepted)
    return;

  const QString name_text = editName->text().trimmed();
  const QString base_url_text = editBaseUrl->text().trimmed();
  QString validation_error;
  if (!ValidateProviderInput(name_text, base_url_text, &validation_error)) {
    QMessageBox::warning(this, tr("Error"), validation_error);
    return;
  }

  QStringList args = {"llm", "add", name_text, "-u", base_url_text};
  QString api_key = editApiKey->text();
  if (!api_key.isEmpty()) {
    args << "-k" << api_key;
  }

  QString error;
  if (!RunVinputCommand(args, &error)) {
    QMessageBox::warning(this, tr("Error"), error);
    return;
  }
  refreshLlmList();
  emit configChanged();
}

void LlmPage::onLlmEdit() {
  auto *item = listProviders_->currentItem();
  if (!item)
    return;

  QString provider_name = item->data(Qt::UserRole).toString();

  // Load current data from CLI
  QJsonDocument doc;
  if (!RunVinputJson({"llm", "list"}, &doc) || !doc.isArray()) {
    return;
  }

  QString current_base_url;
  for (const auto &v : doc.array()) {
    QJsonObject obj = v.toObject();
    if (obj.value("id").toString() == provider_name) {
      current_base_url = obj.value("base_url").toString();
      break;
    }
  }

  QDialog dialog(this);
  dialog.setWindowTitle(tr("Edit LLM Provider"));

  auto *form = new QFormLayout();
  auto *editName = new QLineEdit(provider_name);
  editName->setReadOnly(true);
  auto *editBaseUrl = new QLineEdit(current_base_url);
  auto *editApiKey = new QLineEdit();
  editApiKey->setEchoMode(QLineEdit::Password);
  editApiKey->setPlaceholderText(tr("Leave empty to keep current key"));

  form->addRow(tr("Name:"), editName);
  form->addRow(tr("Base URL:"), editBaseUrl);
  form->addRow(tr("API Key:"), editApiKey);

  auto *buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  auto *dlgLayout = new QVBoxLayout(&dialog);
  dlgLayout->addLayout(form);
  dlgLayout->addWidget(buttons);

  if (dialog.exec() != QDialog::Accepted)
    return;

  const QString base_url_text = editBaseUrl->text().trimmed();
  QString validation_error;
  if (!ValidateProviderInput(provider_name, base_url_text, &validation_error)) {
    QMessageBox::warning(this, tr("Error"), validation_error);
    return;
  }

  QStringList args = {"llm", "edit", provider_name, "-u", base_url_text};
  QString api_key = editApiKey->text();
  if (!api_key.isEmpty()) {
    args << "-k" << api_key;
  }

  QString error;
  if (!RunVinputCommand(args, &error)) {
    QMessageBox::warning(this, tr("Error"), error);
    return;
  }
  refreshLlmList();
  emit configChanged();
}

void LlmPage::onLlmRemove() {
  auto *item = listProviders_->currentItem();
  if (!item)
    return;

  QString provider_name = item->data(Qt::UserRole).toString();
  auto response = QMessageBox::question(
      this, tr("Confirm"),
      tr("Are you sure you want to remove LLM provider '%1'?")
          .arg(provider_name));
  if (response != QMessageBox::Yes)
    return;

  QString error;
  if (!RunVinputCommand({"llm", "remove", provider_name}, &error)) {
    QMessageBox::warning(this, tr("Error"), error);
    return;
  }
  refreshLlmList();
  emit configChanged();
}

void LlmPage::onAdapterEdit() {
  auto *item = listAdapters_->currentItem();
  if (!item)
    return;

  QString adapter_id = item->data(Qt::UserRole).toString();

  // Load current data from CLI
  QJsonDocument doc;
  if (!RunVinputJson({"adapter", "list"}, &doc) || !doc.isArray()) {
    return;
  }

  AdapterData current;
  bool found = false;
  for (const auto &v : doc.array()) {
    QJsonObject obj = v.toObject();
    if (obj.value("id").toString() == adapter_id) {
      current.id = adapter_id.toStdString();
      current.command = obj.value("command").toString().toStdString();
      for (const auto &a : obj.value("args").toArray())
        current.args.push_back(a.toString().toStdString());
      QJsonObject envObj = obj.value("env").toObject();
      for (auto it = envObj.begin(); it != envObj.end(); ++it)
        current.env[it.key().toStdString()] =
            it.value().toString().toStdString();
      found = true;
      break;
    }
  }
  if (!found) {
    QMessageBox::warning(
        this, tr("Error"),
        tr("Adapter '%1' not found in configuration.").arg(adapter_id));
    return;
  }

  AdapterData updated;
  if (!ShowAdapterDialog(this, current, &updated)) {
    return;
  }

  // Use config set to update the adapter fields
  // Find the index of this adapter in the config
  QJsonDocument configDoc;
  if (!RunVinputJson({"adapter", "list"}, &configDoc) ||
      !configDoc.isArray()) {
    return;
  }

  int idx = -1;
  for (int i = 0; i < configDoc.array().size(); ++i) {
    if (configDoc.array()[i].toObject().value("id").toString() ==
            adapter_id ||
        configDoc.array()[i].toObject().value("machine_id").toString() ==
            adapter_id) {
      idx = i;
      break;
    }
  }
  if (idx < 0)
    return;

  // Get the machine_id for config path
  QString machine_id =
      configDoc.array()[idx].toObject().value("machine_id").toString();
  if (machine_id.isEmpty())
    machine_id = adapter_id;

  // Build JSON for the adapter config
  QJsonObject adapterObj;
  adapterObj["id"] = QString::fromStdString(updated.id);
  adapterObj["command"] = QString::fromStdString(updated.command);
  QJsonArray argsArr;
  for (const auto &a : updated.args)
    argsArr.append(QString::fromStdString(a));
  adapterObj["args"] = argsArr;
  QJsonObject envObj;
  for (const auto &[k, v] : updated.env)
    envObj[QString::fromStdString(k)] = QString::fromStdString(v);
  adapterObj["env"] = envObj;

  QString path = QString("/llm/adapters/%1").arg(idx);
  QString error;
  if (!RunVinputCommand(
          {"config", "set", path,
           QString::fromUtf8(
               QJsonDocument(adapterObj).toJson(QJsonDocument::Compact))},
          &error)) {
    QMessageBox::critical(this, tr("Error"), error);
    return;
  }

  refreshAdapterList();
  emit configChanged();
}

void LlmPage::onAdapterStart() {
  auto *item = listAdapters_->currentItem();
  if (!item)
    return;

  const QString adapter_id = item->data(Qt::UserRole).toString();
  QString error;
  if (!RunVinputCommand({"adapter", "start", adapter_id}, &error, -1)) {
    QMessageBox::warning(this, tr("Error"), error);
    return;
  }

  QMessageBox::information(this, tr("LLM Adapter Started"),
                           tr("Adapter '%1' started.").arg(adapter_id));
  refreshAdapterList();
}

void LlmPage::onAdapterStop() {
  auto *item = listAdapters_->currentItem();
  if (!item)
    return;

  const QString adapter_id = item->data(Qt::UserRole).toString();
  QString error;
  if (!RunVinputCommand({"adapter", "stop", adapter_id}, &error, -1)) {
    QMessageBox::warning(this, tr("Error"), error);
    return;
  }
  refreshAdapterList();
}

void LlmPage::refreshSceneList() {
  listScenes_->clear();

  QJsonDocument doc;
  if (!RunVinputJson({"scene", "list"}, &doc) || !doc.isArray()) {
    return;
  }

  for (const auto &v : doc.array()) {
    if (!v.isObject())
      continue;
    QJsonObject obj = v.toObject();
    QString id = obj.value("id").toString();
    QString label = SceneLabelForGui(obj);
    bool active = obj.value("active").toBool(false);

    QString display = label;
    if (active)
      display += " *";

    auto *item = new QListWidgetItem(display, listScenes_);
    item->setData(Qt::UserRole, id);
  }
}

void LlmPage::onSceneAdd() {
  QDialog dialog(this);
  dialog.setWindowTitle(tr("Add Scene"));

  auto *form = new QFormLayout();
  auto *editId = new QLineEdit();
  auto *editLabel = new QLineEdit();
  auto *editPrompt = new QTextEdit();
  editPrompt->setMaximumHeight(100);
  auto *comboProvider = new QComboBox();
  auto *comboModel = new QComboBox();
  auto *spinTimeout = new QSpinBox();
  spinTimeout->setRange(1000, 300000);
  spinTimeout->setSingleStep(1000);
  spinTimeout->setValue(kDefaultTimeoutMs);
  spinTimeout->setSuffix(" ms");
  auto *spinCandidates = new QSpinBox();
  spinCandidates->setRange(kMinCandidateCount, kMaxCandidateCount);
  spinCandidates->setValue(kDefaultCandidateCount);

  SetupProviderModelCombos(comboProvider, comboModel);

  form->addRow(tr("ID:"), editId);
  form->addRow(tr("Label:"), editLabel);
  form->addRow(tr("Prompt:"), editPrompt);
  form->addRow(tr("Provider:"), comboProvider);
  form->addRow(tr("Model:"), comboModel);
  form->addRow(tr("Candidate Count:"), spinCandidates);
  form->addRow(tr("Timeout (ms):"), spinTimeout);

  auto *buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  auto *dlgLayout = new QVBoxLayout(&dialog);
  dlgLayout->addLayout(form);
  dlgLayout->addWidget(buttons);

  if (dialog.exec() != QDialog::Accepted)
    return;

  QStringList args;
  args << "scene" << "add" << "--id" << editId->text().trimmed();
  QString label = editLabel->text().trimmed();
  if (!label.isEmpty())
    args << "-l" << label;
  QString prompt = editPrompt->toPlainText();
  if (!prompt.isEmpty())
    args << "-t" << prompt;
  QString provider = comboProvider->currentText();
  if (!provider.isEmpty())
    args << "-p" << provider;
  QString model = comboModel->currentText().trimmed();
  if (!model.isEmpty())
    args << "-m" << model;
  args << "-c" << QString::number(spinCandidates->value());
  args << "--timeout" << QString::number(spinTimeout->value());

  QString error;
  if (!RunVinputCommand(args, &error)) {
    QMessageBox::warning(this, tr("Error"), error);
    return;
  }
  refreshSceneList();
  emit configChanged();
}

void LlmPage::onSceneEdit() {
  auto *item = listScenes_->currentItem();
  if (!item)
    return;

  QString scene_id = item->data(Qt::UserRole).toString();

  QJsonDocument doc;
  if (!RunVinputJson({"scene", "list"}, &doc) || !doc.isArray())
    return;

  QJsonObject found;
  bool exists = false;
  for (const auto &v : doc.array()) {
    QJsonObject obj = v.toObject();
    if (obj.value("id").toString() == scene_id) {
      found = obj;
      exists = true;
      break;
    }
  }
  if (!exists)
    return;

  QDialog dialog(this);
  dialog.setWindowTitle(tr("Edit Scene"));

  auto *form = new QFormLayout();
  auto *editId = new QLineEdit(scene_id);
  editId->setReadOnly(true);
  auto *editLabel = new QLineEdit(SceneLabelForGui(found));
  auto *editPrompt = new QTextEdit();
  editPrompt->setPlainText(found.value("prompt").toString());
  editPrompt->setMaximumHeight(100);
  auto *comboProvider = new QComboBox();
  auto *comboModel = new QComboBox();
  auto *spinTimeout = new QSpinBox();
  spinTimeout->setRange(1000, 300000);
  spinTimeout->setSingleStep(1000);
  spinTimeout->setValue(found.value("timeout_ms").toInt(kDefaultTimeoutMs));
  spinTimeout->setSuffix(" ms");
  auto *spinCandidates = new QSpinBox();
  spinCandidates->setRange(kMinCandidateCount, kMaxCandidateCount);
  spinCandidates->setValue(
      found.value("candidate_count").toInt(kDefaultCandidateCount));

  SetupProviderModelCombos(comboProvider, comboModel,
                           found.value("provider_id").toString(),
                           found.value("model").toString());

  form->addRow(tr("ID:"), editId);
  form->addRow(tr("Label:"), editLabel);
  form->addRow(tr("Prompt:"), editPrompt);
  form->addRow(tr("Provider:"), comboProvider);
  form->addRow(tr("Model:"), comboModel);
  form->addRow(tr("Candidate Count:"), spinCandidates);
  form->addRow(tr("Timeout (ms):"), spinTimeout);

  auto *buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  auto *dlgLayout = new QVBoxLayout(&dialog);
  dlgLayout->addLayout(form);
  dlgLayout->addWidget(buttons);

  if (dialog.exec() != QDialog::Accepted)
    return;

  QStringList args;
  args << "scene" << "edit" << scene_id;
  args << "-l" << editLabel->text().trimmed();
  args << "-t" << editPrompt->toPlainText();
  args << "-p" << comboProvider->currentText();
  args << "-m" << comboModel->currentText().trimmed();
  args << "-c" << QString::number(spinCandidates->value());
  args << "--timeout" << QString::number(spinTimeout->value());

  QString error;
  if (!RunVinputCommand(args, &error)) {
    QMessageBox::warning(this, tr("Error"), error);
    return;
  }
  refreshSceneList();
  emit configChanged();
}

void LlmPage::onSceneRemove() {
  auto *item = listScenes_->currentItem();
  if (!item)
    return;

  QString scene_id = item->data(Qt::UserRole).toString();
  auto response = QMessageBox::question(
      this, tr("Confirm"),
      tr("Are you sure you want to remove scene '%1'?").arg(scene_id));
  if (response != QMessageBox::Yes)
    return;

  QString error;
  if (!RunVinputCommand({"scene", "remove", scene_id}, &error)) {
    QMessageBox::warning(this, tr("Error"), error);
    return;
  }
  refreshSceneList();
  emit configChanged();
}

void LlmPage::onSceneSetActive() {
  auto *item = listScenes_->currentItem();
  if (!item)
    return;

  QString scene_id = item->data(Qt::UserRole).toString();
  QString error;
  if (!RunVinputCommand({"scene", "use", scene_id}, &error)) {
    QMessageBox::warning(this, tr("Error"), error);
    return;
  }
  refreshSceneList();
  emit configChanged();
}

}  // namespace vinput::gui
