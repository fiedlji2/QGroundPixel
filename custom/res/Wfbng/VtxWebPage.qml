import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtWebView

import QGroundControl
import QGroundControl.Controls

import QGroundPixel

/// "VTX settings": a browser window for the configuration page served BY THE VTX itself
/// (OpenIPC Majestic or a custom busybox-httpd page). Nothing is hosted in the app. The
/// page is reached through the wfb-ng tunnel via a local relay that supplies the VTX's HTTP
/// Basic login (Android's WebView cannot answer 401 challenges by itself). Address and login
/// are configured on the WFB-NG Video page.
Rectangle {
    id:             page
    objectName:     "settingsPage_VtxSettings"
    color:          qgcPal.window
    anchors.fill:   parent

    readonly property real _margins: ScreenTools.defaultFontPixelHeight / 2

    property string _lastError: ""

    QGCPalette { id: qgcPal; colorGroupEnabled: true }

    function load() {
        _lastError = ""
        var url = WfbngManager.startVtxProxy()
        if (url.length === 0) {
            _lastError = qsTr("could not start the local relay")
            return
        }
        webView.url = url
    }

    Component.onCompleted:  load()
    Component.onDestruction: WfbngManager.stopVtxProxy()

    ColumnLayout {
        anchors.fill:       parent
        anchors.margins:    _margins
        spacing:            _margins / 2

        RowLayout {
            Layout.fillWidth:   true
            spacing:            _margins / 2

            QGCButton {
                text:       qsTr("Reload")
                onClicked:  page.load()
            }

            QGCLabel {
                Layout.fillWidth:   true
                elide:              Text.ElideRight
                color:              page._lastError.length ? qgcPal.warningText
                                                           : (WfbngManager.tunnelActive ? qgcPal.text : qgcPal.warningText)
                text:               page._lastError.length
                                    ? qsTr("Cannot load VTX page (%1): %2").arg(WfbngManager.vtxHost).arg(page._lastError)
                                    : (!WfbngManager.tunnelActive
                                        ? qsTr("wfb-ng tunnel is not up (%1) — enable it on the WFB-NG Video page").arg(WfbngManager.tunnelStatus)
                                        : (webView.loading ? qsTr("Loading %1… %2%").arg(WfbngManager.vtxHost).arg(webView.loadProgress)
                                                           : qsTr("VTX %1 — %2").arg(WfbngManager.vtxHost).arg(webView.title)))
            }
        }

        WebView {
            id:                 webView
            Layout.fillWidth:   true
            Layout.fillHeight:  true

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
