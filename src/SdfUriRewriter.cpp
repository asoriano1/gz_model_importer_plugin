#include "gz_model_importer/SdfUriRewriter.hh"

#include <cstdlib>

#include <QDir>
#include <QFileInfo>

#include <ament_index_cpp/get_package_share_directory.hpp>
#include <gz/common/Console.hh>
#include <tinyxml2.h>

namespace gz_model_importer
{

namespace
{

// ============================================================
// resolveModelDir — find the on-disk directory for a model name.
//
// Resolution order (explicit and auditable):
//   1. modelDir itself — URI names the same model that was loaded.
//   2. Siblings under modelsRoot — exact match, then case-insensitive scan.
//   3. Each directory in GZ_SIM_RESOURCE_PATH — e.g. /opt/ros/jazzy/share.
//   4. ament_index — get_package_share_directory(modelName).
//      Covers workspace-installed packages like robotnik_description.
//
// Returns the absolute model directory path, or empty string if not found.
// ============================================================
QString resolveModelDir(const QString &modelName,
                        const QString &modelDir,
                        const QString &modelsRoot)
{
  // 1. Self: the loaded file's own directory.
  if (QFileInfo(modelDir).fileName().compare(modelName, Qt::CaseInsensitive) == 0)
    return modelDir;

  // 2a. Sibling exact match.
  {
    const QString candidate = modelsRoot + QStringLiteral("/") + modelName;
    if (QFileInfo(candidate).isDir())
      return candidate;
  }

  // 2b. Sibling case-insensitive scan.
  {
    const QStringList entries =
        QDir(modelsRoot).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &e : entries)
    {
      if (e.compare(modelName, Qt::CaseInsensitive) == 0)
        return modelsRoot + QStringLiteral("/") + e;
    }
  }

  // 3. GZ_SIM_RESOURCE_PATH — colon-separated list of model root directories.
  {
    const char *gzResPath = std::getenv("GZ_SIM_RESOURCE_PATH");
    if (gzResPath && *gzResPath)
    {
      const QStringList dirs = QString::fromUtf8(gzResPath).split(
          QLatin1Char(':'), Qt::SkipEmptyParts);
      for (const QString &dir : dirs)
      {
        // Exact match first.
        {
          const QString candidate = dir + QStringLiteral("/") + modelName;
          if (QFileInfo(candidate).isDir())
            return candidate;
        }
        // Case-insensitive scan of this resource dir.
        const QStringList entries =
            QDir(dir).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &e : entries)
        {
          if (e.compare(modelName, Qt::CaseInsensitive) == 0)
            return dir + QStringLiteral("/") + e;
        }
      }
    }
  }

  // 4. ament_index — finds packages installed in any workspace overlay.
  {
    try
    {
      const std::string shareDir =
          ament_index_cpp::get_package_share_directory(modelName.toStdString());
      if (!shareDir.empty())
        return QString::fromStdString(shareDir);
    }
    catch (...)
    {
      // PackageNotFoundError or any other exception — package not in ament index.
    }
  }

  return {};
}

// ============================================================
// rewriteUriElement — rewrite one <uri> element in place.
// Returns true if the element was modified.
// Populates unresolvedOut with the original URI text if it could not be resolved.
// ============================================================
bool rewriteUriElement(tinyxml2::XMLElement *elem,
                       const QString &modelDir,
                       const QString &modelsRoot,
                       QString &unresolvedOut)
{
  const char *raw = elem->GetText();
  if (!raw || !*raw)
    return false;

  const QString uri = QString::fromUtf8(raw).trimmed();

  // ---- model:// / models:// ----
  if (uri.startsWith(QStringLiteral("models://")) ||
      uri.startsWith(QStringLiteral("model://")))
  {
    const int sep = uri.indexOf(QStringLiteral("://"));
    const QString rel = uri.mid(sep + 3);   // e.g. "robotnik_description/meshes/foo.dae"
    const int slash = rel.indexOf(QLatin1Char('/'));
    if (slash < 0)
    {
      gzwarn << "[gz_model_importer] SdfUriRewriter: malformed model URI "
             << "(no slash after model name): " << uri.toStdString() << "\n";
      unresolvedOut = uri;
      return false;
    }
    const QString modelName = rel.left(slash);
    const QString rest      = rel.mid(slash + 1);

    const QString dir = resolveModelDir(modelName, modelDir, modelsRoot);
    if (dir.isEmpty())
    {
      gzwarn << "[gz_model_importer] SdfUriRewriter: cannot resolve '"
             << uri.toStdString() << "' — package '" << modelName.toStdString()
             << "' not found in modelDir, modelsRoot, GZ_SIM_RESOURCE_PATH, "
             << "or ament index.\n";
      unresolvedOut = uri;
      return false;
    }

    const QString abs = QDir::cleanPath(dir + QStringLiteral("/") + rest);
    elem->SetText((QStringLiteral("file://") + abs).toUtf8().constData());
    gzmsg << "[gz_model_importer] SdfUriRewriter: " << uri.toStdString()
          << " → file://" << abs.toStdString() << "\n";
    return true;
  }

  // ---- package:// — resolve via ament_index ----
  if (uri.startsWith(QStringLiteral("package://")))
  {
    const QString rel = uri.mid(10);  // strip "package://"
    const int slash = rel.indexOf(QLatin1Char('/'));
    if (slash < 0)
    {
      gzwarn << "[gz_model_importer] SdfUriRewriter: malformed package URI "
             << "(no slash after package name): " << uri.toStdString() << "\n";
      unresolvedOut = uri;
      return false;
    }
    const QString pkgName = rel.left(slash);
    const QString rest    = rel.mid(slash + 1);

    try
    {
      const std::string shareDir =
          ament_index_cpp::get_package_share_directory(pkgName.toStdString());
      const QString abs = QDir::cleanPath(
          QString::fromStdString(shareDir) + QStringLiteral("/") + rest);
      elem->SetText((QStringLiteral("file://") + abs).toUtf8().constData());
      gzmsg << "[gz_model_importer] SdfUriRewriter: " << uri.toStdString()
            << " → file://" << abs.toStdString() << "\n";
      return true;
    }
    catch (...)
    {
      gzwarn << "[gz_model_importer] SdfUriRewriter: cannot resolve '"
             << uri.toStdString() << "' — package '" << pkgName.toStdString()
             << "' not found in ament index.\n";
      unresolvedOut = uri;
      return false;
    }
  }

  // ---- Already absolute ----
  if (uri.startsWith(QStringLiteral("file://"))  ||
      uri.startsWith(QStringLiteral("http://"))  ||
      uri.startsWith(QStringLiteral("https://")))
    return false;  // leave unchanged, count as untracked (neither resolved nor unresolved)

  // ---- Relative path — anchor to modelDir ----
  const QString abs = QDir::cleanPath(modelDir + QStringLiteral("/") + uri);
  elem->SetText((QStringLiteral("file://") + abs).toUtf8().constData());
  return true;
}

