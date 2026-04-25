#include "robot_importer_gui/FileSelector.hh"

#include <QFileInfo>
#include <QUrl>

namespace robot_importer_gui
{

FileSelector::FileSelector(QObject *_parent) : QObject(_parent) {}
FileSelector::~FileSelector() = default;

QString     FileSelector::selectedPath()   const { return selectedPath_; }
QString     FileSelector::detectedFormat() const { return detectedFormat_; }
QString     FileSelector::lastError()      const { return lastError_; }
FileFormat  FileSelector::fileFormat()     const { return fileFormat_; }

void FileSelector::onFileChosen(const QString &_pathOrUrl)
{
  QString path = _pathOrUrl;
  if (path.startsWith(QStringLiteral("file://")))
    path = QUrl(path).toLocalFile();

  if (path.isEmpty())
  {
    lastError_ = QStringLiteral("Empty file path.");
    emit lastErrorChanged();
    emit fileError(lastError_);
    return;
  }

  QFileInfo info(path);
  if (!info.exists() || !info.isReadable())
  {
    lastError_ = QStringLiteral("File not found or not readable: %1").arg(path);
    emit lastErrorChanged();
    emit fileError(lastError_);
    return;
  }

  selectedPath_   = info.absoluteFilePath();
  fileFormat_     = FileLoader::detect(selectedPath_);
  detectedFormat_ = fileFormatName(fileFormat_);
  lastError_.clear();

  emit selectedPathChanged();
  emit detectedFormatChanged();
  emit lastErrorChanged();

  if (fileFormat_ == FileFormat::Unknown)
  {
    lastError_ = QStringLiteral(
        "Cannot determine format for: %1 — "
        "expected .urdf, .xacro or .sdf").arg(selectedPath_);
    emit lastErrorChanged();
    emit fileError(lastError_);
    return;
  }

  emit fileReady(selectedPath_, fileFormat_);
}

void FileSelector::reset()
{
  selectedPath_.clear();
  detectedFormat_.clear();
  lastError_.clear();
  fileFormat_ = FileFormat::Unknown;
  emit selectedPathChanged();
  emit detectedFormatChanged();
  emit lastErrorChanged();
}

}  // namespace robot_importer_gui
