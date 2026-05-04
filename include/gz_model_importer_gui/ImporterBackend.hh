#ifndef GZ_MODEL_IMPORTER_GUI_IMPORTER_BACKEND_HH_
#define GZ_MODEL_IMPORTER_GUI_IMPORTER_BACKEND_HH_

#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <memory>
#include <string>
#include <vector>

#include "gz_model_importer_gui/ImporterState.hh"
#include "gz_model_importer_gui/FileLoader.hh"

namespace gz_model_importer_gui
{

class ImportOptions;
class FileSelector;
class ModelLoader;
class PreviewController;
class GzSpawnClient;

/// Central workflow controller.
///
/// State machine (happy path without preview):
///   Idle → FileSelected → Expanding|Converting → Ready → Spawning → Done
///
/// State machine (with preview):
///   ... → Ready → Previewing → Configuring ──→ Spawning → Done
///                                           └→ Ready (cancel)
///
/// All error states expose lastError. Reset returns unconditionally to Idle.
class ImporterBackend : public QObject
{
  Q_OBJECT

  Q_PROPERTY(int     state     READ stateInt   NOTIFY stateChanged)
  Q_PROPERTY(QString stateName READ stateName  NOTIFY stateChanged)
  Q_PROPERTY(bool    busy      READ isBusy     NOTIFY stateChanged)

  Q_PROPERTY(QString lastError       READ lastError       NOTIFY lastErrorChanged)
  Q_PROPERTY(QString lastWarning     READ lastWarning     NOTIFY lastWarningChanged)
  Q_PROPERTY(QString worldName       READ worldName       NOTIFY worldNameChanged)
  Q_PROPERTY(QString preflightReport READ preflightReport NOTIFY preflightReportChanged)

  // ---- Lightweight ROS 2 hint (informational only, no process management) ----
  Q_PROPERTY(bool    hasRuntimeHint     READ hasRuntimeHint     NOTIFY runtimeHintChanged)
  Q_PROPERTY(int     runtimeHintSensorCount READ runtimeHintSensorCount NOTIFY runtimeHintChanged)
  Q_PROPERTY(int     runtimeHintControllerCount READ runtimeHintControllerCount NOTIFY runtimeHintChanged)
  Q_PROPERTY(QString runtimeHintSummary READ runtimeHintSummary NOTIFY runtimeHintChanged)
  Q_PROPERTY(QString runtimeHint        READ runtimeHint        NOTIFY runtimeHintChanged)
  Q_PROPERTY(QString runtimeHintDetails READ runtimeHintDetails NOTIFY runtimeHintChanged)

  // ---- XACRO namespace override ----
  // Visible only when the loaded XACRO declares a "namespace" arg.
  // Value is passed as namespace:=<v> at expansion time; changing it
  // re-triggers XACRO expansion automatically.
  Q_PROPERTY(bool    hasXacroNamespaceArg READ hasXacroNamespaceArg NOTIFY xacroNamespaceChanged)
  Q_PROPERTY(QString xacroNamespace       READ xacroNamespace       WRITE setXacroNamespace
             NOTIFY xacroNamespaceChanged)

  // ---- XACRO prefix override ----
  // Visible only when the loaded XACRO declares a "prefix" arg.
  // Controls link/sensor/frame_id naming — passed as prefix:=<v> at
  // expansion time; changing it re-triggers XACRO expansion automatically.
  Q_PROPERTY(bool    hasXacroPrefixArg READ hasXacroPrefixArg NOTIFY xacroPrefixChanged)
  Q_PROPERTY(QString xacroPrefix       READ xacroPrefix       WRITE setXacroPrefix
             NOTIFY xacroPrefixChanged)

  Q_PROPERTY(bool    robotStatePublisherSupported
             READ robotStatePublisherSupported
             NOTIFY robotStatePublisherSupportChanged)
  Q_PROPERTY(QString robotStatePublisherSupportText
             READ robotStatePublisherSupportText
             NOTIFY robotStatePublisherSupportChanged)

  Q_PROPERTY(gz_model_importer_gui::FileSelector   *fileSelector
             READ fileSelector   CONSTANT)
  Q_PROPERTY(gz_model_importer_gui::ImportOptions  *importOptions
             READ importOptions  CONSTANT)
  Q_PROPERTY(gz_model_importer_gui::PreviewController *previewController
             READ previewController CONSTANT)

  public: explicit ImporterBackend(QObject *_parent = nullptr);
  public: ~ImporterBackend() override;

  public: int     stateInt()   const;
  public: QString stateName()  const;
  public: bool    isBusy()     const;
  public: QString lastError()        const;
  public: QString lastWarning()      const;
  public: QString worldName()        const;
  public: QString preflightReport()  const;

  public: bool    hasRuntimeHint()     const;
  public: int     runtimeHintSensorCount() const;
  public: int     runtimeHintControllerCount() const;
  public: QString runtimeHintSummary() const;
  public: QString runtimeHint()        const;
  public: QString runtimeHintDetails() const;

