#include "robot_importer_gui/ImporterBackend.hh"

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>
#include <QRegExp>
#include <QSet>
#include <QTextStream>
#include <QUrl>
#include <gz/common/Console.hh>

#include "robot_importer_gui/ImportOptions.hh"
#include "robot_importer_gui/FileSelector.hh"
#include "robot_importer_gui/LaunchGenerator.hh"
#include "robot_importer_gui/ModelLoader.hh"
#include "robot_importer_gui/PreviewController.hh"
#include "robot_importer_gui/GzSpawnClient.hh"
#include "robot_importer_gui/InstanceRewriter.hh"
#include "robot_importer_gui/Ros2RuntimeAnalyzer.hh"
#include "robot_importer_gui/SdfUriRewriter.hh"
#include "robot_importer_gui/SdfPreflightChecker.hh"

namespace robot_importer_gui
{

ImporterBackend::ImporterBackend(QObject *_parent)
: QObject(_parent),
  fileSelector_(std::make_unique<FileSelector>(this)),
  importOptions_(std::make_unique<ImportOptions>(this)),
  modelLoader_(std::make_unique<ModelLoader>(this)),
  previewController_(std::make_unique<PreviewController>(this)),
  spawnClient_(std::make_unique<GzSpawnClient>(this))
{
  connect(fileSelector_.get(), &FileSelector::fileReady,
          this, &ImporterBackend::onFileReady);
  connect(fileSelector_.get(), &FileSelector::fileError,
          this, &ImporterBackend::onFileError);

  connect(modelLoader_.get(), &ModelLoader::loadComplete,
          this, &ImporterBackend::onLoadComplete);
  connect(modelLoader_.get(), &ModelLoader::loadFailed,
          this, &ImporterBackend::onLoadFailed);

  connect(previewController_.get(), &PreviewController::previewSpawned,
          this, &ImporterBackend::onPreviewSpawned);
  connect(previewController_.get(), &PreviewController::previewFailed,
          this, &ImporterBackend::onPreviewFailed);
  connect(previewController_.get(), &PreviewController::previewCancelled,
          this, &ImporterBackend::onPreviewCancelled);
  connect(previewController_.get(), &PreviewController::confirmReady,
          this, &ImporterBackend::onConfirmReady);
  connect(previewController_.get(), &PreviewController::removeWarning,
          this, [this](const QString &msg){ setWarning(msg); });
  connect(previewController_.get(), &PreviewController::previewPoseMoved,
          this, &ImporterBackend::onGazeboPoseMoved);

  connect(spawnClient_.get(), &GzSpawnClient::spawnComplete,
          this, &ImporterBackend::onSpawnComplete);
  connect(spawnClient_.get(), &GzSpawnClient::spawnFailed,
          this, &ImporterBackend::onSpawnFailed);

  // Pose debounce: user changes a pose field → 600ms quiet period → move preview.
  poseDebounceTimer_ = new QTimer(this);
  poseDebounceTimer_->setSingleShot(true);
  poseDebounceTimer_->setInterval(600);
  connect(poseDebounceTimer_, &QTimer::timeout,
          this, &ImporterBackend::onPoseDebounceTimeout);

  // Any ImportOptions pose property change fires the debounce (only active
  // when updatingFromGazebo_ is false and state is Configuring).
  auto scheduleMove = [this]()
  {
    if (updatingFromGazebo_) return;
    if (state_ != ImporterState::Configuring) return;
    poseDebounceTimer_->start();
  };
  connect(importOptions_.get(), &ImportOptions::poseXChanged,     scheduleMove);
  connect(importOptions_.get(), &ImportOptions::poseYChanged,     scheduleMove);
  connect(importOptions_.get(), &ImportOptions::poseZChanged,     scheduleMove);
  connect(importOptions_.get(), &ImportOptions::poseRollChanged,  scheduleMove);
  connect(importOptions_.get(), &ImportOptions::posePitchChanged, scheduleMove);
  connect(importOptions_.get(), &ImportOptions::poseYawChanged,   scheduleMove);
}

ImporterBackend::~ImporterBackend() = default;

int     ImporterBackend::stateInt()  const { return static_cast<int>(state_); }
QString ImporterBackend::stateName() const { return importerStateName(state_); }

// isBusy() gates UI controls. Valid action summary:
//
//   Action          | Allowed from states
//   ----------------+------------------------------------------------
//   requestPreview  | Ready, PreviewFailed, SpawnFailed
//   cancelPreview   | Previewing, Configuring
//   importRobot     | Ready, Configuring, PreviewFailed, SpawnFailed
//   reset           | any (always safe)
//   setXacroArgs    | any (applied at next load)
//
// SpawnFailed is included because currentSdf_ is still valid and the user
// may want to change the instance name and retry without re-loading the file.
//
// The QML layer disables interactive controls when isBusy() is true.
// The C++ guards in each method enforce the same contract defensively.
bool ImporterBackend::isBusy() const
{
  return state_ == ImporterState::Expanding  ||
         state_ == ImporterState::Converting ||
         state_ == ImporterState::Previewing ||
         state_ == ImporterState::Spawning;
}
QString ImporterBackend::lastError()       const { return lastError_; }
QString ImporterBackend::lastWarning()     const { return lastWarning_; }
QString ImporterBackend::worldName()       const { return worldName_; }
QString ImporterBackend::preflightReport() const { return preflightReport_; }

FileSelector      *ImporterBackend::fileSelector()      const { return fileSelector_.get(); }
ImportOptions     *ImporterBackend::importOptions()     const { return importOptions_.get(); }
PreviewController *ImporterBackend::previewController() const { return previewController_.get(); }

// ============================================================
// reset()
// ============================================================
void ImporterBackend::reset()
{
  gzmsg << "[robot_importer_gui] reset() called from state: "
        << stateName().toStdString() << "\n";

  poseDebounceTimer_->stop();
  modelLoader_->cancel();

  // Clear pending file before cancelling preview so onPreviewCancelled()
  // doesn't start loading the pending file as part of the reset flow.
  pendingFilePath_.clear();
  pendingFileFormat_ = FileFormat::Unknown;

  if (previewController_->isPreviewing())
    previewController_->cancelPreview();

  fileSelector_->reset();
  importOptions_->reset();

  currentSdf_.clear();
  lastError_.clear();
  lastWarning_.clear();
  preflightReport_.clear();
  xacroArgs_.clear();
  modelDir_.clear();
  modelsRoot_.clear();

  emit lastErrorChanged();
  emit lastWarningChanged();
  emit preflightReportChanged();

  clearRuntimeState();
  setState(ImporterState::Idle);
}

// ============================================================
// setXacroArgs()
// ============================================================
void ImporterBackend::setXacroArgs(const QStringList &_args)
{
  xacroArgs_ = _args;
}

// ============================================================
// requestPreview()
// ============================================================
void ImporterBackend::requestPreview()
{
  // Configuring: preview already live — cancel first.
  // Previewing: spawn in-flight — wait for it.
  // SpawnFailed: currentSdf_ is valid, allow retry with updated options.
  if (state_ != ImporterState::Ready        &&
      state_ != ImporterState::PreviewFailed &&
      state_ != ImporterState::SpawnFailed)
    return;

  if (!ensureWorldName())
    return;

  // Operational requirement: pause world before any spawn to prevent
  // DART/ODE trimesh crash during physics step. World stays paused for the
  // entire preview+import cycle; user resumes manually after.
  spawnClient_->pauseWorldSync(worldName_);

  setState(ImporterState::Previewing);

  // Apply instance name / namespace to SDF before spawning.
  const std::string rewritten = applyOptionsToSdf();

  const EntitySpawnPose initialPose{
      importOptions_->poseX(),
      importOptions_->poseY(),
      importOptions_->poseZ(),
      importOptions_->poseRoll(),
      importOptions_->posePitch(),
      importOptions_->poseYaw()
  };

  // Preview SDF transformations applied by preparePreviewSdf():
  //   - <plugin> stripped (not needed, avoids spurious ROS connections)
  //   - <static>true</static> injected (no dynamic physics, stays in place)
  //   - <collision> kept (required for auto-inertia computation)
  //   - <visual>, <inertial> unchanged
  gzmsg << "[robot_importer_gui] Spawning preview '__preview_"
        << importOptions_->instanceName().toStdString()
        << "' world='" << worldName_.toStdString()
        << "' pose=(" << initialPose.x << "," << initialPose.y
        << "," << initialPose.z << ")\n";

  previewController_->spawnPreview(
      worldName_,
      QString::fromStdString(rewritten),
      importOptions_->instanceName(),
      initialPose);
}

// ============================================================
// cancelPreview()
// ============================================================
void ImporterBackend::cancelPreview()
{
  // Valid only while preview is live (Previewing = spawn in-flight,
  // Configuring = preview entity exists in scene).
  if (state_ != ImporterState::Previewing &&
      state_ != ImporterState::Configuring)
    return;
  if (!previewController_->isPreviewing())
    return;

  gzmsg << "[robot_importer_gui] Cancelling preview.\n";
  previewController_->cancelPreview();
  // State is restored in onPreviewCancelled().
}

// ============================================================
// importRobot() — final spawn
// ============================================================
void ImporterBackend::importRobot()
{
  // Valid when SDF is ready (Ready/PreviewFailed/SpawnFailed) or preview is
  // live (Configuring). NOT allowed during in-flight ops (Expanding,
  // Converting, Previewing, Spawning).
  if (state_ != ImporterState::Ready        &&
      state_ != ImporterState::Configuring  &&
      state_ != ImporterState::PreviewFailed &&
      state_ != ImporterState::SpawnFailed)
    return;

  if (!ensureWorldName())
    return;

  if (previewController_->isPreviewing())
  {
    // Remove the preview entity first; confirmReady() will trigger
    // the final spawn via onConfirmReady().
    gzmsg << "[robot_importer_gui] Confirm preview → removing preview entity.\n";
    setState(ImporterState::Spawning);
    previewController_->confirmPreview();
    return;
  }

  // No preview active: spawn directly.
  doFinalSpawn();
}

// ============================================================
// Collaborator slots
// ============================================================
void ImporterBackend::onFileReady(const QString &path, FileFormat format)
{
  lastError_.clear();
  emit lastErrorChanged();

  // Cancel any in-flight load (e.g. previous XACRO expansion).
  modelLoader_->cancel();

  // Defer new file load if a preview entity is alive OR a spawn is in-flight
  // (state == Previewing but spawn ack not yet received).
  // - isPreviewing() == true:  entity is in the scene → cancel + wait for remove
  // - state == Previewing:     spawn still in-flight → store pending; the stale
  //   ack handler in onPreviewSpawned() will cancel the orphan once it arrives
  const bool previewLive     = previewController_->isPreviewing();
  const bool spawnInFlight   = (state_ == ImporterState::Previewing);
  if (previewLive || spawnInFlight)
  {
    gzmsg << "[robot_importer_gui] Preview "
          << (spawnInFlight ? "in-flight" : "active")
          << " — deferring load of '" << path.toStdString()
          << "' until preview is removed.\n";
    pendingFilePath_   = path;
    pendingFileFormat_ = format;
    if (previewLive)
      previewController_->cancelPreview();
    // If only in-flight: onPreviewSpawned() stale-ack handler cancels it.
    return;
  }

  startFileLoad(path, format);
}

void ImporterBackend::onFileError(const QString &msg)
{
  gzwarn << "[robot_importer_gui] File error: " << msg.toStdString() << "\n";
  setError(msg);
}

void ImporterBackend::onLoadComplete(const QString &sdfContent)
{
  // Guard: reset() may have been called while load was in-flight.
  if (state_ != ImporterState::Expanding &&
      state_ != ImporterState::Converting)
  {
    gzwarn << "[robot_importer_gui] Stale loadComplete in state "
           << stateName().toStdString() << " — discarded.\n";
    return;
  }

  // Step 1: rewrite model:// / package:// / relative URIs to file://.
  const RewriteResult rw = SdfUriRewriter::rewrite(
      sdfContent, modelDir_, modelsRoot_);
  currentSdf_ = rw.sdf;

  // Step 2: preflight — scan for known issues (no fixes, only reporting).
  const PreflightFindings pf = SdfPreflightChecker::analyze(rw.sdf, rw.unresolvedUris);

  // Build a human-readable preflight report for the panel.
  {
    QStringList lines;

    // URI resolution summary.
    if (rw.totalUris > 0 || !rw.unresolvedUris.isEmpty())
    {
      QString uriLine = QStringLiteral("URIs: %1/%2 resolved")
          .arg(rw.resolvedUris).arg(rw.totalUris);
      if (!pf.unresolvedUris.isEmpty())
      {
        uriLine += QStringLiteral(" — %1 unresolved:").arg(pf.unresolvedUris.size());
        for (const QString &u : pf.unresolvedUris)
          uriLine += QStringLiteral("\n  • ") + u;
      }
      lines.append(uriLine);
    }

    if (!pf.ogreMaterialScripts.isEmpty())
    {
      QString line = QStringLiteral("Ogre material scripts: %1 detected (not supported in gz-sim):")
          .arg(pf.ogreMaterialScripts.size());
      for (const QString &s : pf.ogreMaterialScripts)
        line += QStringLiteral("\n  • ") + s;
      lines.append(line);
    }

    if (pf.meshCollisionCount > 0)
      lines.append(QStringLiteral("Mesh collisions: %1 detected (may cause physics warnings)")
          .arg(pf.meshCollisionCount));

    if (!pf.pluginFilenames.isEmpty())
    {
      QString line = QStringLiteral("Plugins stripped for preview (%1):")
          .arg(pf.pluginFilenames.size());
      for (const QString &fn : pf.pluginFilenames)
        line += QStringLiteral("\n  • ") + fn;
      lines.append(line);
    }

    preflightReport_ = lines.join(QStringLiteral("\n"));
    emit preflightReportChanged();
  }

  gzmsg << "[robot_importer_gui] SDF loaded ("
        << rw.sdf.size() << " chars). Step 3: auto-preview.\n";
  if (!preflightReport_.isEmpty())
    gzmsg << "[robot_importer_gui] Preflight report:\n"
          << preflightReport_.toStdString() << "\n";

  // Step 3: ROS 2 runtime analysis — non-blocking advisory.
  {
    const QString originalPath = fileSelector_->selectedPath();
    const RuntimeFindings rf   = Ros2RuntimeAnalyzer::analyze(rw.sdf, originalPath);

    if (rf.needsRuntime)
    {
      // Build multi-line summary for the UI card.
      QStringList lines;
      lines.append(QStringLiteral(
          "This model may require external ROS 2 nodes/controllers."));

      if (!rf.pluginList.isEmpty())
      {
        lines.append(QStringLiteral("Detected:"));
        for (const QString &pl : rf.pluginList)
          lines.append(QStringLiteral("  • ") + pl);
      }

      if (rf.hasXacroControlArgs)
      {
        lines.append(QStringLiteral("XACRO control args: ") +
                     rf.xacroControlArgs.join(QLatin1String(", ")));
      }

      runtimeWarning_ = lines.join('\n');
      suggestedLaunchContent_ = LaunchGenerator::launchFileContent(
          rf,
          importOptions_->instanceName(),
          importOptions_->rosNamespace());
      suggestedLaunchCommand_ = LaunchGenerator::launchCommand(
          rf,
          importOptions_->instanceName(),
          importOptions_->rosNamespace());
      customLaunchCommand_    = suggestedLaunchCommand_;

      gzmsg << "[robot_importer_gui] Runtime analysis: "
            << runtimeWarning_.toStdString() << "\n";
    }
    else
    {
      runtimeWarning_.clear();
      suggestedLaunchContent_.clear();
      suggestedLaunchCommand_.clear();
      customLaunchCommand_.clear();
    }
    emit runtimeWarningChanged();
    emit customLaunchCommandChanged();
  }

  setState(ImporterState::Ready);
  requestPreview();
}

void ImporterBackend::onLoadFailed(const QString &error)
{
  // Guard: reset() may have been called while load was in-flight.
  if (state_ != ImporterState::Expanding &&
      state_ != ImporterState::Converting)
  {
    gzwarn << "[robot_importer_gui] Stale loadFailed in state "
           << stateName().toStdString() << " — discarded.\n";
    return;
  }

  gzwarn << "[robot_importer_gui] Load failed: " << error.toStdString() << "\n";
  const bool wasXacro = (state_ == ImporterState::Expanding);
  setError(error);
  setState(wasXacro ? ImporterState::ExpansionFailed
                    : ImporterState::ConversionFailed);
}

void ImporterBackend::onPreviewSpawned(const QString &name)
{
  // Guard: state changed while spawn was in-flight (e.g. a new file was
  // selected during the service call). The spawn ack arrived late; the entity
  // IS in Gazebo's scene but we no longer want it. Cancel it immediately so it
  // doesn't linger, then let onPreviewCancelled() dispatch the pending file.
  if (state_ != ImporterState::Previewing)
  {
    gzwarn << "[robot_importer_gui] Stale preview spawn ack for '"
           << name.toStdString() << "' (state=" << stateName().toStdString()
           << "). Removing orphaned entity.\n";
    previewController_->cancelPreview();
    return;
  }

  gzmsg << "[robot_importer_gui] Preview entity alive: "
        << name.toStdString() << "\n";
  // Preview entity is alive in the scene. Move to Configuring so the
  // options panel is shown and import/cancel buttons appear.
  setState(ImporterState::Configuring);
}

void ImporterBackend::onPreviewFailed(const QString &error)
{
  // Guard: discard if we're no longer in Previewing (e.g. reset was called).
  if (state_ != ImporterState::Previewing)
  {
    gzwarn << "[robot_importer_gui] Stale previewFailed in state "
           << stateName().toStdString() << " — discarded.\n";
    return;
  }

  gzwarn << "[robot_importer_gui] Preview failed: " << error.toStdString() << "\n";
  setError(error);
  setState(ImporterState::PreviewFailed);
}

void ImporterBackend::onPreviewCancelled()
{
  if (!pendingFilePath_.isEmpty())
  {
    // A new file was selected while the preview was active.
    // Now that the preview entity is gone, start loading the new file.
    const QString path     = pendingFilePath_;
    const FileFormat fmt   = pendingFileFormat_;
    pendingFilePath_.clear();
    pendingFileFormat_ = FileFormat::Unknown;
    gzmsg << "[robot_importer_gui] Previous preview removed — loading '"
          << path.toStdString() << "'\n";
    startFileLoad(path, fmt);
    return;
  }

  // Normal cancel (user clicked Cancel or explicitly cancelled preview).
  // Guard: if reset() was called while the preview removal was in-flight
  // (e.g. Cancel pressed during Previewing state), the state is already
  // Idle and we must not transition back to Ready with no file loaded.
  if (state_ == ImporterState::Idle)
    return;

  gzmsg << "[robot_importer_gui] Preview cancelled — returning to Ready.\n";
  setState(ImporterState::Ready);
}

void ImporterBackend::onConfirmReady(const QString & /*worldName*/,
                                     const QString & /*sdfContent*/,
                                     const QString & /*instanceName*/)
{
  // The preview entity has been removed by PreviewController.
  // The signal carries worldName, sdfContent, instanceName for context, but
  // doFinalSpawn() re-derives everything from this object's own state
  // (currentSdf_ + importOptions_) to ensure a single authoritative source.
  // The preview SDF was stripped of plugins; the final spawn uses the full SDF.
  doFinalSpawn();
}

void ImporterBackend::onSpawnComplete(const QString &name)
{
  // Guard: discard stale acks (e.g. if reset() races a long service call).
  if (state_ != ImporterState::Spawning)
  {
    gzwarn << "[robot_importer_gui] Stale spawnComplete for '"
           << name.toStdString() << "' in state "
           << stateName().toStdString() << " — discarded.\n";
    return;
  }

  gzmsg << "[robot_importer_gui] Spawn complete: " << name.toStdString() << "\n";
  // Clear all transient diagnostics so the Done state shows a clean slate.
  // lastError_ may contain a stale message from a previous PreviewFailed or
  // world-not-found attempt that survived through the import flow.
  lastError_.clear();
  lastWarning_.clear();
  preflightReport_.clear();
  emit lastErrorChanged();
  emit lastWarningChanged();
  emit preflightReportChanged();
  setState(ImporterState::Done);
}

void ImporterBackend::onSpawnFailed(const QString &error)
{
  // Guard: discard stale acks.
  if (state_ != ImporterState::Spawning)
  {
    gzwarn << "[robot_importer_gui] Stale spawnFailed in state "
           << stateName().toStdString() << " — discarded.\n";
    return;
  }

  gzerr << "[robot_importer_gui] Spawn failed: " << error.toStdString() << "\n";
  setError(error);
  setState(ImporterState::SpawnFailed);
}

// ============================================================
// Internal helpers
// ============================================================
bool ImporterBackend::ensureWorldName()
{
  if (!worldName_.isEmpty())
    return true;

  worldName_ = spawnClient_->discoverWorldName();
  if (worldName_.isEmpty())
  {
    setError(QStringLiteral(
        "Cannot determine active Gazebo world name. "
        "Is Gazebo running and the gz-transport network reachable?"));
    return false;
  }
  gzmsg << "[robot_importer_gui] World discovered: "
        << worldName_.toStdString() << "\n";
  emit worldNameChanged();
  return true;
}

std::string ImporterBackend::applyOptionsToSdf()
{
  QString warnings;
  InstanceRewriter::Options opts{
      importOptions_->instanceName().toStdString(),
      importOptions_->rosNamespace().toStdString(),
      importOptions_->framePrefix().toStdString()};
  const std::string rewritten = InstanceRewriter::rewrite(
      currentSdf_.toStdString(), opts, warnings);
  if (!warnings.isEmpty())
    setWarning(warnings);
  return rewritten;
}

void ImporterBackend::doFinalSpawn()
{
  setState(ImporterState::Spawning);

  // Enforce pause before final spawn. The world should already be paused
  // from the preview phase, but re-asserting is cheap and prevents any race
  // if the user resumed manually between cancel-preview and final spawn.
  spawnClient_->pauseWorldSync(worldName_);

  const std::string rewritten = applyOptionsToSdf();

  // Use the pose currently shown in the panel — which is exactly the pose
  // the user positioned the preview at (or typed in the panel).
  const EntitySpawnPose finalPose{
      importOptions_->poseX(),
      importOptions_->poseY(),
      importOptions_->poseZ(),
      importOptions_->poseRoll(),
      importOptions_->posePitch(),
      importOptions_->poseYaw()
  };

  // Final spawn uses the FULL SDF: plugins intact, collisions intact.
  // No stripping is applied — this is the production model with all physics.
  // Any engine warnings after this point (auto-inertia, dartsim mesh) come
  // from the model or the physics engine, not from the importer.
  gzmsg << "[robot_importer_gui] Final spawn: entity='"
        << importOptions_->instanceName().toStdString()
        << "' world='" << worldName_.toStdString()
        << "' pose=(" << finalPose.x << "," << finalPose.y
        << "," << finalPose.z
        << " R=" << finalPose.roll << " P=" << finalPose.pitch
        << " Y=" << finalPose.yaw << ")\n";

  spawnClient_->spawnEntity(
      worldName_,
      QString::fromStdString(rewritten),
      importOptions_->instanceName(),
      finalPose);
}

void ImporterBackend::setState(ImporterState _s)
{
  if (_s == state_) return;
  state_ = _s;
  emit stateChanged();
}

void ImporterBackend::setError(const QString &_msg)
{
  lastError_ = _msg;
  emit lastErrorChanged();
}

void ImporterBackend::setWarning(const QString &_msg)
{
  lastWarning_ = _msg;
  emit lastWarningChanged();
}

void ImporterBackend::onGazeboPoseMoved(double x, double y, double z,
                                         double roll, double pitch, double yaw)
{
  // Gazebo publishes pose_info at ~60 Hz. If the debounce timer is active the
  // user has committed a new pose value that has not yet been sent to Gazebo.
  // Overwriting importOptions_ here would replace the user's value with the
  // stale Gazebo position, causing the debounce to send (0,0,0) instead of the
  // user's intended pose.
  if (poseDebounceTimer_->isActive()) return;

  updatingFromGazebo_ = true;
  importOptions_->setPoseX(x);
  importOptions_->setPoseY(y);
  importOptions_->setPoseZ(z);
  importOptions_->setPoseRoll(roll);
  importOptions_->setPosePitch(pitch);
  importOptions_->setPoseYaw(yaw);
  updatingFromGazebo_ = false;
}

// ============================================================
// startFileLoad — shared entry point after cancel or direct selection
// ============================================================
void ImporterBackend::startFileLoad(const QString &path, FileFormat format)
{
  const QFileInfo info(path);
  modelDir_   = info.absoluteDir().absolutePath();
  modelsRoot_ = QFileInfo(modelDir_).absoluteDir().absolutePath();

  // Discard diagnostics from any previous load or import attempt so the
  // log section does not show stale content while the new file is loading.
  // lastError_ was already cleared by onFileReady(); clear the rest here.
  lastWarning_.clear();
  preflightReport_.clear();
  emit lastWarningChanged();
  emit preflightReportChanged();

  clearRuntimeState();

  resetPose();
  assignUniqueName(path);

  gzmsg << "[robot_importer_gui] Loading: " << path.toStdString()
        << "  modelDir=" << modelDir_.toStdString() << "\n";

  setState(format == FileFormat::Xacro
           ? ImporterState::Expanding
           : ImporterState::Converting);

  modelLoader_->load(path, format, xacroArgs_);
}

// static
QString ImporterBackend::extractModelBaseName(const QString &filePath)
{
  const QFileInfo info(filePath);
  const QString stem = info.completeBaseName().toLower();

  // When the file is named generically, use the parent directory name instead.
  static const QStringList kGeneric = {"model", "robot", "description", "urdf"};
  const QString raw = kGeneric.contains(stem)
      ? info.dir().dirName().toLower()
      : stem;

  // Keep only ASCII alphanumeric and underscore; collapse runs of other chars.
  QString name;
  bool prevWasUs = true;  // suppress leading underscore
  for (const QChar c : raw)
  {
    const bool ok = (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9') ||
                    c == '_';
    if (ok) { name.append(c); prevWasUs = (c == '_'); }
    else if (!prevWasUs) { name.append('_'); prevWasUs = true; }
  }
  while (name.endsWith('_')) name.chop(1);
  return name.isEmpty() ? QStringLiteral("model") : name;
}

void ImporterBackend::resetPose()
{
  updatingFromGazebo_ = true;  // suppress debounce during programmatic reset
  importOptions_->setPoseX(0.0);
  importOptions_->setPoseY(0.0);
  importOptions_->setPoseZ(0.0);
  importOptions_->setPoseRoll(0.0);
  importOptions_->setPosePitch(0.0);
  importOptions_->setPoseYaw(0.0);
  updatingFromGazebo_ = false;
}

void ImporterBackend::assignUniqueName(const QString &filePath)
{
  const QString base = extractModelBaseName(filePath);

  // Query existing model names from the world (best-effort; empty on failure).
  QSet<QString> taken;
  if (ensureWorldName())
    taken = spawnClient_->queryModelNames(worldName_);

  // Find the lowest integer suffix N such that "<base>_N" is not taken.
  // Also avoid names already proposed this session (nameCounters_) to reduce
  // the chance of collision if the world state cannot be queried.
  int &sessionIdx = nameCounters_[base];
  if (sessionIdx == 0) sessionIdx = 1;

  QString candidate;
  do {
    candidate = base + "_" + QString::number(sessionIdx++);
  } while (taken.contains(candidate));

  importOptions_->setInstanceName(candidate);
  gzmsg << "[robot_importer_gui] Proposed instance name: '"
        << candidate.toStdString() << "'\n";
}

void ImporterBackend::onPoseDebounceTimeout()
{
  if (state_ != ImporterState::Configuring) return;
  if (!previewController_->isPreviewing())  return;

  // Ensure world is paused before remove+respawn. Should already be paused
  // from the initial requestPreview(), but re-assert if the user resumed.
  spawnClient_->pauseWorldSync(worldName_);

  const EntitySpawnPose newPose{
      importOptions_->poseX(),
      importOptions_->poseY(),
      importOptions_->poseZ(),
      importOptions_->poseRoll(),
      importOptions_->posePitch(),
      importOptions_->poseYaw()
  };

  gzmsg << "[robot_importer_gui] Moving preview to pos=("
        << newPose.x << "," << newPose.y << "," << newPose.z
        << ") rpy=(" << newPose.roll << "," << newPose.pitch
        << "," << newPose.yaw << ")\n";

  previewController_->respawnAt(newPose);
}

// ============================================================
// clearRuntimeState — shared teardown for reset() and startFileLoad()
// ============================================================
void ImporterBackend::clearRuntimeState()
{
  runtimeWarning_.clear();
  suggestedLaunchContent_.clear();
  suggestedLaunchCommand_.clear();
  customLaunchCommand_.clear();
  emit runtimeWarningChanged();
  emit customLaunchCommandChanged();

  // Stop any managed launch process.
  if (launchProcess_)
  {
    launchProcess_->terminate();
    launchProcess_->waitForFinished(500);
    launchProcess_->deleteLater();
    launchProcess_ = nullptr;
    launchRunning_ = false;
    emit launchRunningChanged();
  }
}

// ============================================================
// Runtime analysis accessors
// ============================================================
QString ImporterBackend::runtimeWarning()         const { return runtimeWarning_; }
QString ImporterBackend::suggestedLaunchContent() const { return suggestedLaunchContent_; }
QString ImporterBackend::suggestedLaunchCommand() const { return suggestedLaunchCommand_; }
QString ImporterBackend::customLaunchCommand()    const { return customLaunchCommand_; }
bool    ImporterBackend::launchRunning()          const { return launchRunning_; }

void ImporterBackend::setCustomLaunchCommand(const QString &cmd)
{
  if (customLaunchCommand_ == cmd) return;
  customLaunchCommand_ = cmd;
  emit customLaunchCommandChanged();
}

void ImporterBackend::copyLaunchCommand()
{
  const QString cmd = suggestedLaunchCommand_.isEmpty()
                    ? customLaunchCommand_
                    : suggestedLaunchCommand_;
  if (!cmd.isEmpty())
    QGuiApplication::clipboard()->setText(cmd);
}

bool ImporterBackend::saveLaunchFile(const QString &pathOrUrl)
{
  // FileDialog gives us a file:// URL; strip the scheme.
  QString path = pathOrUrl;
  if (path.startsWith(QLatin1String("file://")))
    path = QUrl(path).toLocalFile();

  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
  {
    setError(QStringLiteral("Cannot write launch file: %1").arg(path));
    return false;
  }
  QTextStream out(&f);
  out << suggestedLaunchContent_;
  gzmsg << "[robot_importer_gui] Launch file saved: " << path.toStdString() << "\n";
  return true;
}

void ImporterBackend::runLaunchCommand()
{
  if (launchRunning_) return;

  const QString cmd = customLaunchCommand_.isEmpty()
                    ? suggestedLaunchCommand_
                    : customLaunchCommand_;
  if (cmd.isEmpty()) return;

  const QStringList parts = cmd.split(' ', Qt::SkipEmptyParts);
  if (parts.isEmpty()) return;

  delete launchProcess_;
  launchProcess_ = new QProcess(this);

  connect(launchProcess_,
          QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
          this, [this](int /*code*/, QProcess::ExitStatus /*status*/)
  {
    launchRunning_ = false;
    emit launchRunningChanged();
  });

  launchProcess_->start(parts.first(), parts.mid(1));
  if (launchProcess_->waitForStarted(2000))
  {
    gzmsg << "[robot_importer_gui] Launch command started: "
          << cmd.toStdString() << "\n";
    launchRunning_ = true;
    emit launchRunningChanged();
  }
  else
  {
    gzwarn << "[robot_importer_gui] Failed to start: " << cmd.toStdString() << "\n";
    setError(QStringLiteral("Failed to start: %1").arg(cmd));
    delete launchProcess_;
    launchProcess_ = nullptr;
  }
}

void ImporterBackend::stopLaunchCommand()
{
  if (!launchProcess_ || !launchRunning_) return;
  launchProcess_->terminate();
  // Give it a moment; kill if still alive.
  if (!launchProcess_->waitForFinished(3000))
    launchProcess_->kill();
  gzmsg << "[robot_importer_gui] Launch command stopped.\n";
}

}  // namespace robot_importer_gui
