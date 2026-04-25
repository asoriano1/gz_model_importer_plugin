import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Dialogs 1.3
import QtQuick.Layouts 1.3

// File selection panel: FileDialog trigger, selected path, detected format.
// Bound to `fileSelector` context property (FileSelector QObject).
ColumnLayout {
  id: root
  spacing: 6

  Label {
    text: "Robot file"
    font.bold: true
  }

  RowLayout {
    spacing: 6
    Layout.fillWidth: true

    TextField {
      id: pathField
      readOnly: true
      placeholderText: "No file selected"
      text: fileSelector ? fileSelector.selectedPath : ""
      Layout.fillWidth: true
    }

    Button {
      text: "Browse…"
      enabled: !backend.busy
      onClicked: fileDialog.open()
    }
  }

  RowLayout {
    visible: fileSelector && fileSelector.detectedFormat.length > 0
    spacing: 4
    Label { text: "Format:" }
    Label {
      text: fileSelector ? fileSelector.detectedFormat : ""
      font.bold: true
    }
  }

  Label {
    visible: fileSelector && fileSelector.lastError.length > 0
    text: fileSelector ? fileSelector.lastError : ""
    color: "#b71c1c"
    wrapMode: Text.Wrap
    Layout.fillWidth: true
  }

  FileDialog {
    id: fileDialog
    title: "Select robot file"
    folder: shortcuts.home
    nameFilters: [
      "Robot description (*.urdf *.xacro *.sdf *.xml)",
      "URDF (*.urdf *.xml)",
      "XACRO (*.xacro)",
      "SDF (*.sdf)",
      "All files (*)"
    ]
    onAccepted: fileSelector.onFileChosen(fileDialog.fileUrl.toString())
  }
}
