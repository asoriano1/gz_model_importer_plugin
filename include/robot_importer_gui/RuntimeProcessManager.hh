#ifndef ROBOT_IMPORTER_GUI_RUNTIME_PROCESS_MANAGER_HH_
#define ROBOT_IMPORTER_GUI_RUNTIME_PROCESS_MANAGER_HH_

#include <QObject>
#include <QProcess>
#include <QString>

namespace robot_importer_gui
{

/// Manages a single external process (e.g. ros_gz_bridge parameter_bridge).
///
/// - Captures stdout/stderr asynchronously (does not block the GUI thread).
/// - Distinguishes "missing executable", "crashed", "normal exit" states.
/// - Prevents launching a duplicate if already running.
/// - Stops and cleans up the child on destruction.
class RuntimeProcessManager : public QObject
{
  Q_OBJECT

public:
  enum class Status
  {
    Idle,             ///< no process started yet, or cleanly stopped
    Running,          ///< process is active
    Stopped,          ///< user stopped it or it exited cleanly (code 0)
    Failed,           ///< crashed or exited with non-zero code
    MissingExecutable ///< executable not found (QProcess::FailedToStart)
  };

  explicit RuntimeProcessManager(QObject *parent = nullptr);
  ~RuntimeProcessManager() override;

  /// Start the process with the given shell command string.
  /// If already running, does nothing (returns false).
  bool run(const QString &command);

  /// Send SIGTERM; escalate to SIGKILL after 3 s if still alive.
  void stop();

  /// Stop process and reset to Idle, clearing output.
  void reset();

  Status  status()        const { return status_; }
  QString statusText()    const;
  QString processOutput() const { return output_; }
  bool    isRunning()     const { return status_ == Status::Running; }

signals:
  void statusChanged();
  void outputChanged();

private slots:
  void onFinished(int exitCode, QProcess::ExitStatus exitStatus);
  void onErrorOccurred(QProcess::ProcessError error);
  void onReadyRead();

private:
  void setStatus(Status s);

  QProcess *process_{nullptr};
  Status    status_{Status::Idle};
  QString   output_;
};

}  // namespace robot_importer_gui

#endif
