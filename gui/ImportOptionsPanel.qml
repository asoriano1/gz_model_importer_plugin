import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

// Import options: instance_name, ros_namespace, frame_prefix, spawn pose.
// Bound to `importOptions` context property (ImportOptions QObject).
ColumnLayout {
  id: root
  spacing: 6

  Label {
    text: "Import options"
    font.bold: true
  }

  GridLayout {
    columns: 2
    columnSpacing: 8
    rowSpacing: 4
    Layout.fillWidth: true

    Label { text: "Instance name" }
    TextField {
      id: instanceNameField
      text: importOptions ? importOptions.instanceName : "robot"
      Layout.fillWidth: true
      onEditingFinished: if (importOptions) importOptions.instanceName = text
    }

    Label { text: "ROS namespace" }
    TextField {
      text: importOptions ? importOptions.rosNamespace : ""
      placeholderText: "(empty = no injection)"
      Layout.fillWidth: true
      onEditingFinished: if (importOptions) importOptions.rosNamespace = text
    }

    Label { text: "Frame prefix" }
    TextField {
      text: importOptions ? importOptions.framePrefix : ""
      placeholderText: "(phase 5, not yet applied)"
      Layout.fillWidth: true
      enabled: false   // TODO: enable when InstanceRewriter.rewrite supports it
      onEditingFinished: if (importOptions) importOptions.framePrefix = text
    }
  }

  // ---- Spawn pose ----
  Label {
    text: "Spawn pose (m / rad)"
    font.bold: true
    topPadding: 6
  }

  GridLayout {
    columns: 6
    columnSpacing: 4
    rowSpacing: 4
    Layout.fillWidth: true

    Label { text: "X" }
    SpinBox {
      value: importOptions ? importOptions.poseX * 100 : 0
      from: -100000; to: 100000; stepSize: 10
      onValueChanged: if (importOptions) importOptions.poseX = value / 100.0
    }
    Label { text: "Y" }
    SpinBox {
      value: importOptions ? importOptions.poseY * 100 : 0
      from: -100000; to: 100000; stepSize: 10
      onValueChanged: if (importOptions) importOptions.poseY = value / 100.0
    }
    Label { text: "Z" }
    SpinBox {
      value: importOptions ? importOptions.poseZ * 100 : 10
      from: 0; to: 100000; stepSize: 5
      onValueChanged: if (importOptions) importOptions.poseZ = value / 100.0
    }

    Label { text: "Roll" }
    SpinBox {
      value: importOptions ? importOptions.poseRoll * 100 : 0
      from: -314; to: 314; stepSize: 5
      onValueChanged: if (importOptions) importOptions.poseRoll = value / 100.0
    }
    Label { text: "Pitch" }
    SpinBox {
      value: importOptions ? importOptions.posePitch * 100 : 0
      from: -157; to: 157; stepSize: 5
      onValueChanged: if (importOptions) importOptions.posePitch = value / 100.0
    }
    Label { text: "Yaw" }
    SpinBox {
      value: importOptions ? importOptions.poseYaw * 100 : 0
      from: -314; to: 314; stepSize: 5
      onValueChanged: if (importOptions) importOptions.poseYaw = value / 100.0
    }
  }

  Label {
    text: "Note: ros_namespace is injected only into recognized ROS plugin elements.\n"
        + "Hardcoded topics/services inside arbitrary plugins are not rewritten."
    font.italic: true
    wrapMode: Text.Wrap
    Layout.fillWidth: true
    color: "#5d4037"
    topPadding: 4
  }
}
