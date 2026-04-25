#include "robot_importer_gui/ImporterPlugin.hh"

#include <cmath>
#include <string>

#include <QCoreApplication>
#include <QEvent>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>

#include <gz/gui/Application.hh>
#include <gz/gui/GuiEvents.hh>
#include <gz/plugin/Register.hh>
#include <gz/sim/gui/GuiEvents.hh>
#include <gz/rendering/Camera.hh>
#include <gz/rendering/Material.hh>
#include <gz/rendering/RenderingIface.hh>
#include <gz/rendering/Scene.hh>
#include <gz/rendering/Visual.hh>
#include <gz/common/Console.hh>

#include "robot_importer_gui/ImporterBackend.hh"
#include "robot_importer_gui/ImportOptions.hh"
#include "robot_importer_gui/FileSelector.hh"
#include "robot_importer_gui/PreviewController.hh"

// Q_INIT_RESOURCE cannot be called inside a namespace (Qt restriction).
static void initRobotImporterResources()
{
  Q_INIT_RESOURCE(RobotImporterGui);
}

namespace robot_importer_gui
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

  qRegisterMetaType<robot_importer_gui::FileFormat>(
      "robot_importer_gui::FileFormat");

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
  }
  previewAlive_.store(true, std::memory_order_release);
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

  // Wireframe flag: always kept in sync, independent of materials.
  vis->SetWireframe(mode == 2);

  // Material manipulation is needed ONLY for Transparency (mode 1) and its
  // inverse reset (mode 0). Wireframe (mode 2) never touches materials.
  if (mode != 2)
  {
    const double alpha = (mode == 1) ? 0.6 : 0.0;

    // ---- Geometry-level materials (mesh visuals: DAE/STL/OBJ) ----
    const unsigned int geomCount = vis->GeometryCount();
    for (unsigned int g = 0; g < geomCount; ++g)
    {
      auto geom = vis->GeometryByIndex(g);
      if (!geom) continue;
      auto gMat = geom->Material();
      if (!gMat) continue;
      auto clone = gMat->Clone();
      clone->SetTransparency(alpha);
      geom->SetMaterial(clone, false);
    }

    // ---- Visual-level material fallback (SDF-explicit materials) ----
    if (geomCount == 0)
    {
      auto mat = vis->Material();
      if (mat)
      {
        auto clone = mat->Clone();
        clone->SetTransparency(alpha);
        vis->SetGeometryMaterial(clone, false);
      }
    }
  }

  // Recurse into child visuals.
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
          gzmsg << "[robot_importer_gui] Highlight mode " << mode
                << " applied to '" << entityName << "'.\n";
        }

        if (needSelection)
        {
          const gz::sim::Entity id =
              static_cast<gz::sim::Entity>(visual->Id());
          // sendEvent() must run on the main thread; marshal via invokeMethod.
          QMetaObject::invokeMethod(gz::gui::App(), [id]()
          {
            gz::sim::gui::events::DeselectAllEntities deselectEv(false);
            gz::sim::gui::events::EntitiesSelected    selectEv({id}, true);
            QCoreApplication::sendEvent(gz::gui::App(), &deselectEv);
            QCoreApplication::sendEvent(gz::gui::App(), &selectEv);
          }, Qt::QueuedConnection);
          selectionPending_.store(false, std::memory_order_release);
          gzmsg << "[robot_importer_gui] Selection queued for entity "
                << id << " ('" << entityName << "').\n";
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

    gzmsg << "[robot_importer_gui] Camera saved and focused on preview ("
          << target.X() << ", " << target.Y() << ", " << target.Z() << ").\n";

    cameraState_.store(0, std::memory_order_release);
  }
  else if (camState == 2)  // NeedRestore
  {
    cam->SetWorldPose(savedCamPose_);
    gzmsg << "[robot_importer_gui] Camera pose restored.\n";
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
    gzwarn << "[robot_importer_gui] createPreviewBadge: "
              "QML engine has no root objects yet — badge skipped.\n";
    return;
  }

  auto *rootWin = qobject_cast<QQuickWindow *>(engine->rootObjects().first());
  if (!rootWin)
  {
    gzwarn << "[robot_importer_gui] createPreviewBadge: "
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
    gzwarn << "[robot_importer_gui] Preview badge QML error: "
           << component.errorString().toStdString() << "\n";
    return;
  }

  auto *item = qobject_cast<QQuickItem *>(
      component.create(engine->rootContext()));
  if (!item)
  {
    gzwarn << "[robot_importer_gui] Preview badge: failed to instantiate item.\n";
    return;
  }

  item->setParentItem(rootWin->contentItem());
  previewBadge_ = item;

  gzmsg << "[robot_importer_gui] Preview badge overlay installed in main window.\n";
}

}  // namespace robot_importer_gui

GZ_ADD_PLUGIN(robot_importer_gui::RobotImporterGui,
              gz::gui::Plugin)
