#ifndef GZ_MODEL_IMPORTER_GUI_XACRO_EXPANDER_HH_
#define GZ_MODEL_IMPORTER_GUI_XACRO_EXPANDER_HH_

#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QProcess>
#include <memory>

namespace gz_model_importer_gui
{

/// Async XACRO expander. Shells out to `xacro` CLI via QProcess.
/// Emits expandComplete(urdfContent) on success or expandFailed(error)
/// on non-zero exit or stderr containing "ERROR".
///
/// xacro arguments are passed as key=value pairs via extraArgs.
class XacroExpander : public QObject
{
  Q_OBJECT

  public: explicit XacroExpander(QObject *_parent = nullptr);
  public: ~XacroExpander() override;

  /// Start async expansion. If xacroArgs is non-empty each element is
  /// passed verbatim after the filename (expected format: "key:=value").
  public: void expand(const QString &_xacroPath,
                      const QStringList &_xacroArgs = {});

  /// Abort any running expansion.
  public: void cancel();

  /// Parse <xacro:arg> declarations from a XACRO file without running xacro.
  /// Returns a map of arg name → default value.
  /// Args declared without a default attribute are mapped to an empty string.
  static QMap<QString, QString> discoverArgs(const QString &_xacroPath);

  signals: void expandComplete(const QString &urdfContent);
  signals: void expandFailed(const QString &errorSummary,
                             const QString &fullStderr);

  private slots: void onProcessFinished(int exitCode,
                                        QProcess::ExitStatus status);

  private: std::unique_ptr<QProcess> proc_;
};

}  // namespace gz_model_importer_gui

#endif
