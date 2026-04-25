#ifndef ROBOT_IMPORTER_GUI_IMPORTER_PLUGIN_HH_
#define ROBOT_IMPORTER_GUI_IMPORTER_PLUGIN_HH_

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

#include <QObject>

#include <gz/gui/Plugin.hh>
#include <gz/math/Pose3.hh>
#include <gz/math/Vector3.hh>

namespace robot_importer_gui
{

class ImporterBackend;

/// Gazebo GUI plugin entry point.
///
/// Naming contract (gz/gui/Plugin.hh):
///   class RobotImporterGui  →  libRobotImporterGui.so
///   QML resource at         :/RobotImporterGui/RobotImporterGui.qml
///   gui.config   → <plugin filename="RobotImporterGui" name="Robot Importer">
///
/// Camera lifecycle:
///   On previewSpawned  → saves camera world pose then focuses on preview entity.
///   On previewCancelled / confirmReady → restores the saved camera pose.
///   All camera operations run on the gz rendering thread via eventFilter().
///
/// Preview highlight:
///   highlightMode() controls how the preview entity is visually distinguished:
///     0 = None, 1 = Transparency (default), 2 = Wireframe.
///   The mode is applied via gz::rendering::Visual::SetTransparency /
///   SetWireframe on the render thread. This works for all visual types
///   including mesh-embedded materials (unlike SDF <material> alpha edits).
class RobotImporterGui : public gz::gui::Plugin
{
  Q_OBJECT

  /// 0=None (default)  1=Transparency  2=Wireframe
  Q_PROPERTY(int highlightMode READ highlightMode
             WRITE setHighlightMode NOTIFY highlightModeChanged)

  public: RobotImporterGui();
  public: ~RobotImporterGui() override;

  public: void LoadConfig(const tinyxml2::XMLElement *_pluginElem) override;

  /// Intercepts gz::gui::events::Render from the render thread to perform
  /// camera and preview-highlight operations.
  public: bool eventFilter(QObject *_obj, QEvent *_event) override;

  public: int  highlightMode() const;
  public: void setHighlightMode(int _mode);

  signals: void highlightModeChanged();

  private slots:
  void onPreviewSpawned(const QString &entityName);
  void onPreviewDone();

  private:
  void onRender();

  /// Inject a lightweight QML badge into the Gazebo main window's content item.
  void createPreviewBadge();

  std::unique_ptr<ImporterBackend> backend_;

  // Badge QML item overlaid on the main Gazebo window (nullptr until created).
  QObject *previewBadge_{nullptr};

  // ---------- camera state (main→render handoff) ----------
  // 0=Idle  1=NeedCaptureAndFocus  2=NeedRestore
  std::atomic<int> cameraState_{0};

  // Protected by renderMutex_: written main thread, read render thread.
  gz::math::Vector3d previewPos_;
  std::string        previewEntityName_;   // name of the live preview entity
  std::mutex renderMutex_;

  // Only ever accessed from the render thread — no mutex needed.
  gz::math::Pose3d savedCamPose_;

  // ---------- highlight state (main→render handoff) ----------
  // 0=None  1=Transparency  2=Wireframe
  std::atomic<int>  highlightMode_{0};
  std::atomic<bool> highlightPending_{false};
  std::atomic<bool> previewAlive_{false};

  // ---------- selection state (main→render handoff) ----------
  // Set to true on spawn; cleared after first successful selection.
  std::atomic<bool> selectionPending_{false};
};

}  // namespace robot_importer_gui

#endif
