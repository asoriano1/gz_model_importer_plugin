#include "gz_model_importer_gui/PreviewController.hh"

#include <atomic>
#include <functional>
#include <memory>
#include <string>

#include <gz/common/Console.hh>
#include <gz/math/Quaternion.hh>
#include <gz/msgs/pose_v.pb.h>
#include <gz/transport/Node.hh>
#include <tinyxml2.h>

namespace gz_model_importer_gui
{

// ============================================================
// Pimpl — hides gz-transport from the header
// ============================================================
struct PreviewController::Impl
{
  gz::transport::Node node;
};

// ============================================================
// Construction
// ============================================================
PreviewController::PreviewController(QObject *_parent)
: QObject(_parent),
  impl_(std::make_unique<Impl>()),
  spawnClient_(std::make_unique<GzSpawnClient>(this))
{
  connect(spawnClient_.get(), &GzSpawnClient::spawnComplete,
          this, &PreviewController::onSpawnComplete);
  connect(spawnClient_.get(), &GzSpawnClient::spawnFailed,
          this, &PreviewController::onSpawnFailed);
  connect(spawnClient_.get(), &GzSpawnClient::removeComplete,
          this, &PreviewController::onRemoveComplete);
  connect(spawnClient_.get(), &GzSpawnClient::removeFailed,
          this, &PreviewController::onRemoveFailed);
  connect(spawnClient_.get(), &GzSpawnClient::poseUpdateFailed,
          this, &PreviewController::onPoseUpdateFailed);
}

PreviewController::~PreviewController() = default;

bool    PreviewController::isPreviewing()      const { return previewing_; }
QString PreviewController::previewEntityName() const { return previewEntityName_; }
QString PreviewController::worldName()         const { return worldName_; }

// ============================================================
// spawnPreview
// ============================================================
void PreviewController::spawnPreview(const QString &_worldName,
                                      const QString &_sdfContent,
                                      const QString &_instanceName,
                                      const EntitySpawnPose &_pose)
{
  worldName_      = _worldName;
  origSdfContent_ = _sdfContent;
  instanceName_   = _instanceName;
  pendingAction_  = PendingAction::None;

  previewEntityName_ = QStringLiteral("__preview_%1").arg(_instanceName);
  emit previewEntityNameChanged();

  preparedSdf_ = preparePreviewSdf(_sdfContent);

  gzmsg << "[gz_model_importer_gui] Spawning preview '"
        << previewEntityName_.toStdString()
        << "' at (" << _pose.x << ", " << _pose.y << ", " << _pose.z << ").\n";

  spawnClient_->spawnEntity(worldName_, preparedSdf_, previewEntityName_, _pose);
}

// ============================================================
// cancelPreview
// ============================================================
void PreviewController::cancelPreview()
{
  if (!previewing_ || previewEntityName_.isEmpty())
    return;

  pendingAction_ = PendingAction::Cancel;
  unsubscribeFromPoseInfo();
  spawnClient_->removeEntity(worldName_, previewEntityName_);
}

// ============================================================
// confirmPreview
// ============================================================
void PreviewController::confirmPreview()
{
  if (!previewing_ || previewEntityName_.isEmpty())
    return;

  pendingAction_ = PendingAction::Confirm;
  unsubscribeFromPoseInfo();
  spawnClient_->removeEntity(worldName_, previewEntityName_);
}

// ============================================================
// respawnAt — move preview to a new pose (direct service, no remove+respawn)
//
// Using set_entity_pose instead of remove→spawn avoids the race condition
// where the remove service returns before gz-sim processes the deletion,
// causing an immediate spawn with the same name to fail with "already exists".
// ============================================================
void PreviewController::respawnAt(const EntitySpawnPose &_pose)
{
  if (!previewing_ || previewEntityName_.isEmpty())
    return;

  spawnClient_->setEntityPoseAsync(worldName_, previewEntityName_, _pose);
}

// ============================================================
// Slots
// ============================================================
void PreviewController::onSpawnComplete(const QString &_name)
{
  if (_name != previewEntityName_)
    return;

  pendingAction_ = PendingAction::None;
  previewing_ = true;
  emit previewingChanged();

  subscribeToPoseInfo();

  gzmsg << "[gz_model_importer_gui] Preview entity spawned: "
        << _name.toStdString() << "\n";
  emit previewSpawned(_name);
}

void PreviewController::onSpawnFailed(const QString &_error)
{
  gzwarn << "[gz_model_importer_gui] Preview spawn failed: "
         << _error.toStdString() << "\n";
  clearPreviewState();
  emit previewFailed(_error);
}

void PreviewController::onRemoveComplete(const QString & /*_name*/)
{
  const PendingAction action = pendingAction_;
  const QString worldName    = worldName_;
  const QString origSdf      = origSdfContent_;
  const QString instanceName = instanceName_;

  clearPreviewState();

  if (action == PendingAction::Cancel)
    emit previewCancelled();
  else if (action == PendingAction::Confirm)
    emit confirmReady(worldName, origSdf, instanceName);
}

void PreviewController::onRemoveFailed(const QString &_error)
{
  gzwarn << "[gz_model_importer_gui] Preview removal failed: "
         << _error.toStdString() << ". Proceeding with state cleanup.\n";
  emit removeWarning(
      QStringLiteral("Preview entity removal failed (%1). "
                     "It may have been removed manually.").arg(_error));

  const PendingAction action = pendingAction_;
  const QString worldName    = worldName_;
  const QString origSdf      = origSdfContent_;
  const QString instanceName = instanceName_;

  clearPreviewState();

  if (action == PendingAction::Cancel)
    emit previewCancelled();
  else if (action == PendingAction::Confirm)
    emit confirmReady(worldName, origSdf, instanceName);
}

void PreviewController::onPoseUpdateFailed(const QString &_error)
{
  gzwarn << "[gz_model_importer_gui] " << _error.toStdString() << "\n";
  emit removeWarning(_error);
}

// ============================================================
// Internal helpers
// ============================================================
void PreviewController::clearPreviewState()
{
  previewing_ = false;
  previewEntityName_.clear();
  pendingAction_ = PendingAction::None;
  emit previewingChanged();
  emit previewEntityNameChanged();
}

void PreviewController::subscribeToPoseInfo()
{
  if (worldName_.isEmpty() || previewEntityName_.isEmpty())
    return;

  const std::string topic = "/world/" + worldName_.toStdString() + "/pose/info";
  const std::string targetName = previewEntityName_.toStdString();

  // The lambda is called from a gz-transport thread. We use Qt::QueuedConnection
  // semantics by emitting signals — Qt delivers them to the main thread.
  // Capture self by raw pointer; the subscription is unsubscribed before
  // destructor via clearPreviewState() / unsubscribeFromPoseInfo().
  PreviewController *self = this;

  impl_->node.Subscribe<gz::msgs::Pose_V>(topic,
    [self, targetName](const gz::msgs::Pose_V &_msg)
    {
      for (int i = 0; i < _msg.pose_size(); ++i)
      {
        const auto &p = _msg.pose(i);
        if (p.name() != targetName)
          continue;

        const auto &pos = p.position();
        const auto &ori = p.orientation();

        // Convert quaternion to roll/pitch/yaw (radians).
        gz::math::Quaterniond q(ori.w(), ori.x(), ori.y(), ori.z());
        const gz::math::Vector3d rpy = q.Euler();

        emit self->previewPoseMoved(pos.x(), pos.y(), pos.z(),
                                    rpy.X(), rpy.Y(), rpy.Z());
        break;
      }
    });

  gzmsg << "[gz_model_importer_gui] Subscribed to " << topic << "\n";
}

void PreviewController::unsubscribeFromPoseInfo()
{
  if (worldName_.isEmpty())
    return;

  const std::string topic = "/world/" + worldName_.toStdString() + "/pose/info";
  impl_->node.Unsubscribe(topic);
}

// ============================================================
// preparePreviewSdf — strip plugins + inject <static>true</static>
//
// <plugin> elements removed: prevents ROS/gz plugins from activating during
// preview. <collision> elements kept so auto-inertia computation succeeds.
// <static>true</static> added so the body has no dynamic physics.
//
// Visual distinction (transparency / wireframe) is applied after spawn via
// the gz-rendering API in ImporterPlugin::onRender(), which works for all
// visual types including mesh-embedded materials.
// ============================================================
// static
QString PreviewController::preparePreviewSdf(const QString &_sdf)
{
  tinyxml2::XMLDocument doc;
  if (doc.Parse(_sdf.toUtf8().constData()) != tinyxml2::XML_SUCCESS)
    return _sdf;

  // Strip named element types recursively.
  auto stripElements = [](tinyxml2::XMLElement *root,
                          const char *tagName)
  {
    std::function<void(tinyxml2::XMLElement *)> walk =
        [&walk, tagName](tinyxml2::XMLElement *el)
    {
      tinyxml2::XMLElement *child = el->FirstChildElement();
      while (child)
      {
        tinyxml2::XMLElement *next = child->NextSiblingElement();
        if (std::string(child->Name()) == tagName)
          el->DeleteChild(child);
        else
          walk(child);
        child = next;
      }
    };
    walk(root);
  };

  // Inject <static>true</static> into the first <model> element found.
  std::function<bool(tinyxml2::XMLElement *)> makeStatic =
      [&makeStatic](tinyxml2::XMLElement *el) -> bool
  {
    if (std::string(el->Name()) == "model")
    {
      // Remove existing <static> child if present to avoid duplicates.
      if (auto *existing = el->FirstChildElement("static"))
        el->DeleteChild(existing);
      auto *staticEl = el->GetDocument()->NewElement("static");
      staticEl->SetText("true");
      el->InsertFirstChild(staticEl);
      return true;
    }
    for (auto *c = el->FirstChildElement(); c; c = c->NextSiblingElement())
      if (makeStatic(c)) return true;
    return false;
  };

  if (auto *root = doc.RootElement())
  {
    stripElements(root, "plugin");
    makeStatic(root);
  }

  tinyxml2::XMLPrinter printer;
  doc.Print(&printer);
  return QString::fromUtf8(printer.CStr());
}

}  // namespace gz_model_importer_gui