// Depth-first walk, rewriting every <uri> element.
void walkAndRewrite(tinyxml2::XMLElement *node,
                    const QString &modelDir,
                    const QString &modelsRoot,
                    int &totalCount,
                    int &resolvedCount,
                    QStringList &unresolved)
{
  if (!node)
    return;

  if (std::string(node->Name()) == "uri")
  {
    const char *raw = node->GetText();
    if (raw && *raw)
    {
      const QString uri = QString::fromUtf8(raw).trimmed();
      // file://, http://, https:// are already resolved — don't count them.
      const bool alreadyAbsolute =
          uri.startsWith(QStringLiteral("file://")) ||
          uri.startsWith(QStringLiteral("http://"))  ||
          uri.startsWith(QStringLiteral("https://"));
      if (!alreadyAbsolute)
      {
        ++totalCount;
        QString unresolvedUri;
        if (rewriteUriElement(node, modelDir, modelsRoot, unresolvedUri))
          ++resolvedCount;
        else if (!unresolvedUri.isEmpty())
          unresolved.append(unresolvedUri);
        else
          ++resolvedCount;  // relative path — treated as resolved
      }
    }
  }

  for (auto *child = node->FirstChildElement();
       child;
       child = child->NextSiblingElement())
  {
    walkAndRewrite(child, modelDir, modelsRoot, totalCount, resolvedCount, unresolved);
  }
}

}  // namespace

// ============================================================
// SdfUriRewriter::rewrite
// ============================================================
RewriteResult SdfUriRewriter::rewrite(const QString &_sdf,
                                      const QString &_modelDir,
                                      const QString &_modelsRoot)
{
  RewriteResult result;
  result.sdf = _sdf;

  if (_sdf.isEmpty())
    return result;

  tinyxml2::XMLDocument doc;
  const QByteArray utf8 = _sdf.toUtf8();
  if (doc.Parse(utf8.constData(), utf8.size()) != tinyxml2::XML_SUCCESS)
  {
    gzwarn << "[gz_model_importer] SdfUriRewriter: failed to parse SDF XML ("
           << doc.ErrorStr() << "). URIs left unrewritten.\n";
    return result;
  }

  walkAndRewrite(doc.RootElement(),
                 _modelDir, _modelsRoot,
                 result.totalUris, result.resolvedUris, result.unresolvedUris);

  gzmsg << "[gz_model_importer] SdfUriRewriter: "
        << result.resolvedUris << "/" << result.totalUris
        << " URIs resolved";
  if (!result.unresolvedUris.isEmpty())
    gzmsg << ", " << result.unresolvedUris.size() << " unresolved";
  gzmsg << " (modelDir=" << _modelDir.toStdString()
        << ", modelsRoot=" << _modelsRoot.toStdString() << ")\n";

  if (result.resolvedUris == 0 && result.unresolvedUris.isEmpty())
    return result;  // nothing changed

  tinyxml2::XMLPrinter printer;
  doc.Print(&printer);
  result.sdf = QString::fromUtf8(printer.CStr());
  return result;
}

}  // namespace gz_model_importer
