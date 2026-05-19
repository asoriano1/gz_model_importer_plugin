#ifndef GZ_MODEL_IMPORTER_INSTANCE_REWRITER_HH_
#define GZ_MODEL_IMPORTER_INSTANCE_REWRITER_HH_

#include <string>
#include <QString>

namespace gz_model_importer
{

/// SDF rewriter that prepares an asset for multi-instance spawning.
///
/// Renames the top-level `<model name="...">` element to the desired
/// `instanceName`. This is enough to disambiguate native Gazebo sensor
/// topics, which are derived from the model name
/// (`/model/<name>/link/.../sensor/...`).
///
/// Note: ROS 2 topic remapping (namespace and frame prefix) is not handled
/// here — that is the bridge manager's job once the model is spawned.
struct InstanceRewriter
{
  /// Inputs that drive the rewrite.
  struct Options
  {
    std::string instanceName;  ///< New value for the top-level `<model name>`.
  };

  /// Rewrites `_sdf` according to `_opts`.
  ///
  /// Returns the rewritten SDF string on success, or the original SDF
  /// unchanged when rewriting fails (in that case `_warnings` is populated
  /// with a human-readable explanation).
  static std::string rewrite(const std::string &_sdf,
                             const Options &_opts,
                             QString &_warnings);
};

}  // namespace gz_model_importer

#endif
