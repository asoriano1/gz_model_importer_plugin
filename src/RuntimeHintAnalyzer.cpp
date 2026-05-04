#include "gz_model_importer_gui/RuntimeHintAnalyzer.hh"

#include <ament_index_cpp/get_package_prefix.hpp>
#include <functional>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

#include <tinyxml2.h>

namespace gz_model_importer_gui
{

namespace
{

// ---- Plugin filename classification ----------------------------------------

enum class PluginClass { Ros2Control, RosPlugin, Unrelated };

struct ControllerParamsSummary
{
  int count{0};
  QStringList names;
};

static PluginClass classifyFilename(const QString &fn)
{
  QString s = fn.toLower();
  if (s.startsWith(QLatin1String("lib"))) s = s.mid(3);
  if (s.endsWith(QLatin1String(".so")))   s.chop(3);

  static const QStringList kControl = {
    "gz_ros2_control", "gazebo_ros2_control", "gazebo_ros_control",
  };
  for (const QString &k : kControl)
    if (s.contains(k)) return PluginClass::Ros2Control;

  if (s.startsWith(QLatin1String("gazebo_ros")) ||
      s.startsWith(QLatin1String("gz_ros")))
    return PluginClass::RosPlugin;

  return PluginClass::Unrelated;
}

// ---- Native sensor types that need a bridge --------------------------------

static bool isNativeSensorType(const QString &stype)
{
  static const QStringList kNative = {
    "camera", "depth_camera", "rgbd_camera", "gpu_lidar", "lidar", "ray",
    "imu", "navsat", "gps", "contact", "altimeter", "magnetometer",
    "air_pressure", "odometer",
  };
  return kNative.contains(stype.toLower());
}

// ---- Check whether a sensor element has a ROS plugin child -----------------

static bool sensorHasRosPlugin(const tinyxml2::XMLElement *sensorEl)
{
  for (const auto *child = sensorEl->FirstChildElement("plugin");
       child;
       child = child->NextSiblingElement("plugin"))
  {
    const char *fn = child->Attribute("filename");
    if (fn && classifyFilename(QString::fromUtf8(fn)) != PluginClass::Unrelated)
      return true;
  }
  return false;
}

static int leadingIndent(const QString &line)
{
  int indent = 0;
  for (const QChar ch : line)
  {
    if (ch == QLatin1Char(' '))
      ++indent;
    else if (ch == QLatin1Char('\t'))
      indent += 2;
    else
      break;
  }
  return indent;
}

static QString resolvePackageShareDir(
    const QString &packageName,
    const QString &originalPath)
{
  if (packageName.isEmpty())
    return {};

  try
  {
    const QString prefix = QString::fromStdString(
        ament_index_cpp::get_package_prefix(packageName.toStdString()));
    const QString shareDir = QDir(prefix).filePath(
        QStringLiteral("share/%1").arg(packageName));
    if (QFileInfo::exists(shareDir))
      return QDir(shareDir).absolutePath();
  }
  catch (const std::exception &)
  {
  }

  QDir cursor = QFileInfo(originalPath).absoluteDir();
  while (!cursor.absolutePath().isEmpty())
  {
    if (cursor.dirName() == packageName &&
        QFileInfo::exists(cursor.filePath(QStringLiteral("package.xml"))))
      return cursor.absolutePath();

    const QString srcCandidate = cursor.filePath(QStringLiteral("src/%1").arg(packageName));
    if (QFileInfo(srcCandidate).isDir())
      return QDir(srcCandidate).absolutePath();

    if (!cursor.cdUp())
      break;
  }

  return {};
}

static QString resolveParametersPath(
    const QString &rawPath,
    const QString &originalPath)
{
  QString path = rawPath.trimmed();
  if ((path.startsWith(QLatin1Char('"')) && path.endsWith(QLatin1Char('"'))) ||
      (path.startsWith(QLatin1Char('\'')) && path.endsWith(QLatin1Char('\''))))
  {
    path = path.mid(1, path.size() - 2).trimmed();
  }

  if (path.isEmpty())
    return {};

  QFileInfo directInfo(path);
  if (directInfo.isAbsolute() && directInfo.exists())
    return directInfo.absoluteFilePath();

  const QRegularExpression findExpr(
      QStringLiteral(R"(^\$\(\s*find\s+([^)\/\s]+)\s*\)(/.*)?$)"));
  const QRegularExpressionMatch findMatch = findExpr.match(path);
  if (findMatch.hasMatch())
  {
    const QString shareDir = resolvePackageShareDir(
        findMatch.captured(1), originalPath);
    if (!shareDir.isEmpty())
    {
      const QString suffix = findMatch.captured(2).remove(0, 1);
      const QString candidate = QDir(shareDir).filePath(suffix);
      if (QFileInfo::exists(candidate))
        return QDir::cleanPath(candidate);
    }
  }

  const QRegularExpression packageExpr(
      QStringLiteral(R"(^package://([^/]+)/(.+)$)"));
  const QRegularExpressionMatch packageMatch = packageExpr.match(path);
  if (packageMatch.hasMatch())
  {
    const QString shareDir = resolvePackageShareDir(
        packageMatch.captured(1), originalPath);
    if (!shareDir.isEmpty())
    {
      const QString candidate = QDir(shareDir).filePath(packageMatch.captured(2));
      if (QFileInfo::exists(candidate))
        return QDir::cleanPath(candidate);
    }
  }

  const QFileInfo originalInfo(originalPath);
  const QString relativeCandidate = originalInfo.absoluteDir().filePath(path);
  if (QFileInfo::exists(relativeCandidate))
    return QDir::cleanPath(relativeCandidate);

  return {};
}

static ControllerParamsSummary parseControllerParamsFile(const QString &path)
{
  ControllerParamsSummary summary;

  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    return summary;

  static const QSet<QString> kIgnoredKeys = {
    QStringLiteral("update_rate"),
    QStringLiteral("use_sim_time"),
    QStringLiteral("enforce_command_limits"),
    QStringLiteral("cpu_affinity"),
    QStringLiteral("thread_priority"),
    QStringLiteral("lock_memory"),
    QStringLiteral("diagnostics"),
    QStringLiteral("fallback_controllers"),
  };

  bool inControllerManager = false;
  bool inRosParameters = false;
  int controllerManagerIndent = -1;
  int rosParametersIndent = -1;
  QString currentKey;
  int currentKeyIndent = -1;
  bool currentKeyHasType = false;
  QSet<QString> seenNames;

  const auto flushCurrentKey = [&]()
  {
    if (!currentKey.isEmpty() && currentKeyHasType &&
        !kIgnoredKeys.contains(currentKey.toLower()) &&
        !seenNames.contains(currentKey))
    {
      seenNames.insert(currentKey);
      summary.names << currentKey;
      ++summary.count;
    }
    currentKey.clear();
    currentKeyIndent = -1;
    currentKeyHasType = false;
  };

  while (!file.atEnd())
  {
    QString line = QString::fromUtf8(file.readLine());
    const int commentPos = line.indexOf(QLatin1Char('#'));
    if (commentPos >= 0)
      line.truncate(commentPos);

    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty())
      continue;

    const int indent = leadingIndent(line);

    if (!inControllerManager)
    {
      if (trimmed == QStringLiteral("controller_manager:"))
      {
        inControllerManager = true;
        controllerManagerIndent = indent;
      }
      continue;
    }

    if (!inRosParameters)
    {
      if (indent <= controllerManagerIndent)
      {
        inControllerManager = false;
        continue;
      }

      if (trimmed == QStringLiteral("ros__parameters:"))
      {
        inRosParameters = true;
        rosParametersIndent = indent;
      }
      continue;
    }

    if (indent <= rosParametersIndent)
    {
      flushCurrentKey();
      inRosParameters = false;
      if (trimmed == QStringLiteral("controller_manager:"))
      {
        inControllerManager = true;
        controllerManagerIndent = indent;
      }
      else
      {
        inControllerManager = false;
      }
      continue;
    }

    static const QRegularExpression keyOnlyExpr(
        QStringLiteral(R"(^([A-Za-z0-9_]+):\s*$)"));
    const QRegularExpressionMatch keyMatch = keyOnlyExpr.match(trimmed);
    if (keyMatch.hasMatch())
    {
      flushCurrentKey();
      currentKey = keyMatch.captured(1);
      currentKeyIndent = indent;
      continue;
    }

    if (!currentKey.isEmpty() &&
        indent > currentKeyIndent &&
        trimmed.startsWith(QStringLiteral("type:")))
    {
      currentKeyHasType = true;
    }
  }