  public: bool    hasXacroNamespaceArg() const;
  public: QString xacroNamespace()       const;
  public: void    setXacroNamespace(const QString &v);

  public: bool    hasXacroPrefixArg() const;
  public: QString xacroPrefix()       const;
  public: void    setXacroPrefix(const QString &v);

  public: bool    robotStatePublisherSupported() const;
  public: QString robotStatePublisherSupportText() const;

  public: FileSelector      *fileSelector()      const;
  public: ImportOptions     *importOptions()     const;
  public: PreviewController *previewController() const;

  public: Q_INVOKABLE void reset();
  public: Q_INVOKABLE void importRobot();
  public: Q_INVOKABLE void requestPreview();
  public: Q_INVOKABLE void cancelPreview();
  public: Q_INVOKABLE void setXacroArgs(const QStringList &_args);

  signals: void stateChanged();
  signals: void lastErrorChanged();
  signals: void lastWarningChanged();
  signals: void worldNameChanged();
  signals: void preflightReportChanged();
  signals: void runtimeHintChanged();
  signals: void xacroNamespaceChanged();
  signals: void xacroPrefixChanged();
  signals: void robotStatePublisherSupportChanged();

  // ---- Collaborator slots ----
  private slots:
  void onFileReady(const QString &path, gz_model_importer_gui::FileFormat format);
  void onFileError(const QString &msg);
  void onLoadComplete(const QString &sdfContent,
                      const QString &resolvedUrdfContent);
  void onLoadFailed(const QString &error);
  void onPreviewSpawned(const QString &name);
  void onPreviewFailed(const QString &error);
  void onPreviewCancelled();
  void onConfirmReady(const QString &worldName,
                      const QString &sdfContent,
                      const QString &instanceName);
  void onSpawnComplete(const QString &name);
  void onSpawnFailed(const QString &error);
  void onGazeboPoseMoved(double x, double y, double z,
                          double roll, double pitch, double yaw);
  void onPoseDebounceTimeout();

  // ---- Internal helpers ----
  private: void setState(ImporterState _s);
  private: void setError(const QString &_msg);
  private: void setWarning(const QString &_msg);
  private: bool ensureWorldName();
  private: std::string applyOptionsToSdf();
  private: void doFinalSpawn();
  private: void startFileLoad(const QString &path,
                               gz_model_importer_gui::FileFormat format);
  private: void resetPose();
  private: void assignUniqueName(const QString &filePath);
  private: void clearRuntimeHint();
  private: QString currentRosNamespace() const;
  private: void maybeLaunchRobotStatePublisher();
  private: void stopRobotStatePublisherProcesses();
  private: void removeRobotStatePublisherLaunch(void *processKey);
  static QString normalizeRosNamespace(const QString &value);
  static QString extractModelBaseName(const QString &filePath);

  private: ImporterState state_{ImporterState::Idle};
  private: QString lastError_;
  private: QString lastWarning_;
  private: QString worldName_;
  private: QString currentSdf_;
  private: QString currentResolvedUrdf_;
  private: QString preflightReport_;
  private: QStringList xacroArgs_;

  // ROS 2 hint — populated in onLoadComplete(), cleared on reset/new load.
  private: int runtimeHintSensorCount_{0};
  private: int runtimeHintControllerCount_{0};
  private: QString runtimeHintSummary_;
  private: QString runtimeHint_;
  private: QString runtimeHintDetails_;

  // XACRO namespace / prefix overrides.
  private: bool    hasXacroNamespaceArg_{false};
  private: QString xacroNamespace_;
  private: bool    hasXacroPrefixArg_{false};
  private: QString xacroPrefix_;
  // Current file — stored to allow re-expansion when args change.
  private: QString     currentFilePath_;
  private: FileFormat  currentFileFormat_{FileFormat::Unknown};
  // True when startFileLoad is triggered internally by a XACRO arg change,
  // not by a new user file selection.
  private: bool reexpanding_{false};

  // Set when a file is chosen; used by SdfUriRewriter.
  private: QString modelDir_;
  private: QString modelsRoot_;

  // Pose sync: prevents feedback loop when Gazebo updates ImportOptions pose.
  private: bool updatingFromGazebo_{false};
  private: QTimer *poseDebounceTimer_{nullptr};

  // Deferred file load.
  private: QString pendingFilePath_;
  private: FileFormat pendingFileFormat_{FileFormat::Unknown};

  // Per-session counters for unique name generation.
  private: QMap<QString, int> nameCounters_;

  private: struct RobotStatePublisherLaunch;
  private: std::vector<std::shared_ptr<RobotStatePublisherLaunch>> rspLaunches_;
  private: bool shuttingDownRobotStatePublishers_{false};

  private: std::unique_ptr<FileSelector>      fileSelector_;
  private: std::unique_ptr<ImportOptions>     importOptions_;
  private: std::unique_ptr<ModelLoader>       modelLoader_;
  private: std::unique_ptr<PreviewController> previewController_;
  private: std::unique_ptr<GzSpawnClient>     spawnClient_;
};

}  // namespace gz_model_importer_gui

#endif
