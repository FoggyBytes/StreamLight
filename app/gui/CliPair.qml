import QtQuick 2.0
import QtQuick.Controls 2.2

import ComputerManager 1.0

Item {
    function onSearchingComputer() {
        stageLabel.text = qsTr("Establishing connection to PC...")
    }

    function onPairing(pcName, pin) {
        stageLabel.text = qsTr("Pairing... Please enter '%1' on %2.").arg(pin).arg(pcName)
    }

    function onFailed(message) {
        stageIndicator.visible = false
        errorDialog.text = message
        errorDialog.open()
    }

    function onSuccess(appName) {
        stageIndicator.visible = false
        pairCompleteDialog.open()
    }

    // Allow user to back out of pairing
    Keys.onEscapePressed: {
        Qt.quit()
    }
    Keys.onBackPressed: {
        Qt.quit()
    }
    Keys.onCancelPressed: {
        Qt.quit()
    }

    StackView.onActivated: {
        if (!launcher.isExecuted()) {
            // (toolbar removed in 3.0 redesign)

            launcher.searchingComputer.connect(onSearchingComputer)
            launcher.pairing.connect(onPairing)
            launcher.failed.connect(onFailed)
            launcher.success.connect(onSuccess)
            launcher.execute(ComputerManager)
        }
    }

    Row {
        anchors.centerIn: parent
        spacing: Math.round(stageLabel.font.pixelSize * 0.45)
        id: stageIndicator

        Spinner {
            id: stageSpinner
            bodySize: stageLabel.font.pixelSize
            running: visible
        }

        Label {
            id: stageLabel
            height: stageSpinner.height
            font.pointSize: 20
            verticalAlignment: Text.AlignVCenter

            wrapMode: Text.Wrap
        }
    }

    ErrorMessageDialog {
        id: errorDialog

        onClosed: {
            Qt.quit();
        }
    }

    NavigableMessageDialog {
        id: pairCompleteDialog
        closePolicy: Popup.CloseOnEscape

        text:qsTr("Pairing completed successfully")
        standardButtons: Dialog.Ok
        onClosed: {
            Qt.quit()
        }
    }
}
