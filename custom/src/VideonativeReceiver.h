#pragma once

#include "VideoReceiver.h"

#include <QtCore/QLoggingCategory>

#include <atomic>

class QVideoSink;

Q_DECLARE_LOGGING_CATEGORY(VideonativeReceiverLog)

/// VideoReceiver backed by PixelPilot's videonative decoder instead of
/// GStreamer. The Java side (QGCVideoNativeBridge) receives the wfb-ng RTP
/// stream on UDP 5600, decodes via MediaCodec into an ImageReader, and hands
/// YUV frames to this class over JNI; each frame is wrapped as a CPU
/// QVideoFrame and pushed into the stock QML VideoOutput's QVideoSink.
///
/// Only the "videoContent" stream is backed by a real decoder; the
/// "thermalVideo" instance stays inert (reports STATUS_INVALID_URL so
/// VideoManager does not restart it).
class VideonativeReceiver : public VideoReceiver
{
    Q_OBJECT

public:
    explicit VideonativeReceiver(QObject *parent = nullptr);
    ~VideonativeReceiver() override;

    /// Registers the JNI natives for QGCVideoNativeBridge. Safe to call
    /// repeatedly; only the first call does work.
    static bool registerNatives();

    /// The receiver currently owning the Java decoder (frames are routed here).
    static VideonativeReceiver *activeInstance();

    // Called from the JNI trampolines (ImageReader handler thread).
    void deliverFrame(const class QVideoFrame &frame);
    void deliverVideoSize(int width, int height);

public slots:
    void start(uint32_t timeout) final;
    void stop() final;
    void startDecoding(VideoSinkHandle sink) final;
    void stopDecoding() final;
    void startRecording(const QString &videoFile, FILE_FORMAT format) final;
    void stopRecording() final;
    void takeScreenshot(const QString &imageFile) final;

private:
    void _teardownJava();

    QVideoSink *_videoSink = nullptr;
    bool _javaStarted = false;
    std::atomic<bool> _decodingActive{false};
};
