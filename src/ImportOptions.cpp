#include "gz_model_importer/ImportOptions.hh"

namespace gz_model_importer
{

ImportOptions::ImportOptions(QObject *_parent) : QObject(_parent) {}
ImportOptions::~ImportOptions() = default;

QString ImportOptions::instanceName()  const { return instanceName_; }
bool    ImportOptions::launchRobotStatePublisher() const
{ return launchRobotStatePublisher_; }
double  ImportOptions::poseX()         const { return poseX_; }
double  ImportOptions::poseY()         const { return poseY_; }
double  ImportOptions::poseZ()         const { return poseZ_; }
double  ImportOptions::poseRoll()      const { return poseRoll_; }
double  ImportOptions::posePitch()     const { return posePitch_; }
double  ImportOptions::poseYaw()       const { return poseYaw_; }

void ImportOptions::setInstanceName(const QString &v)
{ if (instanceName_ == v) return; instanceName_ = v; emit instanceNameChanged(); }

void ImportOptions::setLaunchRobotStatePublisher(bool v)
{
  if (launchRobotStatePublisher_ == v) return;
  launchRobotStatePublisher_ = v;
  emit launchRobotStatePublisherChanged();
}

void ImportOptions::setPoseX(double v)
{ if (poseX_ == v) return; poseX_ = v; emit poseXChanged(); }

void ImportOptions::setPoseY(double v)
{ if (poseY_ == v) return; poseY_ = v; emit poseYChanged(); }

void ImportOptions::setPoseZ(double v)
{ if (poseZ_ == v) return; poseZ_ = v; emit poseZChanged(); }

void ImportOptions::setPoseRoll(double v)
{ if (poseRoll_ == v) return; poseRoll_ = v; emit poseRollChanged(); }

void ImportOptions::setPosePitch(double v)
{ if (posePitch_ == v) return; posePitch_ = v; emit posePitchChanged(); }

void ImportOptions::setPoseYaw(double v)
{ if (poseYaw_ == v) return; poseYaw_ = v; emit poseYawChanged(); }

void ImportOptions::reset()
{
  setInstanceName(QStringLiteral("robot"));
  setLaunchRobotStatePublisher(true);
  setPoseX(0.0);
  setPoseY(0.0);
  setPoseZ(0.1);
  setPoseRoll(0.0);
  setPosePitch(0.0);
  setPoseYaw(0.0);
}

}  // namespace gz_model_importer
