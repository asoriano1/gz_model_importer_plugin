#ifndef GZ_MODEL_IMPORTER_GUI_FILE_SELECTOR_HH_
#define GZ_MODEL_IMPORTER_GUI_FILE_SELECTOR_HH_

#include <QObject>
#include <QString>

#include "gz_model_importer_gui/FileLoader.hh"

namespace gz_model_importer_gui
{

/// Owns the file-selection step: accepts a path (or file:// URL from QML
/// FileDialog), validates existence, detects format, and emits results.
class FileSelector : public QObject
{
  Q_OBJECT

  Q_PROPERTY(QString selectedPath READ selectedPath NOTIFY selectedPathChanged)
  Q_PROPERTY(QString detectedFormat READ detectedFormat NOTIFY detectedFormatChanged)
  Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

  public: explicit FileSelector(QObject *_parent = nullptr);
  public: ~FileSelector() override;

  public: QString selectedPath()    const;
  public: QString detectedFormat()  const;
  public: QString lastError()       const;
  public: FileFormat fileFormat()   const;

  /// Called from QML after FileDialog resolves (accepts both plain paths and
  /// file:// URLs). Validates, detects format, emits fileReady or fileError.
  public: Q_INVOKABLE void onFileChosen(const QString &_pathOrUrl);

  public: Q_INVOKABLE void reset();

  signals: void fileReady(const QString &path, gz_model_importer_gui::FileFormat format);
  signals: void fileError(const QString &message);
  signals: void selectedPathChanged();
  signals: void detectedFormatChanged();
  signals: void lastErrorChanged();

  private: QString selectedPath_;
  private: QString detectedFormat_;
  private: QString lastError_;
  private: FileFormat fileFormat_{FileFormat::Unknown};
};

}  // namespace gz_model_importer_gui

#endif
