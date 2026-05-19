#ifndef GZ_MODEL_IMPORTER_SDF_URI_REWRITER_HH_
#define GZ_MODEL_IMPORTER_SDF_URI_REWRITER_HH_

#include <QString>
#include <QStringList>

namespace gz_model_importer
{

/// Results from a rewrite pass.
struct RewriteResult
{
  QString     sdf;              ///< Rewritten SDF (or original if parsing failed)
  int         totalUris{0};     ///< Total <uri> elements examined
  int         resolvedUris{0};  ///< URIs successfully rewritten to file://
  QStringList unresolvedUris;   ///< Original text of URIs that could not be resolved
};

/// Rewrites all <uri> elements in an SDF XML string to file:// absolute paths.
///
/// Resolution order for model:// and models:// URIs:
///   1. modelDir itself (URI names the same model that was loaded)
///   2. Sibling directories under modelsRoot (exact match, then case-insensitive)
///   3. Each directory listed in GZ_SIM_RESOURCE_PATH (colon-separated)
///   4. ament_index: get_package_share_directory(modelName)
///
/// Resolution for package:// URIs:
///   ament_index: get_package_share_directory(packageName) + rest
///
/// Other schemes:
///   file://   → unchanged (already absolute)
///   http(s):// → unchanged (Fuel URL)
///   relative  → anchored to modelDir
struct SdfUriRewriter
{
  /// \param[in] _sdf        SDF XML string to rewrite
  /// \param[in] _modelDir   Absolute path of the directory containing the
  ///                        selected model file
  /// \param[in] _modelsRoot Absolute path of the parent of _modelDir
  /// \return RewriteResult with rewritten SDF and resolution statistics
  static RewriteResult rewrite(const QString &_sdf,
                               const QString &_modelDir,
                               const QString &_modelsRoot);
};

}  // namespace gz_model_importer

#endif
