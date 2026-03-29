#include "mainwindow.h"

#include <QDesktopServices>
#include <QFile>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QTextStream>
#include <QUrl>
#include <QVBoxLayout>

#include "pages/control/control_page.h"
#include "pages/hotwords/hotword_page.h"
#include "pages/llm/llm_page.h"
#include "pages/resources/resource_page.h"
#include "utils/cli_runner.h"

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
  setWindowTitle(tr("Vinput Configuration"));

  auto *centralWidget = new QWidget(this);
  setCentralWidget(centralWidget);
  auto *mainLayout = new QVBoxLayout(centralWidget);

  tabWidget_ = new QTabWidget(this);
  mainLayout->addWidget(tabWidget_);

  controlPage_ = new vinput::gui::ControlPage(this);
  resourcePage_ = new vinput::gui::ResourcePage(this);
  llmPage_ = new vinput::gui::LlmPage(this);
  hotwordPage_ = new vinput::gui::HotwordPage(this);

  tabWidget_->addTab(controlPage_, tr("Control"));
  tabWidget_->addTab(resourcePage_, tr("Resources"));
  tabWidget_->addTab(llmPage_, tr("LLM"));
  tabWidget_->addTab(hotwordPage_, tr("Hotwords"));

  // Cross-page refresh: any config change reloads affected pages.
  connect(controlPage_, &vinput::gui::ControlPage::configChanged, this,
          &MainWindow::reloadAll);
  connect(resourcePage_, &vinput::gui::ResourcePage::configChanged, this,
          &MainWindow::reloadAll);
  connect(llmPage_, &vinput::gui::LlmPage::configChanged, this,
          &MainWindow::reloadAll);

  // Bottom bar
  auto *bottomLayout = new QHBoxLayout();
  auto *btnOpenConfig = new QPushButton(tr("Open Config"), this);
  connect(btnOpenConfig, &QPushButton::clicked, this,
          &MainWindow::onOpenConfigClicked);

  auto *btnSave = new QPushButton(tr("Save Settings"), this);
  connect(btnSave, &QPushButton::clicked, this, &MainWindow::onSaveClicked);

  bottomLayout->addWidget(btnOpenConfig);
  bottomLayout->addStretch();
  bottomLayout->addWidget(btnSave);
  mainLayout->addLayout(bottomLayout);

  // Initial load
  controlPage_->reload();
}

MainWindow::~MainWindow() = default;

void MainWindow::reloadAll() {
  controlPage_->reload();
  llmPage_->reload();
}

void MainWindow::onSaveClicked() {
  // Save device via CLI
  QString device = controlPage_->currentDevice();
  if (!device.isEmpty() && device != "default") {
    QString error;
    vinput::gui::RunVinputCommand({"device", "use", device}, &error);
  }

  // Save hotwords via CLI
  QString hotwordsFile = hotwordPage_->hotwordsFilePath();
  if (!hotwordsFile.isEmpty()) {
    QString error;
    vinput::gui::RunVinputCommand({"hotword", "set", hotwordsFile}, &error);

    // Write hotwords content to file
    QFile f(hotwordsFile);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
      QTextStream(&f) << hotwordPage_->hotwordsContent();
    }
  } else {
    QString error;
    vinput::gui::RunVinputCommand({"hotword", "clear"}, &error);
  }

  // Restart daemon to apply
  vinput::gui::RestartDaemon();
  QMessageBox::information(this, tr("Success"),
                           tr("Settings saved successfully!"));
  close();
}

void MainWindow::onOpenConfigClicked() {
  // Use config edit to get the path, or just open known location
  QJsonDocument doc;
  QString err;
  if (vinput::gui::RunVinputJson({"config", "get", "/version"}, &doc, &err) &&
      doc.isObject()) {
    // config get returns {"path": ..., "value": ...}
    // We just want to open the config file
  }
  // Fallback: open via CLI
  vinput::gui::StartVinputDetached({"config", "edit", "core"});
}
