#ifndef ROBOT_IMPORTER_GUI_LAUNCH_GENERATOR_HH_
#define ROBOT_IMPORTER_GUI_LAUNCH_GENERATOR_HH_

#include <QString>

#include "robot_importer_gui/Ros2RuntimeAnalyzer.hh"

namespace robot_importer_gui
{

/// Generates ROS 2 launch file content and CLI command strings from
/// RuntimeFindings.  All methods are stateless (static).
struct LaunchGenerator
{
  /// Returns a Python ROS 2 launch file as a QString.
  /// The file is a starting point — the user must adjust controller names,
  /// robot_description source, and namespace remappings for their robot.
  static QString launchFileContent(const RuntimeFindings &findings,
                                   const QString &instanceName,
                                   const QString &rosNamespace);

  /// Returns a minimal one-liner CLI command to start the required nodes.
  /// Suitable for pasting into a terminal or populating the Run field.
  static QString launchCommand(const RuntimeFindings &findings,
                               const QString &instanceName,
                               const QString &rosNamespace);
};

}  // namespace robot_importer_gui

#endif
