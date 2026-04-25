import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

// Thin status bar: shows state name, spinner when busy, errors and warnings.
// Bound to the `backend` context property.
RowLayout {
  id: root
  spacing: 8

  BusyIndicator {
    running: backend && backend.busy
    width: 18; height: 18
  }

  Label {
    text: backend ? backend.stateName : "—"
    font.bold: true
    Layout.fillWidth: true
  }

  Label {
    visible: backend && backend.lastWarning.length > 0
    text: "⚠️ " + (backend ? backend.lastWarning : "")
    color: "#e65100"
    wrapMode: Text.Wrap
    Layout.fillWidth: true
  }

  Label {
    visible: backend && backend.lastError.length > 0
    text: "❌ " + (backend ? backend.lastError : "")
    color: "#b71c1c"
    wrapMode: Text.Wrap
    Layout.fillWidth: true
  }
}
