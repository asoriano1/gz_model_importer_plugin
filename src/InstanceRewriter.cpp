#include "robot_importer_gui/InstanceRewriter.hh"

#include <QStringList>
#include <gz/common/Console.hh>
#include <tinyxml2.h>

namespace robot_importer_gui
{

namespace
{

// ---- Plugin filename classification ----------------------------------------

enum class PluginClass { Ros2Control, RosPlugin, Unrelated };

static PluginClass classifyFilename(const char *fn)
{
  if (!fn || !*fn) return PluginClass::Unrelated;
  std::string s(fn);
  // strip lib prefix and .so suffix for matching
  if (s.size() > 3 && s.substr(0, 3) == "lib") s = s.substr(3);
  if (s.size() > 3 && s.substr(s.size() - 3) == ".so") s.resize(s.size() - 3);
  // lower-case
  for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

  for (const char *k : {"gz_ros2_control", "gazebo_ros2_control", "gazebo_ros_control"})
    if (s.find(k) != std::string::npos) return PluginClass::Ros2Control;

  if (s.rfind("gazebo_ros", 0) == 0 || s.rfind("gz_ros", 0) == 0)
    return PluginClass::RosPlugin;

  return PluginClass::Unrelated;
}

// ---- Namespace normalization ------------------------------------------------

static std::string normalizeNs(const std::string &ns)
{
  if (ns.empty()) return {};
  return ns[0] == '/' ? ns : "/" + ns;
}

// ---- Inject namespace into a single plugin element -------------------------

static bool injectNsIntoPlugin(tinyxml2::XMLElement *pluginEl,
                                const std::string &normalizedNs,
                                tinyxml2::XMLDocument &doc)
{
  const char *fn        = pluginEl->Attribute("filename");
  const bool knownRos   = classifyFilename(fn) != PluginClass::Unrelated;
  auto *rosEl           = pluginEl->FirstChildElement("ros");

  if (rosEl)
  {
    // Plugin already has <ros> — rewrite or inject <namespace> inside it.
    if (auto *nsEl = rosEl->FirstChildElement("namespace"))
      nsEl->SetText(normalizedNs.c_str());
    else
    {
      auto *newNs = doc.NewElement("namespace");
      newNs->SetText(normalizedNs.c_str());
      rosEl->InsertFirstChild(newNs);
    }
    return true;
  }

  if (knownRos)
  {
    // Known ROS plugin without <ros> — inject <ros><namespace/></ros>.
    auto *newRos = doc.NewElement("ros");
    auto *newNs  = doc.NewElement("namespace");
    newNs->SetText(normalizedNs.c_str());
    newRos->InsertFirstChild(newNs);
    pluginEl->InsertFirstChild(newRos);
    return true;
  }

  return false;
}

// ---- Walk tree for namespace injection -------------------------------------

static int walkAndInjectNs(tinyxml2::XMLElement *el,
                            const std::string &normalizedNs,
                            tinyxml2::XMLDocument &doc)
{
  if (!el) return 0;
  int count = 0;

  if (std::string(el->Name()) == "plugin")
  {
    if (injectNsIntoPlugin(el, normalizedNs, doc)) ++count;
    // Don't recurse into plugin children — nested <plugin> tags are rare
    // and treating them as independent plugins would be wrong.
    return count;
  }

  for (auto *child = el->FirstChildElement();
       child;
       child = child->NextSiblingElement())
    count += walkAndInjectNs(child, normalizedNs, doc);

  return count;
}

}  // namespace

// ============================================================
std::string InstanceRewriter::rewrite(const std::string &_sdf,
                                      const Options &_opts,
                                      QString &_warnings)
{
  tinyxml2::XMLDocument doc;
  if (doc.Parse(_sdf.c_str()) != tinyxml2::XML_SUCCESS)
  {
    _warnings = QStringLiteral("InstanceRewriter: XML parse failed — returning original SDF");
    return _sdf;
  }

  QStringList warningList;
  auto *root = doc.RootElement();

  // ---- 1. Rename top-level <model name="..."> --------------------------------
  if (root && !_opts.instanceName.empty())
  {
    tinyxml2::XMLElement *modelEl = nullptr;
    if (std::string(root->Name()) == "model")
      modelEl = root;
    else
      modelEl = root->FirstChildElement("model");

    if (modelEl)
      modelEl->SetAttribute("name", _opts.instanceName.c_str());
    else
      warningList << QStringLiteral("No <model> element found — instance name not applied");
  }

  // ---- 2. Inject ROS namespace into plugin elements -------------------------
  // Targets: any <plugin> that already has a <ros> child (regardless of
  // filename) plus known gazebo_ros / gz_ros plugins that don't yet have one.
  // Native Gazebo sensor topics already include the model name and are
  // therefore unique once the model name is rewritten — no extra injection
  // needed for those.
  if (!_opts.rosNamespace.empty())
  {
    const std::string ns = normalizeNs(_opts.rosNamespace);
    const int injected   = walkAndInjectNs(root, ns, doc);

    if (injected > 0)
      gzmsg << "[robot_importer_gui] Namespace '" << ns << "' injected into "
            << injected << " plugin element(s).\n";
    else
      warningList << QStringLiteral(
          "Namespace '%1' specified but no injectable ROS plugin elements were found. "
          "The model may not use gazebo_ros/gz_ros plugins with <ros> support.")
          .arg(QString::fromStdString(ns));

    // Warn about limits: topic names hardcoded as <topic>...</topic> inside
    // plugin configs are not rewritten; those stay as-is.
    warningList << QStringLiteral(
        "Topic names hardcoded inside plugin <topic> parameters are not "
        "rewritten — only <ros><namespace> is updated.");
  }

  // ---- 3. frame_prefix: deferred to phase 5 ---------------------------------
  if (!_opts.framePrefix.empty())
  {
    warningList << QStringLiteral(
        "frame_prefix '%1' is noted but link/joint renaming is not yet "
        "implemented. TF frame names inside the model are unchanged.")
        .arg(QString::fromStdString(_opts.framePrefix));
  }

  if (!warningList.isEmpty())
    _warnings = warningList.join(QLatin1Char('\n'));

  tinyxml2::XMLPrinter printer;
  doc.Print(&printer);
  return printer.CStr();
}

}  // namespace robot_importer_gui
