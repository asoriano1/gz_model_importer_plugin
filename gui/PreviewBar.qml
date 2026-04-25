import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

// Preview and action bar. Wired to `backend` and `previewCtrl`.
RowLayout {
  id: root
  spacing: 8

  // ---- Preview button (shown when Ready or Configuring) ----
  Button {
    text: previewCtrl && previewCtrl.previewing ? "Despawn preview" : "Preview"
    visible: {
      if (!backend) return false
      const s = backend.stateName
      return s === "Ready" || s === "Configuring" ||
             s === "PreviewFailed" || s === "Previewing"
    }
    enabled: !backend.busy
    onClicked: {
      if (previewCtrl && previewCtrl.previewing)
        backend.cancelPreview()
      else
        backend.requestPreview()
    }
  }

  Label {
    visible: previewCtrl && previewCtrl.previewing
    text: "Preview: " + (previewCtrl ? previewCtrl.previewEntityName : "")
    font.italic: true
    Layout.fillWidth: true
  }

  Item { Layout.fillWidth: true }

  // ---- Import / Cancel ----
  Button {
    text: "Import"
    visible: {
      if (!backend) return false
      const s = backend.stateName
      return s === "Ready" || s === "Configuring" || s === "PreviewFailed"
    }
    enabled: !backend.busy
    highlighted: true
    onClicked: backend.importRobot()
  }

  Button {
    text: "Reset"
    onClicked: backend.reset()
  }
}
