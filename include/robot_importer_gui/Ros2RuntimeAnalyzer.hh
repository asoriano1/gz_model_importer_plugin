#ifndef ROBOT_IMPORTER_GUI_ROS2_RUNTIME_ANALYZER_HH_
#define ROBOT_IMPORTER_GUI_ROS2_RUNTIME_ANALYZER_HH_

#include <QString>
#include <QStringList>

namespace robot_importer_gui
{

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

  bool hasRos2Control{false};   ///< ros2_control detected
  bool hasSensorPlugins{false}; ///< sensor plugins with ROS topics
  bool hasOtherRosPlugins{false};

  QList<RuntimeRequirement> requirements;
  QStringList               pluginList;   ///< unique plugin filenames/labels

  /// Whether the original source was a XACRO that declared control-related args.
  bool hasXacroControlArgs{false};
  QStringList xacroControlArgs;  ///< arg names found (e.g. "use_ros2_control")
};

/// Stateless analyser — scans a post-rewrite SDF string for ROS 2 runtime
/// dependencies.  Does not modify the content.
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
