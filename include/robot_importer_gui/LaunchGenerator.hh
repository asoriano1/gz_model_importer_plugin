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
  /// Returns a one-line ros_gz_bridge parameter_bridge command covering all
  /// inferred sensor bridges.  Returns empty string if there are no bridge specs.
  static QString bridgeCommand(const RuntimeFindings &findings,
                               const QString &rosNamespace = {});

  /// Returns a Python ROS 2 launch file as a QString.
  static QString launchFileContent(const RuntimeFindings &findings,
                                   const QString &instanceName,
                                   const QString &rosNamespace);

  /// Returns a minimal one-liner CLI command to start the required nodes.
  /// Prefers bridgeCommand() when bridges are available.
  static QString launchCommand(const RuntimeFindings &findings,
                               const QString &instanceName,
                               const QString &rosNamespace);
};

}  // namespace robot_importer_gui

#endif
