#ifndef ROBOT_IMPORTER_GUI_FILE_LOADER_HH_
#define ROBOT_IMPORTER_GUI_FILE_LOADER_HH_

#include <string>
#include <QString>

namespace robot_importer_gui
{

enum class FileFormat
{
  Unknown,
  Urdf,
  Xacro,
  Sdf,
};

inline QString fileFormatName(FileFormat f)
{
  switch (f)
  {
    case FileFormat::Urdf:  return QStringLiteral("URDF");
    case FileFormat::Xacro: return QStringLiteral("XACRO");
    case FileFormat::Sdf:   return QStringLiteral("SDF");
    default:                return QStringLiteral("Unknown");
  }
}

/// Stateless helper: inspects a file path and determines its format.
/// Decision order: extension first, then XML root-element sniff if ambiguous.
struct FileLoader
{
  /// Detect format from extension + file header (first 512 bytes).
  static FileFormat detect(const QString &_path);

  /// Read full file content as UTF-8 string. Returns empty on failure.
  static std::string readContent(const QString &_path, QString &_errorOut);
};

}  // namespace robot_importer_gui

#endif
