#ifndef ROBOT_IMPORTER_GUI_RUNTIME_HINT_ANALYZER_HH_
#define ROBOT_IMPORTER_GUI_RUNTIME_HINT_ANALYZER_HH_

#include <QString>
#include <QStringList>

namespace robot_importer_gui
{

/// Lightweight result of scanning an SDF for ROS 2 runtime relevance.
/// No bridge commands, no launch files — just enough to show a hint.
struct RuntimeHint
{
  bool hasRuntimeRelevantContent{false};

  int  sensorCount{0};      ///< native Gazebo sensors without a ROS plugin child
  int  rosPluginCount{0};   ///< gazebo_ros / gz_ros plugin filenames detected
  bool hasRos2Control{false};

  QStringList detectedItems;  ///< human-readable list for the details section
  QString     summary;        ///< one-liner, e.g. "2 sensors, 1 ROS plugin"
};

/// Stateless scanner — inspects a post-rewrite SDF for ROS 2 runtime relevance.
/// Does not generate bridge commands or attempt any external interaction.
struct RuntimeHintAnalyzer
{
  /// \param[in] _sdfXml       Post-rewrite SDF XML string.
  /// \param[in] _originalPath Path to the original robot file (used to scan
  ///            XACRO args for ros2_control indicators; may be empty).
  static RuntimeHint analyze(const QString &_sdfXml,
                              const QString &_originalPath = {});
};

}  // namespace robot_importer_gui

#endif
