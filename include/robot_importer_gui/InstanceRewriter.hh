#ifndef ROBOT_IMPORTER_GUI_INSTANCE_REWRITER_HH_
#define ROBOT_IMPORTER_GUI_INSTANCE_REWRITER_HH_

#include <string>
#include <QString>

namespace robot_importer_gui
{

/// SDF rewriter for multi-instance support.
///
/// Rewrites performed in order:
///
///  1. Renames the top-level <model name="..."> to instanceName.
///     This makes the Gazebo entity name unique.
///     Native Gazebo sensor topics embed the model name
///     (/model/<name>/link/.../sensor/...) so they become unique too.
///
///  2. Injects rosNamespace into plugin <ros><namespace> elements:
///       - If a <plugin> already has a <ros> child, the <namespace> inside it
///         is created or overwritten with the normalised namespace value.
///       - If a <plugin> has no <ros> child but its filename matches a known
///         gazebo_ros / gz_ros pattern, a <ros><namespace/></ros> block is
///         injected as the first child.
///     The namespace is normalised to start with '/'.
///
/// Limitations:
///   - Topic names hardcoded as <topic>...</topic> inside plugin configs are
///     not rewritten; only <ros><namespace> is updated.
///   - frame_prefix (TF link/joint renaming) is not yet implemented.
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
