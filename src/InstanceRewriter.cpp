#include "gz_model_importer_gui/InstanceRewriter.hh"

#include <QString>
#include <tinyxml2.h>

namespace gz_model_importer_gui
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
      _warnings = QStringLiteral("No <model> element found — instance name not applied");
  }

  tinyxml2::XMLPrinter printer;
  doc.Print(&printer);
  return printer.CStr();
}

}  // namespace gz_model_importer_gui
