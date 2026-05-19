#include "gz_model_importer/ModelLoader.hh"

#include "gz_model_importer/XacroExpander.hh"
#include "gz_model_importer/UrdfToSdf.hh"
#include "gz_model_importer/FileLoader.hh"

namespace gz_model_importer
{

ModelLoader::ModelLoader(QObject *_parent)
: QObject(_parent),
  expander_(std::make_unique<XacroExpander>(this))
{
  connect(expander_.get(), &XacroExpander::expandComplete,
          this, &ModelLoader::onExpandComplete);
  connect(expander_.get(), &XacroExpander::expandFailed,
          this, &ModelLoader::onExpandFailed);
}

ModelLoader::~ModelLoader() = default;

bool    ModelLoader::isLoading()  const { return loading_; }
QString ModelLoader::lastError()  const { return lastError_; }

void ModelLoader::load(const QString &_path,
                       FileFormat _format,
                       const QStringList &_xacroArgs)
{
  loading_ = true;
  lastError_.clear();
  emit loadingChanged();
  emit lastErrorChanged();

  if (_format == FileFormat::Xacro)
  {
    expander_->expand(_path, _xacroArgs);
    return;
  }

  // URDF or SDF: read synchronously then convert.
  QString readError;
  const std::string content = FileLoader::readContent(_path, readError);
  if (content.empty())
  {
    loading_ = false;
    emit loadingChanged();
    lastError_ = readError;
    emit lastErrorChanged();
    emit loadFailed(readError);
    return;
  }

  // For SDF, conversion is essentially a validation pass.
  // For URDF, sdf::readString performs the URDF→SDF translation.
  QString convError;
  const std::string sdf = UrdfToSdf::convert(content, convError);
  loading_ = false;
  emit loadingChanged();

  if (sdf.empty())
  {
    lastError_ = convError;
    emit lastErrorChanged();
    emit loadFailed(convError);
    return;
  }

  const QString resolvedUrdf = (_format == FileFormat::Urdf)
      ? QString::fromStdString(content)
      : QString();
  emit loadComplete(QString::fromStdString(sdf), resolvedUrdf);
}

void ModelLoader::cancel()
{
  expander_->cancel();
  loading_ = false;
  emit loadingChanged();
}

void ModelLoader::onExpandComplete(const QString &urdfContent)
{
  // XACRO expanded to URDF string → convert to SDF.
  QString convError;
  const std::string sdf = UrdfToSdf::convert(urdfContent.toStdString(), convError);
  loading_ = false;
  emit loadingChanged();

  if (sdf.empty())
  {
    lastError_ = convError;
    emit lastErrorChanged();
    emit loadFailed(convError);
    return;
  }

  emit loadComplete(QString::fromStdString(sdf), urdfContent);
}

void ModelLoader::onExpandFailed(const QString &errorSummary,
                                  const QString & /*fullStderr*/)
{
  loading_ = false;
  emit loadingChanged();
  lastError_ = errorSummary;
  emit lastErrorChanged();
  emit loadFailed(errorSummary);
}

}  // namespace gz_model_importer
