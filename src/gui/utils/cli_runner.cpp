#include "utils/cli_runner.h"

#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>
#include <QJsonParseError>
#include <QMessageBox>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include "common/utils/path_utils.h"

namespace vinput::gui {

namespace {

// QProcess subclass that ensures the child process is killed before
// destruction, avoiding the "QProcess: Destroyed while process is still
// running" warning.
class SafeProcess : public QProcess {
public:
  using QProcess::QProcess;
  ~SafeProcess() override {
    if (state() != NotRunning) {
      kill();
      waitForFinished(1000);
    }
  }
};

} // namespace

QString ResolveVinputExecutable(QString *error_out) {
  const std::filesystem::path cli_path = vinput::path::CliExecutablePath();
  const QString path_str = QString::fromStdString(cli_path.string());

  // Absolute path (e.g. Flatpak bundle) — verify it exists directly.
  if (cli_path.is_absolute()) {
    if (QFileInfo::exists(path_str)) {
      return path_str;
    }
    if (error_out) {
      *error_out =
          QObject::tr("vinput executable not found at %1").arg(path_str);
    }
    return {};
  }

  // Relative name — search PATH.
  const QString resolved = QStandardPaths::findExecutable(path_str);
  if (resolved.isEmpty() && error_out) {
    *error_out = QObject::tr("vinput not found in PATH");
  }
  return resolved;
}

bool StartVinputDetached(const QStringList &args, QString *error_out) {
  QString vinput_path = ResolveVinputExecutable(error_out);
  if (vinput_path.isEmpty()) {
    return false;
  }
  const bool started = QProcess::startDetached(vinput_path, args);
  if (!started && error_out) {
    *error_out = QObject::tr("Failed to launch %1").arg(vinput_path);
  }
  return started;
}

bool RunVinputJson(const QStringList &args, QJsonDocument *out_doc,
                   QString *error_out, int timeout_ms) {
  QString vinput_path = ResolveVinputExecutable(error_out);
  if (vinput_path.isEmpty()) {
    return false;
  }

  QProcess proc;
  QStringList cmd_args;
  cmd_args << "--json" << args;
  proc.start(vinput_path, cmd_args);
  if (!proc.waitForFinished(timeout_ms)) {
    proc.kill();
    if (error_out)
      *error_out = QObject::tr("vinput command timed out");
    return false;
  }

  if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
    if (error_out)
      *error_out = QString::fromUtf8(proc.readAllStandardError()).trimmed();
    return false;
  }

  QByteArray output = proc.readAllStandardOutput();
  QJsonParseError parse_error;
  QJsonDocument doc = QJsonDocument::fromJson(output, &parse_error);
  if (parse_error.error != QJsonParseError::NoError) {
    if (error_out)
      *error_out = "invalid JSON output from vinput";
    return false;
  }

  if (out_doc)
    *out_doc = doc;
  return true;
}

bool RunVinputCommand(const QStringList &args, QString *error_out,
                      int timeout_ms) {
  QString vinput_path = ResolveVinputExecutable(error_out);
  if (vinput_path.isEmpty()) {
    return false;
  }

  QProcess proc;
  proc.start(vinput_path, args);
  if (!proc.waitForFinished(timeout_ms)) {
    proc.kill();
    if (error_out) {
      *error_out = QObject::tr("vinput command timed out");
    }
    return false;
  }

  if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
    QString stderr_text =
        QString::fromUtf8(proc.readAllStandardError()).trimmed();
    QString stdout_text =
        QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
    if (error_out) {
      *error_out = !stderr_text.isEmpty()
                       ? stderr_text
                       : (!stdout_text.isEmpty()
                              ? stdout_text
                              : QObject::tr("vinput command failed"));
    }
    return false;
  }

  return true;
}

void RestartDaemon(const QStringList &extra_args) {
  QStringList args = {"daemon", "restart"};
  args << extra_args;
  StartVinputDetached(args);
}

QProcess *RunVinputJsonAsync(const QStringList &args, QObject *parent,
                             JsonCallback callback, int timeout_ms) {
  QString exe_error;
  QString vinput_path = ResolveVinputExecutable(&exe_error);
  if (vinput_path.isEmpty()) {
    if (callback)
      callback(false, {}, exe_error);
    return nullptr;
  }

  auto *proc = new SafeProcess(parent);
  QStringList cmd_args;
  cmd_args << "--json" << args;

  auto *timeout = new QTimer(proc);
  timeout->setSingleShot(true);

  QObject::connect(timeout, &QTimer::timeout, proc, [proc]() {
    if (proc->state() != QProcess::NotRunning) {
      proc->kill();
    }
  });

  QObject::connect(
      proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
      proc, [proc, timeout, callback](int exitCode, QProcess::ExitStatus st) {
        timeout->stop();

        if (st != QProcess::NormalExit || exitCode != 0) {
          QString err =
              QString::fromUtf8(proc->readAllStandardError()).trimmed();
          if (callback)
            callback(false, {}, err);
          proc->deleteLater();
          return;
        }

        QByteArray output = proc->readAllStandardOutput();
        QJsonParseError pe;
        QJsonDocument doc = QJsonDocument::fromJson(output, &pe);
        if (pe.error != QJsonParseError::NoError) {
          if (callback)
            callback(false, {}, "invalid JSON output from vinput");
        } else {
          if (callback)
            callback(true, doc, {});
        }
        proc->deleteLater();
      });

  proc->start(vinput_path, cmd_args);
  timeout->start(timeout_ms);
  return proc;
}

}  // namespace vinput::gui
