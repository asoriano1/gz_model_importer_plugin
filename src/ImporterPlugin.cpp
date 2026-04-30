#include "gz_model_importer_gui/ImporterPlugin.hh"

#include <cmath>
#include <string>

#include <QCoreApplication>
#include <QEvent>
#include <QtConcurrent/QtConcurrentRun>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>

#include <gz/gui/Application.hh>
#include <gz/gui/GuiEvents.hh>
#include <gz/plugin/Register.hh>
#include <gz/sim/gui/GuiEvents.hh>
#include <gz/transport/Node.hh>
#include <gz/msgs/empty.pb.h>
#include <gz/msgs/scene.pb.h>
#include <gz/rendering/Camera.hh>
#include <gz/rendering/Material.hh>
#include <gz/rendering/RenderingIface.hh>
#include <gz/rendering/Scene.hh>
#include <gz/rendering/Visual.hh>
#include <gz/common/Console.hh>

#include "gz_model_importer_gui/ImporterBackend.hh"
#include "gz_model_importer_gui/ImportOptions.hh"
#include "gz_model_importer_gui/FileSelector.hh"
#include "gz_model_importer_gui/PreviewController.hh"

// Q_INIT_RESOURCE cannot be called inside a namespace (Qt restriction).
static void initRobotImporterResources()
{
  Q_INIT_RESOURCE(RobotImporterGui);
}

