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
#include "robot_importer_gui/XacroExpander.hh"
#include "robot_importer_gui/PreviewController.hh"
#include "robot_importer_gui/GzSpawnClient.hh"
#include "robot_importer_gui/InstanceRewriter.hh"
#include "robot_importer_gui/Ros2RuntimeAnalyzer.hh"
#include "robot_importer_gui/RuntimeProcessManager.hh"
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
  spawnClient_(std::make_unique<GzSpawnClient>(this)),
  processManager_(std::make_unique<RuntimeProcessManager>(this))
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

  connect(processManager_.get(), &RuntimeProcessManager::statusChanged,
          this, &ImporterBackend::runtimeRunningChanged);
  connect(processManager_.get(), &RuntimeProcessManager::outputChanged,
          this, &ImporterBackend::runtimeOutputChanged);

  poseDebounceTimer_ = new QTimer(this);
  poseDebounceTimer_->setSingleShot(true);
  poseDebounceTimer_->setInterval(600);
  connect(poseDebounceTimer_, &QTimer::timeout,
          this, &ImporterBackend::onPoseDebounceTimeout);

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
void ImporterBackend::reset()
{
  gzmsg << "[robot_importer_gui] reset() called from state: "
        << stateName().toStdString() << "\n";

  poseDebounceTimer_->stop();
  modelLoader_->cancel();

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

void ImporterBackend::setXacroArgs(const QStringList &_args)
{
  xacroArgs_ = _args;
}

void ImporterBackend::requestPreview()
{
  if (state_ != ImporterState::Ready        &&
      state_ != ImporterState::PreviewFailed &&
      state_ != ImporterState::SpawnFailed)
    return;

  if (!ensureWorldName()) return;

  spawnClient_->pauseWorldSync(worldName_);
  setState(ImporterState::Previewing);

  const std::string rewritten = applyOptionsToSdf();
  const EntitySpawnPose initialPose{
      importOptions_->poseX(), importOptions_->poseY(), importOptions_->poseZ(),
      importOptions_->poseRoll(), importOptions_->posePitch(), importOptions_->poseYaw()
  };

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

void ImporterBackend::cancelPreview()
{
  if (state_ != ImporterState::Previewing &&
      state_ != ImporterState::Configuring)
    return;
  if (!previewController_->isPreviewing()) return;

  gzmsg << "[robot_importer_gui] Cancelling preview.\n";
  previewController_->cancelPreview();
}

void ImporterBackend::importRobot()
{
  if (state_ != ImporterState::Ready        &&
      state_ != ImporterState::Configuring  &&
      state_ != ImporterState::PreviewFailed &&
      state_ != ImporterState::SpawnFailed)
    return;

  if (!ensureWorldName()) return;

  if (previewController_->isPreviewing())
  {
    gzmsg << "[robot_importer_gui] Confirm preview → removing preview entity.\n";
    setState(ImporterState::Spawning);
    previewController_->confirmPreview();
    return;
  }

  doFinalSpawn();
}

// ---- Collaborator slots ----------------------------------------------------

void ImporterBackend::onFileReady(const QString &path, FileFormat format)
{
  lastError_.clear();
  emit lastErrorChanged();

  modelLoader_->cancel();

  const bool previewLive   = previewController_->isPreviewing();
  const bool spawnInFlight = (state_ == ImporterState::Previewing);
  if (previewLive || spawnInFlight)
  {
    gzmsg << "[robot_importer_gui] Preview "
          << (spawnInFlight ? "in-flight" : "active")
          << " — deferring load of '" << path.toStdString() << "'\n";
    pendingFilePath_   = path;
    pendingFileFormat_ = format;
    if (previewLive)
      previewController_->cancelPreview();
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
  if (state_ != ImporterState::Expanding &&
      state_ != ImporterState::Converting)
  {
    gzwarn << "[robot_importer_gui] Stale loadComplete in state "
           << stateName().toStdString() << " — discarded.\n";
    return;
  }

  const RewriteResult rw = SdfUriRewriter::rewrite(sdfContent, modelDir_, modelsRoot_);
  currentSdf_ = rw.sdf;

  const PreflightFindings pf = SdfPreflightChecker::analyze(rw.sdf, rw.unresolvedUris);

  {
    QStringList lines;
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
      QString line = QStringLiteral("Ogre material scripts: %1 detected:")
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

  gzmsg << "[robot_importer_gui] SDF loaded (" << rw.sdf.size() << " chars).\n";
  if (!preflightReport_.isEmpty())
    gzmsg << "[robot_importer_gui] Preflight:\n" << preflightReport_.toStdString() << "\n";

  // Runtime analysis.
  {
    const QString originalPath = fileSelector_->selectedPath();
    const RuntimeFindings rf   = Ros2RuntimeAnalyzer::analyze(rw.sdf, originalPath);

    if (rf.needsRuntime)
    {
      runtimeSummary_ = rf.summary;

      // Detailed warning text (multi-line list for the panel).
      QStringList lines;
      lines.append(QStringLiteral(
          "This model may require external ROS 2 nodes to function correctly."));

      // Bridge requirements.
      int bridgeCount = 0;
      for (const SensorFindings &s : rf.sensors)
      {
        if (!s.needsBridge) continue;
        ++bridgeCount;
        QString desc = QStringLiteral("  • %1/%2 (%3) — %4")
            .arg(s.sensorType,
                 s.sensorName.isEmpty() ? QStringLiteral("?") : s.sensorName,
                 s.bridge.confidence,
                 s.bridge.gazeboTopic.isEmpty() ? QStringLiteral("topic unknown") : s.bridge.gazeboTopic);
        lines.append(desc);
      }
      if (bridgeCount > 0)
        lines.prepend(QStringLiteral("Native sensors needing ros_gz_bridge (%1):").arg(bridgeCount));

      if (!rf.pluginList.isEmpty())
      {
        lines.append(QStringLiteral("ROS plugins detected:"));
        for (const QString &pl : rf.pluginList)
          lines.append(QStringLiteral("  • ") + pl);
      }
      if (rf.hasXacroControlArgs)
        lines.append(QStringLiteral("XACRO control args: ") +
                     rf.xacroControlArgs.join(QLatin1String(", ")));

      runtimeWarning_ = lines.join('\n');
      hasBridgeRequirements_    = rf.hasBridgeRequirements;
      hasUnresolvedRuntimeItems_ = rf.hasUnresolvedRuntimeItems;

      bridgeCommand_          = LaunchGenerator::bridgeCommand(
          rf, importOptions_->rosNamespace());
      suggestedLaunchContent_ = LaunchGenerator::launchFileContent(
          rf, importOptions_->instanceName(), importOptions_->rosNamespace());
      suggestedLaunchCommand_ = LaunchGenerator::launchCommand(
          rf, importOptions_->instanceName(), importOptions_->rosNamespace());
      customLaunchCommand_    = suggestedLaunchCommand_;

      gzmsg << "[robot_importer_gui] Runtime: " << runtimeSummary_.toStdString() << "\n";
      if (!bridgeCommand_.isEmpty())
        gzmsg << "[robot_importer_gui] Bridge cmd: " << bridgeCommand_.toStdString() << "\n";
    }
    else
    {
      runtimeWarning_.clear();
      runtimeSummary_.clear();
      suggestedLaunchContent_.clear();
      suggestedLaunchCommand_.clear();
      bridgeCommand_.clear();
      customLaunchCommand_.clear();
      hasBridgeRequirements_     = false;
      hasUnresolvedRuntimeItems_ = false;
    }
    emit runtimeWarningChanged();
    emit customLaunchCommandChanged();
  }

  setState(ImporterState::Ready);
  requestPreview();
}

void ImporterBackend::onLoadFailed(const QString &error)
{
  if (state_ != ImporterState::Expanding &&
      state_ != ImporterState::Converting)
  {
    gzwarn << "[robot_importer_gui] Stale loadFailed — discarded.\n";
    return;
  }
  gzwarn << "[robot_importer_gui] Load failed: " << error.toStdString() << "\n";
  const bool wasXacro = (state_ == ImporterState::Expanding);
  setError(error);
  setState(wasXacro ? ImporterState::ExpansionFailed : ImporterState::ConversionFailed);
}

void ImporterBackend::onPreviewSpawned(const QString &name)
{
  if (state_ != ImporterState::Previewing)
  {
    gzwarn << "[robot_importer_gui] Stale preview spawn ack for '"
           << name.toStdString() << "'. Removing orphan.\n";
    previewController_->cancelPreview();
    return;
  }
  gzmsg << "[robot_importer_gui] Preview entity alive: " << name.toStdString() << "\n";
  setState(ImporterState::Configuring);
}

void ImporterBackend::onPreviewFailed(const QString &error)
{
  if (state_ != ImporterState::Previewing)
  {
    gzwarn << "[robot_importer_gui] Stale previewFailed — discarded.\n";
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
    const QString path   = pendingFilePath_;
    const FileFormat fmt = pendingFileFormat_;
    pendingFilePath_.clear();
    pendingFileFormat_ = FileFormat::Unknown;
    gzmsg << "[robot_importer_gui] Previous preview removed — loading '"
          << path.toStdString() << "'\n";
    startFileLoad(path, fmt);
    return;
  }
  if (state_ == ImporterState::Idle) return;
  gzmsg << "[robot_importer_gui] Preview cancelled — returning to Ready.\n";
  setState(ImporterState::Ready);
}

void ImporterBackend::onConfirmReady(const QString &, const QString &, const QString &)
{
  doFinalSpawn();
}

void ImporterBackend::onSpawnComplete(const QString &name)
{
  if (state_ != ImporterState::Spawning)
  {
    gzwarn << "[robot_importer_gui] Stale spawnComplete — discarded.\n";
    return;
  }
  gzmsg << "[robot_importer_gui] Spawn complete: " << name.toStdString() << "\n";
  lastError_.clear(); lastWarning_.clear(); preflightReport_.clear();
  emit lastErrorChanged(); emit lastWarningChanged(); emit preflightReportChanged();
  clearRuntimeState();
  setState(ImporterState::Done);
}

void ImporterBackend::onSpawnFailed(const QString &error)
{
  if (state_ != ImporterState::Spawning)
  {
    gzwarn << "[robot_importer_gui] Stale spawnFailed — discarded.\n";
    return;
  }
  gzerr << "[robot_importer_gui] Spawn failed: " << error.toStdString() << "\n";
  setError(error);
  setState(ImporterState::SpawnFailed);
}

// ---- Internal helpers ------------------------------------------------------

bool ImporterBackend::ensureWorldName()
{
  if (!worldName_.isEmpty()) return true;
  worldName_ = spawnClient_->discoverWorldName();
  if (worldName_.isEmpty())
  {
    setError(QStringLiteral(
        "Cannot determine active Gazebo world name. "
        "Is Gazebo running and the gz-transport network reachable?"));
    return false;
  }
  gzmsg << "[robot_importer_gui] World discovered: " << worldName_.toStdString() << "\n";
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
  if (!warnings.isEmpty()) setWarning(warnings);
  return rewritten;
}

void ImporterBackend::doFinalSpawn()
{
  setState(ImporterState::Spawning);
  spawnClient_->pauseWorldSync(worldName_);

  const std::string rewritten = applyOptionsToSdf();
  const EntitySpawnPose finalPose{
      importOptions_->poseX(), importOptions_->poseY(), importOptions_->poseZ(),
      importOptions_->poseRoll(), importOptions_->posePitch(), importOptions_->poseYaw()
  };

  gzmsg << "[robot_importer_gui] Final spawn: entity='"
        << importOptions_->instanceName().toStdString()
        << "' world='" << worldName_.toStdString() << "'\n";

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
  if (poseDebounceTimer_->isActive()) return;
  updatingFromGazebo_ = true;
  importOptions_->setPoseX(x); importOptions_->setPoseY(y); importOptions_->setPoseZ(z);
  importOptions_->setPoseRoll(roll); importOptions_->setPosePitch(pitch);
  importOptions_->setPoseYaw(yaw);
  updatingFromGazebo_ = false;
}

void ImporterBackend::startFileLoad(const QString &path, FileFormat format)
{
  const QFileInfo info(path);
  modelDir_   = info.absoluteDir().absolutePath();
  modelsRoot_ = QFileInfo(modelDir_).absoluteDir().absolutePath();

  lastWarning_.clear(); preflightReport_.clear();
  emit lastWarningChanged(); emit preflightReportChanged();

  clearRuntimeState();
  resetPose();
  assignUniqueName(path);

  gzmsg << "[robot_importer_gui] Loading: " << path.toStdString()
        << "  modelDir=" << modelDir_.toStdString() << "\n";

  setState(format == FileFormat::Xacro
           ? ImporterState::Expanding : ImporterState::Converting);

  QStringList effectiveArgs = xacroArgs_;
  if (format == FileFormat::Xacro)
  {
    const QMap<QString, QString> discovered = XacroExpander::discoverArgs(path);
    if (!discovered.isEmpty())
    {
      QSet<QString> userArgNames;
      for (const QString &a : xacroArgs_)
      {
        const int sep = a.indexOf(QStringLiteral(":="));
        if (sep > 0) userArgNames.insert(a.left(sep));
      }
      for (auto it = discovered.cbegin(); it != discovered.cend(); ++it)
        if (!userArgNames.contains(it.key()))
          effectiveArgs << (it.key() + QStringLiteral(":=") + it.value());

      gzmsg << "[robot_importer_gui] XACRO args discovered: " << discovered.size();
      for (const QString &a : effectiveArgs) gzmsg << " [" << a.toStdString() << "]";
      gzmsg << "\n";
    }
  }
  modelLoader_->load(path, format, effectiveArgs);
}

// static
QString ImporterBackend::extractModelBaseName(const QString &filePath)
{
  const QFileInfo info(filePath);
  const QString stem = info.completeBaseName().toLower();
  static const QStringList kGeneric = {"model", "robot", "description", "urdf"};
  const QString raw = kGeneric.contains(stem) ? info.dir().dirName().toLower() : stem;

  QString name;
  bool prevWasUs = true;
  for (const QChar c : raw)
  {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    if (ok) { name.append(c); prevWasUs = (c == '_'); }
    else if (!prevWasUs) { name.append('_'); prevWasUs = true; }
  }
  while (name.endsWith('_')) name.chop(1);
  return name.isEmpty() ? QStringLiteral("model") : name;
}

void ImporterBackend::resetPose()
{
  updatingFromGazebo_ = true;
  importOptions_->setPoseX(0.0); importOptions_->setPoseY(0.0); importOptions_->setPoseZ(0.0);
  importOptions_->setPoseRoll(0.0); importOptions_->setPosePitch(0.0); importOptions_->setPoseYaw(0.0);
  updatingFromGazebo_ = false;
}

void ImporterBackend::assignUniqueName(const QString &filePath)
{
  const QString base = extractModelBaseName(filePath);
  QSet<QString> taken;
  if (ensureWorldName()) taken = spawnClient_->queryModelNames(worldName_);

  int &sessionIdx = nameCounters_[base];
  if (sessionIdx == 0) sessionIdx = 1;

  QString candidate;
  do { candidate = base + "_" + QString::number(sessionIdx++); }
  while (taken.contains(candidate));

  importOptions_->setInstanceName(candidate);
  gzmsg << "[robot_importer_gui] Proposed instance name: '"
        << candidate.toStdString() << "'\n";
}

void ImporterBackend::onPoseDebounceTimeout()
{
  if (state_ != ImporterState::Configuring) return;
  if (!previewController_->isPreviewing()) return;

  spawnClient_->pauseWorldSync(worldName_);
  const EntitySpawnPose newPose{
      importOptions_->poseX(), importOptions_->poseY(), importOptions_->poseZ(),
      importOptions_->poseRoll(), importOptions_->posePitch(), importOptions_->poseYaw()
  };
  previewController_->respawnAt(newPose);
}

void ImporterBackend::clearRuntimeState()
{
  runtimeWarning_.clear();
  runtimeSummary_.clear();
  suggestedLaunchContent_.clear();
  suggestedLaunchCommand_.clear();
  bridgeCommand_.clear();
  customLaunchCommand_.clear();
  hasBridgeRequirements_     = false;
  hasUnresolvedRuntimeItems_ = false;
  emit runtimeWarningChanged();
  emit customLaunchCommandChanged();

  processManager_->reset();
}

// ---- Accessors -------------------------------------------------------------

QString ImporterBackend::runtimeWarning()          const { return runtimeWarning_; }
QString ImporterBackend::suggestedLaunchContent()  const { return suggestedLaunchContent_; }
QString ImporterBackend::suggestedLaunchCommand()  const { return suggestedLaunchCommand_; }
QString ImporterBackend::customLaunchCommand()     const { return customLaunchCommand_; }

bool    ImporterBackend::runtimeRequired()          const { return !runtimeSummary_.isEmpty(); }
QString ImporterBackend::runtimeSummary()           const { return runtimeSummary_; }
QString ImporterBackend::bridgeCommand()            const { return bridgeCommand_; }
bool    ImporterBackend::hasBridgeRequirements()    const { return hasBridgeRequirements_; }
bool    ImporterBackend::hasUnresolvedRuntimeItems() const { return hasUnresolvedRuntimeItems_; }

bool    ImporterBackend::runtimeRunning() const { return processManager_->isRunning(); }
QString ImporterBackend::runtimeStatus()  const { return processManager_->statusText(); }
QString ImporterBackend::runtimeOutput()  const { return processManager_->processOutput(); }

void ImporterBackend::setCustomLaunchCommand(const QString &cmd)
{
  if (customLaunchCommand_ == cmd) return;
  customLaunchCommand_ = cmd;
  emit customLaunchCommandChanged();
}

void ImporterBackend::copyLaunchCommand()
{
  const QString cmd = !bridgeCommand_.isEmpty() ? bridgeCommand_
                    : !customLaunchCommand_.isEmpty() ? customLaunchCommand_
                    : suggestedLaunchCommand_;
  if (!cmd.isEmpty()) QGuiApplication::clipboard()->setText(cmd);
}

bool ImporterBackend::saveLaunchFile(const QString &pathOrUrl)
{
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
  if (processManager_->isRunning()) return;

  const QString cmd = !customLaunchCommand_.isEmpty()
                    ? customLaunchCommand_ : suggestedLaunchCommand_;
  if (cmd.isEmpty()) return;

  if (!processManager_->run(cmd))
    setError(QStringLiteral("Failed to start: %1").arg(cmd));
}

void ImporterBackend::stopLaunchCommand()
{
  processManager_->stop();
}

}  // namespace robot_importer_gui
