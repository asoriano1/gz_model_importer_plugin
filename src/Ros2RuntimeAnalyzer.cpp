#include "robot_importer_gui/Ros2RuntimeAnalyzer.hh"

#include <functional>

#include <QFile>
#include <QSet>

#include <tinyxml2.h>

namespace robot_importer_gui
{

namespace
{

// ---- Plugin filename classification ----------------------------------------

enum class PluginClass { Ros2Control, Sensor, OtherRos, Unrelated };

static PluginClass classifyFilename(const QString &fn)
{
  // Normalise: lower-case, strip lib prefix and .so suffix for matching.
  QString s = fn.toLower();
  if (s.startsWith(QLatin1String("lib"))) s = s.mid(3);
  if (s.endsWith(QLatin1String(".so")))   s.chop(3);

  // --- ros2_control ---------------------------------------------------------
  static const QStringList kControl = {
    "gz_ros2_control",
    "gazebo_ros2_control",
    "gazebo_ros_control",      // older Gazebo Classic naming
  };
  for (const QString &k : kControl)
    if (s.contains(k)) return PluginClass::Ros2Control;

  // --- sensor plugins with ROS topics ---------------------------------------
  // These publish data directly to ROS topics from within Gazebo.
  static const QStringList kSensor = {
    "gazebo_ros_camera",
    "gazebo_ros_depth_camera",
    "gazebo_ros_imu_sensor",
    "gazebo_ros_ray_sensor",
    "gazebo_ros_laser",
    "gazebo_ros_lidar",
    "gazebo_ros_gps",
    "gazebo_ros_p3d",
    "gazebo_ros_joint_state",
    "gazebo_ros_range",
    "gazebo_ros_bumper",
    "gz_ros_camera",
    "gz_ros_imu",
    "gz_ros_depth_camera",
  };
  for (const QString &k : kSensor)
    if (s.contains(k)) return PluginClass::Sensor;

  // --- general gazebo_ros / gz_ros plugins -----------------------------------
  if (s.startsWith(QLatin1String("gazebo_ros")) ||
      s.startsWith(QLatin1String("gz_ros")))
    return PluginClass::OtherRos;

  return PluginClass::Unrelated;
}

// ---- SDF tree walker -------------------------------------------------------

struct Collector
{
  QSet<QString> seenPlugins;   // dedup by filename

  bool hasRos2Control{false};
  bool hasSensorPlugins{false};
  bool hasOtherRosPlugins{false};
  bool hasRos2ControlElement{false};  // from <ros2_control> XML element

  QList<RuntimeRequirement> requirements;

  void walk(const tinyxml2::XMLElement *el)
  {
    if (!el) return;

    const std::string name = el->Name();

    // <plugin filename="..."> --------------------------------------------------
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

          RuntimeRequirement req;
          req.pluginFilename = filename;

          switch (cls)
          {
            case PluginClass::Ros2Control:
              req.kind  = RuntimeRequirement::Kind::Ros2Control;
              req.label = QStringLiteral("%1 (ros2_control)").arg(filename);
              hasRos2Control = true;
              break;
            case PluginClass::Sensor:
              req.kind  = RuntimeRequirement::Kind::SensorPlugin;
              req.label = QStringLiteral("%1 (sensor)").arg(filename);
              hasSensorPlugins = true;
              break;
            case PluginClass::OtherRos:
              req.kind  = RuntimeRequirement::Kind::OtherRos;
              req.label = QStringLiteral("%1 (ros plugin)").arg(filename);
              hasOtherRosPlugins = true;
              break;
            default: break;
          }
          requirements.append(req);
        }

        // A <ros> child inside any plugin also signals ROS topic integration,
        // even if the filename wasn't matched above (custom plugin names).
        if (el->FirstChildElement("ros") && !seenPlugins.contains(filename))
        {
          seenPlugins.insert(filename);
          RuntimeRequirement req;
          req.kind           = RuntimeRequirement::Kind::OtherRos;
          req.pluginFilename = filename;
          req.label          = QStringLiteral("%1 (ros topic)").arg(filename);
          requirements.append(req);
          hasOtherRosPlugins = true;
        }
      }
    }

