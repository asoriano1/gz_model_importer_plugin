#include "robot_importer_gui/RuntimeHintAnalyzer.hh"

#include <functional>

#include <QFile>
#include <QSet>

#include <tinyxml2.h>

namespace robot_importer_gui
{

namespace
{

// ---- Plugin filename classification ----------------------------------------

enum class PluginClass { Ros2Control, RosPlugin, Unrelated };

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

// ---- SDF tree walker -------------------------------------------------------

struct Collector
{
  QSet<QString> seenPlugins;

  int  sensorCount{0};
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

  if (!_sdfXml.isEmpty())
  {
    tinyxml2::XMLDocument doc;
    const QByteArray utf8 = _sdfXml.toUtf8();
    if (doc.Parse(utf8.constData(), utf8.size()) == tinyxml2::XML_SUCCESS)
    {
      Collector col;
      col.walk(doc.RootElement());

      hint.sensorCount    = col.sensorCount;
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

  hint.hasRuntimeRelevantContent =
      hint.sensorCount > 0 || hint.rosPluginCount > 0 || hint.hasRos2Control;

  if (hint.hasRuntimeRelevantContent)
  {
    QStringList parts;
    if (hint.sensorCount > 0)
      parts << QStringLiteral("%1 sensor%2").arg(hint.sensorCount).arg(hint.sensorCount > 1 ? "s" : "");
    if (hint.rosPluginCount > 0)
      parts << QStringLiteral("%1 ROS plugin%2").arg(hint.rosPluginCount).arg(hint.rosPluginCount > 1 ? "s" : "");
    if (hint.hasRos2Control)
      parts << QStringLiteral("ros2_control");
    hint.summary = parts.join(QStringLiteral(", "));
  }

  return hint;
}

}  // namespace robot_importer_gui
