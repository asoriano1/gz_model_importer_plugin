#ifndef ROBOT_IMPORTER_GUI_ROS2_RUNTIME_ANALYZER_HH_
#define ROBOT_IMPORTER_GUI_ROS2_RUNTIME_ANALYZER_HH_

#include <QString>
#include <QStringList>
#include <QList>

namespace robot_importer_gui
{

/// One ros_gz_bridge parameter_bridge argument.
struct BridgeSpec
{
  QString gazeboTopic;  ///< e.g. "/model/robot/link/base/sensor/cam/image"
  QString rosTopic;     ///< same as gazeboTopic for 1:1 parameter_bridge mapping
  QString gzMsgType;    ///< e.g. "gz.msgs.Image"
  QString rosMsgType;   ///< e.g. "sensor_msgs/msg/Image"
  QString direction;    ///< "[" = gz→ros, "]" = ros→gz, "@" = bidirectional
  QString confidence;   ///< "explicit", "inferred", "manual_review"

  /// Returns the full parameter_bridge argument string.
  QString paramBridgeArg() const
  {
    return gazeboTopic + "@" + rosMsgType + direction + gzMsgType;
  }
};

/// A single native Gazebo sensor detected in the SDF.
struct SensorFindings
{
  QString modelName;
  QString linkName;
  QString sensorName;
  QString sensorType;
  QString explicitTopic;   ///< from <topic> element, empty if absent
  double  updateRate{0.0};
  bool    hasRosPlugin{false};  ///< true → ROS plugin likely handles bridging already
  bool    needsBridge{false};   ///< true → a bridge is recommended
  BridgeSpec bridge;            ///< populated when needsBridge is true
};

/// A single detected ROS 2 runtime dependency found in the SDF.
struct RuntimeRequirement
{
  enum class Kind
  {
    Ros2Control,  ///< ros2_control plugin or <ros2_control> element
    SensorPlugin, ///< gazebo_ros sensor plugin with ROS topic config
    OtherRos,     ///< other gazebo_ros / gz_ros plugin
  };

  Kind    kind;
  QString pluginFilename;  ///< e.g. "libgz_ros2_control.so"
  QString label;           ///< human-readable short description
};

/// Aggregate result of scanning an SDF for ROS 2 runtime requirements.
struct RuntimeFindings
{
  bool needsRuntime{false};
  bool hasBridgeRequirements{false};    ///< true if any bridge spec was generated
  bool hasUnresolvedRuntimeItems{false}; ///< true if any item needs manual review

  bool hasRos2Control{false};
  bool hasSensorPlugins{false};
  bool hasOtherRosPlugins{false};
  bool hasNativeSensors{false};   ///< at least one native Gazebo sensor detected

  QList<RuntimeRequirement> requirements;
  QList<SensorFindings>     sensors;
  QStringList               pluginList;   ///< unique plugin filenames/labels

  QString summary; ///< concise one-liner, e.g. "2 sensor bridges · 1 ros2_control"

  bool hasXacroControlArgs{false};
  QStringList xacroControlArgs;
};

/// Stateless analyser — scans a post-rewrite SDF string for ROS 2 runtime
/// dependencies (plugins and native sensors).  Does not modify the content.
struct Ros2RuntimeAnalyzer
{
  /// \param[in] _sdfXml       Post-rewrite SDF XML string.
  /// \param[in] _originalPath Path to the original robot file (may be empty).
  ///            If the file is a .xacro, its arg declarations are also scanned.
  static RuntimeFindings analyze(const QString &_sdfXml,
                                 const QString &_originalPath = {});
};

}  // namespace robot_importer_gui

#endif
