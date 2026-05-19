#include "gz_model_importer/GzSpawnClient.hh"

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <regex>
#include <string>
#include <vector>

#include <QSet>
#include <QString>

#include <gz/common/Console.hh>
#include <gz/math/Quaternion.hh>

#include <QtConcurrent/QtConcurrentRun>

#include <gz/transport/Node.hh>
#include <gz/msgs/empty.pb.h>
#include <gz/msgs/entity_factory.pb.h>
#include <gz/msgs/boolean.pb.h>
#include <gz/msgs/entity.pb.h>
#include <gz/msgs/pose.pb.h>
#include <gz/msgs/scene.pb.h>
#include <gz/msgs/world_control.pb.h>
#include <gz/msgs/world_stats.pb.h>

namespace gz_model_importer
{

// ============================================================
// Pimpl: hides gz::transport from headers that include only GzSpawnClient.hh
// ============================================================
struct GzSpawnClient::Impl
{
  gz::transport::Node node;
};

// ============================================================
// Construction
// ============================================================
GzSpawnClient::GzSpawnClient(QObject *_parent)
: QObject(_parent),
  impl_(std::make_unique<Impl>())
{
  // Ensure signals crossing thread boundaries are delivered via
  // Qt::QueuedConnection (the default when sender and receiver live on
  // different threads). Register QString so it can travel across threads.
  qRegisterMetaType<QString>("QString");
}

GzSpawnClient::~GzSpawnClient() = default;

// ============================================================
// World name discovery (synchronous, fast — local topic introspection)
// ============================================================
QString GzSpawnClient::discoverWorldName()
{
  // TopicList() performs a local lookup of discovered gz-transport topics.
  // No network round-trip; usually returns in < 10 ms.
  // Pattern: /world/<world_name>/stats is published by every gz-sim world.
  std::vector<std::string> topics;
  impl_->node.TopicList(topics);

  const std::regex statsPattern(R"(^/world/([^/]+)/stats$)");
  std::smatch m;
  for (const auto &t : topics)
  {
    if (std::regex_match(t, m, statsPattern))
      return QString::fromStdString(m[1].str());
  }

  // No world found — could mean gz sim is not running or not connected yet.
  emit worldDiscoveryFailed(
      QStringLiteral("No /world/*/stats topic found. "
                     "Is Gazebo running and connected?"));
  return {};
}

// ============================================================
// queryWorldPauseState — synchronous one-shot subscribe
// ============================================================
bool GzSpawnClient::queryWorldPauseState(const QString &_worldName)
{
  const std::string topic = "/world/" + _worldName.toStdString() + "/stats";

  // Use a shared promise so the callback can safely set the value even if
  // it fires after wait_for returns (the promise outlives this function
  // because the lambda captures shared_ptr).
  struct Shared {
    std::promise<bool> promise;
    std::atomic<bool>  fired{false};
  };
  auto shared = std::make_shared<Shared>();
  std::future<bool> future = shared->promise.get_future();

  // Local node, unsubscribed when it goes out of scope at function exit.
  gz::transport::Node localNode;
  const bool ok = localNode.Subscribe<gz::msgs::WorldStatistics>(topic,
    [shared](const gz::msgs::WorldStatistics &_msg)
    {
      bool expected = false;
      if (shared->fired.compare_exchange_strong(expected, true))
        shared->promise.set_value(_msg.paused());
    });

  if (!ok)
  {
    gzwarn << "[gz_model_importer] Failed to subscribe to " << topic
           << ". Assuming world is running (paused=false).\n";
    return false;
  }

  const bool timedOut =
      future.wait_for(std::chrono::milliseconds(kPauseQueryTimeoutMs)) !=
      std::future_status::ready;

  if (timedOut)
  {
    gzwarn << "[gz_model_importer] Timed out waiting for " << topic
           << ". Assuming world is running (paused=false).\n";
    return false;
  }

  return future.get();
}

// ============================================================
// spawnEntity — non-blocking, result via signals
// ============================================================
void GzSpawnClient::spawnEntity(const QString &_worldName,
                                const QString &_sdfContent,
                                const QString &_name,
                                std::optional<EntitySpawnPose> _poseOverride)
{
  // Capture everything by value so lifetime is safe if GzSpawnClient is
  // destroyed before the thread pool worker finishes.
  const std::string worldName = _worldName.toStdString();
  const std::string sdfContent = _sdfContent.toStdString();
  const std::string name = _name.toStdString();
  const std::string service = "/world/" + worldName + "/create";
  const std::optional<EntitySpawnPose> pose = _poseOverride;

  // Raw pointer to emit signals from the worker thread.
  // Qt delivers them via QueuedConnection to the main thread.
  GzSpawnClient *self = this;

  QtConcurrent::run([self, service, sdfContent, name, pose]()
  {
    gz::msgs::EntityFactory req;
    req.set_sdf(sdfContent);
    // EntityFactory::name overrides the model name in the SDF string.
    req.set_name(name);

    if (pose)
    {
      gz::msgs::Pose *poseMsg = req.mutable_pose();
      gz::msgs::Vector3d *pos = poseMsg->mutable_position();
      pos->set_x(pose->x);
      pos->set_y(pose->y);
      pos->set_z(pose->z);
      // RPY → quaternion.
      const gz::math::Quaterniond q(pose->roll, pose->pitch, pose->yaw);
      gz::msgs::Quaternion *quat = poseMsg->mutable_orientation();
      quat->set_w(q.W());
      quat->set_x(q.X());
      quat->set_y(q.Y());
      quat->set_z(q.Z());
    }

    gz::msgs::Boolean rep;
    bool result = false;

    // gz::transport::Node is thread-safe for concurrent calls.
    gz::transport::Node node;
    const bool called = node.Request(service, req,
                                     GzSpawnClient::kServiceTimeoutMs,
                                     rep, result);

    const QString qname = QString::fromStdString(name);
    if (!called)
    {
      emit self->spawnFailed(
          QStringLiteral("Spawn service call timed out: %1").arg(
              QString::fromStdString(service)));
    }
    else if (!result || !rep.data())
    {
      emit self->spawnFailed(
          QStringLiteral("Server refused spawn request for entity '%1'. "
                         "Check that the SDF is valid and the name is unique.")
              .arg(qname));
    }
    else
    {
      emit self->spawnComplete(qname);
    }
  });
}

// ============================================================
// removeEntity — non-blocking, result via signals
// ============================================================
void GzSpawnClient::removeEntity(const QString &_worldName,
                                  const QString &_name)
{
  const std::string worldName = _worldName.toStdString();
  const std::string name = _name.toStdString();
  const std::string service = "/world/" + worldName + "/remove";
  GzSpawnClient *self = this;

  QtConcurrent::run([self, service, name]()
  {
    gz::msgs::Entity req;
    req.set_name(name);
    req.set_type(gz::msgs::Entity::MODEL);

    gz::msgs::Boolean rep;
    bool result = false;

    gz::transport::Node node;
    const bool called = node.Request(service, req,
                                     GzSpawnClient::kServiceTimeoutMs,
                                     rep, result);

    const QString qname = QString::fromStdString(name);
    if (!called || !result || !rep.data())
    {
      emit self->removeFailed(
          QStringLiteral("Failed to remove entity '%1': "
                         "%2").arg(qname,
                                   called ? QStringLiteral("server refused")
                                          : QStringLiteral("service timeout")));
    }
    else
    {
      emit self->removeComplete(qname);
    }
  });
}

// ============================================================
// queryModelNames — synchronous scene/info service call
// ============================================================
QSet<QString> GzSpawnClient::queryModelNames(const QString &_worldName)
{
  const std::string service =
      "/world/" + _worldName.toStdString() + "/scene/info";

  gz::msgs::Empty req;
  gz::msgs::Scene rep;
  bool result = false;

  gz::transport::Node node;
  const bool called = node.Request(service, req,
                                   static_cast<unsigned int>(500),
                                   rep, result);
  if (!called || !result)
  {
    gzwarn << "[gz_model_importer] queryModelNames: scene/info call failed "
           << "for world '" << _worldName.toStdString() << "'\n";
    return {};
  }

  QSet<QString> names;
  for (int i = 0; i < rep.model_size(); ++i)
    names.insert(QString::fromStdString(rep.model(i).name()));
  return names;
}

// ============================================================
// pauseWorldSync — synchronous pause (operational safety requirement)
// ============================================================
bool GzSpawnClient::pauseWorldSync(const QString &_worldName)
{
  const std::string service =
      "/world/" + _worldName.toStdString() + "/control";

  gz::msgs::WorldControl req;
  req.set_pause(true);

  gz::msgs::Boolean rep;
  bool result = false;

  gz::transport::Node node;
  const bool called = node.Request(service, req, 2000u, rep, result);

  if (!called || !result)
  {
    gzwarn << "[gz_model_importer] pauseWorldSync: /world/control call failed"
           << " for world '" << _worldName.toStdString()
           << "'. Physics may still be running — spawn may be unsafe.\n";
    return false;
  }
  return rep.data();
}

// ============================================================
// setEntityPoseAsync — direct entity pose update (no remove+respawn)
//
// Uses /world/<w>/set_pose (gz::msgs::Pose → gz::msgs::Boolean), provided
// by the UserCommands system in gz-sim 8 (Gazebo Harmonic).
// The Pose message carries the entity name in the `name` field.
// This avoids the remove→respawn race condition (service returns before
// gz-sim processes the removal, causing "already exists" on re-spawn).
// ============================================================
void GzSpawnClient::setEntityPoseAsync(const QString &_worldName,
                                        const QString &_entityName,
                                        const EntitySpawnPose &_pose)
{
  const std::string service =
      "/world/" + _worldName.toStdString() + "/set_pose";
  const std::string name = _entityName.toStdString();
  const EntitySpawnPose pose = _pose;
  GzSpawnClient *self = this;

  gzmsg << "[gz_model_importer] set_pose → service='" << service
        << "' entity='" << name
        << "' pos=(" << pose.x << "," << pose.y << "," << pose.z
        << ") rpy=(" << pose.roll << "," << pose.pitch << "," << pose.yaw
        << ")\n";

  QtConcurrent::run([self, service, name, pose]()
  {
    gz::msgs::Pose req;
    req.set_name(name);

    gz::msgs::Vector3d *pos = req.mutable_position();
    pos->set_x(pose.x);
    pos->set_y(pose.y);
    pos->set_z(pose.z);

    const gz::math::Quaterniond q(pose.roll, pose.pitch, pose.yaw);
    gz::msgs::Quaternion *quat = req.mutable_orientation();
    quat->set_w(q.W());
    quat->set_x(q.X());
    quat->set_y(q.Y());
    quat->set_z(q.Z());

    gz::msgs::Boolean rep;
    bool result = false;
    gz::transport::Node node;
    const bool called = node.Request(service, req,
                                     GzSpawnClient::kServiceTimeoutMs,
                                     rep, result);

    if (!called || !result || !rep.data())
    {
      const std::string reason = called ? "server refused" : "service timeout";
      gzwarn << "[gz_model_importer] set_pose failed: service='" << service
             << "' entity='" << name
             << "' result='" << reason
             << "' pos=(" << pose.x << "," << pose.y << "," << pose.z << ")\n";
      emit self->poseUpdateFailed(
          QStringLiteral("set_pose failed for '%1' (%2). "
                         "Service: %3").arg(
              QString::fromStdString(name),
              QString::fromStdString(reason),
              QString::fromStdString(service)));
    }
    else
    {
      gzmsg << "[gz_model_importer] set_pose succeeded for '" << name << "'\n";
    }
  });
}

// ============================================================
// setWorldPaused — fire-and-forget (kept for async UX use only)
// ============================================================
void GzSpawnClient::setWorldPaused(const QString &_worldName, bool _paused)
{
  const std::string service =
      "/world/" + _worldName.toStdString() + "/control";
  const bool paused = _paused;

  // Fire-and-forget: we don't surface pause errors to the user.
  // Pause is a UX convenience; if it fails the preview still works.
  QtConcurrent::run([service, paused]()
  {
    gz::msgs::WorldControl req;
    req.set_pause(paused);
    gz::transport::Node node;
    gz::msgs::Boolean rep;
    bool result = false;
    node.Request(service, req, 2000u, rep, result);
    // Errors silently ignored — pause is best-effort UX.
  });
}

}  // namespace gz_model_importer