namespace gz_model_importer_gui
{

// Forward declaration — defined before onRender().
static void applyHighlight(gz::rendering::VisualPtr vis, int mode);

RobotImporterGui::RobotImporterGui()
: gz::gui::Plugin(),
  backend_(std::make_unique<ImporterBackend>())
{
  initRobotImporterResources();

  // Context properties must be set on the engine ROOT context in the
  // constructor — NOT in LoadConfig(). gz-gui calls QQmlComponent::create()
  // before calling LoadConfig(), so any setContextProperty() call made in
  // LoadConfig() arrives after QML has already evaluated all its bindings.
  auto *engine = gz::gui::App()->Engine();
  if (engine)
  {
    auto *rootCtx = engine->rootContext();
    rootCtx->setContextProperty(QStringLiteral("backend"),
                                backend_.get());
    rootCtx->setContextProperty(QStringLiteral("fileSelector"),
                                backend_->fileSelector());
    rootCtx->setContextProperty(QStringLiteral("importOptions"),
                                backend_->importOptions());
    rootCtx->setContextProperty(QStringLiteral("previewCtrl"),
                                backend_->previewController());
    // Exposes highlightMode property to QML.
    rootCtx->setContextProperty(QStringLiteral("importerPlugin"), this);
  }

  qRegisterMetaType<gz_model_importer_gui::FileFormat>(
      "gz_model_importer_gui::FileFormat");

  auto *ctrl = backend_->previewController();

  connect(ctrl, &PreviewController::previewSpawned,
          this, &RobotImporterGui::onPreviewSpawned);

  connect(ctrl, &PreviewController::previewCancelled,
          this, &RobotImporterGui::onPreviewDone);

  connect(backend_.get(), &ImporterBackend::stateChanged,
          this, [this]()
          {
            if (backend_->stateName() == QStringLiteral("Done"))
              this->onPreviewDone();
          });

  gz::gui::App()->installEventFilter(this);
}

RobotImporterGui::~RobotImporterGui()
{
  gz::gui::App()->removeEventFilter(this);
  delete previewBadge_;
}

void RobotImporterGui::LoadConfig(const tinyxml2::XMLElement *_pluginElem)
{
  (void)_pluginElem;

  if (this->title.empty())
    this->title = "Robot Importer";

  QTimer::singleShot(500, this, [this]() { createPreviewBadge(); });
}

// ============================================================
// Highlight mode property
// ============================================================
int RobotImporterGui::highlightMode() const
{
  return highlightMode_.load(std::memory_order_relaxed);
}

void RobotImporterGui::setHighlightMode(int _mode)
{
  if (highlightMode_.exchange(_mode, std::memory_order_release) == _mode)
    return;
  // If a preview is alive, schedule re-application on the render thread.
  if (previewAlive_.load(std::memory_order_relaxed))
    highlightPending_.store(true, std::memory_order_release);
  emit highlightModeChanged();
}

// ============================================================
// Camera lifecycle slots (main thread)
// ============================================================
void RobotImporterGui::onPreviewSpawned(const QString &_entityName)
{
  const auto *opts = backend_->importOptions();
  {
    std::lock_guard<std::mutex> lock(renderMutex_);
    previewPos_        = gz::math::Vector3d(opts->poseX(), opts->poseY(), opts->poseZ());
    previewEntityName_ = _entityName.toStdString();
    previewWorldName_  = backend_->previewController()->worldName().toStdString();
  }
  previewAlive_.store(true, std::memory_order_release);
  // Only queue a highlight pass if the mode actually modifies the visual.
  // Mode 0 (None) must not touch materials or wireframe on spawn.
  if (highlightMode_.load(std::memory_order_relaxed) != 0)
    highlightPending_.store(true, std::memory_order_release);
  selectionPending_.store(true, std::memory_order_release);
  cameraState_.store(1, std::memory_order_release);
}

void RobotImporterGui::onPreviewDone()
{
  previewAlive_.store(false, std::memory_order_release);
  highlightPending_.store(false, std::memory_order_release);
  selectionPending_.store(false, std::memory_order_release);

  // Deselect the preview entity when preview ends (main thread → sendEvent ok).
  gz::sim::gui::events::DeselectAllEntities deselectEv(false);
  QCoreApplication::sendEvent(gz::gui::App(), &deselectEv);

  if (cameraState_.load(std::memory_order_relaxed) == 1)
  {
    cameraState_.store(0, std::memory_order_release);
    return;
  }
  cameraState_.store(2, std::memory_order_release);
}

// ============================================================
// eventFilter — called on the render thread for Render events
// ============================================================
bool RobotImporterGui::eventFilter(QObject *_obj, QEvent *_event)
{
  if (_event->type() == gz::gui::events::Render::kType)
    this->onRender();

  return QObject::eventFilter(_obj, _event);
}

// ============================================================
// applyHighlight — recursive render-thread helper
//
// mode 0 = None         → clear wireframe; undo transparency if active
// mode 1 = Transparency → clone each geometry's material, set 60 % transparency
// mode 2 = Wireframe    → SetWireframe(true) only; materials are NOT touched
//
// WHY wireframe must NOT touch materials:
//   Material::Clone() on a DAE/mesh-embedded material does not preserve all
//   Ogre2-specific properties (textures, PBR shaders). Calling Clone()+SetMaterial
//   in wireframe mode corrupts the visual — mesh loses textures and renders wrong.
//   SetWireframe() is a rendering-mode flag independent of the material system;
//   it can be toggled safely without ever modifying materials.
//
// WHY geometry-level traversal for transparency:
//   Mesh visuals store their material on the Geometry (Ogre2Mesh), not on the
//   Visual node. Visual::Material() returns null for these. Iterating
//   Geometry objects directly reaches their materials and preserves per-part
//   colors (each geometry gets its own cloned material with transparency).
// ============================================================
static void applyHighlight(gz::rendering::VisualPtr vis, int mode)
{
  if (!vis) return;

  vis->SetWireframe(mode == 2);

  if (mode == 1)
  {
    // Transparency: clone each material to avoid touching shared originals.
    const unsigned int geomCount = vis->GeometryCount();
    for (unsigned int g = 0; g < geomCount; ++g)
    {
      auto geom = vis->GeometryByIndex(g);
      if (!geom) continue;
      auto gMat = geom->Material();
      if (!gMat) continue;
      auto clone = gMat->Clone();
      clone->SetTransparency(0.6);
      geom->SetMaterial(clone, false);
    }
    if (geomCount == 0)
    {
      auto mat = vis->Material();
      if (mat)
      {
        auto clone = mat->Clone();
        clone->SetTransparency(0.6);
        vis->SetGeometryMaterial(clone, false);
      }
    }
  }
  else if (mode == 0)
  {
    // None: reset transparency in-place — no Clone(), no SetMaterial() call.
    // Called only when switching back from Transparent; if materials are
    // originals (never touched), SetTransparency(0.0) is a safe no-op.
    const unsigned int geomCount = vis->GeometryCount();
    for (unsigned int g = 0; g < geomCount; ++g)
    {
      auto geom = vis->GeometryByIndex(g);
      if (!geom) continue;
      auto mat = geom->Material();
      if (mat) mat->SetTransparency(0.0);
    }
    if (geomCount == 0)
    {
      auto mat = vis->Material();
      if (mat) mat->SetTransparency(0.0);
    }
  }
  // mode 2 (Wireframe): SetWireframe already done above; no material changes.

  for (unsigned int i = 0; i < vis->ChildCount(); ++i)
  {
    auto child = std::dynamic_pointer_cast<gz::rendering::Visual>(
        vis->ChildByIndex(i));
    if (child)
      applyHighlight(child, mode);
  }
}

// ============================================================
// onRender — camera + highlight operations, runs on the render thread
// ============================================================
void RobotImporterGui::onRender()
{
  const int camState = cameraState_.load(std::memory_order_acquire);
  const bool needHighlight = highlightPending_.load(std::memory_order_acquire);
  const bool needSelect    = selectionPending_.load(std::memory_order_acquire);

  if (camState == 0 && !needHighlight && !needSelect)
    return;

  auto scene = gz::rendering::sceneFromFirstRenderEngine();
  if (!scene || !scene->IsInitialized())
    return;

  // ---- highlight + selection (applied as soon as the visual appears) ----
  const bool needSelection = selectionPending_.load(std::memory_order_acquire);
  if (needHighlight || needSelection)
  {
    std::string entityName;
    {
      std::lock_guard<std::mutex> lock(renderMutex_);
      entityName = previewEntityName_;
    }

    if (!entityName.empty())
    {
      auto visual = scene->VisualByName(entityName);
      if (visual)
      {
        if (needHighlight)
        {
          const int mode = highlightMode_.load(std::memory_order_relaxed);
          applyHighlight(visual, mode);
          highlightPending_.store(false, std::memory_order_release);
          gzmsg << "[gz_model_importer_gui] Highlight mode " << mode
                << " applied to '" << entityName << "'.\n";
        }

        if (needSelection)
        {
          // visual->Id() is the rendering-assigned ID, NOT the gz-sim entity
          // ID. EntityTree / ComponentInspector need the gz-sim entity ID.
          // Fetch it from /world/<w>/scene/info in a worker thread to avoid
          // blocking the render thread.
          std::string worldName;
          {
            std::lock_guard<std::mutex> lock(renderMutex_);
            worldName = previewWorldName_;
          }
          selectionPending_.store(false, std::memory_order_release);

          QtConcurrent::run([worldName, entityName]()
          {
            gz::sim::Entity id = 0;
            if (!worldName.empty())
            {
              gz::transport::Node node;
              gz::msgs::Empty     req;
              gz::msgs::Scene     rep;
              bool result = false;
              node.Request("/world/" + worldName + "/scene/info",
                           req, 500u, rep, result);
              if (result)
              {
                for (int i = 0; i < rep.model_size(); ++i)
                {
                  if (rep.model(i).name() == entityName)
                  {
                    id = static_cast<gz::sim::Entity>(rep.model(i).id());
                    break;
                  }
                }
              }
            }

            if (id == 0)
            {
              gzwarn << "[gz_model_importer_gui] Could not resolve entity ID for '"
                     << entityName << "' — selection skipped.\n";
              return;
            }

            gzmsg << "[gz_model_importer_gui] Selecting entity " << id
                  << " ('" << entityName << "').\n";

            QMetaObject::invokeMethod(gz::gui::App(), [id]()
            {
              gz::sim::gui::events::DeselectAllEntities deselectEv(false);
              gz::sim::gui::events::EntitiesSelected    selectEv({id}, true);
              QCoreApplication::sendEvent(gz::gui::App(), &deselectEv);
              QCoreApplication::sendEvent(gz::gui::App(), &selectEv);
            }, Qt::QueuedConnection);
          });
        }
      }
      // else: visual not in scene yet — keep flags true, retry next frame
    }
    else
    {
      highlightPending_.store(false, std::memory_order_release);
      selectionPending_.store(false, std::memory_order_release);
    }
  }

  // ---- camera ----
  if (camState == 0)
    return;

  gz::rendering::CameraPtr cam;
  cam = std::dynamic_pointer_cast<gz::rendering::Camera>(
      scene->SensorByName("UserCamera"));
  if (!cam)
  {
    for (unsigned int i = 0; i < scene->SensorCount(); ++i)
    {
      cam = std::dynamic_pointer_cast<gz::rendering::Camera>(
          scene->SensorByIndex(i));
      if (cam) break;
    }
  }
  if (!cam)
    return;

  if (camState == 1)  // NeedCaptureAndFocus
  {
    savedCamPose_ = cam->WorldPose();

    gz::math::Vector3d target;
    {
      std::lock_guard<std::mutex> lock(renderMutex_);
      target = previewPos_;
    }
    const gz::math::Vector3d eye(target.X() - 5.0, target.Y(), target.Z() + 2.0);

    const gz::math::Vector3d dir = (target - eye).Normalized();
    const double yaw   = std::atan2(dir.Y(), dir.X());
    const double pitch = std::atan2(-dir.Z(),
                                    std::sqrt(dir.X() * dir.X() +
                                              dir.Y() * dir.Y()));
    cam->SetWorldPose(
        gz::math::Pose3d(eye, gz::math::Quaterniond(0.0, pitch, yaw)));

    gzmsg << "[gz_model_importer_gui] Camera saved and focused on preview ("
          << target.X() << ", " << target.Y() << ", " << target.Z() << ").\n";

    cameraState_.store(0, std::memory_order_release);
  }
  else if (camState == 2)  // NeedRestore
  {
    cam->SetWorldPose(savedCamPose_);
    gzmsg << "[gz_model_importer_gui] Camera pose restored.\n";
    cameraState_.store(0, std::memory_order_release);
  }
}

// ============================================================
// createPreviewBadge
// ============================================================
void RobotImporterGui::createPreviewBadge()
{
  auto *engine = gz::gui::App()->Engine();
  if (!engine || engine->rootObjects().isEmpty())
  {
    gzwarn << "[gz_model_importer_gui] createPreviewBadge: "
              "QML engine has no root objects yet — badge skipped.\n";
    return;
  }

  auto *rootWin = qobject_cast<QQuickWindow *>(engine->rootObjects().first());
  if (!rootWin)
  {
    gzwarn << "[gz_model_importer_gui] createPreviewBadge: "
              "root object is not a QQuickWindow — badge skipped.\n";
    return;
  }

  static const char kBadgeQml[] = R"QML(
import QtQuick 2.9
Rectangle {
    anchors.top:    parent.top
    anchors.left:   parent.left
    anchors.topMargin:  152
    anchors.leftMargin: 10
    implicitWidth:  lbl.implicitWidth + 20
    implicitHeight: 28
    radius: 4
    color: "#DE7B1A00"
    visible: (typeof previewCtrl !== "undefined") && previewCtrl.previewing
    enabled: false
    z: 1000
    Text {
        id: lbl
        anchors.centerIn: parent
        text: "MODEL PREVIEW (SIM PAUSED)"
        color: "white"
        font.bold: true
        font.pixelSize: 13
    }
}
)QML";

  QQmlComponent component(engine);
  component.setData(QByteArray(kBadgeQml), QUrl(QStringLiteral("qrc:/__preview_badge__")));
  if (component.isError())
  {
    gzwarn << "[gz_model_importer_gui] Preview badge QML error: "
           << component.errorString().toStdString() << "\n";
    return;
  }

  auto *item = qobject_cast<QQuickItem *>(
      component.create(engine->rootContext()));
  if (!item)
  {
    gzwarn << "[gz_model_importer_gui] Preview badge: failed to instantiate item.\n";
    return;
  }

  item->setParentItem(rootWin->contentItem());
  previewBadge_ = item;

  gzmsg << "[gz_model_importer_gui] Preview badge overlay installed in main window.\n";
}

}  // namespace gz_model_importer_gui

GZ_ADD_PLUGIN(gz_model_importer_gui::RobotImporterGui,
              gz::gui::Plugin)
