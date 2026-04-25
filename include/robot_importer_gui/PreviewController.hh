#ifndef ROBOT_IMPORTER_GUI_PREVIEW_CONTROLLER_HH_
#define ROBOT_IMPORTER_GUI_PREVIEW_CONTROLLER_HH_

#include <QObject>
#include <QString>
#include <memory>

#include "robot_importer_gui/GzSpawnClient.hh"

namespace robot_importer_gui
{

/// Manages the modal preview mode lifecycle.
///
/// FLOW (auto-preview):
///   ImporterBackend calls spawnPreview() after SDF is loaded.
///   Preview entity spawns at the pose given by the caller (from ImportOptions).
///   Entity is marked <static> in SDF so it does not fall under physics.
///   World is NOT paused — this keeps /world/<w>/pose/info publishing
///   so that Gazebo→Panel pose sync works via the pose subscription.
///
/// POSE SYNC:
///   After spawn, PreviewController subscribes to /world/<w>/pose/info.
///   When the preview entity is moved (TransformControl), the new pose is
///   emitted via previewPoseMoved() so ImporterBackend can update ImportOptions.
///   When ImportOptions pose changes from the panel, ImporterBackend calls
///   respawnAt() which sends a direct /world/<w>/set_entity_pose service call
///   (no remove+respawn, no state change, no race condition).
///
/// PLUGIN STRIPPING:
///   All <plugin> elements are removed from the preview SDF.
///   <static>true</static> is injected into the model element.
class PreviewController : public QObject
{
  Q_OBJECT

  Q_PROPERTY(bool previewing READ isPreviewing NOTIFY previewingChanged)
  Q_PROPERTY(QString previewEntityName READ previewEntityName
             NOTIFY previewEntityNameChanged)

  public: explicit PreviewController(QObject *_parent = nullptr);
  public: ~PreviewController() override;

  public: bool    isPreviewing()      const;
  public: QString previewEntityName() const;
  public: QString worldName()         const;

  /// Enter preview mode. Spawns the preview entity at _pose.
  /// \param _worldName    Active Gazebo world name.
  /// \param _sdfContent   SDF with URIs already rewritten to file://.
  /// \param _instanceName Clean final name; preview entity is "__preview_<name>".
  /// \param _pose         Initial spawn pose (from ImportOptions).
  public: void spawnPreview(const QString &_worldName,
                            const QString &_sdfContent,
                            const QString &_instanceName,
                            const EntitySpawnPose &_pose);

  /// Cancel preview: remove preview entity.
  public: void cancelPreview();

  /// Confirm: remove preview entity, then signal ImporterBackend to final-spawn.
  public: void confirmPreview();

  /// Move the preview entity to a new pose via /world/<w>/set_entity_pose.
  /// Direct service call — no remove+respawn, no state change.
  public: void respawnAt(const EntitySpawnPose &_pose);

  signals:
  void previewSpawned(const QString &entityName);
  void previewFailed(const QString &error);
  void previewCancelled();
  void confirmReady(const QString &worldName,
                    const QString &sdfContent,
                    const QString &instanceName);
  void removeWarning(const QString &message);

  /// Emitted when the preview entity is moved in the Gazebo scene.
  /// Pose is in metres and radians (roll, pitch, yaw).
  void previewPoseMoved(double x, double y, double z,
                        double roll, double pitch, double yaw);

  void previewingChanged();
  void previewEntityNameChanged();

  private slots:
  void onSpawnComplete(const QString &name);
  void onSpawnFailed(const QString &error);
  void onRemoveComplete(const QString &name);
  void onRemoveFailed(const QString &error);
  void onPoseUpdateFailed(const QString &error);

  private:
  void clearPreviewState();
  void subscribeToPoseInfo();
  void unsubscribeFromPoseInfo();

  /// Returns a copy of _sdf with all <plugin> elements removed and
  /// <static>true</static> injected into the first <model> element.
  static QString preparePreviewSdf(const QString &_sdf);

  bool    previewing_{false};
  QString previewEntityName_;

  QString worldName_;
  QString preparedSdf_;    // stripped+static SDF (plugins removed, <static>true</static>)
  QString origSdfContent_; // original SDF (with plugins), for confirmPreview()
  QString instanceName_;

  enum class PendingAction { None, Cancel, Confirm };
  PendingAction  pendingAction_{PendingAction::None};

  struct Impl;
  std::unique_ptr<Impl> impl_;

  std::unique_ptr<GzSpawnClient> spawnClient_;
};

}  // namespace robot_importer_gui

#endif
