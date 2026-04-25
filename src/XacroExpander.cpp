#include "robot_importer_gui/XacroExpander.hh"

#include <QProcess>

namespace robot_importer_gui
{

XacroExpander::XacroExpander(QObject *_parent)
: QObject(_parent),
  proc_(std::make_unique<QProcess>(this))
{
  connect(proc_.get(), QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
          this, &XacroExpander::onProcessFinished);
}

XacroExpander::~XacroExpander() = default;

void XacroExpander::expand(const QString &_xacroPath,
                           const QStringList &_xacroArgs)
{
  if (proc_->state() != QProcess::NotRunning)
    proc_->kill();

  QStringList args;
  args << _xacroPath;
  args << _xacroArgs;

  // Use `xacro` from PATH. ROS 2 Jazzy installs it at /opt/ros/jazzy/bin/xacro.
  proc_->start(QStringLiteral("xacro"), args);
}

void XacroExpander::cancel()
{
  if (proc_->state() != QProcess::NotRunning)
    proc_->kill();
}

void XacroExpander::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
  const QString stdoutStr = QString::fromUtf8(proc_->readAllStandardOutput());
  const QString stderrStr = QString::fromUtf8(proc_->readAllStandardError());

  if (status == QProcess::CrashExit || exitCode != 0)
  {
    const QString summary = stderrStr.isEmpty()
        ? QStringLiteral("xacro exited with code %1").arg(exitCode)
        : stderrStr.section(QLatin1Char('\n'), 0, 2);  // first 3 lines
    emit expandFailed(summary, stderrStr);
    return;
  }

  if (stdoutStr.trimmed().isEmpty())
  {
    emit expandFailed(
        QStringLiteral("xacro produced empty output"), stderrStr);
    return;
  }

  emit expandComplete(stdoutStr);
}

}  // namespace robot_importer_gui
