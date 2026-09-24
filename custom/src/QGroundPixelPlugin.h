#pragma once

#include "QGCCorePlugin.h"

class QQmlApplicationEngine;
class WfbngManager;

Q_DECLARE_LOGGING_CATEGORY(QGroundPixelLog)

/// QGroundControl core plugin for the QGroundPixel build: stock QGC plus
/// native WFB-NG video reception. Overrides video setting defaults so the
/// wfb-ng RTP stream on udp://127.0.0.1:5600 plays out of the box (H.265,
/// low latency, software decoder — the amcviddec hardware path renders broken
/// video on the target device), and registers the WfbngManager QML singleton
/// used by the WFB-NG settings page.
///
/// Optionally (WFB-NG settings page, "native decoder") the GStreamer pipeline is
/// replaced by VideonativeReceiver: PixelPilot's MediaCodec decoder fed straight
/// from UDP 5600, for low-latency hardware decoding on the AX12.
class QGroundPixelPlugin : public QGCCorePlugin
{
    Q_OBJECT

public:
    explicit QGroundPixelPlugin(QObject *parent = nullptr);

    static QGCCorePlugin *instance();

    // Overrides from QGCCorePlugin
    void adjustSettingMetaData(const QString &settingsGroup, FactMetaData &metaData, bool &userVisible) final;
    QQmlApplicationEngine *createQmlApplicationEngine(QObject *parent) final;
    VideoReceiver *createVideoReceiver(QObject *parent) final;
    /// Applies the per-codec decoder preference right before the sink is built, so the
    /// sink's GPU/CPU mode always matches the decoder that will feed it.
    void *createVideoSink(QQuickItem *widget, QObject *parent) final;
};