  flushCurrentKey();
  return summary;
}

// ---- SDF tree walker -------------------------------------------------------

struct Collector
{
  QSet<QString> seenPlugins;
  QSet<QString> seenControllerParamRefs;

  int  sensorCount{0};
  int  controllerCount{0};
  int  rosPluginCount{0};
  bool hasRos2Control{false};

  QStringList detectedItems;

  void walkPlugins(const tinyxml2::XMLElement *el)
  {
    if (!el) return;
    const std::string name = el->Name();

    if (name == "plugin")
    {
      const char *fn = el->Attribute("filename");
      if (fn && *fn)
      {
        const QString filename = QString::fromUtf8(fn);
        const PluginClass cls  = classifyFilename(filename);

        if (cls != PluginClass::Unrelated && !seenPlugins.contains(filename))
        {
          seenPlugins.insert(filename);
          if (cls == PluginClass::Ros2Control)
          {
            hasRos2Control = true;
            detectedItems << QStringLiteral("ros2_control: %1").arg(filename);

            for (const auto *params = el->FirstChildElement("parameters");
                 params;
                 params = params->NextSiblingElement("parameters"))
            {
              const char *text = params->GetText();
              if (!text || !*text)
                continue;

              const QString ref = QString::fromUtf8(text).trimmed();
              if (!ref.isEmpty() && !seenControllerParamRefs.contains(ref))
                seenControllerParamRefs.insert(ref);
            }
          }
          else
          {
            ++rosPluginCount;
            detectedItems << QStringLiteral("ROS plugin: %1").arg(filename);
          }
        }

        // A <ros> child inside a custom plugin also signals ROS integration.
        if (el->FirstChildElement("ros") && !seenPlugins.contains(filename))
        {
          seenPlugins.insert(filename);
          ++rosPluginCount;
          detectedItems << QStringLiteral("ROS plugin (ros topic): %1").arg(filename);
        }
      }
    }

    if (name == "ros2_control" && !hasRos2Control)
    {
      hasRos2Control = true;
      detectedItems << QStringLiteral("ros2_control element");
    }
  }

