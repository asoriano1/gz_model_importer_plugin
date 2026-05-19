#ifndef GZ_MODEL_IMPORTER_URDF_TO_SDF_HH_
#define GZ_MODEL_IMPORTER_URDF_TO_SDF_HH_

#include <string>
#include <QString>

namespace gz_model_importer
{

/// Synchronous wrapper around libsdformat14's `sdf::readString()`.
///
/// `sdf::readString()` happily ingests either URDF or SDF XML and returns a
/// normalised SDF representation, so this helper works for both inputs.
struct UrdfToSdf
{
  /// Converts `_urdfOrSdfXml` to a validated SDF XML string.
  ///
  /// Returns an empty string on failure and populates `_errorOut` with the
  /// reason reported by libsdformat.
  static std::string convert(const std::string &_urdfOrSdfXml,
                             QString &_errorOut);
};

}  // namespace gz_model_importer

#endif
