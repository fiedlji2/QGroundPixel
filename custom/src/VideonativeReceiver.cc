#include "VideonativeReceiver.h"

#include "QGCLoggingCategory.h"

#include <QtCore/QPointer>
#include <QtMultimedia/QVideoFrame>
#include <QtMultimedia/QVideoFrameFormat>
#include <QtMultimedia/QVideoSink>
#include <QtMultimedia/qabstractvideobuffer.h>

#ifdef Q_OS_ANDROID
#include <QtCore/QCoreApplication>
#include <QtCore/QJniEnvironment>
#include <QtCore/QJniObject>
#endif

#include <atomic>
#include <cstring>
#include <memory>

QGC_LOGGING_CATEGORY(VideonativeReceiverLog, "QGroundPixel.VideonativeReceiver")

namespace {

constexpr const char *kBridgeClass = "org/mavlink/qgroundcontrol/wfb/QGCVideoNativeBridge";

std::atomic<VideonativeReceiver *> s_activeReceiver{nullptr};

/// CPU I420 frame storage handed to QVideoFrame (public QAbstractVideoBuffer API).
class QgpI420Buffer : public QAbstractVideoBuffer
{
public:
    QgpI420Buffer(QByteArray data, int width, int height)
        : _data(std::move(data))
        , _width(width)
        , _height(height)
        , _format(QSize(width, height), QVideoFrameFormat::Format_YUV420P)
    {
    }

    MapData map(QVideoFrame::MapMode mode) final
    {
        Q_UNUSED(mode);
        MapData mapData;
        uchar *base = reinterpret_cast<uchar *>(_data.data());
        const int ySize = _width * _height;
        const int cWidth = _width / 2;
        const int cHeight = _height / 2;
        const int cSize = cWidth * cHeight;

        mapData.planeCount = 3;
        mapData.data[0] = base;
        mapData.bytesPerLine[0] = _width;
        mapData.dataSize[0] = ySize;
        mapData.data[1] = base + ySize;
        mapData.bytesPerLine[1] = cWidth;
        mapData.dataSize[1] = cSize;
        mapData.data[2] = base + ySize + cSize;
        mapData.bytesPerLine[2] = cWidth;
        mapData.dataSize[2] = cSize;
        return mapData;
    }

    QVideoFrameFormat format() const final { return _format; }

private:
    QByteArray _data;
    int _width = 0;
    int _height = 0;
    QVideoFrameFormat _format;
};

#ifdef Q_OS_ANDROID

/// Copies one chroma plane of an android YUV_420_888 Image into tightly packed
/// I420 (handles both planar pixelStride=1 and semi-planar pixelStride=2).
void copyChromaPlane(const uint8_t *src, int rowStride, int pixelStride,
                     uint8_t *dst, int cWidth, int cHeight)
{
    if (pixelStride == 1) {
        for (int row = 0; row < cHeight; ++row) {
            std::memcpy(dst + (row * cWidth), src + (row * rowStride), cWidth);
        }
        return;
    }
    for (int row = 0; row < cHeight; ++row) {
        const uint8_t *srcRow = src + (row * rowStride);
        uint8_t *dstRow = dst + (row * cWidth);
        for (int col = 0; col < cWidth; ++col) {
            dstRow[col] = srcRow[col * pixelStride];
        }
    }
}

void jniOnFrame(JNIEnv *env, jclass,
                jobject yBuf, jint yRowStride,
                jobject uBuf, jint uRowStride, jint uPixelStride,
                jobject vBuf, jint vRowStride, jint vPixelStride,
                jint width, jint height)
{
    VideonativeReceiver *receiver = VideonativeReceiver::activeInstance();
    if (!receiver) {
        return;
    }

    const auto *ySrc = static_cast<const uint8_t *>(env->GetDirectBufferAddress(yBuf));
    const auto *uSrc = static_cast<const uint8_t *>(env->GetDirectBufferAddress(uBuf));
    const auto *vSrc = static_cast<const uint8_t *>(env->GetDirectBufferAddress(vBuf));
    if (!ySrc || !uSrc || !vSrc || width <= 0 || height <= 0) {
        return;
    }

    // Even dimensions required for I420; codecs emit even sizes for this stream.
    const int w = width & ~1;
    const int h = height & ~1;
    const int cWidth = w / 2;
    const int cHeight = h / 2;

    QByteArray data;
    data.resize((w * h) + 2 * (cWidth * cHeight));
    auto *dst = reinterpret_cast<uint8_t *>(data.data());

    for (int row = 0; row < h; ++row) {
        std::memcpy(dst + (row * w), ySrc + (row * yRowStride), w);
    }
    copyChromaPlane(uSrc, uRowStride, uPixelStride, dst + (w * h), cWidth, cHeight);
    copyChromaPlane(vSrc, vRowStride, vPixelStride, dst + (w * h) + (cWidth * cHeight), cWidth, cHeight);

    const QVideoFrame frame(std::make_unique<QgpI420Buffer>(std::move(data), w, h));
    receiver->deliverFrame(frame);
}

void jniOnVideoSize(JNIEnv *, jclass, jint width, jint height)
{
    VideonativeReceiver *receiver = VideonativeReceiver::activeInstance();
    if (receiver) {
        receiver->deliverVideoSize(width, height);
    }
}

QJniObject &javaBridge()
{
    static QJniObject bridge;
    return bridge;
}

#endif // Q_OS_ANDROID

} // namespace

VideonativeReceiver::VideonativeReceiver(QObject *parent)
    : VideoReceiver(parent)
{
    // VideoManager checks this before handing the sink to the (GStreamer)
    // backend attach; our frames go straight to the widget's QVideoSink.
    setProperty("qgpExternalSink", true);
    qCDebug(VideonativeReceiverLog) << this;
}