  void walk(const tinyxml2::XMLElement *el)
  {
    if (!el) return;
    const std::string name = el->Name();

    if (name == "sensor")
    {
      const char *stype = el->Attribute("type");
      const char *sname = el->Attribute("name");
      if (stype && *stype && isNativeSensorType(QString::fromUtf8(stype)))
      {
        if (!sensorHasRosPlugin(el))
        {
          ++sensorCount;
          const QString label = sname
              ? QStringLiteral("sensor: %1 (%2)").arg(QString::fromUtf8(sname), QString::fromUtf8(stype))
              : QStringLiteral("sensor: (%1)").arg(QString::fromUtf8(stype));
          detectedItems << label;
        }
      }
      return;  // don't recurse into sensor children
    }

    walkPlugins(el);

    for (const auto *child = el->FirstChildElement();
         child;
         child = child->NextSiblingElement())
      walk(child);
  }
};

// ---- XACRO arg scanner for ros2_control indicators ------------------------

static bool xacroHasRos2ControlArgs(const QString &path)
{
  if (path.isEmpty()) return false;
  if (!path.endsWith(QLatin1String(".xacro"), Qt::CaseInsensitive) &&
      !path.endsWith(QLatin1String(".urdf.xacro"), Qt::CaseInsensitive))
    return false;

  QFile f(path);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;

  tinyxml2::XMLDocument doc;
  const QByteArray raw = f.readAll();
  if (doc.Parse(raw.constData(), raw.size()) != tinyxml2::XML_SUCCESS) return false;

  static const QStringList kKeywords = {
    "ros2_control", "use_ros2_control", "use_control", "use_sim", "use_simulation",
  };

  bool found = false;
  std::function<void(const tinyxml2::XMLElement *)> walkArgs;
  walkArgs = [&](const tinyxml2::XMLElement *el)
  {
    if (!el || found) return;
    if (std::string(el->Name()) == "xacro:arg")
    {
      const char *argName = el->Attribute("name");
      if (argName)
      {
        const QString qn = QString::fromUtf8(argName).toLower();
        for (const QString &kw : kKeywords)
          if (qn.contains(kw)) { found = true; return; }
      }
    }
    for (const auto *child = el->FirstChildElement(); child; child = child->NextSiblingElement())
      walkArgs(child);
  };
  walkArgs(doc.RootElement());
  return found;
}

}  // namespace

