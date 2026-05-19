#ifndef GZ_MODEL_IMPORTER_IMPORTER_STATE_HH_
#define GZ_MODEL_IMPORTER_IMPORTER_STATE_HH_

#include <QString>

namespace gz_model_importer
{

enum class ImporterState : int
{
  Idle = 0,          // waiting for a file
  FileSelected,      // file path known, format not yet inspected
  Expanding,         // XACRO subprocess running
  ExpansionFailed,   // XACRO returned non-zero or parse error
  Converting,        // sdf::readString / readFile in progress
  ConversionFailed,  // sdformat rejected the content
  Ready,             // SDF text available, ready for options / preview / spawn
  Previewing,        // temporary entity spawned in main scene
  PreviewFailed,     // spawn of __preview_* entity failed
  Configuring,       // user is filling in import options
  Spawning,          // final spawn in progress
  SpawnFailed,       // gz-transport /world/.../create returned error
  Done,              // robot in world, workflow complete
  Error,             // unrecoverable; lastError has details
};

inline QString importerStateName(ImporterState s)
{
  switch (s)
  {
    case ImporterState::Idle:             return QStringLiteral("Idle");
    case ImporterState::FileSelected:     return QStringLiteral("FileSelected");
    case ImporterState::Expanding:        return QStringLiteral("Expanding");
    case ImporterState::ExpansionFailed:  return QStringLiteral("ExpansionFailed");
    case ImporterState::Converting:       return QStringLiteral("Converting");
    case ImporterState::ConversionFailed: return QStringLiteral("ConversionFailed");
    case ImporterState::Ready:            return QStringLiteral("Ready");
    case ImporterState::Previewing:       return QStringLiteral("Previewing");
    case ImporterState::PreviewFailed:    return QStringLiteral("PreviewFailed");
    case ImporterState::Configuring:      return QStringLiteral("Configuring");
    case ImporterState::Spawning:         return QStringLiteral("Spawning");
    case ImporterState::SpawnFailed:      return QStringLiteral("SpawnFailed");
    case ImporterState::Done:             return QStringLiteral("Done");
    case ImporterState::Error:            return QStringLiteral("Error");
    default:                              return QStringLiteral("Unknown");
  }
}

}  // namespace gz_model_importer

#endif
