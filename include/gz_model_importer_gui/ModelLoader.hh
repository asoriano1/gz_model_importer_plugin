#ifndef GZ_MODEL_IMPORTER_GUI_MODEL_LOADER_HH_
#define GZ_MODEL_IMPORTER_GUI_MODEL_LOADER_HH_

#include <QObject>
#include <QString>
#include <QStringList>
#include <memory>

#include "gz_model_importer_gui/FileLoader.hh"

namespace gz_model_importer_gui
{

class XacroExpander;

/// Orchestrates the load pipeline: FileLoader → XacroExpander → UrdfToSdf.
///
/// Input:  file path + detected FileFormat + optional XACRO key:=value args
/// Output: validated SDF string via loadComplete(), or loadFailed()
///
/// All async work is routed through XacroExpander (QProcess). The
/// UrdfToSdf step is synchronous and runs on the Qt thread — it is fast
/// enough for typical robot descriptions (< 1 MB). If that proves wrong,
/// move it to a QThreadPool worker.
class ModelLoader : public QObject
{
  Q_OBJECT

  Q_PROPERTY(bool loading    READ isLoading    NOTIFY loadingChanged)
  Q_PROPERTY(QString lastError READ lastError  NOTIFY lastErrorChanged)

  public: explicit ModelLoader(QObject *_parent = nullptr);
  public: ~ModelLoader() override;

  public: bool    isLoading()  const;
  public: QString lastError()  const;

  /// Start the pipeline for the given file. xacroArgs is only used when
  /// format == Xacro.
  public: void load(const QString &_path,
                    FileFormat _format,
                    const QStringList &_xacroArgs = {});

  public: void cancel();

  signals: void loadComplete(const QString &sdfContent);
  signals: void loadFailed(const QString &error);
  signals: void loadingChanged();
  signals: void lastErrorChanged();

  private slots: void onExpandComplete(const QString &urdfContent);
  private slots: void onExpandFailed(const QString &errorSummary,
                                     const QString &fullStderr);

  private: bool    loading_{false};
  private: QString lastError_;
  private: std::unique_ptr<XacroExpander> expander_;
};

}  // namespace gz_model_importer_gui

#endif
