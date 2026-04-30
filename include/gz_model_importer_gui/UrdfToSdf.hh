#ifndef GZ_MODEL_IMPORTER_GUI_URDF_TO_SDF_HH_
#define GZ_MODEL_IMPORTER_GUI_URDF_TO_SDF_HH_

#include <string>
#include <QString>

namespace gz_model_importer_gui
{

/// Converts a URDF XML string (or SDF string) to a validated SDF XML string
/// using libsdformat14. sdf::readString() handles both URDF and SDF inputs
/// transparently.
///
/// Returns empty string and sets errorOut on failure.
struct UrdfToSdf
{
  static std::string convert(const std::string &_urdfOrSdfXml,
                             QString &_errorOut);
};

}  // namespace gz_model_importer_gui

#endif
