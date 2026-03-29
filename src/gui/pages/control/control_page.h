#pragma once

#include <QComboBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>
#include <QWidget>

namespace vinput::gui {

class ControlPage : public QWidget {
  Q_OBJECT

public:
  explicit ControlPage(QWidget *parent = nullptr);

  // Reload device combo and ASR list from CLI.
  void reload();

  // Current device value for saving.
  QString currentDevice() const;

signals:
  void configChanged();

private slots:
  void refreshAsrList();
  void updateAsrButtons();
  void onAsrEdit();
  void onAsrRemove();
  void onAsrSetActive();

  void refreshDaemonStatus();
  void onDaemonStart();
  void onDaemonStop();
  void onDaemonRestart();

  void checkSandboxPermissions();

private:
  QComboBox *comboDevice_;
  QListWidget *listAsrProviders_;
  QPushButton *btnAsrEdit_;
  QPushButton *btnAsrRemove_;
  QPushButton *btnAsrSetActive_;

  QLabel *lblDaemonStatus_;
  QPushButton *btnDaemonStart_;
  QPushButton *btnDaemonStop_;
  QPushButton *btnDaemonRestart_;
  QTimer *daemonRefreshTimer_ = nullptr;
};

}  // namespace vinput::gui
