#ifndef ROBOT_IMPORTER_GUI_SDF_PREFLIGHT_CHECKER_HH_
#define ROBOT_IMPORTER_GUI_SDF_PREFLIGHT_CHECKER_HH_

#include <QStringList>

namespace robot_importer_gui
{

/// Results of a preflight scan of the post-rewrite SDF.
/// These are detected but NOT fixed — they are reported to the user.
struct PreflightFindings
{
  /// URIs still using model:// or package:// after rewriting (could not be resolved).
  QStringList unresolvedUris;

  /// <script><uri> values that end in ".material" — Ogre Classic material scripts.
  /// gz-sim (Ogre 2) cannot load these. Visuals referencing them will be invisible.
  QStringList ogreMaterialScripts;

  /// Number of <collision><geometry><mesh> elements found.
  /// Mesh collisions are unsupported by some physics backends and may cause errors.
  int meshCollisionCount{0};

  /// <plugin filename="..."> attribute values found in the original SDF.
  /// These are stripped for preview but logged so the user knows what was removed.
  QStringList pluginFilenames;
};

/// Analyzes a post-rewrite SDF string for known compatibility issues.
struct SdfPreflightChecker
{
  /// \param[in] _sdf   SDF XML string (after URI rewriting)
  /// \param[in] _unresolvedFromRewrite  Unresolved URIs already identified by
  ///            SdfUriRewriter — merged into PreflightFindings::unresolvedUris.
  static PreflightFindings analyze(const QString &_sdf,
                                   const QStringList &_unresolvedFromRewrite);
};

}  // namespace robot_importer_gui

#endif
