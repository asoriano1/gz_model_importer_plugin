#include "gz_model_importer_gui/XacroExpander.hh"

#include <QFile>
#include <QMap>
#include <QProcess>
#include <QString>
#include <tinyxml2.h>

namespace gz_model_importer_gui
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

// ============================================================
// discoverArgs — parse <xacro:arg> declarations from a XACRO file.
//
// XACRO allows any namespace prefix for the xacro namespace, but in practice
// "xacro" is the universal convention. We match on local name "arg" in any
// element whose prefix is "xacro" (i.e. "xacro:arg").
//
// Format: <xacro:arg name="prefix" default=""/>
// If `default` attribute is absent the arg is required; we map it to "".
// ============================================================
// static
QMap<QString, QString> XacroExpander::discoverArgs(const QString &_xacroPath)
{
  QMap<QString, QString> result;

  QFile f(_xacroPath);
  if (!f.open(QIODevice::ReadOnly))
    return result;

  const QByteArray data = f.readAll();
  f.close();

  tinyxml2::XMLDocument doc;
  // Use XML_SUCCESS only — silently return empty map on malformed input.
  if (doc.Parse(data.constData(), data.size()) != tinyxml2::XML_SUCCESS)
    return result;

  // Depth-first walk: collect every element whose Name() is "xacro:arg".
  std::function<void(tinyxml2::XMLElement *)> walk =
      [&](tinyxml2::XMLElement *el)
  {
    if (!el) return;

    if (std::string(el->Name()) == "xacro:arg")
    {
      const char *nameAttr = el->Attribute("name");
      if (nameAttr && *nameAttr)
      {
        const char *defAttr = el->Attribute("default");
        result.insert(QString::fromUtf8(nameAttr),
                      defAttr ? QString::fromUtf8(defAttr) : QString());
      }
    }

    for (auto *c = el->FirstChildElement(); c; c = c->NextSiblingElement())
      walk(c);
  };
  walk(doc.RootElement());

  return result;
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

}  // namespace gz_model_importer_gui
