package org.mavlink.qgroundcontrol.wfb;

import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbManager;
import android.os.Build;
import android.util.Log;

import com.openipc.wfbngrtl8812.WfbNgLink;

import java.util.HashMap;
import java.util.Iterator;
import java.util.Map;

/**
 * Owns the wfb-ng receive stack inside QGroundControl. Constructed and driven
 * from C++ (WfbngManager) over JNI; no Activity involvement. Handles USB
 * hot-plug and the permission flow itself: unlike PixelPilot, there is no
 * Activity.onResume() to re-trigger adapter startup after the permission
 * dialog, so the broadcast receiver must do it on every trigger path
 * (attach, detach, permission grant).
 */
public class QGCWfbManager {
    private static final String TAG = "QGCWfbManager";
    private static final String ACTION_USB_PERMISSION = "org.mavlink.qgroundcontrol.wfb.USB_PERMISSION";

    // Supported RTL8812AU adapters: {vendorId, productId}
    // (mirrors PixelPilot's res/xml/usb_device_filter.xml)
    private static final int[][] SUPPORTED_DEVICES = {
        {0x0BDA, 0x8812}, // RTL8812AU
        {0x0BDA, 0x881A}, // RTL8812AU-VS
        {0x0BDA, 0x881B}, // RTL8812AU-VL
        {0x2357, 0x0101}, // TP-Link Archer T4U
        {0x2357, 0x0103}, // TP-Link Archer T4UH
        {0x2357, 0x010D}, // TP-Link Archer T4U V2
        {0x2357, 0x010E}, // TP-Link Archer T4UH V2
        {0x0B05, 0x17D2}, // Asus AC56
        {0x2604, 0x0012}, // Tenda U12
        {0x0409, 0x0408}, // NEC AtermWL900U
        {0x0586, 0x3426}, // ZyXel 6605
    };

    private final Context context;
    private final WfbNgLink link;
    private final Map<String, UsbDevice> activeAdapters = new HashMap<>();

    private int channel = 161;
    private int bandwidth = 20;
    private boolean adaptiveLink = false;
    private int txPower = 20;
    private boolean enabled = true;

    // Implemented in C++ (WfbngManager.cc, registered via RegisterNatives).
    public static native void nativeAdapters(int count);
    public static native void nativeStats(int rssi, int ok, int recovered, int lost);

