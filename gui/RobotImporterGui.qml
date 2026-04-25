import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Dialogs 1.3
import QtQuick.Layouts 1.3

// Context properties set on engine rootContext() in plugin constructor:
//   backend, fileSelector, importOptions, previewCtrl, importerPlugin
Rectangle {
  id: root
  color: "transparent"
  Layout.minimumWidth: 300
  Layout.minimumHeight: 380
  anchors.fill: parent

  // Numeric input that stays in sync with an ImportOptions property.
  component PoseField: TextField {
    property double modelValue: 0.0
    property bool   userEditing: false

    text: modelValue.toFixed(3)
    font.pixelSize: 12
    horizontalAlignment: Text.AlignRight
    validator: DoubleValidator { decimals: 3; notation: DoubleValidator.StandardNotation }

    onActiveFocusChanged: userEditing = activeFocus
    onModelValueChanged: if (!userEditing) text = modelValue.toFixed(3)
  }

  ScrollView {
    anchors.fill: parent
    contentWidth: availableWidth

    Item {
      width: root.width
      implicitHeight: col.implicitHeight + 20

      ColumnLayout {
        id: col
        anchors { top: parent.top; left: parent.left; right: parent.right; margins: 14 }
        spacing: 7

        // ---- PREVIEW MODE banner ----
        Rectangle {
          visible: previewCtrl.previewing
          Layout.fillWidth: true
          implicitHeight: 28
          color: "#e65100"
          radius: 3

          Label {
            anchors.centerIn: parent
            text: "MODEL PREVIEW (SIM PAUSED)"
            color: "white"; font.bold: true; font.pixelSize: 13
          }
        }

        // ---- Status line ----
        RowLayout {
          Layout.fillWidth: true
          spacing: 6

          Label { text: "Status:"; font.pixelSize: 13; color: "#555" }
          Label {
            text: backend.stateName
            font.bold: true; font.pixelSize: 13
            color: {
              var s = backend.stateName
              if (s === "Done") return "#2e7d32"
              if (s === "ConversionFailed" || s === "ExpansionFailed" ||
                  s === "PreviewFailed"    || s === "SpawnFailed")
                return "#b71c1c"
              return "#333333"
            }
          }
          BusyIndicator { running: backend.busy; width: 16; height: 16 }
          Item { Layout.fillWidth: true }
        }

        Label {
          visible: backend.lastWarning.length > 0
          text: "(!) " + backend.lastWarning
          color: "#e65100"; font.pixelSize: 12
          wrapMode: Text.Wrap; Layout.fillWidth: true
        }
        Label {
          visible: backend.lastError.length > 0
          text: backend.lastError
          color: "#b71c1c"; font.pixelSize: 12
          wrapMode: Text.Wrap; Layout.fillWidth: true
        }

        // ---- Preflight report ----
        Rectangle {
          visible: backend.preflightReport.length > 0
          Layout.fillWidth: true
          color: backend.preflightReport.indexOf("unresolved") >= 0 ||
                 backend.preflightReport.indexOf("Ogre") >= 0
                 ? "#FFF3E0" : "#F1F8E9"
          border.color: backend.preflightReport.indexOf("unresolved") >= 0 ||
                        backend.preflightReport.indexOf("Ogre") >= 0
                        ? "#e65100" : "#558b2f"
          border.width: 1; radius: 3
          implicitHeight: preflightLabel.implicitHeight + 10

          Label {
            id: preflightLabel
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 5 }
            text: backend.preflightReport
            font.pixelSize: 11; font.family: "monospace"
            color: "#333"; wrapMode: Text.Wrap
          }
        }

        Rectangle { height: 1; color: "#dddddd"; Layout.fillWidth: true }

        // ---- File selection ----
        RowLayout {
          Layout.fillWidth: true
          spacing: 8

          Label {
            id: fileLabel
            text: {
              var p = fileSelector.selectedPath
              if (!p || p.length === 0) return "No file selected"
              var parts = p.split("/")
              return parts[parts.length - 1]
            }
            font.pixelSize: 13
            elide: Text.ElideRight
            Layout.fillWidth: true
            Layout.minimumWidth: 80
            color: fileSelector.selectedPath.length > 0 ? "#111" : "#888"

            ToolTip.visible: fileSelector.selectedPath.length > 0 && fileLabelHover.containsMouse
            ToolTip.text: fileSelector.selectedPath
            ToolTip.delay: 500

            MouseArea {
              id: fileLabelHover
              anchors.fill: parent
              hoverEnabled: true
              acceptedButtons: Qt.NoButton
            }
          }

          Button {
            text: "Browse"
            font.pixelSize: 12
            enabled: !backend.busy
            onClicked: fileDialog.open()
            implicitWidth: 90
          }
        }

        RowLayout {
          visible: fileSelector.detectedFormat.length > 0
          spacing: 6
          Label { text: "Format:"; font.pixelSize: 12; color: "#555" }
          Label { text: fileSelector.detectedFormat; font.pixelSize: 12; font.bold: true }
          Label {
            visible: previewCtrl.previewing
            text: "  Preview: " + previewCtrl.previewEntityName
            font.pixelSize: 12; font.italic: true; color: "#555"
          }
        }

        Label {
          visible: fileSelector.lastError.length > 0
          text: fileSelector.lastError
          color: "#b71c1c"; font.pixelSize: 12
          wrapMode: Text.Wrap; Layout.fillWidth: true
        }

        FileDialog {
          id: fileDialog
          title: "Select robot file"
          folder: shortcuts.home
          nameFilters: [
            "Robot description (*.urdf *.xacro *.sdf *.xml)",
            "URDF (*.urdf *.xml)", "XACRO (*.xacro)", "SDF (*.sdf)", "All files (*)"
          ]
          onAccepted: fileSelector.onFileChosen(fileDialog.fileUrl.toString())
        }

        // ---- Options section (shown once file is loaded) ----
        ColumnLayout {
          id: optionsSection
          Layout.fillWidth: true
          spacing: 7
          property bool poseExpanded: false
          visible: {
            var s = backend.stateName
            return s === "Ready"      || s === "Configuring"  ||
                   s === "Previewing" || s === "PreviewFailed" ||
                   s === "SpawnFailed"
          }

          Rectangle { height: 1; color: "#dddddd"; Layout.fillWidth: true }

          // Instance name + namespace
          GridLayout {
            columns: 2; columnSpacing: 8; rowSpacing: 4
            Layout.fillWidth: true

            Label { text: "Name"; font.pixelSize: 13 }
            TextField {
              text: importOptions.instanceName
              font.pixelSize: 12; Layout.fillWidth: true
              onEditingFinished: importOptions.instanceName = text
            }

            Label { text: "Namespace"; font.pixelSize: 13 }
            TextField {
              text: importOptions.rosNamespace
              placeholderText: "(none)"
              font.pixelSize: 12; Layout.fillWidth: true
              onEditingFinished: importOptions.rosNamespace = text
            }
          }

          // ---- Pose header (clickable toggle) ----
          Item {
            Layout.fillWidth: true
            implicitHeight: poseToggleLabel.implicitHeight + 6

            Label {
              id: poseToggleLabel
              anchors.verticalCenter: parent.verticalCenter
              text: (optionsSection.poseExpanded ? "▼" : "▶") + "  Pose  (m / rad)"
              font.pixelSize: 13; color: "#555"
            }

            MouseArea {
              anchors.fill: parent
              cursorShape: Qt.PointingHandCursor
              onClicked: optionsSection.poseExpanded = !optionsSection.poseExpanded
            }
          }

          // ---- Position row ----
          RowLayout {
            visible: optionsSection.poseExpanded
            Layout.fillWidth: true; spacing: 4

            Label { text: "X"; font.pixelSize: 12; color: "#555"; Layout.preferredWidth: 18 }
            PoseField {
              id: pxf; Layout.fillWidth: true
              modelValue: importOptions.poseX
              onEditingFinished: importOptions.poseX = parseFloat(text)
            }
            Label { text: "Y"; font.pixelSize: 12; color: "#555"; Layout.preferredWidth: 18 }
            PoseField {
              id: pyf; Layout.fillWidth: true
              modelValue: importOptions.poseY
              onEditingFinished: importOptions.poseY = parseFloat(text)
            }
            Label { text: "Z"; font.pixelSize: 12; color: "#555"; Layout.preferredWidth: 18 }
            PoseField {
              id: pzf; Layout.fillWidth: true
              modelValue: importOptions.poseZ
              onEditingFinished: importOptions.poseZ = parseFloat(text)
            }
          }

          // ---- Orientation row ----
          RowLayout {
            visible: optionsSection.poseExpanded
            Layout.fillWidth: true; spacing: 4

            Label { text: "R"; font.pixelSize: 12; color: "#555"; Layout.preferredWidth: 18 }
            PoseField {
              id: prf; Layout.fillWidth: true
              modelValue: importOptions.poseRoll
              onEditingFinished: importOptions.poseRoll = parseFloat(text)
            }
            Label { text: "P"; font.pixelSize: 12; color: "#555"; Layout.preferredWidth: 18 }
            PoseField {
              id: ppf; Layout.fillWidth: true
              modelValue: importOptions.posePitch
              onEditingFinished: importOptions.posePitch = parseFloat(text)
            }
            Label { text: "Y"; font.pixelSize: 12; color: "#555"; Layout.preferredWidth: 18 }
            PoseField {
              id: pwf; Layout.fillWidth: true
              modelValue: importOptions.poseYaw
              onEditingFinished: importOptions.poseYaw = parseFloat(text)
            }
          }

          // ---- Preview highlight mode ----
          RowLayout {
            Layout.fillWidth: true; spacing: 8
            visible: previewCtrl.previewing

            Label { text: "Highlight:"; font.pixelSize: 12; color: "#555" }

            ComboBox {
              id: highlightCombo
              font.pixelSize: 12
              Layout.fillWidth: true
              // ComboBox index: 0=Wireframe  1=Transparent  2=None
              // C++ highlightMode: 0=None  1=Transparency  2=Wireframe
              model: ["Wireframe", "Transparent", "None"]

              Component.onCompleted: {
                var m = importerPlugin.highlightMode
                currentIndex = (m === 2) ? 0 : (m === 1) ? 1 : 2
              }

              onActivated: {
                var mode = (index === 0) ? 2 : (index === 1) ? 1 : 0
                importerPlugin.highlightMode = mode
              }

              Connections {
                target: importerPlugin
                function onHighlightModeChanged() {
                  var m = importerPlugin.highlightMode
                  highlightCombo.currentIndex = (m === 2) ? 0 : (m === 1) ? 1 : 2
                }
              }
            }
          }
        }

        Item { implicitHeight: 4 }

        // ---- Action bar ----
        RowLayout {
          Layout.fillWidth: true
          spacing: 6
          visible: backend.stateName !== "Idle"

          Button {
            text: "Import"
            highlighted: true
            font.pixelSize: 12
            visible: {
              var s = backend.stateName
              return s === "Ready"       || s === "Configuring" ||
                     s === "PreviewFailed" || s === "SpawnFailed"
            }
            enabled: !backend.busy
            onClicked: backend.importRobot()
            implicitWidth: 72
          }

          Label {
            visible: backend.stateName === "Done"
            text: "Imported successfully."
            color: "#2e7d32"; font.bold: true; font.pixelSize: 13
            Layout.fillWidth: true
          }

          Item { Layout.fillWidth: true }

          Button {
            text: "Cancel"
            font.pixelSize: 12
            onClicked: backend.reset()
            implicitWidth: 64
          }
        }
      }
    }
  }
}
