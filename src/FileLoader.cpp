#include "robot_importer_gui/FileLoader.hh"

#include <QFile>
#include <QFileInfo>
#include <QTextStream>

namespace robot_importer_gui
{

FileFormat FileLoader::detect(const QString &_path)
{
  const QString ext = QFileInfo(_path).suffix().toLower();

  if (ext == QLatin1String("xacro"))
    return FileFormat::Xacro;
  if (ext == QLatin1String("sdf"))
    return FileFormat::Sdf;
  if (ext == QLatin1String("urdf"))
    return FileFormat::Urdf;

  // For .xml and other ambiguous extensions: sniff the root element tag.
  QFile f(_path);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    return FileFormat::Unknown;

  const QByteArray header = f.read(512);
  f.close();

  if (header.contains("<robot"))
    return FileFormat::Urdf;   // URDF root
  if (header.contains("<sdf"))
    return FileFormat::Sdf;
  if (header.contains("xmlns:xacro") || header.contains("xacro:"))
    return FileFormat::Xacro;

  return FileFormat::Unknown;
}

std::string FileLoader::readContent(const QString &_path, QString &_errorOut)
{
  QFile f(_path);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
  {
    _errorOut = QStringLiteral("Cannot open file: %1").arg(_path);
    return {};
  }

  QTextStream stream(&f);
  stream.setCodec("UTF-8");
  const QString content = stream.readAll();
  f.close();

  if (content.isEmpty())
  {
    _errorOut = QStringLiteral("File is empty: %1").arg(_path);
    return {};
  }

  return content.toStdString();
}

}  // namespace robot_importer_gui
