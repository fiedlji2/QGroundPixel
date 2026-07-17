import QtQuick
import QtQuick.Layouts
import QtQuick.Dialogs

import QGroundControl
import QGroundControl.Controls

import QGroundPixel

Rectangle {
    objectName:     "settingsPage_WfbngVideo"
    color:          qgcPal.window
    anchors.fill:   parent

    readonly property real _margins: ScreenTools.defaultFontPixelHeight
    readonly property real _comboWidth: ScreenTools.defaultFontPixelWidth * 20
    readonly property var _videoSettings: QGroundControl.settingsManager.videoSettings

    QGCPalette { id: qgcPal; colorGroupEnabled: true }

    FileDialog {
        id:             keyFileDialog
        title:          qsTr("Select gs.key")
        nameFilters:    [qsTr("Key files (*.key)"), qsTr("All files (*)")]
        onAccepted:     WfbngManager.importGsKey(selectedFile)
    }

    QGCFlickable {
        anchors.margins:    _margins
        anchors.fill:       parent
        contentWidth:       column.width
        contentHeight:      column.height
        clip:               true

        ColumnLayout {
            id:         column
            spacing:    _margins

            QGCLabel {
                text:       qsTr("WFB-NG Video Receiver")
                font.bold:  true
            }

            QGCLabel {
                Layout.maximumWidth: ScreenTools.defaultFontPixelWidth * 60
                wrapMode:   Text.WordWrap
                text:       WfbngManager.supported
                            ? qsTr("Receives OpenIPC wifibroadcast video from an RTL8812AU USB adapter and feeds it to the QGC video view (UDP 5600).")
                            : qsTr("WFB-NG reception is only available on Android.")
            }

            GridLayout {
                columns:        2
                columnSpacing:  _margins
                rowSpacing:     ScreenTools.defaultFontPixelHeight / 2

                QGCLabel { text: qsTr("Receiver enabled") }
                QGCCheckBox {
                    checked:    WfbngManager.enabled
                    onClicked:  WfbngManager.enabled = checked
                }

                QGCLabel { text: qsTr("Video codec") }
                QGCComboBox {
                    Layout.preferredWidth: _comboWidth
                    model: [qsTr("H.265 (OpenIPC default)"), qsTr("H.264")]
                    currentIndex: _videoSettings.videoSource.rawValue === _videoSettings.udp264VideoSource ? 1 : 0
                    onActivated: (index) => {
                        _videoSettings.videoSource.rawValue =
                            (index === 1) ? _videoSettings.udp264VideoSource : _videoSettings.udp265VideoSource
                    }
                }

                QGCLabel { text: qsTr("WiFi channel") }
                QGCComboBox {
                    id: channelCombo
                    Layout.preferredWidth: _comboWidth
                    model: [1,2,3,4,5,6,7,8,9,10,11,12,13,
                            32,36,40,44,48,52,56,60,64,68,96,100,104,108,112,116,120,
                            124,128,132,136,140,144,149,153,157,161,165,169,173,177]
                    currentIndex: {
                        var idx = model.indexOf(WfbngManager.channel)
                        return idx >= 0 ? idx : model.indexOf(161)
                    }
                    onActivated: (index) => WfbngManager.channel = model[index]
                }

                QGCLabel { text: qsTr("Channel width") }
                QGCComboBox {
                    Layout.preferredWidth: _comboWidth
                    model: [qsTr("20 MHz"), qsTr("40 MHz (experimental)")]
                    currentIndex: WfbngManager.bandwidth === 40 ? 1 : 0
                    onActivated: (index) => WfbngManager.bandwidth = (index === 1) ? 40 : 20
                }

                QGCLabel { text: qsTr("TX power (adaptive link)") }
                QGCComboBox {
                    Layout.preferredWidth: _comboWidth
                    model: [1, 10, 20, 30, 40]
                    currentIndex: {
                        var idx = model.indexOf(WfbngManager.txPower)
                        return idx >= 0 ? idx : 2
                    }
                    onActivated: (index) => WfbngManager.txPower = model[index]
                }

                QGCLabel { text: qsTr("Adaptive link") }
                QGCCheckBox {
                    checked:    WfbngManager.adaptiveLink
                    onClicked:  WfbngManager.adaptiveLink = checked
                }
            }

            QGCLabel {
                text:       qsTr("Encryption key")
                font.bold:  true
            }

            RowLayout {
                spacing: _margins / 2

                QGCLabel { text: WfbngManager.keyStatus }
                QGCButton {
                    text:       qsTr("Import gs.key…")
                    onClicked:  keyFileDialog.open()
                }
                QGCButton {
                    text:       qsTr("Reset to default")
                    onClicked:  WfbngManager.resetGsKey()
                }
            }

            QGCLabel {
                text:       qsTr("Link status")
                font.bold:  true
            }

            GridLayout {
                columns:        2
                columnSpacing:  _margins

                QGCLabel { text: qsTr("Adapters") }
                QGCLabel { text: WfbngManager.adapterCount }

                QGCLabel { text: qsTr("RSSI") }
                QGCLabel { text: WfbngManager.adapterCount > 0 ? WfbngManager.rssi + " dBm" : "—" }

                QGCLabel { text: qsTr("Packets OK / recovered / lost") }
                QGCLabel { text: WfbngManager.packetsOk + " / " + WfbngManager.packetsRecovered + " / " + WfbngManager.packetsLost }
            }

            QGCButton {
                text:       qsTr("Restart receiver")
                enabled:    WfbngManager.supported && WfbngManager.enabled
                onClicked:  WfbngManager.restart()
            }

            QGCLabel {
                Layout.maximumWidth: ScreenTools.defaultFontPixelWidth * 60
                wrapMode:   Text.WordWrap
                color:      qgcPal.colorGrey
                text:       qsTr("The codec above must match the drone camera encoder (majestic.yaml). A mismatch shows no video while the pipeline silently restarts every 3 seconds.")
            }
        }
    }
}
