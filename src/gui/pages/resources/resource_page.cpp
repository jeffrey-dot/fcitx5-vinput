#include "pages/resources/resource_page.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QColor>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPalette>
#include <QProcess>
#include <QTimer>
#include <QVBoxLayout>

#include "utils/cli_runner.h"
#include "utils/gui_helpers.h"

namespace vinput::gui {

namespace {

void SetupTable(QTableWidget *t, const QStringList &headers) {
  t->setColumnCount(headers.size());
  t->setHorizontalHeaderLabels(headers);
  t->setSelectionBehavior(QAbstractItemView::SelectRows);
  t->setSelectionMode(QAbstractItemView::SingleSelection);
  t->setEditTriggers(QAbstractItemView::NoEditTriggers);
  t->setAlternatingRowColors(true);
  t->verticalHeader()->hide();
  t->horizontalHeader()->setStretchLastSection(true);
  t->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
}

}  // namespace

ResourcePage::ResourcePage(QWidget *parent) : QWidget(parent) {
  auto *layout = new QVBoxLayout(this);

  auto *lblLocal = new QLabel(tr("<b>Installed Models</b>"));
  layout->addWidget(lblLocal);

  auto *topLayout = new QHBoxLayout();
  tableInstalledModels_ = new QTableWidget();
  SetupTable(tableInstalledModels_,
             {tr("Name"), tr("Type"), tr("Language"), tr("Size"),
              tr("Hotwords"), tr("Status")});
  topLayout->addWidget(tableInstalledModels_, 1);

  auto *btnLayout = new QVBoxLayout();
  btnUseModel_ = new QPushButton(tr("Use"));
  btnRemoveModel_ = new QPushButton(tr("Remove"));
  btnRefreshResources_ = new QPushButton(tr("Refresh"));
  btnLayout->addWidget(btnUseModel_);
  btnLayout->addWidget(btnRemoveModel_);
  btnLayout->addWidget(btnRefreshResources_);
  btnLayout->addStretch();
  topLayout->addLayout(btnLayout);
  layout->addLayout(topLayout);

  auto *lblRemote = new QLabel(tr("<b>Available Models</b>"));
  layout->addWidget(lblRemote);

  auto *remoteLayout = new QHBoxLayout();
  tableAvailableModels_ = new QTableWidget();
  SetupTable(tableAvailableModels_,
             {tr("Title"), tr("Description"), tr("Type"), tr("Language"),
              tr("Size"), tr("Hotwords"), tr("Status")});
  remoteLayout->addWidget(tableAvailableModels_, 1);

  btnDownloadModel_ = new QPushButton(tr("Download"));
  auto *dlLayout = new QVBoxLayout();
  dlLayout->addWidget(btnDownloadModel_);
  dlLayout->addStretch();
  remoteLayout->addLayout(dlLayout);
  layout->addLayout(remoteLayout);

  auto *lblProviders = new QLabel(tr("<b>Available ASR Providers</b>"));
  layout->addWidget(lblProviders);

  auto *providerLayout = new QHBoxLayout();
  tableAvailableProviders_ = new QTableWidget();
  SetupTable(tableAvailableProviders_,
             {tr("Title"), tr("Description"), tr("Mode"), tr("Status")});
  providerLayout->addWidget(tableAvailableProviders_, 1);

  btnAddProvider_ = new QPushButton(tr("Install"));
  auto *providerBtnLayout = new QVBoxLayout();
  providerBtnLayout->addWidget(btnAddProvider_);
  providerBtnLayout->addStretch();
  providerLayout->addLayout(providerBtnLayout);
  layout->addLayout(providerLayout);

  auto *lblAdapters = new QLabel(tr("<b>Available LLM Adapters</b>"));
  layout->addWidget(lblAdapters);

  auto *adapterLayout = new QHBoxLayout();
  tableAvailableAdapters_ = new QTableWidget();
  SetupTable(tableAvailableAdapters_,
             {tr("Title"), tr("Description"), tr("Status")});
  adapterLayout->addWidget(tableAvailableAdapters_, 1);

  btnAddAdapter_ = new QPushButton(tr("Install"));
  auto *adapterBtnLayout = new QVBoxLayout();
  adapterBtnLayout->addWidget(btnAddAdapter_);
  adapterBtnLayout->addStretch();
  adapterLayout->addLayout(adapterBtnLayout);
  layout->addLayout(adapterLayout);

  textLog_ = new QTextEdit();
  textLog_->setReadOnly(true);
  textLog_->setMaximumHeight(100);
  layout->addWidget(textLog_);

  connect(btnUseModel_, &QPushButton::clicked, this,
          &ResourcePage::onUseModelClicked);
  connect(btnRemoveModel_, &QPushButton::clicked, this,
          &ResourcePage::onRemoveModelClicked);
  connect(btnRefreshResources_, &QPushButton::clicked, this,
          &ResourcePage::refreshAll);
  connect(btnDownloadModel_, &QPushButton::clicked, this,
          &ResourcePage::onDownloadModelClicked);
  connect(btnAddProvider_, &QPushButton::clicked, this,
          &ResourcePage::onAddProviderClicked);
  connect(btnAddAdapter_, &QPushButton::clicked, this,
          &ResourcePage::onAddAdapterClicked);

  QTimer::singleShot(0, this, &ResourcePage::refreshAll);
}

void ResourcePage::reload() { refreshAll(); }

// ---------------------------------------------------------------------------
// Populate helpers (called from sync or async paths)
// ---------------------------------------------------------------------------

void ResourcePage::populateLocalModels(const QJsonDocument &doc) {
  const QPalette pal = QApplication::palette();
  const QColor colorPositive = pal.color(QPalette::Active, QPalette::Link);
  const QColor colorDisabled = pal.color(QPalette::Disabled, QPalette::Text);
  const QColor colorHighlight =
      pal.color(QPalette::Active, QPalette::Highlight);
  const QColor colorError = QColor(198, 40, 40);

  tableInstalledModels_->setRowCount(0);
  if (!doc.isArray())
    return;

  for (const auto &v : doc.array()) {
    if (!v.isObject())
      continue;
    QJsonObject obj = v.toObject();
    QString name = obj.value("name").toString();
    if (name.isEmpty())
      name = obj.value("id").toString();
    if (name.isEmpty())
      continue;

    int row = tableInstalledModels_->rowCount();
    tableInstalledModels_->insertRow(row);
    tableInstalledModels_->setItem(row, 0, MakeCell(name, name));
    tableInstalledModels_->setItem(
        row, 1, MakeCell(obj.value("model_type").toString()));
    tableInstalledModels_->setItem(
        row, 2, MakeCell(obj.value("language").toString()));
    auto *sizeCell = MakeCell(obj.value("size").toString());
    sizeCell->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    tableInstalledModels_->setItem(row, 3, sizeCell);

    bool hw = obj.value("supports_hotwords").toBool(false);
    auto *hwCell = MakeCell(hw ? tr("yes") : tr("no"));
    hwCell->setForeground(hw ? colorPositive : colorDisabled);
    tableInstalledModels_->setItem(row, 4, hwCell);

    QString rawStatus = obj.value("status").toString();
    QString status = rawStatus;
    if (status.isEmpty())
      status = tr("installed");
    else if (status == "active")
      status = tr("active");
    else if (status == "broken")
      status = tr("broken");
    else if (status == "installed")
      status = tr("installed");

    auto *stCell = MakeCell(status);
    if (rawStatus == "active") {
      stCell->setForeground(colorHighlight);
      QFont f = stCell->font();
      f.setBold(true);
      stCell->setFont(f);
    } else if (rawStatus == "broken") {
      stCell->setForeground(colorError);
    }
    tableInstalledModels_->setItem(row, 5, stCell);
  }
}

void ResourcePage::populateRemoteModels(const QJsonDocument &doc) {
  const QPalette pal = QApplication::palette();
  const QColor colorPositive = pal.color(QPalette::Active, QPalette::Link);
  const QColor colorDisabled = pal.color(QPalette::Disabled, QPalette::Text);

  tableAvailableModels_->setRowCount(0);
  if (!doc.isArray())
    return;

  for (const auto &v : doc.array()) {
    if (!v.isObject())
      continue;
    QJsonObject obj = v.toObject();
    QString id = obj.value("id").toString();
    if (id.isEmpty())
      continue;

    QString title = obj.value("title").toString();
    if (title.isEmpty())
      title = id;

    int row = tableAvailableModels_->rowCount();
    tableAvailableModels_->insertRow(row);
    tableAvailableModels_->setItem(row, 0, MakeCell(title, id));
    tableAvailableModels_->setItem(
        row, 1, MakeCell(obj.value("description").toString()));
    tableAvailableModels_->setItem(
        row, 2, MakeCell(obj.value("model_type").toString()));
    tableAvailableModels_->setItem(
        row, 3, MakeCell(obj.value("language").toString()));
    auto *sizeCell = MakeCell(obj.value("size").toString());
    sizeCell->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    tableAvailableModels_->setItem(row, 4, sizeCell);

    bool hw = obj.value("supports_hotwords").toBool(false);
    auto *hwCell = MakeCell(hw ? tr("yes") : tr("no"));
    hwCell->setForeground(hw ? colorPositive : colorDisabled);
    tableAvailableModels_->setItem(row, 5, hwCell);

    QString rawStatus = obj.value("status").toString();
    QString status = rawStatus;
    if (status == "installed")
      status = tr("installed");
    else if (status == "available")
      status = tr("available");
    auto *stCell = MakeCell(status);
    if (rawStatus == "installed") {
      stCell->setForeground(colorDisabled);
      for (int c = 0; c < tableAvailableModels_->columnCount(); ++c) {
        if (auto *ci = tableAvailableModels_->item(row, c))
          ci->setFlags(ci->flags() & ~Qt::ItemIsEnabled);
      }
    } else {
      stCell->setForeground(colorPositive);
    }
    tableAvailableModels_->setItem(row, 6, stCell);
  }
}

void ResourcePage::populateRemoteProviders(const QJsonDocument &doc) {
  const QPalette pal = QApplication::palette();
  const QColor colorPositive = pal.color(QPalette::Active, QPalette::Link);
  const QColor colorDisabled = pal.color(QPalette::Disabled, QPalette::Text);

  tableAvailableProviders_->setRowCount(0);
  if (!doc.isArray())
    return;

  for (const auto &v : doc.array()) {
    if (!v.isObject())
      continue;
    QJsonObject obj = v.toObject();
    QString id = obj.value("id").toString();
    if (id.isEmpty())
      continue;

    QString title = obj.value("title").toString();
    if (title.isEmpty())
      title = id;

    int row = tableAvailableProviders_->rowCount();
    tableAvailableProviders_->insertRow(row);
    tableAvailableProviders_->setItem(row, 0, MakeCell(title, id));
    tableAvailableProviders_->setItem(
        row, 1, MakeCell(obj.value("description").toString()));
    bool stream = obj.value("stream").toBool(false);
    tableAvailableProviders_->setItem(
        row, 2, MakeCell(stream ? tr("stream") : tr("non-stream")));

    QString rawStatus = obj.value("status").toString();
    QString status = rawStatus;
    if (status == "installed")
      status = tr("installed");
    else if (status == "available")
      status = tr("available");
    auto *stCell = MakeCell(status);
    if (rawStatus == "installed") {
      stCell->setForeground(colorDisabled);
      for (int c = 0; c < tableAvailableProviders_->columnCount(); ++c) {
        if (auto *cell = tableAvailableProviders_->item(row, c))
          cell->setFlags(cell->flags() & ~Qt::ItemIsEnabled);
      }
    } else {
      stCell->setForeground(colorPositive);
    }
    tableAvailableProviders_->setItem(row, 3, stCell);
  }
}

void ResourcePage::populateRemoteAdapters(const QJsonDocument &doc) {
  const QPalette pal = QApplication::palette();
  const QColor colorPositive = pal.color(QPalette::Active, QPalette::Link);
  const QColor colorDisabled = pal.color(QPalette::Disabled, QPalette::Text);

  tableAvailableAdapters_->setRowCount(0);
  if (!doc.isArray())
    return;

  for (const auto &v : doc.array()) {
    if (!v.isObject())
      continue;
    QJsonObject obj = v.toObject();
    QString id = obj.value("id").toString();
    if (id.isEmpty())
      continue;

    QString title = obj.value("title").toString();
    if (title.isEmpty())
      title = id;

    int row = tableAvailableAdapters_->rowCount();
    tableAvailableAdapters_->insertRow(row);
    tableAvailableAdapters_->setItem(row, 0, MakeCell(title, id));
    tableAvailableAdapters_->setItem(
        row, 1, MakeCell(obj.value("description").toString()));

    QString rawStatus = obj.value("status").toString();
    QString status = rawStatus;
    if (status == "installed")
      status = tr("installed");
    else if (status == "available")
      status = tr("available");
    auto *stCell = MakeCell(status);
    if (rawStatus == "installed") {
      stCell->setForeground(colorDisabled);
      for (int c = 0; c < tableAvailableAdapters_->columnCount(); ++c) {
        if (auto *cell = tableAvailableAdapters_->item(row, c))
          cell->setFlags(cell->flags() & ~Qt::ItemIsEnabled);
      }
    } else {
      stCell->setForeground(colorPositive);
    }
    tableAvailableAdapters_->setItem(row, 2, stCell);
  }
}

// ---------------------------------------------------------------------------
// refreshAll: local lists sync (fast), remote lists async (network)
// ---------------------------------------------------------------------------

void ResourcePage::refreshAll() {
  // Local models — synchronous (reads filesystem, fast)
  {
    QJsonDocument doc;
    QString err;
    if (RunVinputJson({"model", "list"}, &doc, &err)) {
      populateLocalModels(doc);
    } else if (!err.isEmpty()) {
      textLog_->append(tr("Local model list error: %1").arg(err));
    }
  }

  // Remote models — async (network)
  tableAvailableModels_->setRowCount(0);
  RunVinputJsonAsync(
      {"model", "list", "--available"}, this,
      [this](bool ok, const QJsonDocument &doc, const QString &err) {
        if (ok) {
          populateRemoteModels(doc);
        } else if (!err.isEmpty()) {
          textLog_->append(tr("Remote model list error: %1").arg(err));
        }
      });

  // Remote providers — async (network)
  tableAvailableProviders_->setRowCount(0);
  RunVinputJsonAsync(
      {"provider", "list", "--available"}, this,
      [this](bool ok, const QJsonDocument &doc, const QString &err) {
        if (ok) {
          populateRemoteProviders(doc);
        } else if (!err.isEmpty()) {
          textLog_->append(tr("Remote provider list error: %1").arg(err));
        }
      });

  // Remote adapters — async (network)
  tableAvailableAdapters_->setRowCount(0);
  RunVinputJsonAsync(
      {"adapter", "list", "--available"}, this,
      [this](bool ok, const QJsonDocument &doc, const QString &err) {
        if (ok) {
          populateRemoteAdapters(doc);
        } else if (!err.isEmpty()) {
          textLog_->append(tr("Remote adapter list error: %1").arg(err));
        }
      });
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

void ResourcePage::killCliProcess() {
  if (cliProcess_ && cliProcess_->state() != QProcess::NotRunning) {
    cliProcess_->disconnect();
    cliProcess_->kill();
    cliProcess_->waitForFinished(3000);
  }
}

void ResourcePage::ensureCliProcess() {
  if (!cliProcess_) {
    cliProcess_ = new QProcess(this);
    connect(cliProcess_, &QProcess::readyReadStandardOutput, this,
            &ResourcePage::onProcessReadyReadStandardOutput);
    connect(cliProcess_, &QProcess::readyReadStandardError, this,
            &ResourcePage::onProcessReadyReadStandardError);
    connect(cliProcess_,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &ResourcePage::onProcessFinished);
  }
}

void ResourcePage::onUseModelClicked() {
  auto items = tableInstalledModels_->selectedItems();
  if (items.isEmpty())
    return;
  QString model_name =
      tableInstalledModels_->item(tableInstalledModels_->currentRow(), 0)
          ->data(Qt::UserRole)
          .toString();
  if (model_name.isEmpty())
    model_name =
        tableInstalledModels_->item(tableInstalledModels_->currentRow(), 0)
            ->text();

  QString error;
  if (!RunVinputCommand({"model", "use", model_name}, &error)) {
    QMessageBox::critical(this, tr("Error"), error);
    return;
  }

  RestartDaemon();
  QMessageBox::information(
      this, tr("Local Model Updated"),
      tr("Selected model '%1' has been assigned to the preferred local ASR "
         "provider.")
          .arg(model_name));
  refreshAll();
  emit configChanged();
}

void ResourcePage::onRemoveModelClicked() {
  auto items = tableInstalledModels_->selectedItems();
  if (items.isEmpty())
    return;
  QString model_name =
      tableInstalledModels_->item(tableInstalledModels_->currentRow(), 0)
          ->data(Qt::UserRole)
          .toString();
  if (model_name.isEmpty())
    model_name =
        tableInstalledModels_->item(tableInstalledModels_->currentRow(), 0)
            ->text();

  auto response = QMessageBox::question(
      this, tr("Confirm"),
      tr("Are you sure you want to remove model '%1'?").arg(model_name));
  if (response == QMessageBox::Yes) {
    killCliProcess();
    ensureCliProcess();
    btnDownloadModel_->setEnabled(false);
    btnRemoveModel_->setEnabled(false);
    textLog_->append(tr("Removing %1...").arg(model_name));

    QString vinput_path = ResolveVinputExecutable();
    if (!vinput_path.isEmpty()) {
      cliProcess_->start(vinput_path,
                         {"model", "remove", "--force", model_name});
    }
  }
}

void ResourcePage::onDownloadModelClicked() {
  auto items = tableAvailableModels_->selectedItems();
  if (items.isEmpty())
    return;
  QString model_name =
      tableAvailableModels_->item(tableAvailableModels_->currentRow(), 0)
          ->data(Qt::UserRole)
          .toString();
  if (model_name.isEmpty())
    model_name =
        tableAvailableModels_->item(tableAvailableModels_->currentRow(), 0)
            ->text();

  killCliProcess();
  ensureCliProcess();
  btnDownloadModel_->setEnabled(false);
  btnRemoveModel_->setEnabled(false);

  QString vinput_path = ResolveVinputExecutable();
  if (!vinput_path.isEmpty()) {
    cliProcess_->start(vinput_path, {"model", "add", model_name});
  }
}

void ResourcePage::onAddProviderClicked() {
  auto items = tableAvailableProviders_->selectedItems();
  if (items.isEmpty())
    return;
  QString id =
      tableAvailableProviders_
          ->item(tableAvailableProviders_->currentRow(), 0)
          ->data(Qt::UserRole)
          .toString();
  if (id.isEmpty())
    id = tableAvailableProviders_
             ->item(tableAvailableProviders_->currentRow(), 0)
             ->text();

  QString error;
  if (!RunVinputCommand({"provider", "add", id}, &error, -1)) {
    QMessageBox::warning(this, tr("Error"), error);
    return;
  }
  RestartDaemon();
  refreshAll();
  emit configChanged();
}

void ResourcePage::onAddAdapterClicked() {
  auto items = tableAvailableAdapters_->selectedItems();
  if (items.isEmpty())
    return;
  QString id = tableAvailableAdapters_
                   ->item(tableAvailableAdapters_->currentRow(), 0)
                   ->data(Qt::UserRole)
                   .toString();
  if (id.isEmpty())
    id = tableAvailableAdapters_
             ->item(tableAvailableAdapters_->currentRow(), 0)
             ->text();

  QString error;
  if (!RunVinputCommand({"adapter", "add", id}, &error, -1)) {
    QMessageBox::warning(this, tr("Error"), error);
    return;
  }
  refreshAll();
  emit configChanged();
}

void ResourcePage::onProcessReadyReadStandardOutput() {
  if (cliProcess_) {
    textLog_->append(
        QString::fromUtf8(cliProcess_->readAllStandardOutput()).trimmed());
  }
}

void ResourcePage::onProcessReadyReadStandardError() {
  if (cliProcess_) {
    textLog_->append(
        QString::fromUtf8(cliProcess_->readAllStandardError()).trimmed());
  }
}

void ResourcePage::onProcessFinished(int exitCode, int exitStatus) {
  (void)exitCode;
  (void)exitStatus;
  textLog_->append(tr("Process finished"));
  btnDownloadModel_->setEnabled(true);
  btnRemoveModel_->setEnabled(true);
  refreshAll();
  emit configChanged();
}

}  // namespace vinput::gui
