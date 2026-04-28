import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Dialogs 1.3
import QtQuick.Layouts 1.3

// Context properties injected via engine rootContext() in ImporterPlugin constructor:
//   backend        → ImporterBackend
//   fileSelector   → FileSelector
//   importOptions  → ImportOptions
//   previewCtrl    → PreviewController
//   importerPlugin → RobotImporterGui (for highlightMode)
Rectangle {
  id: root
  color: "transparent"
  // anchors.fill lets the root fill whatever space the Gazebo container
  // assigns it.  implicitHeight (fixed) is what the container reads to
  // decide how tall to make that space — keeping it at 480 prevents the
  // panel from growing to match the full content height, which would
  // defeat the internal ScrollView.
  anchors.fill: parent
  implicitWidth:  300
  implicitHeight: 480
  Layout.minimumWidth:    300
  Layout.minimumHeight:   300
  Layout.preferredHeight: 480
  Layout.maximumHeight:   480

  // ---- Derived state helpers ----
  readonly property bool isIdle:         backend.stateName === "Idle"
  readonly property bool isPreviewActive: previewCtrl.previewing
  readonly property bool isBusy:         backend.busy
  readonly property bool isDone:         backend.stateName === "Done"

  readonly property bool isCancellable: {
    var s = backend.stateName
    return s === "Previewing" || s === "Configuring" || s === "Spawning"
  }

  readonly property bool isImportable: {
    var s = backend.stateName
    return s === "Ready"       || s === "Configuring" ||
           s === "PreviewFailed" || s === "SpawnFailed"
  }

  readonly property bool showOptions: {
    var s = backend.stateName
    return s === "Ready"       || s === "Configuring" || s === "Previewing" ||
           s === "PreviewFailed" || s === "SpawnFailed"
  }

  readonly property bool isErrorState: {
    var s = backend.stateName
    return s === "SpawnFailed"      || s === "PreviewFailed" ||
           s === "ExpansionFailed"  || s === "ConversionFailed" || s === "Error"
  }

  readonly property bool hasLogs:
    backend.preflightReport.length > 0 ||
    backend.lastWarning.length  > 0    ||
    backend.lastError.length    > 0

  // ---- Pill color ----
  function pillColor() {
    var s = backend.stateName
    if (s === "Done") return "#2e7d32"
    if (s === "SpawnFailed" || s === "PreviewFailed" ||
        s === "ExpansionFailed" || s === "ConversionFailed" || s === "Error")
      return "#c62828"
    if (s === "Configuring")  return "#e65100"
    if (s === "Spawning" || s === "Expanding" || s === "Converting")
      return "#1565c0"
    if (s === "Previewing")   return "#6a1b9a"
    if (s === "Ready")        return "#2e7d32"
    return "#757575"
  }

  // ---- Friendly state label ----
  function prettyState() {
    var s = backend.stateName
    if (s === "Idle")             return "Idle"
    if (s === "FileSelected")     return "Loading…"
    if (s === "Expanding")        return "Expanding XACRO…"
    if (s === "ExpansionFailed")  return "Expansion failed"
    if (s === "Converting")       return "Loading SDF…"
    if (s === "ConversionFailed") return "Conversion failed"
    if (s === "Ready")            return "Ready"
    if (s === "Previewing")       return "Spawning preview…"
    if (s === "PreviewFailed")    return "Preview failed"
    if (s === "Configuring")      return "Preview active"
    if (s === "Spawning")         return "Importing…"
    if (s === "SpawnFailed")      return "Import failed"
    if (s === "Done")             return "Done"
    if (s === "Error")            return "Error"
    return s
  }

  // Numeric pose input that stays in sync with an ImportOptions property.
  component PoseField: TextField {
    property double modelValue: 0.0
    property bool   userEditing: false

    text: modelValue.toFixed(3)
    font.pixelSize: 12
    horizontalAlignment: Text.AlignRight
    validator: DoubleValidator { decimals: 3; notation: DoubleValidator.StandardNotation }

    onActiveFocusChanged: userEditing = activeFocus
    onModelValueChanged:  if (!userEditing) text = modelValue.toFixed(3)
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

  // Auto-expand the log section when diagnostics become available.
  // Also auto-scroll so newly visible sections are reachable.
  Connections {
    target: backend

    function onStateChanged() {
      var s = backend.stateName
      // Expand logs on error transitions.
      if (root.isErrorState && root.hasLogs)
        logsCard.logsExpanded = true
      // Scroll to bottom when the options / runtime sections appear so the
      // user can see the Import button without manually scrolling.
      // Qt.callLater defers until the layout pass has settled.
      if (s === "Configuring" || s === "Done")
        Qt.callLater(function() {
          var sb = mainScrollView.ScrollBar.vertical
          sb.position = Math.max(0, 1.0 - sb.size)
        })
      else if (s === "Idle")
        mainScrollView.ScrollBar.vertical.position = 0
    }

    // Also expand when an error is set without a state transition — e.g. when
    // importRobot() or requestPreview() fails synchronously (world not found)
    // while the state stays in Ready or Configuring.
    function onLastErrorChanged() {
      if (backend.lastError.length > 0)
        logsCard.logsExpanded = true
    }
  }

  // ================================================================
  ScrollView {
    id: mainScrollView
    anchors.fill: parent
    contentWidth: availableWidth
    // Explicit contentHeight is required: Qt cannot derive it automatically
    // from a ColumnLayout nested inside an Item inside a ScrollView.
    contentHeight: mainCol.implicitHeight + 20
    clip: true
    ScrollBar.vertical.policy: ScrollBar.AsNeeded

    Item {
      width:  root.width
      height: mainScrollView.contentHeight

      ColumnLayout {
        id: mainCol
        anchors { top: parent.top; left: parent.left; right: parent.right; margins: 10 }
        spacing: 5

        // ---- Section 1: preview banner (only while preview is live) ----
        Rectangle {
          visible: isPreviewActive
          Layout.fillWidth: true
          implicitHeight: 26
          color: "#bf360c"
          radius: 4

          Label {
            anchors.centerIn: parent
            text: "MODEL PREVIEW ACTIVE  •  SIMULATION PAUSED"
            color: "white"; font.bold: true; font.pixelSize: 12
          }
        }

        // ---- Section 2: status row ----
        Rectangle {
          Layout.fillWidth: true
          implicitHeight: statusRow.implicitHeight + 10
          color: "#eeeeee"
          radius: 4

          RowLayout {
            id: statusRow
            anchors {
              left:  parent.left;  right: parent.right
              verticalCenter: parent.verticalCenter
              leftMargin: 8;  rightMargin: 8
            }
            spacing: 8

            Label { text: "Status"; font.pixelSize: 12; color: "#616161" }

            // Coloured pill
            Rectangle {
              radius: 3
              color: pillColor()
              implicitWidth:  pillLabel.implicitWidth  + 16
              implicitHeight: pillLabel.implicitHeight +  6

              Label {
                id: pillLabel
                anchors.centerIn: parent
                text:  prettyState()
                color: "white"; font.bold: true; font.pixelSize: 12
              }
            }

            BusyIndicator {
              running: isBusy; visible: isBusy
              width: 18; height: 18
            }

            Item { Layout.fillWidth: true }

            // Success message only in Done state
            Label {
              visible: isDone
              text: "Imported successfully"
              color: "#2e7d32"; font.bold: true; font.pixelSize: 12
            }

            // Preview entity name tag when preview is live
            Label {
              visible: isPreviewActive && previewCtrl.previewEntityName.length > 0
              text: previewCtrl.previewEntityName
              font.pixelSize: 11; font.italic: true; color: "#555"
              elide: Text.ElideRight
              Layout.maximumWidth: 160
            }
          }
        }

        // ---- Section 3: model file selection ----
        Rectangle {
          Layout.fillWidth: true
          implicitHeight: fileCol.implicitHeight + 16
          color: "#fafafa"
          border.color: "#e0e0e0"; border.width: 1
          radius: 4

          ColumnLayout {
            id: fileCol
            anchors {
              top: parent.top; left: parent.left; right: parent.right
              topMargin: 8; leftMargin: 8; rightMargin: 8
            }
            spacing: 4

            Label {
              text: "Model file"
              font.bold: true; font.pixelSize: 12
            }

            // File picker row: [name+path   |  BROWSE]
            RowLayout {
              Layout.fillWidth: true; spacing: 8

              // Two stacked labels: prominent basename + muted full path
              ColumnLayout {
                Layout.fillWidth: true; spacing: 1

                Label {
                  id: fileBaseName
                  text: {
                    var p = fileSelector.selectedPath
                    if (!p || p.length === 0) return "No file selected"
                    var parts = p.split("/")
                    return parts[parts.length - 1]
                  }
                  font.pixelSize: 13
                  font.bold: fileSelector.selectedPath.length > 0
                  elide: Text.ElideRight
                  Layout.fillWidth: true
                  color: fileSelector.selectedPath.length > 0 ? "#212121" : "#9e9e9e"

                  ToolTip.visible: fileSelector.selectedPath.length > 0 && fileHover.containsMouse
                  ToolTip.text:    fileSelector.selectedPath
                  ToolTip.delay:   500

                  MouseArea {
                    id: fileHover
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.NoButton
                  }
                }

                Label {
                  visible: fileSelector.selectedPath.length > 0
                  text:    fileSelector.selectedPath
                  font.pixelSize: 10; color: "#9e9e9e"
                  elide: Text.ElideMiddle
                  Layout.fillWidth: true
                }
              }

              Button {
                text: "Browse"
                font.pixelSize: 12
                enabled: !isBusy
                implicitWidth: 80
                onClicked: fileDialog.open()
              }
            }

            // Detected format badge
            RowLayout {
              visible: fileSelector.detectedFormat.length > 0
              spacing: 6
              Label { text: "Format:"; font.pixelSize: 11; color: "#616161" }
              Label {
                text:  fileSelector.detectedFormat
                font.pixelSize: 11; font.bold: true; color: "#333"
              }
            }

            // File-selection error
            Label {
              visible: fileSelector.lastError.length > 0
              text:    fileSelector.lastError
              color: "#c62828"; font.pixelSize: 11
              wrapMode: Text.Wrap; Layout.fillWidth: true
            }
          }
        }

        // ---- Section 4: actions ----
        RowLayout {
          Layout.fillWidth: true; spacing: 8
          visible: isImportable || isCancellable || isErrorState || isDone

          Button {
            text: "Import Model"
            highlighted: true
            font.pixelSize: 13
            visible: isImportable
            enabled: !isBusy
            implicitWidth: 120
            onClicked: {
              // Collapse stale log output before starting a new import.
              logsCard.logsExpanded = false
              backend.importRobot()
            }
          }

          Item { Layout.fillWidth: true }

          // "Cancel Import" — only while an operation is actively cancellable.
          Button {
            text: "Cancel Import"
            font.pixelSize: 12
            visible: isCancellable
            implicitWidth: 104
            onClicked: {
              logsCard.logsExpanded = false
              // Always do a full reset so the panel returns to the minimal
              // initial view (Idle).  reset() removes any live preview entity
              // before transitioning, so the scene stays clean.
              backend.reset()
            }
          }

          // "Import another" after a successful import
          Button {
            text: "Import another"
            font.pixelSize: 12
            visible: isDone
            implicitWidth: 110
            onClicked: backend.reset()
          }

          // "Reset" in terminal-error states (SpawnFailed, ExpansionFailed, …)
          Button {
            text: "Reset"
            font.pixelSize: 12
            visible: isErrorState
            implicitWidth: 66
            onClicked: backend.reset()
          }
        }

        // ---- Section 5: import options (collapsed after Done or error) ----
        Rectangle {
          id: optionsCard
          visible: showOptions
          Layout.fillWidth: true
          implicitHeight: optionsCol.implicitHeight + 16
          color: "#fafafa"
          border.color: "#e0e0e0"; border.width: 1
          radius: 4

          property bool poseExpanded: true

          ColumnLayout {
            id: optionsCol
            anchors {
              top: parent.top; left: parent.left; right: parent.right
              topMargin: 8; leftMargin: 8; rightMargin: 8
            }
            spacing: 4

            Label { text: "Import options"; font.bold: true; font.pixelSize: 12 }

            // Identity fields
            GridLayout {
              columns: 2; columnSpacing: 8; rowSpacing: 4
              Layout.fillWidth: true

              Label { text: "Name";       font.pixelSize: 12; color: "#555" }
              TextField {
                text: importOptions.instanceName
                font.pixelSize: 12; Layout.fillWidth: true
                onEditingFinished: importOptions.instanceName = text
              }

              Label { text: "Namespace";  font.pixelSize: 12; color: "#555" }
              TextField {
                text: importOptions.rosNamespace
                placeholderText: "(none)"
                font.pixelSize: 12; Layout.fillWidth: true
                onEditingFinished: importOptions.rosNamespace = text
              }
            }

            // ---- Pose toggle ----
            Item {
              Layout.fillWidth: true
              implicitHeight: poseToggleLabel.implicitHeight + 4

              Label {
                id: poseToggleLabel
                anchors.verticalCenter: parent.verticalCenter
                text: (optionsCard.poseExpanded ? "▼" : "▶") + "  Pose (m / rad)"
                font.pixelSize: 12; color: "#555"
              }

              MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: optionsCard.poseExpanded = !optionsCard.poseExpanded
              }
            }

            // ---- Position X Y Z ----
            RowLayout {
              visible: optionsCard.poseExpanded
              Layout.fillWidth: true; spacing: 4

              Label { text: "X"; font.pixelSize: 12; color: "#555"; Layout.preferredWidth: 16 }
              PoseField {
                Layout.fillWidth: true
                modelValue: importOptions.poseX
                onEditingFinished: importOptions.poseX = parseFloat(text)
              }
              Label { text: "Y"; font.pixelSize: 12; color: "#555"; Layout.preferredWidth: 16 }
              PoseField {
                Layout.fillWidth: true
                modelValue: importOptions.poseY
                onEditingFinished: importOptions.poseY = parseFloat(text)
              }
              Label { text: "Z"; font.pixelSize: 12; color: "#555"; Layout.preferredWidth: 16 }
              PoseField {
                Layout.fillWidth: true
                modelValue: importOptions.poseZ
                onEditingFinished: importOptions.poseZ = parseFloat(text)
              }
            }

            // ---- Orientation Roll Pitch Yaw ----
            RowLayout {
              visible: optionsCard.poseExpanded
              Layout.fillWidth: true; spacing: 4

              Label { text: "R"; font.pixelSize: 12; color: "#555"; Layout.preferredWidth: 16 }
              PoseField {
                Layout.fillWidth: true
                modelValue: importOptions.poseRoll
                onEditingFinished: importOptions.poseRoll = parseFloat(text)
              }
              Label { text: "P"; font.pixelSize: 12; color: "#555"; Layout.preferredWidth: 16 }
              PoseField {
                Layout.fillWidth: true
                modelValue: importOptions.posePitch
                onEditingFinished: importOptions.posePitch = parseFloat(text)
              }
              Label { text: "Y"; font.pixelSize: 12; color: "#555"; Layout.preferredWidth: 16 }
              PoseField {
                Layout.fillWidth: true
                modelValue: importOptions.poseYaw
                onEditingFinished: importOptions.poseYaw = parseFloat(text)
              }
            }

            // ---- Highlight mode (only while preview entity is live) ----
            RowLayout {
              visible: isPreviewActive
              Layout.fillWidth: true; spacing: 8

              Label { text: "Highlight:"; font.pixelSize: 12; color: "#555" }

              ComboBox {
                id: highlightCombo
                font.pixelSize: 12
                Layout.preferredWidth: 130
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

              // TODO: Transparent mode may not render correctly — Material::Clone()
              // in gz-rendering8 does not fully preserve Ogre2 PBR shaders.
              // Wireframe is the reliable default.
              Label {
                visible: importerPlugin.highlightMode === 1
                text: "(Transparent may not render correctly)"
                font.pixelSize: 10; font.italic: true; color: "#e65100"
                wrapMode: Text.Wrap; Layout.fillWidth: true
              }
            }
          }
        }

        // ---- Section 6: namespace / injection caveat ----
        Rectangle {
          visible: showOptions
          Layout.fillWidth: true
          implicitHeight: noteText.implicitHeight + 14
          color: "#fff8e1"
          border.color: "#ffe082"; border.width: 1
          radius: 4

          Label {
            id: noteText
            anchors {
              top: parent.top; left: parent.left; right: parent.right
              topMargin: 7; leftMargin: 8; rightMargin: 8
            }
            text: "Namespace injection applies only to recognised ROS plugin elements. " +
                  "Hardcoded topics and plugin-internal names are left untouched."
            font.pixelSize: 10; font.italic: true
            wrapMode: Text.Wrap
            color: "#5d4037"
          }
        }

        // ---- Section 7: ROS 2 bridge hint (informational only) ----
        Rectangle {
          id: runtimeHintCard
          visible: backend.hasRuntimeHint
          Layout.fillWidth: true
          implicitHeight: hintCol.implicitHeight + 16
          color: "#fff8e1"
          border.color: "#ffb300"
          border.width: 1
          radius: 4

          property bool detailsExpanded: false

          ColumnLayout {
            id: hintCol
            anchors {
              top: parent.top; left: parent.left; right: parent.right
              topMargin: 8; leftMargin: 8; rightMargin: 8
            }
            spacing: 4

            Label {
              text: "ROS 2 bridge hint"
              font.bold: true; font.pixelSize: 12; color: "#e65100"
            }

            Label {
              text: backend.runtimeHint
              font.pixelSize: 11; color: "#4e342e"
              wrapMode: Text.Wrap; Layout.fillWidth: true
            }

            // Details toggle
            Item {
              Layout.fillWidth: true
              implicitHeight: hintDetailsToggle.implicitHeight + 2
              visible: backend.runtimeHintDetails.length > 0

              Label {
                id: hintDetailsToggle
                anchors.verticalCenter: parent.verticalCenter
                text: (runtimeHintCard.detailsExpanded ? "▼" : "▶") + "  Detected items"
                font.pixelSize: 11; color: "#555"
              }
              MouseArea {
                anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                onClicked: runtimeHintCard.detailsExpanded = !runtimeHintCard.detailsExpanded
              }
            }

            Rectangle {
              visible: runtimeHintCard.detailsExpanded && backend.runtimeHintDetails.length > 0
              Layout.fillWidth: true
              implicitHeight: hintDetailsLabel.implicitHeight + 8
              color: "#fff3e0"; radius: 3; border.color: "#ffe082"; border.width: 1

              Label {
                id: hintDetailsLabel
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 4 }
                text: backend.runtimeHintDetails
                font.pixelSize: 10; font.family: "monospace"
                color: "#4e342e"; wrapMode: Text.Wrap
              }
            }

            Label {
              text: "Load the 'ROS 2 Bridge Manager' Gazebo GUI plugin to inspect topics and create bridges."
              font.pixelSize: 10; font.italic: true; color: "#795548"
              wrapMode: Text.Wrap; Layout.fillWidth: true
            }
          }
        }

        // ---- Section 8: collapsible details / log output ----
        Rectangle {
          id: logsCard
          visible: hasLogs
          Layout.fillWidth: true
          implicitHeight: logsToggleRow.implicitHeight +
                          (logsExpanded ? logsContent.implicitHeight + 8 : 0) +
                          14
          color: "#fafafa"
          border.color: isErrorState ? "#ef9a9a" : "#e0e0e0"
          border.width: 1
          radius: 4

          property bool logsExpanded: false

          // Toggle row (always visible inside the card)
          Item {
            id: logsToggleRow
            anchors { top: parent.top; left: parent.left; right: parent.right; topMargin: 6; leftMargin: 8; rightMargin: 8 }
            implicitHeight: logsToggleLabel.implicitHeight + 4

            RowLayout {
              anchors.fill: parent
              spacing: 6

              Label {
                id: logsToggleLabel
                text: (logsCard.logsExpanded ? "▼" : "▶") + "  Details / Log output"
                font.pixelSize: 12; color: "#555"
              }

              // Small error indicator dot
              Rectangle {
                visible: isErrorState && backend.lastError.length > 0
                width: 8; height: 8; radius: 4
                color: "#c62828"
              }

              Item { Layout.fillWidth: true }
            }

            MouseArea {
              anchors.fill: parent
              cursorShape: Qt.PointingHandCursor
              onClicked: logsCard.logsExpanded = !logsCard.logsExpanded
            }
          }

          // Collapsible log content
          ColumnLayout {
            id: logsContent
            visible: logsCard.logsExpanded
            anchors {
              top: logsToggleRow.bottom; left: parent.left; right: parent.right
              topMargin: 4; leftMargin: 8; rightMargin: 8
            }
            spacing: 4

            Label {
              visible: backend.lastWarning.length > 0
              text: "(!) " + backend.lastWarning
              color: "#e65100"; font.pixelSize: 11
              wrapMode: Text.Wrap; Layout.fillWidth: true
            }

            Label {
              visible: backend.lastError.length > 0
              text: backend.lastError
              color: "#c62828"; font.pixelSize: 11
              wrapMode: Text.Wrap; Layout.fillWidth: true
            }

            Rectangle {
              visible: backend.preflightReport.length > 0
              Layout.fillWidth: true
              implicitHeight: preflightLabel.implicitHeight + 10
              color: backend.preflightReport.indexOf("unresolved") >= 0 ||
                     backend.preflightReport.indexOf("Ogre") >= 0
                     ? "#FFF3E0" : "#F1F8E9"
              border.color: backend.preflightReport.indexOf("unresolved") >= 0 ||
                            backend.preflightReport.indexOf("Ogre") >= 0
                            ? "#e65100" : "#558b2f"
              border.width: 1; radius: 3

              Label {
                id: preflightLabel
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 5 }
                text: backend.preflightReport
                font.pixelSize: 10; font.family: "monospace"
                color: "#333"; wrapMode: Text.Wrap
              }
            }
          }
        }

        // Bottom padding
        Item { implicitHeight: 4 }
      }
    }
  }
}
