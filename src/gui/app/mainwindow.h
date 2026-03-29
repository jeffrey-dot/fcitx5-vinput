#pragma once

#include <QMainWindow>
#include <QTabWidget>

namespace vinput::gui {
class ControlPage;
class ResourcePage;
class LlmPage;
class HotwordPage;
}  // namespace vinput::gui

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(QWidget *parent = nullptr);
  ~MainWindow() override;

private slots:
  void onSaveClicked();
  void onOpenConfigClicked();
  void reloadAll();

private:
  QTabWidget *tabWidget_;

  vinput::gui::ControlPage *controlPage_;
  vinput::gui::ResourcePage *resourcePage_;
  vinput::gui::LlmPage *llmPage_;
  vinput::gui::HotwordPage *hotwordPage_;
};