// ============================================================
RuntimeHint RuntimeHintAnalyzer::analyze(const QString &_sdfXml,
                                          const QString &_originalPath)
{
  RuntimeHint hint;
  Collector col;

  if (!_sdfXml.isEmpty())
  {
    tinyxml2::XMLDocument doc;
    const QByteArray utf8 = _sdfXml.toUtf8();
    if (doc.Parse(utf8.constData(), utf8.size()) == tinyxml2::XML_SUCCESS)
    {
      col.walk(doc.RootElement());

      hint.sensorCount    = col.sensorCount;
      hint.controllerCount = col.controllerCount;
      hint.rosPluginCount = col.rosPluginCount;
      hint.hasRos2Control = col.hasRos2Control;
      hint.detectedItems  = col.detectedItems;
    }
  }

  // Scan XACRO args for ros2_control indicators if not already detected.
  if (!hint.hasRos2Control && xacroHasRos2ControlArgs(_originalPath))
  {
    hint.hasRos2Control = true;
    hint.detectedItems << QStringLiteral("ros2_control (XACRO arg)");
  }

  for (const QString &paramRef : col.seenControllerParamRefs)
  {
    const QString resolvedPath = resolveParametersPath(paramRef, _originalPath);
    if (resolvedPath.isEmpty())
    {
      hint.detectedItems << QStringLiteral("controller params: %1 (unresolved)")
          .arg(paramRef);
      continue;
    }

    const ControllerParamsSummary params = parseControllerParamsFile(resolvedPath);
    if (params.count == 0)
    {
      hint.detectedItems << QStringLiteral("controller params: %1 (no controllers detected)")
          .arg(QFileInfo(resolvedPath).fileName());
      continue;
    }

    hint.controllerCount += params.count;
    for (const QString &name : params.names)
      hint.detectedItems << QStringLiteral("controller: %1").arg(name);
  }

  hint.hasRuntimeRelevantContent =
      hint.sensorCount > 0 || hint.controllerCount > 0 ||
      hint.rosPluginCount > 0 || hint.hasRos2Control;

  if (hint.hasRuntimeRelevantContent)
  {
    QStringList parts;
    if (hint.sensorCount > 0)
      parts << QStringLiteral("%1 sensor%2").arg(hint.sensorCount).arg(hint.sensorCount > 1 ? "s" : "");
    if (hint.controllerCount > 0)
      parts << QStringLiteral("%1 controller%2").arg(hint.controllerCount).arg(hint.controllerCount > 1 ? "s" : "");
    if (hint.rosPluginCount > 0)
      parts << QStringLiteral("%1 ROS plugin%2").arg(hint.rosPluginCount).arg(hint.rosPluginCount > 1 ? "s" : "");
    if (hint.hasRos2Control && hint.controllerCount == 0)
      parts << QStringLiteral("ros2_control");
    hint.summary = parts.join(QStringLiteral(", "));
  }

  return hint;
}

}  // namespace gz_model_importer_gui
