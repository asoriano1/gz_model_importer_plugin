#ifndef GZ_MODEL_IMPORTER_GUI_INSTANCE_REWRITER_HH_
#define GZ_MODEL_IMPORTER_GUI_INSTANCE_REWRITER_HH_

#include <string>
#include <QString>

namespace gz_model_importer_gui
{

/// SDF rewriter for multi-instance support.
///
/// Renames the top-level <model name="..."> to instanceName.
/// Native Gazebo sensor topics embed the model name
/// (/model/<name>/link/.../sensor/...) so they become unique per instance.
/// ROS 2 topic mapping is handled externally by the bridge manager.
struct InstanceRewriter
{
  struct Options
  {
    std::string instanceName;  // renames <model name="...">
  };

  /// Returns rewritten SDF, or original SDF unchanged if rewriting fails.
  /// warnings is populated with human-readable notices about limits.
  static std::string rewrite(const std::string &_sdf,
                             const Options &_opts,
                             QString &_warnings);
};

}  // namespace gz_model_importer_gui

#endif