    // <ros2_control> element --------------------------------------------------
    // This element appears in URDF and may survive into the converted SDF.
    if (name == "ros2_control")
    {
      hasRos2Control          = true;
      hasRos2ControlElement   = true;
      // Don't add a duplicate requirement here; the plugin entry above covers it.
      // Only add if no plugin entry was found yet.
      if (!hasRos2Control || requirements.isEmpty())
      {
        RuntimeRequirement req;
        req.kind           = RuntimeRequirement::Kind::Ros2Control;
        req.pluginFilename = QStringLiteral("<ros2_control>");
        req.label          = QStringLiteral("ros2_control hardware interface");
        requirements.append(req);
      }
    }

    // Recurse.
    for (const auto *child = el->FirstChildElement();
         child;
         child = child->NextSiblingElement())
      walk(child);
  }
};

// ---- XACRO arg scanner ----------------------------------------------------
// Reads the raw XACRO source (before expansion) and collects arg names that
// indicate control or simulation integration intent.

static QStringList scanXacroControlArgs(const QString &path)
{
  if (path.isEmpty()) return {};

  // Only worth reading .xacro files.
  if (!path.endsWith(QLatin1String(".xacro"), Qt::CaseInsensitive) &&
      !path.endsWith(QLatin1String(".urdf.xacro"), Qt::CaseInsensitive))
    return {};

  QFile f(path);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    return {};

  tinyxml2::XMLDocument doc;
  const QByteArray raw = f.readAll();
  if (doc.Parse(raw.constData(), raw.size()) != tinyxml2::XML_SUCCESS)
    return {};

  // Control-related arg name keywords.
  static const QStringList kKeywords = {
    "ros2_control", "use_ros2_control", "use_control",
    "use_sim", "use_simulation",
  };

  QStringList found;

  // Walk looking for <xacro:arg name="..."> elements.
  std::function<void(const tinyxml2::XMLElement *)> walk;
  walk = [&](const tinyxml2::XMLElement *el)
  {
    if (!el) return;

    // xacro:arg appears as element name "xacro:arg" in the raw XML.
    const std::string ename = el->Name();
    if (ename == "xacro:arg")
    {
      const char *argName = el->Attribute("name");
      if (argName)
      {
        const QString qn = QString::fromUtf8(argName).toLower();
        for (const QString &kw : kKeywords)
          if (qn.contains(kw) && !found.contains(qn))
          { found.append(qn); break; }
      }
    }

    for (const auto *child = el->FirstChildElement();
         child;
         child = child->NextSiblingElement())
      walk(child);
  };
  walk(doc.RootElement());

  return found;
}

}  // namespace

// ============================================================
RuntimeFindings Ros2RuntimeAnalyzer::analyze(const QString &_sdfXml,
                                             const QString &_originalPath)
{
  RuntimeFindings findings;

  // --- SDF analysis ---------------------------------------------------------
  if (!_sdfXml.isEmpty())
  {
    tinyxml2::XMLDocument doc;
    const QByteArray utf8 = _sdfXml.toUtf8();
    if (doc.Parse(utf8.constData(), utf8.size()) == tinyxml2::XML_SUCCESS)
    {
      Collector col;
      col.walk(doc.RootElement());

      findings.hasRos2Control    = col.hasRos2Control;
      findings.hasSensorPlugins  = col.hasSensorPlugins;
      findings.hasOtherRosPlugins = col.hasOtherRosPlugins;
      findings.requirements      = col.requirements;
      for (const RuntimeRequirement &r : col.requirements)
        findings.pluginList.append(r.label);
    }
  }

  // --- XACRO arg scan (original source file) --------------------------------
  const QStringList xacroArgs = scanXacroControlArgs(_originalPath);
  if (!xacroArgs.isEmpty())
  {
    findings.hasXacroControlArgs = true;
    findings.xacroControlArgs    = xacroArgs;
    // A XACRO with use_ros2_control / use_sim args very likely needs control nodes.
    if (!findings.hasRos2Control)
    {
      // Promote as ros2_control hint only if the arg names explicitly say so.
      for (const QString &a : xacroArgs)
        if (a.contains(QLatin1String("ros2_control")) ||
            a.contains(QLatin1String("use_control")))
        { findings.hasRos2Control = true; break; }
    }
  }

  findings.needsRuntime =
      findings.hasRos2Control   ||
      findings.hasSensorPlugins ||
      findings.hasOtherRosPlugins;

  return findings;
}

}  // namespace robot_importer_gui