    private final BroadcastReceiver usbReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context ctx, Intent intent) {
            final String action = intent.getAction();
            if (UsbManager.ACTION_USB_DEVICE_ATTACHED.equals(action)
                    || UsbManager.ACTION_USB_DEVICE_DETACHED.equals(action)
                    || ACTION_USB_PERMISSION.equals(action)) {
                Log.d(TAG, "USB event: " + action);
                refreshAdapters();
            }
        }
    };

    public QGCWfbManager(Context context) {
        this.context = context;

        // gs.key must already exist in filesDir at this point: WfbNgLink's
        // native side loads it during construction and aborts if missing.
        this.link = new WfbNgLink(context);
        this.link.SetWfbNGStatsChanged(stats -> nativeStats(
                stats.avg_rssi, stats.count_p_dec_ok, stats.count_p_fec_recovered, stats.count_p_lost));

        IntentFilter filter = new IntentFilter();
        filter.addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED);
        filter.addAction(UsbManager.ACTION_USB_DEVICE_DETACHED);
        filter.addAction(ACTION_USB_PERMISSION);
        if (Build.VERSION.SDK_INT >= 33) {
            context.registerReceiver(usbReceiver, filter, Context.RECEIVER_NOT_EXPORTED);
        } else {
            context.registerReceiver(usbReceiver, filter);
        }
    }

    public synchronized void configure(int channel, int bandwidth, boolean adaptiveLink, int txPower, boolean enabled) {
        this.channel = channel;
        this.bandwidth = bandwidth;
        this.adaptiveLink = adaptiveLink;
        this.txPower = txPower;
        this.enabled = enabled;
        link.nativeSetAdaptiveLinkEnabled(adaptiveLink);
        link.nativeSetTxPower(txPower);
    }

    private static boolean isSupported(UsbDevice dev) {
        for (int[] id : SUPPORTED_DEVICES) {
            if (dev.getVendorId() == id[0] && dev.getProductId() == id[1]) {
                return true;
            }
        }
        return false;
    }

    private Map<String, UsbDevice> attachedSupportedDevices() {
        UsbManager usbManager = (UsbManager) context.getSystemService(Context.USB_SERVICE);
        Map<String, UsbDevice> result = new HashMap<>();
        for (UsbDevice dev : usbManager.getDeviceList().values()) {
            if (isSupported(dev)) {
                result.put(dev.getDeviceName(), dev);
            }
        }
        return result;
    }

    /**
     * Reconciles running wfb-ng instances with currently attached and
     * permitted adapters. Requests permission where missing; the resulting
     * broadcast re-enters this method.
     */
    public synchronized void refreshAdapters() {
        UsbManager usbManager = (UsbManager) context.getSystemService(Context.USB_SERVICE);
        Map<String, UsbDevice> attached = enabled ? attachedSupportedDevices() : new HashMap<>();

        // Stop adapters that disappeared (or everything when disabled).
        Iterator<Map.Entry<String, UsbDevice>> it = activeAdapters.entrySet().iterator();
        while (it.hasNext()) {
            Map.Entry<String, UsbDevice> entry = it.next();
            if (!attached.containsKey(entry.getKey())) {
                try {
                    link.stop(entry.getValue());
                } catch (InterruptedException e) {
                    Log.w(TAG, "Interrupted stopping adapter", e);
                }
                it.remove();
            }
        }

        // Start newly attached adapters we have permission for.
        for (Map.Entry<String, UsbDevice> entry : attached.entrySet()) {
            if (activeAdapters.containsKey(entry.getKey())) {
                continue;
            }
            UsbDevice dev = entry.getValue();
            if (!usbManager.hasPermission(dev)) {
                Log.d(TAG, "Requesting USB permission for " + dev.getDeviceName());
                Intent intent = new Intent(ACTION_USB_PERMISSION);
                intent.setPackage(context.getPackageName());
                int flags = (Build.VERSION.SDK_INT >= 31) ? PendingIntent.FLAG_MUTABLE : 0;
                PendingIntent pi = PendingIntent.getBroadcast(context, 0, intent, flags);
                usbManager.requestPermission(dev, pi);
                continue;
            }
            Log.d(TAG, "Starting wfb-ng on " + dev.getDeviceName()
                    + " channel " + channel + " bw " + bandwidth);
            link.nativeSetAdaptiveLinkEnabled(adaptiveLink);
            link.nativeSetTxPower(txPower);
            link.start(channel, bandwidth, dev);
            activeAdapters.put(entry.getKey(), dev);
        }

        nativeAdapters(activeAdapters.size());
    }

    private synchronized void stopAll() {
        try {
            link.stopAll();
        } catch (InterruptedException e) {
            Log.w(TAG, "Interrupted stopping adapters", e);
        }
        activeAdapters.clear();
        nativeAdapters(0);
    }

    public synchronized void restart() {
        stopAll();
        refreshAdapters();
    }

    public synchronized void setEnabled(boolean enabled) {
        this.enabled = enabled;
        if (enabled) {
            refreshAdapters();
        } else {
            stopAll();
        }
    }

    public synchronized void setChannel(int channel) {
        this.channel = channel;
        if (!activeAdapters.isEmpty()) {
            restart();
        }
    }

    public synchronized void setBandwidth(int bandwidth) {
        this.bandwidth = bandwidth;
        if (!activeAdapters.isEmpty()) {
            restart();
        }
    }

    public synchronized void setAdaptiveLink(boolean adaptiveLink) {
        this.adaptiveLink = adaptiveLink;
        link.nativeSetAdaptiveLinkEnabled(adaptiveLink);
    }

    public synchronized void setTxPower(int txPower) {
        this.txPower = txPower;
        link.nativeSetTxPower(txPower);
    }

    public synchronized void refreshKeyAndRestart() {
        stopAll();
        link.refreshKey();
        refreshAdapters();
    }
}
