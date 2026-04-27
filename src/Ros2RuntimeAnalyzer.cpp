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
  QString s = fn.toLower();
  if (s.startsWith(QLatin1String("lib"))) s = s.mid(3);
  if (s.endsWith(QLatin1String(".so")))   s.chop(3);

  static const QStringList kControl = {
    "gz_ros2_control", "gazebo_ros2_control", "gazebo_ros_control",
  };
  for (const QString &k : kControl)
    if (s.contains(k)) return PluginClass::Ros2Control;

  static const QStringList kSensor = {
    "gazebo_ros_camera", "gazebo_ros_depth_camera", "gazebo_ros_imu_sensor",
    "gazebo_ros_ray_sensor", "gazebo_ros_laser", "gazebo_ros_lidar",
    "gazebo_ros_gps", "gazebo_ros_p3d", "gazebo_ros_joint_state",
    "gazebo_ros_range", "gazebo_ros_bumper",
    "gz_ros_camera", "gz_ros_imu", "gz_ros_depth_camera",
  };
  for (const QString &k : kSensor)
    if (s.contains(k)) return PluginClass::Sensor;

  if (s.startsWith(QLatin1String("gazebo_ros")) ||
      s.startsWith(QLatin1String("gz_ros")))
    return PluginClass::OtherRos;

  return PluginClass::Unrelated;
}

// ---- Sensor type → bridge mapping ------------------------------------------

struct SensorBridgeMapping
{
  const char *sensorType;
  const char *gzMsgType;
  const char *rosMsgType;
  const char *topicSuffix; ///< default Gazebo topic suffix for this sensor type
};

// Gazebo Sim Harmonic default topic suffixes for native sensors.
// Inferred topic: /model/<model>/link/<link>/sensor/<sensor>/<suffix>
static const SensorBridgeMapping kBridgeMap[] = {
  {"camera",       "gz.msgs.Image",        "sensor_msgs/msg/Image",            "/image"},
  {"depth_camera", "gz.msgs.Image",        "sensor_msgs/msg/Image",            "/image"},
  {"rgbd_camera",  "gz.msgs.Image",        "sensor_msgs/msg/Image",            "/image"},
  {"gpu_lidar",    "gz.msgs.LaserScan",    "sensor_msgs/msg/LaserScan",        "/scan"},
  {"lidar",        "gz.msgs.LaserScan",    "sensor_msgs/msg/LaserScan",        "/scan"},
  {"ray",          "gz.msgs.LaserScan",    "sensor_msgs/msg/LaserScan",        "/scan"},
  {"imu",          "gz.msgs.IMU",          "sensor_msgs/msg/Imu",              "/imu"},
  {"navsat",       "gz.msgs.NavSat",       "sensor_msgs/msg/NavSatFix",        "/navsat"},
  {"gps",          "gz.msgs.NavSat",       "sensor_msgs/msg/NavSatFix",        "/navsat"},
  {"contact",      "gz.msgs.Contacts",     "ros_gz_interfaces/msg/Contacts",   "/contacts"},
  {"altimeter",    "gz.msgs.Altimeter",    "ros_gz_interfaces/msg/Altimeter",  "/altitude"},
  {"magnetometer", "gz.msgs.Magnetometer", "sensor_msgs/msg/MagneticField",    "/magnetometer"},
  {"air_pressure", "gz.msgs.FluidPressure","sensor_msgs/msg/FluidPressure",    "/air_pressure"},
  {"odometer",     "gz.msgs.Odometry",     "nav_msgs/msg/Odometry",            "/odometry"},
};

