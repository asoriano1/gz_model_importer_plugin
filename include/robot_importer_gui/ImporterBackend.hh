#ifndef ROBOT_IMPORTER_GUI_IMPORTER_BACKEND_HH_
#define ROBOT_IMPORTER_GUI_IMPORTER_BACKEND_HH_

#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <memory>
#include <string>

#include "robot_importer_gui/ImporterState.hh"
#include "robot_importer_gui/FileLoader.hh"

namespace robot_importer_gui
{

class ImportOptions;
class FileSelector;
class ModelLoader;
class PreviewController;
class GzSpawnClient;
class RuntimeProcessManager;

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

  // ---- Runtime analysis (string-based, for backward compat) ----
  Q_PROPERTY(QString runtimeWarning         READ runtimeWarning         NOTIFY runtimeWarningChanged)
  Q_PROPERTY(QString suggestedLaunchContent READ suggestedLaunchContent NOTIFY runtimeWarningChanged)
  Q_PROPERTY(QString suggestedLaunchCommand READ suggestedLaunchCommand NOTIFY runtimeWarningChanged)
  Q_PROPERTY(QString customLaunchCommand    READ customLaunchCommand    NOTIFY customLaunchCommandChanged)

  // ---- Runtime analysis (structured) ----
  Q_PROPERTY(bool    runtimeRequired          READ runtimeRequired          NOTIFY runtimeWarningChanged)
  Q_PROPERTY(QString runtimeSummary           READ runtimeSummary           NOTIFY runtimeWarningChanged)
  Q_PROPERTY(QString bridgeCommand            READ bridgeCommand            NOTIFY runtimeWarningChanged)
  Q_PROPERTY(bool    hasBridgeRequirements    READ hasBridgeRequirements    NOTIFY runtimeWarningChanged)
  Q_PROPERTY(bool    hasUnresolvedRuntimeItems READ hasUnresolvedRuntimeItems NOTIFY runtimeWarningChanged)

  // ---- Process status ----
  Q_PROPERTY(bool    runtimeRunning  READ runtimeRunning  NOTIFY runtimeRunningChanged)
  Q_PROPERTY(bool    launchRunning   READ runtimeRunning  NOTIFY runtimeRunningChanged)  // compat alias
  Q_PROPERTY(QString runtimeStatus   READ runtimeStatus   NOTIFY runtimeRunningChanged)

  Q_PROPERTY(robot_importer_gui::FileSelector   *fileSelector
             READ fileSelector   CONSTANT)
  Q_PROPERTY(robot_importer_gui::ImportOptions  *importOptions
             READ importOptions  CONSTANT)
  Q_PROPERTY(robot_importer_gui::PreviewController *previewController
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

  public: QString runtimeWarning()          const;
  public: QString suggestedLaunchContent()  const;
  public: QString suggestedLaunchCommand()  const;
  public: QString customLaunchCommand()     const;

  public: bool    runtimeRequired()          const;
  public: QString runtimeSummary()           const;
  public: QString bridgeCommand()            const;
  public: bool    hasBridgeRequirements()    const;
  public: bool    hasUnresolvedRuntimeItems() const;

  public: bool    runtimeRunning() const;
  public: QString runtimeStatus()  const;

  public: FileSelector      *fileSelector()      const;
  public: ImportOptions     *importOptions()     const;
  public: PreviewController *previewController() const;

  public: Q_INVOKABLE void reset();
  public: Q_INVOKABLE void importRobot();
  public: Q_INVOKABLE void requestPreview();
  public: Q_INVOKABLE void cancelPreview();
  public: Q_INVOKABLE void setXacroArgs(const QStringList &_args);

  public: Q_INVOKABLE void setCustomLaunchCommand(const QString &cmd);
  public: Q_INVOKABLE void copyLaunchCommand();
  public: Q_INVOKABLE bool saveLaunchFile(const QString &path);
  public: Q_INVOKABLE void runLaunchCommand();
  public: Q_INVOKABLE void stopLaunchCommand();

  signals: void stateChanged();
  signals: void lastErrorChanged();
  signals: void lastWarningChanged();
  signals: void worldNameChanged();
  signals: void preflightReportChanged();
  signals: void runtimeWarningChanged();
  signals: void customLaunchCommandChanged();
  signals: void runtimeRunningChanged();

  // ---- Collaborator slots ----
  private slots:
  void onFileReady(const QString &path, robot_importer_gui::FileFormat format);
  void onFileError(const QString &msg);
  void onLoadComplete(const QString &sdfContent);
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
                               robot_importer_gui::FileFormat format);
  private: void resetPose();
  private: void assignUniqueName(const QString &filePath);
  private: void clearRuntimeState();
  static QString extractModelBaseName(const QString &filePath);

  private: ImporterState state_{ImporterState::Idle};
  private: QString lastError_;
  private: QString lastWarning_;
  private: QString worldName_;
  private: QString currentSdf_;
  private: QString preflightReport_;
  private: QStringList xacroArgs_;

  // Runtime analysis results — populated in onLoadComplete(), cleared in
  // startFileLoad() and reset().
  private: QString runtimeWarning_;
  private: QString runtimeSummary_;
  private: QString suggestedLaunchContent_;
  private: QString suggestedLaunchCommand_;
  private: QString bridgeCommand_;
  private: QString customLaunchCommand_;
  private: bool    hasBridgeRequirements_{false};
  private: bool    hasUnresolvedRuntimeItems_{false};

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

  private: std::unique_ptr<FileSelector>          fileSelector_;
  private: std::unique_ptr<ImportOptions>         importOptions_;
  private: std::unique_ptr<ModelLoader>           modelLoader_;
  private: std::unique_ptr<PreviewController>     previewController_;
  private: std::unique_ptr<GzSpawnClient>         spawnClient_;
  private: std::unique_ptr<RuntimeProcessManager> processManager_;
};

}  // namespace robot_importer_gui

#endif
