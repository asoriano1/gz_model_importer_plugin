#ifndef ROBOT_IMPORTER_GUI_IMPORT_OPTIONS_HH_
#define ROBOT_IMPORTER_GUI_IMPORT_OPTIONS_HH_

#include <QObject>
#include <QString>

namespace robot_importer_gui
{

/// Import options exposed to QML as a flat set of bindable properties.
/// Owns no business logic; purely a value object that the backend reads
/// when building the final SDF for spawn.
class ImportOptions : public QObject
{
  Q_OBJECT

  Q_PROPERTY(QString instanceName  READ instanceName  WRITE setInstanceName
             NOTIFY instanceNameChanged)
  Q_PROPERTY(QString rosNamespace  READ rosNamespace  WRITE setRosNamespace
             NOTIFY rosNamespaceChanged)
  Q_PROPERTY(QString framePrefix   READ framePrefix   WRITE setFramePrefix
             NOTIFY framePrefixChanged)

  // Spawn pose (metres / radians)
  Q_PROPERTY(double poseX    READ poseX    WRITE setPoseX    NOTIFY poseXChanged)
  Q_PROPERTY(double poseY    READ poseY    WRITE setPoseY    NOTIFY poseYChanged)
  Q_PROPERTY(double poseZ    READ poseZ    WRITE setPoseZ    NOTIFY poseZChanged)
  Q_PROPERTY(double poseRoll READ poseRoll WRITE setPoseRoll NOTIFY poseRollChanged)
  Q_PROPERTY(double posePitch READ posePitch WRITE setPosePitch NOTIFY posePitchChanged)
  Q_PROPERTY(double poseYaw  READ poseYaw  WRITE setPoseYaw  NOTIFY poseYawChanged)

  public: explicit ImportOptions(QObject *_parent = nullptr);
  public: ~ImportOptions() override;

  public: QString instanceName()  const;
  public: QString rosNamespace()  const;
  public: QString framePrefix()   const;

  public: double poseX()     const;
  public: double poseY()     const;
  public: double poseZ()     const;
  public: double poseRoll()  const;
  public: double posePitch() const;
  public: double poseYaw()   const;

  public: void setInstanceName(const QString &v);
  public: void setRosNamespace(const QString &v);
  public: void setFramePrefix(const QString &v);

  public: void setPoseX(double v);
  public: void setPoseY(double v);
  public: void setPoseZ(double v);
  public: void setPoseRoll(double v);
  public: void setPosePitch(double v);
  public: void setPoseYaw(double v);

  /// Reset all fields to their defaults. Called on backend reset().
  public: Q_INVOKABLE void reset();

  signals: void instanceNameChanged();
  signals: void rosNamespaceChanged();
  signals: void framePrefixChanged();
  signals: void poseXChanged();
  signals: void poseYChanged();
  signals: void poseZChanged();
  signals: void poseRollChanged();
  signals: void posePitchChanged();
  signals: void poseYawChanged();

  private: QString instanceName_{"robot"};
  private: QString rosNamespace_;
  private: QString framePrefix_;
  private: double poseX_{0.0}, poseY_{0.0}, poseZ_{0.1};
  private: double poseRoll_{0.0}, posePitch_{0.0}, poseYaw_{0.0};
};

}  // namespace robot_importer_gui

#endif
