#include "gz_model_importer_gui/UrdfToSdf.hh"

#include <QStringList>
#include <sdf/sdf.hh>
#include <sdf/parser.hh>

namespace gz_model_importer_gui
{

std::string UrdfToSdf::convert(const std::string &_urdfOrSdfXml,
                               QString &_errorOut)
{
  // sdf::readString handles both URDF and SDF inputs transparently.
  // It converts URDF to SDF internally via libsdformat's URDF converter.
  sdf::SDFPtr sdfDoc = std::make_shared<sdf::SDF>();
  sdf::init(sdfDoc);

  sdf::Errors errors;
  const bool ok = sdf::readString(_urdfOrSdfXml, sdfDoc, errors);

  if (!ok || !errors.empty())
  {
    QStringList msgs;
    for (const auto &e : errors)
      msgs << QString::fromStdString(e.Message());
    _errorOut = msgs.join(QLatin1Char('\n'));
    return {};
  }

  if (!sdfDoc->Root())
  {
    _errorOut = QStringLiteral("sdformat produced no root element");
    return {};
  }

  return sdfDoc->Root()->ToString("");
}

}  // namespace gz_model_importer_gui
