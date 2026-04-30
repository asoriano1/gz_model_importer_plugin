#ifndef GZ_MODEL_IMPORTER_GUI_GZ_SPAWN_CLIENT_HH_
#define GZ_MODEL_IMPORTER_GUI_GZ_SPAWN_CLIENT_HH_

#include <QObject>
#include <QSet>
#include <QString>
#include <memory>
#include <optional>

namespace gz { namespace msgs { class Boolean; } }

namespace gz_model_importer_gui
{

/// Optional pose override for spawnEntity().
/// When passed, sets EntityFactory::pose and overrides whatever <pose> is in
/// the SDF. When nullopt (the default), EntityFactory::pose is not set and
/// the server uses the SDF's own <pose> element (or the origin if absent).
struct EntitySpawnPose
{
  double x{0.0}, y{0.0}, z{0.0};
  double roll{0.0}, pitch{0.0}, yaw{0.0};
};

/// Thin adapter over gz::transport::Node for world-level operations.
///
/// All service calls are NON-BLOCKING: each call is dispatched to Qt's
/// global thread pool (QThreadPool via QtConcurrent::run). Results come back
/// via signals emitted on the transport thread and delivered to the Qt main
/// thread through queued connections.
///
/// Exceptions: discoverWorldName() and queryWorldPauseState() are synchronous
/// (blocking) but fast (< 50 ms when Gazebo is running).
///
/// Services used (Gazebo Harmonic / gz-sim 8):
///   /world/<w>/create   EntityFactory  → Boolean
///   /world/<w>/remove   Entity         → Boolean
///   /world/<w>/control  WorldControl   → Boolean
/// Topic subscribed (one-shot):
///   /world/<w>/stats    WorldStatistics (paused field)
class GzSpawnClient : public QObject
{
  Q_OBJECT

  public: static constexpr unsigned int kServiceTimeoutMs = 5000;
  /// Timeout for the one-shot pause-state query. Stats topic fires at ~60 Hz,
  /// so 500 ms is generous when Gazebo is actually running.
  public: static constexpr unsigned int kPauseQueryTimeoutMs = 500;

  public: explicit GzSpawnClient(QObject *_parent = nullptr);
  public: ~GzSpawnClient() override;

  /// Discover the first active world name.
  /// Uses TopicList() + /world/<name>/stats pattern.
  /// Returns empty string (and emits worldDiscoveryFailed) if no world found.
  /// Synchronous, fast (local topic introspection, no network round-trip).
  public: QString discoverWorldName();

  /// Query whether the world is currently paused.
  /// Subscribes to /world/<_worldName>/stats, reads the first message, then
  /// returns. Blocks for up to kPauseQueryTimeoutMs (500 ms). Returns false
  /// (assume running) on timeout.
  /// Call from a non-rendering thread or accept a brief UI stall: in practice
  /// the stats topic fires within ~17 ms when Gazebo is running.
  public: bool queryWorldPauseState(const QString &_worldName);

  /// Spawn an SDF entity. The call is async; result arrives via
  /// spawnComplete / spawnFailed signals.
  /// _name overrides the model name in the SDF (via EntityFactory::name).
  /// _poseOverride, when not nullopt, overrides the SDF's own <pose>.
  public: void spawnEntity(const QString &_worldName,
                           const QString &_sdfContent,
                           const QString &_name,
                           std::optional<EntitySpawnPose> _poseOverride = std::nullopt);

  /// Remove a named model entity. Async; result via removeComplete /
  /// removeFailed.
  public: void removeEntity(const QString &_worldName,
                            const QString &_name);

  /// Pause the world synchronously.
  /// Blocks until the /world/<w>/control service returns or times out (~2 s).
  /// Returns true if the service confirmed success, false on timeout or refusal.
  /// Call this before any spawnEntity() to guarantee the physics engine is idle.
  public: bool pauseWorldSync(const QString &_worldName);

  /// Pause or resume the world asynchronously. Fire-and-forget; kept for
  /// any future UX-only use. For safety-critical pauses use pauseWorldSync().
  public: void setWorldPaused(const QString &_worldName, bool _paused);

  /// Update the pose of an already-spawned entity without removing and
  /// re-creating it. Uses /world/<w>/set_entity_pose (UserCommands system).
  /// Fire-and-forget async; failures are reported via poseUpdateFailed().
  public: void setEntityPoseAsync(const QString &_worldName,
                                  const QString &_entityName,
                                  const EntitySpawnPose &_pose);

  /// Query the names of all model entities currently in the world.
  /// Calls /world/<_worldName>/scene/info (synchronous, ~50 ms).
  /// Returns an empty set on timeout or service failure.
  public: QSet<QString> queryModelNames(const QString &_worldName);

  signals: void spawnComplete(const QString &entityName);
  signals: void spawnFailed(const QString &error);
  signals: void removeComplete(const QString &entityName);
  signals: void removeFailed(const QString &error);
  signals: void worldDiscoveryFailed(const QString &error);
  signals: void poseUpdateFailed(const QString &error);

  private: struct Impl;
  private: std::unique_ptr<Impl> impl_;
};

}  // namespace gz_model_importer_gui

#endif