static const SensorBridgeMapping *findBridgeMapping(const QString &sensorType)
{
  const std::string st = sensorType.toLower().toStdString();
  for (const auto &m : kBridgeMap)
    if (st == m.sensorType) return &m;
  return nullptr;
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

  bool hasRos2Control{false};
  bool hasSensorPlugins{false};
  bool hasOtherRosPlugins{false};
  bool hasRos2ControlElement{false};

  QList<RuntimeRequirement> requirements;
  QList<SensorFindings>     sensors;

  // Context tracked while walking the tree.
  QString currentModel_;
  QString currentLink_;

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

        // A <ros> child inside a custom plugin also signals ROS integration.
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

    if (name == "ros2_control")
    {
      hasRos2Control        = true;
      hasRos2ControlElement = true;
      if (requirements.isEmpty())
      {
        RuntimeRequirement req;
        req.kind           = RuntimeRequirement::Kind::Ros2Control;
        req.pluginFilename = QStringLiteral("<ros2_control>");
        req.label          = QStringLiteral("ros2_control hardware interface");
        requirements.append(req);
      }
    }
  }

  void walkSensors(const tinyxml2::XMLElement *el)
  {
    if (!el) return;
    const std::string name = el->Name();

    if (name == "sensor")
    {
      const char *stype = el->Attribute("type");
      const char *sname = el->Attribute("name");
      if (!stype || !*stype) return;

      SensorFindings sf;
      sf.modelName  = currentModel_;
      sf.linkName   = currentLink_;
      sf.sensorType = QString::fromUtf8(stype).toLower();
      sf.sensorName = sname ? QString::fromUtf8(sname) : QString{};

      // Explicit <topic>
      if (const auto *tEl = el->FirstChildElement("topic"))
      {
        const char *t = tEl->GetText();
        if (t && *t)
          sf.explicitTopic = QString::fromUtf8(t).trimmed();
      }

      // <update_rate>
      if (const auto *rEl = el->FirstChildElement("update_rate"))
      {
        const char *r = rEl->GetText();
        if (r && *r) sf.updateRate = std::stod(r);
      }

      sf.hasRosPlugin = sensorHasRosPlugin(el);

      // Decide whether a bridge is needed and build the spec.
      const SensorBridgeMapping *mapping = findBridgeMapping(sf.sensorType);
      if (mapping && !sf.hasRosPlugin)
      {
        sf.needsBridge = true;

        BridgeSpec bs;
        bs.gzMsgType  = QString::fromUtf8(mapping->gzMsgType);
        bs.rosMsgType = QString::fromUtf8(mapping->rosMsgType);
        bs.direction  = "[";  // gz → ros

        if (!sf.explicitTopic.isEmpty())
        {
          bs.gazeboTopic = sf.explicitTopic;
          bs.rosTopic    = sf.explicitTopic;
          bs.confidence  = QStringLiteral("explicit");
        }
        else if (!sf.modelName.isEmpty() && !sf.sensorName.isEmpty())
        {
          // Standard Gazebo Sim Harmonic naming convention.
          QString topic = QStringLiteral("/model/%1").arg(sf.modelName);
          if (!sf.linkName.isEmpty())
            topic += QStringLiteral("/link/%1").arg(sf.linkName);
          topic += QStringLiteral("/sensor/%1%2")
              .arg(sf.sensorName, QString::fromUtf8(mapping->topicSuffix));
          bs.gazeboTopic = topic;
          bs.rosTopic    = topic;
          bs.confidence  = QStringLiteral("inferred");
        }
        else
        {
          bs.gazeboTopic = QStringLiteral("<topic_unknown>");
          bs.rosTopic    = QStringLiteral("<topic_unknown>");
          bs.confidence  = QStringLiteral("manual_review");
        }

        sf.bridge = bs;
      }
      else if (!mapping)
      {
        // Unknown sensor type — user needs to inspect manually.
        sf.needsBridge = false;
      }

      sensors.append(sf);
      return;  // don't recurse into sensor children for context tracking
    }

    // Track model / link context.
    QString prevModel = currentModel_;
    QString prevLink  = currentLink_;

    if (name == "model")
    {
      const char *mn = el->Attribute("name");
      if (mn && *mn) currentModel_ = QString::fromUtf8(mn);
      currentLink_.clear();
    }
    if (name == "link")
    {
      const char *ln = el->Attribute("name");
      if (ln && *ln) currentLink_ = QString::fromUtf8(ln);
    }

    walkPlugins(el);

    for (const auto *child = el->FirstChildElement();
         child;
         child = child->NextSiblingElement())
    {
      walkSensors(child);
    }

    currentModel_ = prevModel;
    currentLink_  = prevLink;
  }
};

// ---- XACRO arg scanner ----------------------------------------------------

