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

                QGCLabel { text: qsTr("H.264 decoder") }
                QGCComboBox {
                    Layout.preferredWidth: _comboWidth
                    model: [qsTr("Software (libav)"), qsTr("Hardware (MediaCodec)"), qsTr("Follow Video settings")]
                    currentIndex: WfbngManager.decoderH264 === 1 ? 0 : (WfbngManager.decoderH264 === 8 ? 1 : 2)
                    onActivated: (index) => WfbngManager.decoderH264 = (index === 0 ? 1 : (index === 1 ? 8 : -1))
                }

                QGCLabel { text: qsTr("H.265 decoder") }
                QGCComboBox {
                    Layout.preferredWidth: _comboWidth
                    model: [qsTr("Software (libav)"), qsTr("Hardware (MediaCodec)"), qsTr("Follow Video settings")]
                    currentIndex: WfbngManager.decoderH265 === 1 ? 0 : (WfbngManager.decoderH265 === 8 ? 1 : 2)
                    onActivated: (index) => WfbngManager.decoderH265 = (index === 0 ? 1 : (index === 1 ? 8 : -1))
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
                text:       qsTr("VTX tunnel")
                font.bold:  true
            }

            GridLayout {
                columns:        2
                columnSpacing:  _margins
                rowSpacing:     ScreenTools.defaultFontPixelHeight / 2

                QGCLabel { text: qsTr("IP tunnel over wfb-ng (10.5.0.0/24)") }
                QGCCheckBox {
                    checked:    WfbngManager.tunnelEnabled
                    onClicked:  WfbngManager.tunnelEnabled = checked
                }

                QGCLabel { text: qsTr("Status") }
                QGCLabel { text: WfbngManager.tunnelStatus; color: WfbngManager.tunnelActive ? qgcPal.colorGreen : qgcPal.text }

                QGCLabel { text: qsTr("VTX address") }
                QGCTextField {
                    Layout.preferredWidth: _comboWidth
                    text:               WfbngManager.vtxHost
                    inputMethodHints:   Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                    onEditingFinished:  WfbngManager.vtxHost = text
                }

                QGCLabel { text: qsTr("VTX web login") }
                RowLayout {
                    spacing: _margins / 2
                    QGCTextField {
                        Layout.preferredWidth: _comboWidth / 2
                        text:               WfbngManager.vtxUser
                        placeholderText:    qsTr("user")
                        inputMethodHints:   Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                        onEditingFinished:  WfbngManager.vtxUser = text
                    }
                    QGCTextField {
                        Layout.preferredWidth: _comboWidth / 2
                        text:               WfbngManager.vtxPassword
                        placeholderText:    qsTr("password")
                        echoMode:           TextInput.Password
                        inputMethodHints:   Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText | Qt.ImhSensitiveData
                        onEditingFinished:  WfbngManager.vtxPassword = text
                    }
                }
            }

            QGCLabel {
                Layout.maximumWidth: ScreenTools.defaultFontPixelWidth * 60
                wrapMode:   Text.WordWrap
                color:      qgcPal.colorGrey
                text:       qsTr("Makes the VTX reachable at its tunnel address from this device (VTX settings page, adaptive-link uplink) through the wifibroadcast link, like PixelPilot. Android asks once for VPN permission; the tunnel is local only and routes nothing else. The login is the VTX web page's HTTP login (OpenIPC default root / 12345).")
            }

            QGCLabel {
                text:       qsTr("Diagnostics")
                font.bold:  true
            }

            GridLayout {
                columns:        2
                columnSpacing:  _margins
                rowSpacing:     ScreenTools.defaultFontPixelHeight / 2

                QGCLabel { text: qsTr("Capture raw RTP stream to file") }
                QGCCheckBox {
                    checked:    WfbngManager.rtpCapture
                    onClicked:  WfbngManager.rtpCapture = checked
                }

                QGCLabel { text: qsTr("Native MediaCodec decoder (experimental)") }
                QGCCheckBox {
                    checked:    WfbngManager.nativeDecoder
                    onClicked:  WfbngManager.nativeDecoder = checked
                }
            }

            QGCLabel {
                Layout.maximumWidth: ScreenTools.defaultFontPixelWidth * 60
                wrapMode:   Text.WordWrap
                color:      qgcPal.colorGrey
                text:       qsTr("Native decoder: PixelPilot's hardware (MediaCodec) decode path instead of GStreamer — low latency, no recording/screenshots. Restart the app after changing.")
            }

            QGCLabel {
                Layout.maximumWidth: ScreenTools.defaultFontPixelWidth * 60
                wrapMode:   Text.WordWrap
                color:      qgcPal.colorGrey
                text:       qsTr("While enabled, every video packet is written (with a timestamp) to a new file under:\n%1\nReproduce a video dropout, then turn it off — captures grow about 1 MB per second.").arg(WfbngManager.rtpCaptureDir)
            }

            QGCLabel {
                Layout.maximumWidth: ScreenTools.defaultFontPixelWidth * 60
                wrapMode:   Text.WordWrap
                color:      qgcPal.colorGrey
                text:       qsTr("The codec must match the drone camera encoder (majestic.yaml); a mismatch shows no video. The decoder is chosen per codec: software keeps latency low for H.264 sources such as the XFRobot Z2, while 720p60 H.265 from OpenIPC needs the hardware decoder (software cannot keep up and the picture goes gray every keyframe). Changing the codec applies the matching decoder automatically; if the picture looks wrong after a change, restart the app.")
            }
        }
    }
}
