#include "robot_importer_gui/InstanceRewriter.hh"

#include <QStringList>
#include <tinyxml2.h>

namespace robot_importer_gui
{

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

  // ---- Rename top-level <model name="..."> ----
  // This is the only rewrite guaranteed to work for any well-formed SDF.
  auto *root = doc.RootElement();
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

  // ---- ros_namespace: noted, not injected ----
  // Injecting <ros><namespace> only works for a known subset of ROS plugins
  // and that subset changes with each ros2 release. To avoid false promises,
  // we surface a warning and leave namespace management to the user.
  if (!_opts.rosNamespace.empty())
  {
    warningList << QStringLiteral(
        "ros_namespace '%1' is noted but NOT automatically injected into "
        "plugin elements. Topics and services inside this robot model that "
        "are hardcoded in the SDF remain unchanged. Set the namespace "
        "manually in each plugin's <ros><namespace> block if needed.")
        .arg(QString::fromStdString(_opts.rosNamespace));
  }

  // ---- frame_prefix: deferred to phase 5 ----
  // Renaming every link and joint while keeping all cross-references
  // consistent requires a full graph walk. Deferred to avoid silent bugs.
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
