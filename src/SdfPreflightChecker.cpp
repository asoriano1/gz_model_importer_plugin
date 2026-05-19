#include "gz_model_importer/SdfPreflightChecker.hh"

#include <string>

#include <tinyxml2.h>

namespace gz_model_importer
{

namespace
{

// Walk the tree and collect:
//   - Remaining model:// / package:// URIs (rewriter left them)
//   - Ogre material script URIs (*.material under <material><script><uri>)
//   - Mesh collision count (<collision><geometry><mesh>)
//   - Plugin filenames (<plugin filename="...">)
struct Collector
{
  QStringList ogreMaterialScripts;
  int         meshCollisionCount{0};
  QStringList pluginFilenames;

  // Track whether we are inside a <material><script> context.
  int scriptDepth{0};
  bool inCollision{false};
  bool inGeometry{false};

  void walk(tinyxml2::XMLElement *el)
  {
    if (!el) return;

    const std::string name = el->Name();

    // ---- <plugin> ----
    if (name == "plugin")
    {
      const char *fn = el->Attribute("filename");
      if (fn && *fn)
        pluginFilenames.append(QString::fromUtf8(fn));
    }

    // ---- Ogre material script detection ----
    // Pattern: <material><script><uri>path/to/file.material</uri></script></material>
    if (name == "script")
      ++scriptDepth;
    if (name == "uri" && scriptDepth > 0)
    {
      const char *txt = el->GetText();
      if (txt)
      {
        const QString uri = QString::fromUtf8(txt).trimmed();
        if (uri.endsWith(QStringLiteral(".material"), Qt::CaseInsensitive))
          ogreMaterialScripts.append(uri);
      }
    }

    // ---- Mesh collision detection ----
    // Pattern: <collision><geometry><mesh>
    if (name == "collision") inCollision = true;
    if (name == "geometry" && inCollision) inGeometry = true;
    if (name == "mesh" && inCollision && inGeometry)
      ++meshCollisionCount;

    // Recurse.
    for (auto *child = el->FirstChildElement();
         child;
         child = child->NextSiblingElement())
    {
      walk(child);
    }

    // Unwind context flags on the way back up (simple stack substitute).
    if (name == "script" && scriptDepth > 0)
      --scriptDepth;
    if (name == "geometry" && inCollision) inGeometry = false;
    if (name == "collision") inCollision = false;
  }
};

}  // namespace

PreflightFindings SdfPreflightChecker::analyze(const QString &_sdf,
                                                const QStringList &_unresolvedFromRewrite)
{
  PreflightFindings findings;
  findings.unresolvedUris = _unresolvedFromRewrite;

  if (_sdf.isEmpty())
    return findings;

  tinyxml2::XMLDocument doc;
  const QByteArray utf8 = _sdf.toUtf8();
  if (doc.Parse(utf8.constData(), utf8.size()) != tinyxml2::XML_SUCCESS)
    return findings;

  Collector col;
  col.walk(doc.RootElement());

  findings.ogreMaterialScripts = col.ogreMaterialScripts;
  findings.meshCollisionCount  = col.meshCollisionCount;
  findings.pluginFilenames      = col.pluginFilenames;

  return findings;
}

}  // namespace gz_model_importer
