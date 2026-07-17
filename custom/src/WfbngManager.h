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

private:
    QString _gsKeyPath() const;
    bool _provisionDefaultKey(bool overwrite);
    void _applyKeyStatus();

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
