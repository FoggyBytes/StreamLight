import QtQuick 2.0
import QtQuick.Controls 2.2

import ComputerManager 1.0
import Session 1.0

Item {
    function onSearchingComputer() {
        stageLabel.text = qsTr("Establishing connection to PC...")
    }

    function onQuittingApp() {
        stageLabel.text = qsTr("Quitting app...")
    }

    function onFailure(message) {
        errorDialog.text = message
        errorDialog.open()
    }

    StackView.onActivated: {
        if (!launcher.isExecuted()) {
            // (toolbar removed in 3.0 redesign)
            launcher.searchingComputer.connect(onSearchingComputer)
            launcher.quittingApp.connect(onQuittingApp)
            launcher.failed.connect(onFailure)
            launcher.execute(ComputerManager)
        }
    }

    Row {
        anchors.centerIn: parent
        spacing: Math.round(stageLabel.font.pixelSize * 0.45)

        Spinner {
            id: stageSpinner
            bodySize: stageLabel.font.pixelSize
            running: visible
        }

        Label {
            id: stageLabel
            height: stageSpinner.height
            text: stageText
            font.pointSize: 20
            verticalAlignment: Text.AlignVCenter

            wrapMode: Text.Wrap
        }
    }

    ErrorMessageDialog {
        id: errorDialog

        onClosed: {
            Qt.quit()
        }
    }
}
