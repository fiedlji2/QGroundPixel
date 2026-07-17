#include "QGroundPixelPlugin.h"

#include "QGCLoggingCategory.h"
#include "VideoSettings.h"
#include "WfbngManager.h"

#ifdef QGC_GST_STREAMING
#include "GStreamer.h"
#endif

#include <QtCore/QApplicationStatic>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/qqml.h>

QGC_LOGGING_CATEGORY(QGroundPixelLog, "QGroundPixel.Plugin")

Q_APPLICATION_STATIC(QGroundPixelPlugin, _qgroundPixelPluginInstance);

QGroundPixelPlugin::QGroundPixelPlugin(QObject *parent)
    : QGCCorePlugin(parent)
{
    qCDebug(QGroundPixelLog) << this;
}

QGCCorePlugin *QGroundPixelPlugin::instance()
{
    return _qgroundPixelPluginInstance();
}

void QGroundPixelPlugin::adjustSettingMetaData(const QString &settingsGroup, FactMetaData &metaData, bool &userVisible)
{
    QGCCorePlugin::adjustSettingMetaData(settingsGroup, metaData, userVisible);

    if (settingsGroup == VideoSettings::settingsGroup) {
        // The wfb-ng layer outputs the drone's RTP stream on udp://127.0.0.1:5600.
        // Default to the matching video source (OpenIPC cameras default to H.265)
        // and to low latency mode (no jitter buffer; the wfb-ng aggregator already
        // delivers packets in order over loopback).
        if (metaData.name() == VideoSettings::videoSourceName) {
            metaData.setRawDefaultValue(QString(VideoSettings::videoSourceUDPH265));
        } else if (metaData.name() == VideoSettings::lowLatencyModeName) {
            metaData.setRawDefaultValue(true);
#ifdef QGC_GST_STREAMING
        } else if (metaData.name() == VideoSettings::forceVideoDecoderName) {
            // The GStreamer amcviddec (hardware MediaCodec) path renders broken
            // video on the target device; the software decoder is verified good.
            metaData.setRawDefaultValue(GStreamer::VideoDecoderOptions::ForceVideoDecoderSoftware);
#endif
        }
    }
}

QQmlApplicationEngine *QGroundPixelPlugin::createQmlApplicationEngine(QObject *parent)
{
    QQmlApplicationEngine *engine = QGCCorePlugin::createQmlApplicationEngine(parent);

    WfbngManager *manager = WfbngManager::instance();
    (void) qmlRegisterSingletonInstance("QGroundPixel", 1, 0, "WfbngManager", manager);
    manager->init();

    return engine;
}
