#ifndef ROBOT_IMPORTER_GUI_INSTANCE_REWRITER_HH_
#define ROBOT_IMPORTER_GUI_INSTANCE_REWRITER_HH_

#include <string>
#include <QString>

namespace robot_importer_gui
{

/// SDF rewriter for basic multi-instance support.
///
/// GUARANTEED behaviour:
///   - Replaces the top-level <model name="..."> with instanceName.
///     This makes the Gazebo entity name unique.
///
/// BEST-EFFORT (noted, not universally applied):
///   - rosNamespace and framePrefix are stored in Options and exposed in the
///     UI, but are NOT rewritten into the SDF by this class.
///     Reasons:
///       * ros_namespace injection is only meaningful for ROS plugin elements
///         that actually expose a <ros><namespace> parameter. The set of such
///         plugins changes with each ros2 release and cannot be maintained
///         without constant updates.
///       * frame_prefix would require renaming every link and joint name while
///         keeping all cross-references consistent — full graph walk, phase 5.
///     The caller (ImporterBackend) will emit a user-visible warning if either
///     option is non-empty, so the user knows they are taking manual action.
///
/// CANNOT do:
///   - Rewrite hardcoded topic/service strings inside arbitrary plugins.
///   - Guarantee collision-free isolation for any model.
struct InstanceRewriter
{
  struct Options
  {
    std::string instanceName;   // renames <model name="..."> — guaranteed
    std::string rosNamespace;   // noted in warning; NOT injected into plugins
    std::string framePrefix;    // noted in warning; NOT applied (phase 5)
  };

  /// Returns rewritten SDF, or original SDF unchanged if rewriting fails.
  /// warnings is populated with human-readable notices about limits.
  static std::string rewrite(const std::string &_sdf,
                             const Options &_opts,
                             QString &_warnings);
};

}  // namespace robot_importer_gui

#endif
