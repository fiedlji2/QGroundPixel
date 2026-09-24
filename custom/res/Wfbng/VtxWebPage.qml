import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtWebView

import QGroundControl
import QGroundControl.Controls

import QGroundPixel

/// "VTX settings": an embedded browser window for the configuration page served BY THE VTX
/// itself (OpenIPC Majestic web UI or a custom busybox-httpd page with channel / TX power /
/// camera-URL fields). Nothing is hosted in the app — it only provides the address bar, the
/// presets and the tunnel. Reachable over the wfb-ng tunnel (10.5.0.10) or Ethernet
/// (192.168.144.20), whichever path this device currently has to the VTX.
Rectangle {
    id:             page
    objectName:     "settingsPage_VtxSettings"
    color:          qgcPal.window
    anchors.fill:   parent

    readonly property real   _margins:      ScreenTools.defaultFontPixelHeight / 2
    readonly property string _tunnelUrl:    "http://10.5.0.10"
    readonly property string _ethernetUrl:  "http://192.168.144.20"

    property string _lastError: ""

    QGCPalette { id: qgcPal; colorGroupEnabled: true }

    function go(text) {
        var url = text.trim()
        if (url.length === 0) {
            return
        }
        if (url.indexOf("://") < 0) {
            url = "http://" + url
        }
        _lastError = ""
        WfbngManager.vtxUrl = url
        urlField.text = url
        webView.url = url
    }

    ColumnLayout {
        anchors.fill:       parent
        anchors.margins:    _margins
        spacing:            _margins / 2

        RowLayout {
            Layout.fillWidth:   true
            spacing:            _margins / 2

            QGCTextField {
                id:                 urlField
                Layout.fillWidth:   true
                text:               WfbngManager.vtxUrl
                inputMethodHints:   Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                onAccepted:         page.go(text)
            }

            QGCButton {
                text:       qsTr("Go")
                onClicked:  page.go(urlField.text)
            }

            QGCButton {
                text:       qsTr("Reload")
                enabled:    webView.url.toString().length > 0
                onClicked:  { page._lastError = ""; webView.reload() }
            }

            QGCButton {
                text:       qsTr("Browser")
                onClicked:  Qt.openUrlExternally(urlField.text.trim())
            }
        }

        RowLayout {
            Layout.fillWidth:   true
            spacing:            _margins / 2

            QGCLabel { text: qsTr("Presets:") }

            QGCButton {
                text:       qsTr("Tunnel (10.5.0.10)")
                onClicked:  page.go(page._tunnelUrl)
            }

            QGCButton {
                text:       qsTr("Ethernet (192.168.144.20)")
                onClicked:  page.go(page._ethernetUrl)
            }

            QGCLabel {
                Layout.fillWidth:   true
                elide:              Text.ElideRight
                color:              page._lastError.length ? qgcPal.warningText : qgcPal.text
                text:               page._lastError.length
                                    ? qsTr("Cannot load page: %1").arg(page._lastError)
                                    : (webView.loading ? qsTr("Loading… %1%").arg(webView.loadProgress) : webView.title)
            }
        }

        RowLayout {
            Layout.fillWidth:   true
            spacing:            _margins / 2

            QGCLabel {
                text:   qsTr("wfb-ng tunnel: %1").arg(WfbngManager.tunnelStatus)
                color:  WfbngManager.tunnelActive ? qgcPal.colorGreen : qgcPal.colorGrey
            }

            QGCButton {
                text:       WfbngManager.tunnelEnabled ? qsTr("Reconnect tunnel") : qsTr("Enable tunnel")
                visible:    !WfbngManager.tunnelActive
                onClicked:  {
                    if (!WfbngManager.tunnelEnabled) {
                        WfbngManager.tunnelEnabled = true
                    } else {
                        WfbngManager.startTunnel()
                    }
                }
            }
        }

        WebView {
            id:                 webView
            Layout.fillWidth:   true
            Layout.fillHeight:  true
            url:                WfbngManager.vtxUrl

            onLoadingChanged: (loadRequest) => {
                if (loadRequest.status === WebView.LoadFailedStatus) {
                    page._lastError = loadRequest.errorString
                } else if (loadRequest.status === WebView.LoadSucceededStatus) {
                    page._lastError = ""
                }
            }
        }
    }
}