static QStringList scanXacroControlArgs(const QString &path)
{
  if (path.isEmpty()) return {};
  if (!path.endsWith(QLatin1String(".xacro"), Qt::CaseInsensitive) &&
      !path.endsWith(QLatin1String(".urdf.xacro"), Qt::CaseInsensitive))
    return {};

  QFile f(path);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};

  tinyxml2::XMLDocument doc;
  const QByteArray raw = f.readAll();
  if (doc.Parse(raw.constData(), raw.size()) != tinyxml2::XML_SUCCESS) return {};

  static const QStringList kKeywords = {
    "ros2_control", "use_ros2_control", "use_control", "use_sim", "use_simulation",
  };

  QStringList found;
  std::function<void(const tinyxml2::XMLElement *)> walk;
  walk = [&](const tinyxml2::XMLElement *el)
  {
    if (!el) return;
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
    for (const auto *child = el->FirstChildElement(); child; child = child->NextSiblingElement())
      walk(child);
  };
  walk(doc.RootElement());
  return found;
}

// ---- Summary string generation ---------------------------------------------

static QString buildSummary(const RuntimeFindings &f)
{
  QStringList parts;

  const int bridgeCount = [&]{
    int n = 0;
    for (const auto &s : f.sensors)
      if (s.needsBridge) ++n;
    return n;
  }();
  if (bridgeCount > 0)
    parts << QStringLiteral("%1 sensor bridge%2").arg(bridgeCount).arg(bridgeCount > 1 ? "s" : "");

  if (f.hasRos2Control)
    parts << QStringLiteral("ros2_control");

  const int otherRos = [&]{
    int n = 0;
    for (const auto &r : f.requirements)
      if (r.kind == RuntimeRequirement::Kind::SensorPlugin ||
          r.kind == RuntimeRequirement::Kind::OtherRos) ++n;
    return n;
  }();
  if (otherRos > 0)
    parts << QStringLiteral("%1 ROS plugin%2").arg(otherRos).arg(otherRos > 1 ? "s" : "");

  if (f.hasXacroControlArgs && !f.hasRos2Control)
    parts << QStringLiteral("xacro control args");

  return parts.join(QStringLiteral(" · "));
}

}  // namespace

// ============================================================
RuntimeFindings Ros2RuntimeAnalyzer::analyze(const QString &_sdfXml,
                                             const QString &_originalPath)
{
  RuntimeFindings findings;

  if (!_sdfXml.isEmpty())
  {
    tinyxml2::XMLDocument doc;
    const QByteArray utf8 = _sdfXml.toUtf8();
    if (doc.Parse(utf8.constData(), utf8.size()) == tinyxml2::XML_SUCCESS)
    {
      Collector col;
      col.walkSensors(doc.RootElement());

      findings.hasRos2Control     = col.hasRos2Control;
      findings.hasSensorPlugins   = col.hasSensorPlugins;
      findings.hasOtherRosPlugins = col.hasOtherRosPlugins;
      findings.requirements       = col.requirements;
      findings.sensors            = col.sensors;

      for (const RuntimeRequirement &r : col.requirements)
        findings.pluginList.append(r.label);

      findings.hasNativeSensors = !col.sensors.isEmpty();

      for (const SensorFindings &s : col.sensors)
      {
        if (s.needsBridge)
          findings.hasBridgeRequirements = true;
        if (s.needsBridge && s.bridge.confidence == QLatin1String("manual_review"))
          findings.hasUnresolvedRuntimeItems = true;
      }
    }
  }

  const QStringList xacroArgs = scanXacroControlArgs(_originalPath);
  if (!xacroArgs.isEmpty())
  {
    findings.hasXacroControlArgs = true;
    findings.xacroControlArgs    = xacroArgs;
    if (!findings.hasRos2Control)
    {
      for (const QString &a : xacroArgs)
        if (a.contains(QLatin1String("ros2_control")) ||
            a.contains(QLatin1String("use_control")))
        { findings.hasRos2Control = true; break; }
    }
  }

  findings.needsRuntime =
      findings.hasRos2Control    ||
      findings.hasSensorPlugins  ||
      findings.hasOtherRosPlugins ||
      findings.hasNativeSensors;

  findings.summary = buildSummary(findings);

  return findings;
}

}  // namespace robot_importer_gui