VideonativeReceiver::~VideonativeReceiver()
{
    _teardownJava();
}

bool VideonativeReceiver::registerNatives()
{
#ifdef Q_OS_ANDROID
    static bool registered = []() {
        QJniEnvironment env;
        jclass cls = env.findClass(kBridgeClass);
        if (!cls) {
            qCWarning(VideonativeReceiverLog) << "QGCVideoNativeBridge class not found";
            return false;
        }
        static const JNINativeMethod methods[] = {
            {"nativeOnFrame", "(Ljava/nio/ByteBuffer;ILjava/nio/ByteBuffer;IILjava/nio/ByteBuffer;IIII)V",
             reinterpret_cast<void *>(jniOnFrame)},
            {"nativeOnVideoSize", "(II)V", reinterpret_cast<void *>(jniOnVideoSize)},
        };
        if (env->RegisterNatives(cls, methods, sizeof(methods) / sizeof(methods[0])) != JNI_OK) {
            qCWarning(VideonativeReceiverLog) << "RegisterNatives failed for QGCVideoNativeBridge";
            return false;
        }
        return true;
    }();
    return registered;
#else
    return false;
#endif
}

VideonativeReceiver *VideonativeReceiver::activeInstance()
{
    return s_activeReceiver.load(std::memory_order_acquire);
}

void VideonativeReceiver::deliverFrame(const QVideoFrame &frame)
{
    QVideoSink *sink = _videoSink;
    if (!sink) {
        return;
    }

    if (!_decodingActive.exchange(true, std::memory_order_acq_rel)) {
        QMetaObject::invokeMethod(this, [this]() {
            _decoding = true;
            emit decodingChanged(true);
        }, Qt::QueuedConnection);
    }

    // Same handoff the GStreamer qgcqvideosink uses: queue onto the sink's thread.
    QMetaObject::invokeMethod(sink, [sink, frame]() {
        sink->setVideoFrame(frame);
    }, Qt::QueuedConnection);
}

void VideonativeReceiver::deliverVideoSize(int width, int height)
{
    QMetaObject::invokeMethod(this, [this, width, height]() {
        qCDebug(VideonativeReceiverLog) << "Video size" << width << "x" << height;
        emit videoSizeChanged(QSize(width, height));
    }, Qt::QueuedConnection);
}

void VideonativeReceiver::start(uint32_t timeout)
{
    Q_UNUSED(timeout);

#ifdef Q_OS_ANDROID
    if (name() != QStringLiteral("videoContent")) {
        // Thermal stream is not backed by wfb-ng video; INVALID_URL stops
        // VideoManager from retrying.
        emit onStartComplete(STATUS_INVALID_URL);
        return;
    }
    if (_javaStarted) {
        emit onStartComplete(STATUS_INVALID_STATE);
        return;
    }
    if (!registerNatives()) {
        emit onStartComplete(STATUS_FAIL);
        return;
    }

    if (!javaBridge().isValid()) {
        QJniObject context = QNativeInterface::QAndroidApplication::context();
        javaBridge() = QJniObject(kBridgeClass, "(Landroid/content/Context;)V", context.object());
    }
    if (!javaBridge().isValid()) {
        qCWarning(VideonativeReceiverLog) << "Failed to construct QGCVideoNativeBridge";
        emit onStartComplete(STATUS_FAIL);
        return;
    }

    s_activeReceiver.store(this, std::memory_order_release);
    javaBridge().callMethod<void>("start");
    _javaStarted = true;

    qCDebug(VideonativeReceiverLog) << "videonative started (uri ignored, fixed UDP 5600):" << uri();
    emit onStartComplete(STATUS_OK);
    _streaming = true;
    emit streamingChanged(true);
#else
    emit onStartComplete(STATUS_NOT_IMPLEMENTED);
#endif
}

void VideonativeReceiver::stop()
{
    _teardownJava();
    emit onStopComplete(STATUS_OK);
}

void VideonativeReceiver::startDecoding(VideoSinkHandle sink)
{
    _videoSink = static_cast<QVideoSink *>(sink);
    emit onStartDecodingComplete(_videoSink ? STATUS_OK : STATUS_FAIL);
}

void VideonativeReceiver::stopDecoding()
{
    _videoSink = nullptr;
    if (_decodingActive.exchange(false, std::memory_order_acq_rel)) {
        _decoding = false;
        emit decodingChanged(false);
    }
    emit onStopDecodingComplete(STATUS_OK);
}

void VideonativeReceiver::startRecording(const QString &videoFile, FILE_FORMAT format)
{
    Q_UNUSED(videoFile);
    Q_UNUSED(format);
    emit onStartRecordingComplete(STATUS_NOT_IMPLEMENTED);
}

void VideonativeReceiver::stopRecording()
{
    emit onStopRecordingComplete(STATUS_NOT_IMPLEMENTED);
}

void VideonativeReceiver::takeScreenshot(const QString &imageFile)
{
    Q_UNUSED(imageFile);
    emit onTakeScreenshotComplete(STATUS_NOT_IMPLEMENTED);
}

void VideonativeReceiver::_teardownJava()
{
#ifdef Q_OS_ANDROID
    if (!_javaStarted) {
        return;
    }
    VideonativeReceiver *expected = this;
    s_activeReceiver.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
    if (javaBridge().isValid()) {
        javaBridge().callMethod<void>("stop");
    }
    _javaStarted = false;

    if (_decodingActive.exchange(false, std::memory_order_acq_rel)) {
        _decoding = false;
        emit decodingChanged(false);
    }
    if (_streaming) {
        _streaming = false;
        emit streamingChanged(false);
    }
#endif
}
