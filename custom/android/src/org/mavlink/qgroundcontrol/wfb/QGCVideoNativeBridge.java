package org.mavlink.qgroundcontrol.wfb;

import android.content.Context;
import android.graphics.ImageFormat;
import android.media.Image;
import android.media.ImageReader;
import android.os.Handler;
import android.os.HandlerThread;
import android.util.Log;

import com.openipc.videonative.DecodingInfo;
import com.openipc.videonative.IVideoParamsChanged;
import com.openipc.videonative.VideoPlayer;

import java.nio.ByteBuffer;

/**
 * Bridges PixelPilot's videonative decoder into QGC's video display.
 *
 * videonative receives the wfb-ng RTP stream on UDP 0.0.0.0:5600 and decodes
 * via MediaCodec into the Surface of an ImageReader (YUV_420_888). Each
 * decoded frame is handed to C++ (VideonativeReceiver) as raw YUV planes and
 * pushed into the stock QML VideoOutput as a QVideoFrame. This is the C-CPU
 * spike variant: one CPU copy per frame, correctness over latency.
 *
 * Driven from C++ over JNI; no Activity involvement. All callbacks below
 * arrive on non-UI threads (ImageReader handler thread / videonative Timer
 * thread) — the C++ side marshals.
 */
public class QGCVideoNativeBridge implements IVideoParamsChanged {
    private static final String TAG = "QGCVideoNativeBridge";
    private static final int MAX_IMAGES = 4;
    // Initial ImageReader geometry only; the producer (MediaCodec) resizes the
    // buffer queue to the real stream size and each Image reports its own WxH.
    private static final int INITIAL_WIDTH = 1920;
    private static final int INITIAL_HEIGHT = 1080;

    private final Context context;
    private VideoPlayer videoPlayer;
    private ImageReader imageReader;
    private HandlerThread imageThread;

    // Implemented in C++ (VideonativeReceiver.cc, registered via RegisterNatives).
    public static native void nativeOnFrame(ByteBuffer y, int yRowStride,
                                            ByteBuffer u, int uRowStride, int uPixelStride,
                                            ByteBuffer v, int vRowStride, int vPixelStride,
                                            int width, int height);
    public static native void nativeOnVideoSize(int width, int height);

    public QGCVideoNativeBridge(Context context) {
        this.context = context;
    }

    public synchronized void start() {
        if (videoPlayer != null) {
            return;
        }
        Log.d(TAG, "Starting videonative receiver (UDP 5600 -> MediaCodec -> ImageReader)");

        imageThread = new HandlerThread("qgp-videoframes");
        imageThread.start();
        Handler handler = new Handler(imageThread.getLooper());

        imageReader = ImageReader.newInstance(INITIAL_WIDTH, INITIAL_HEIGHT,
                ImageFormat.YUV_420_888, MAX_IMAGES);
        imageReader.setOnImageAvailableListener(reader -> {
            Image image = null;
            try {
                image = reader.acquireLatestImage();
                if (image == null) {
                    return;
                }
                Image.Plane[] planes = image.getPlanes();
                nativeOnFrame(planes[0].getBuffer(), planes[0].getRowStride(),
                        planes[1].getBuffer(), planes[1].getRowStride(), planes[1].getPixelStride(),
                        planes[2].getBuffer(), planes[2].getRowStride(), planes[2].getPixelStride(),
                        image.getWidth(), image.getHeight());
            } catch (Exception e) {
                Log.w(TAG, "Frame handling failed", e);
            } finally {
                if (image != null) {
                    image.close();
                }
            }
        }, handler);

        videoPlayer = new VideoPlayer(context);
        videoPlayer.setIVideoParamsChanged(this);
        videoPlayer.addAndStartDecoderReceiver(imageReader.getSurface(), 0);
        videoPlayer.start();
    }

    public synchronized void stop() {
        if (videoPlayer == null) {
            return;
        }
        Log.d(TAG, "Stopping videonative receiver");
        try {
            videoPlayer.stopAndRemoveReceiverDecoder(0);
            videoPlayer.stop();
        } catch (Exception e) {
            Log.w(TAG, "videonative stop failed", e);
        }
        videoPlayer = null;

        if (imageReader != null) {
            imageReader.close();
            imageReader = null;
        }
        if (imageThread != null) {
            imageThread.quitSafely();
            imageThread = null;
        }
    }

    @Override
    public void onVideoRatioChanged(int videoW, int videoH) {
        nativeOnVideoSize(videoW, videoH);
    }

    @Override
    public void onDecodingInfoChanged(DecodingInfo decodingInfo) {
        // FPS/bitrate stats; not surfaced in the spike.
    }
}
