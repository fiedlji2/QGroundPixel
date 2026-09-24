#pragma once

#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QUrl>

Q_DECLARE_LOGGING_CATEGORY(WfbngManagerLog)

/// Controls the embedded WFB-NG (wifibroadcast) receive stack.
///
/// The heavy lifting happens in the vendored wfbngrtl8812 AAR (userspace
/// RTL8812AU driver + wfb-ng RX). This manager owns the Java-side
/// QGCWfbManager instance over JNI, persists user configuration, and exposes
/// state to the WFB-NG settings page QML. The received video ends up as a
/// standard RTP stream on udp://127.0.0.1:5600 which QGC's video pipeline
/// consumes without knowing wfb-ng exists.
class WfbngManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool supported READ supported CONSTANT)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(int channel READ channel WRITE setChannel NOTIFY channelChanged)
    Q_PROPERTY(int bandwidth READ bandwidth WRITE setBandwidth NOTIFY bandwidthChanged)
    Q_PROPERTY(int txPower READ txPower WRITE setTxPower NOTIFY txPowerChanged)
    Q_PROPERTY(bool adaptiveLink READ adaptiveLink WRITE setAdaptiveLink NOTIFY adaptiveLinkChanged)
    Q_PROPERTY(int adapterCount READ adapterCount NOTIFY linkStatsChanged)
    Q_PROPERTY(int rssi READ rssi NOTIFY linkStatsChanged)
    Q_PROPERTY(int packetsOk READ packetsOk NOTIFY linkStatsChanged)
    Q_PROPERTY(int packetsRecovered READ packetsRecovered NOTIFY linkStatsChanged)
    Q_PROPERTY(int packetsLost READ packetsLost NOTIFY linkStatsChanged)
    Q_PROPERTY(QString keyStatus READ keyStatus NOTIFY keyStatusChanged)
    Q_PROPERTY(bool rtpCapture READ rtpCapture WRITE setRtpCapture NOTIFY rtpCaptureChanged)
    Q_PROPERTY(QString rtpCaptureDir READ rtpCaptureDir CONSTANT)
    Q_PROPERTY(bool nativeDecoder READ nativeDecoder WRITE setNativeDecoder NOTIFY nativeDecoderChanged)
    Q_PROPERTY(QString vtxUrl READ vtxUrl WRITE setVtxUrl NOTIFY vtxUrlChanged)
    Q_PROPERTY(bool tunnelEnabled READ tunnelEnabled WRITE setTunnelEnabled NOTIFY tunnelEnabledChanged)
    Q_PROPERTY(bool tunnelActive READ tunnelActive NOTIFY tunnelActiveChanged)
    Q_PROPERTY(QString tunnelStatus READ tunnelStatus NOTIFY tunnelActiveChanged)

public:
    explicit WfbngManager(QObject *parent = nullptr);

    static WfbngManager *instance();

    /// One-time startup: provision gs.key, create the Java manager, start
    /// listening for adapters. Safe to call multiple times.
    void init();

    bool supported() const;
    bool enabled() const { return _enabled; }
    int channel() const { return _channel; }
    int bandwidth() const { return _bandwidth; }
    int txPower() const { return _txPower; }
    bool adaptiveLink() const { return _adaptiveLink; }
    int adapterCount() const { return _adapterCount; }
    int rssi() const { return _rssi; }
    int packetsOk() const { return _packetsOk; }
    int packetsRecovered() const { return _packetsRecovered; }
    int packetsLost() const { return _packetsLost; }
    QString keyStatus() const { return _keyStatus; }

    /// Diagnostics: dump the raw RTP stream (as delivered by wfb-ng on UDP 5600) to
    /// rtpCaptureDir for offline link analysis. Toggling restarts the video pipeline.
    bool rtpCapture() const;
    void setRtpCapture(bool enabled);
    QString rtpCaptureDir() const;

    /// Use PixelPilot's MediaCodec decoder (VideonativeReceiver) instead of the
    /// GStreamer pipeline. Read once when the video receivers are created, so a
    /// change takes effect after an app restart.
    bool nativeDecoder() const;
    void setNativeDecoder(bool enabled);

    /// Last URL opened on the "VTX Web UI" page (Majestic / custom link-settings page on
    /// the VTX). Default is the wfb-ng tunnel address; the Ethernet address is a preset.
    QString vtxUrl() const;
    void setVtxUrl(const QString &url);

    /// wfb-ng IP tunnel (QGCWfbVpnService): TUN 10.5.0.3/24 over the wifibroadcast link,
    /// so the VTX at 10.5.0.10 is reachable from this device (web UI, adaptive link uplink).
    bool tunnelEnabled() const;
    void setTunnelEnabled(bool enabled);
    bool tunnelActive() const { return _tunnelActive; }
    QString tunnelStatus() const { return _tunnelStatus; }
    /// Start the tunnel now (asks for the one-time Android VPN consent if needed).
    Q_INVOKABLE void startTunnel();
    Q_INVOKABLE void stopTunnel();

    void setEnabled(bool enabled);
    void setChannel(int channel);
    void setBandwidth(int bandwidth);
    void setTxPower(int txPower);
    void setAdaptiveLink(bool adaptiveLink);

    /// Copy a user-picked gs.key (matching the drone's drone.key) over the
    /// active key and restart reception.
    Q_INVOKABLE void importGsKey(const QUrl &fileUrl);
    /// Restore the built-in OpenIPC default key and restart reception.
    Q_INVOKABLE void resetGsKey();
    /// Stop and restart reception on all adapters.
    Q_INVOKABLE void restart();

    // Called from the JNI callback trampolines (queued onto the main thread).
    void updateAdapterCount(int count);
    void updateStats(int rssi, int ok, int recovered, int lost);

signals:
    void enabledChanged();
    void channelChanged();
    void bandwidthChanged();
    void txPowerChanged();
    void adaptiveLinkChanged();
    void linkStatsChanged();
    void keyStatusChanged();
    void rtpCaptureChanged();
    void nativeDecoderChanged();
    void vtxUrlChanged();
    void tunnelEnabledChanged();
    void tunnelActiveChanged();

private:
    QString _gsKeyPath() const;
    bool _provisionDefaultKey(bool overwrite);
    void _applyKeyStatus();
    void _refreshTunnelState(const QString &statusOverride = QString());

    bool _tunnelActive = false;
    QString _tunnelStatus;

    bool _initialized = false;
    bool _enabled = true;
    int _channel = 161;
    int _bandwidth = 20;
    int _txPower = 20;
    bool _adaptiveLink = false;
    int _adapterCount = 0;
    int _rssi = 0;
    int _packetsOk = 0;
    int _packetsRecovered = 0;
    int _packetsLost = 0;
    QString _keyStatus;
};
