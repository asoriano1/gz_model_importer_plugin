#include "robot_importer_gui/RuntimeProcessManager.hh"

#include <signal.h>
#include <unistd.h>

#include <gz/common/Console.hh>

namespace robot_importer_gui
{

namespace
{

// QProcess subclass that places the child in its own process group before exec.
// This lets us send signals to the entire group (bash + ros2 + parameter_bridge)
// rather than just the direct child.
class GroupedProcess : public QProcess
{
protected:
  void setupChildProcess() override { ::setpgid(0, 0); }
};

static void killGroup(qint64 pid, int sig)
{
  if (pid > 0)
    ::killpg(static_cast<pid_t>(pid), sig);
}

}  // namespace

RuntimeProcessManager::RuntimeProcessManager(QObject *parent)
: QObject(parent)
{}

RuntimeProcessManager::~RuntimeProcessManager()
{
  if (process_)
  {
    const qint64 pid = process_->processId();
    killGroup(pid, SIGTERM);
    process_->waitForFinished(1500);
    if (process_->state() != QProcess::NotRunning)
      killGroup(pid, SIGKILL);
    delete process_;
  }
}

bool RuntimeProcessManager::run(const QString &command)
{
  if (status_ == Status::Running) return false;

  if (command.trimmed().isEmpty()) return false;

  delete process_;
  process_ = new GroupedProcess();
  process_->setParent(this);
  output_.clear();

  connect(process_,
          QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
          this, &RuntimeProcessManager::onFinished);
  connect(process_, &QProcess::errorOccurred,
          this, &RuntimeProcessManager::onErrorOccurred);
  connect(process_, &QProcess::readyReadStandardOutput,
          this, &RuntimeProcessManager::onReadyRead);
  connect(process_, &QProcess::readyReadStandardError,
          this, &RuntimeProcessManager::onReadyRead);

  // Run via bash so that shell quoting, line continuations (\\\n), and
  // special characters in topic specs are handled correctly.
  process_->start(QStringLiteral("/bin/bash"), {QStringLiteral("-c"), command});
  if (!process_->waitForStarted(2000))
  {
    gzwarn << "[robot_importer_gui] Failed to start: " << command.toStdString() << "\n";
    // onErrorOccurred will fire and set the correct status.
    return false;
  }

  gzmsg << "[robot_importer_gui] Process started (pgid=" << process_->processId()
        << "): " << command.toStdString() << "\n";
  setStatus(Status::Running);
  return true;
}

void RuntimeProcessManager::stop()
{
  if (!process_ || status_ != Status::Running) return;

  const qint64 pid = process_->processId();
  killGroup(pid, SIGTERM);
  if (!process_->waitForFinished(3000))
    killGroup(pid, SIGKILL);

  gzmsg << "[robot_importer_gui] Process group stopped (pgid=" << pid << ").\n";
  // onFinished will fire and update status.
}

void RuntimeProcessManager::reset()
{
  stop();
  output_.clear();
  setStatus(Status::Idle);
}

QString RuntimeProcessManager::statusText() const
{
  switch (status_)
  {
    case Status::Idle:             return QStringLiteral("Not started");
    case Status::Running:          return QStringLiteral("Running");
    case Status::Stopped:          return QStringLiteral("Stopped");
    case Status::Failed:           return QStringLiteral("Failed");
    case Status::MissingExecutable:return QStringLiteral("Executable not found");
  }
  return {};
}

void RuntimeProcessManager::onFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
  gzmsg << "[robot_importer_gui] Process finished, exit="
        << exitCode << " status=" << static_cast<int>(exitStatus) << "\n";

  if (exitStatus == QProcess::CrashExit || exitCode != 0)
    setStatus(Status::Failed);
  else
    setStatus(Status::Stopped);
}

void RuntimeProcessManager::onErrorOccurred(QProcess::ProcessError error)
{
  if (error == QProcess::FailedToStart)
    setStatus(Status::MissingExecutable);
  else if (status_ == Status::Running)
    setStatus(Status::Failed);
}

void RuntimeProcessManager::onReadyRead()
{
  if (!process_) return;

  const QByteArray outData = process_->readAllStandardOutput();
  const QByteArray errData = process_->readAllStandardError();

  // Keep last 4 KB of output to avoid unbounded growth.
  if (!outData.isEmpty()) output_ += QString::fromUtf8(outData);
  if (!errData.isEmpty()) output_ += QString::fromUtf8(errData);
  if (output_.size() > 4096)
    output_ = output_.right(4096);
  emit outputChanged();
}

void RuntimeProcessManager::setStatus(Status s)
{
  if (s == status_) return;
  status_ = s;
  emit statusChanged();
}

}  // namespace robot_importer_gui
