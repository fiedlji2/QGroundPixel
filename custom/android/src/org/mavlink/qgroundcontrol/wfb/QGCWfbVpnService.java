package org.mavlink.qgroundcontrol.wfb;

import android.content.Context;
import android.content.Intent;
import android.net.VpnService;
import android.os.ParcelFileDescriptor;
import android.util.Log;

import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetSocketAddress;

/**
 * wfb-ng IP tunnel as an Android VPN (ported from PixelPilot's WfbNgVpnService).
 *
 * The native wfb-ng receiver already carries a third stream besides video and MAVLink:
 * the "udp" channel (radio port 32). Every tunnel packet it receives is delivered to
 * 127.0.0.1:8000, and anything sent to 127.0.0.1:8001 is injected over the RTL8812AU on
 * radio port 160 — both with a 2-byte big-endian length prefix. This service turns that
 * pair of ports into a real network interface: a TUN at 10.5.0.3/24 routing 10.5.0.0/24,
 * so the embedded WebView (or any app) can reach the VTX at 10.5.0.10, and the native
 * adaptive-link uplink (10.5.0.10:9999) finally has a route.
 *
 * Driven from C++ (WfbngManager) through the static helpers below; the one-time
 * VpnService consent dialog is requested via QtAndroidPrivate::startActivity.
 */
public class QGCWfbVpnService extends VpnService {
    private static final String TAG = "QGCWfbVpnService";
    public static final String ACTION_STOP = "org.mavlink.qgroundcontrol.wfb.VPN_STOP";

    private static final String TUN_ADDRESS = "10.5.0.3";
    private static final String TUN_ROUTE = "10.5.0.0";
    private static final int TUN_PREFIX = 24;
    private static final int TUN_MTU = 1400;          // wfb_tun convention; keeps frames under the air MTU
    private static final int RX_PORT = 8000;          // native wfb-ng RX -> here
    private static final int TX_PORT = 8001;          // here -> native wfb-ng TX
    private static final int LENGTH_PREFIX = 2;

    private static volatile boolean sRunning = false;

    private ParcelFileDescriptor vpnInterface = null;
    private Thread udpToVpnThread;
    private Thread vpnToUdpThread;
    private DatagramSocket rxSocket;

    // ---- static helpers used from C++ -------------------------------------------------

    /** Consent intent to launch with startActivityForResult, or null when already granted. */
    public static Intent prepareIntent(Context context) {
        return VpnService.prepare(context);
    }

    public static void startService(Context context) {
        context.startService(new Intent(context, QGCWfbVpnService.class));
    }

    public static void stopService(Context context) {
        Intent intent = new Intent(context, QGCWfbVpnService.class);
        intent.setAction(ACTION_STOP);
        context.startService(intent);
    }

    public static boolean isRunning() {
        return sRunning;
    }

    // ---- service lifecycle -----------------------------------------------------------

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent != null && ACTION_STOP.equals(intent.getAction())) {
            Log.i(TAG, "stopping tunnel");
            teardown();
            stopSelf();
            return START_NOT_STICKY;
        }

        if (sRunning) {
            Log.w(TAG, "tunnel already running");
            return START_STICKY;
        }

        try {
            Builder builder = new Builder();
            builder.setSession("wfb-ng");
            builder.setMtu(TUN_MTU);
            builder.addAddress(TUN_ADDRESS, TUN_PREFIX);
            builder.addRoute(TUN_ROUTE, TUN_PREFIX);
            builder.setBlocking(true);
            vpnInterface = builder.establish();
            if (vpnInterface == null) {
                throw new IllegalStateException("establish() returned null (consent missing?)");
            }
        } catch (Exception e) {
            Log.e(TAG, "failed to establish TUN", e);
            teardown();
            stopSelf();
            return START_NOT_STICKY;
        }

        sRunning = true;
        startThreads(vpnInterface);
        Log.i(TAG, "tunnel up: " + TUN_ADDRESS + "/" + TUN_PREFIX + " mtu " + TUN_MTU);
        return START_STICKY;
    }

    @Override
    public void onRevoke() {
        Log.w(TAG, "VPN permission revoked by the system/user");
        teardown();
        stopSelf();
        super.onRevoke();
    }

    @Override
    public void onDestroy() {
        teardown();
        super.onDestroy();
    }

    private void startThreads(final ParcelFileDescriptor pfd) {
        final FileInputStream tunIn = new FileInputStream(pfd.getFileDescriptor());
        final FileOutputStream tunOut = new FileOutputStream(pfd.getFileDescriptor());

        // native wfb-ng RX (UDP 8000, length-prefixed) -> TUN
        udpToVpnThread = new Thread(() -> {
            byte[] buffer = new byte[65536];
            try {
                rxSocket = new DatagramSocket(null);
                rxSocket.setReuseAddress(true);
                rxSocket.bind(new InetSocketAddress(RX_PORT));
                DatagramPacket packet = new DatagramPacket(buffer, buffer.length);
                while (sRunning) {
                    rxSocket.receive(packet);
                    int len = packet.getLength();
                    if (len <= LENGTH_PREFIX) {
                        continue;
                    }
                    try {
                        tunOut.write(packet.getData(), LENGTH_PREFIX, len - LENGTH_PREFIX);
                    } catch (IOException e) {
                        if (sRunning) Log.w(TAG, "TUN write failed", e);
                    }
                }
            } catch (IOException e) {
                if (sRunning) Log.e(TAG, "RX thread error", e);
            }
            Log.i(TAG, "RX thread stopped");
        }, "wfb-tun-rx");

        // TUN -> native wfb-ng TX (UDP 8001, length-prefixed)
        vpnToUdpThread = new Thread(() -> {
            byte[] buffer = new byte[LENGTH_PREFIX + 65536];
            try (DatagramSocket txSocket = new DatagramSocket()) {
                InetSocketAddress txTarget = new InetSocketAddress("127.0.0.1", TX_PORT);
                while (sRunning) {
                    int len = tunIn.read(buffer, LENGTH_PREFIX, buffer.length - LENGTH_PREFIX);
                    if (len < 0) {
                        break;
                    }
                    if (len == 0) {
                        continue;
                    }
                    buffer[0] = (byte) ((len >> 8) & 0xFF);
                    buffer[1] = (byte) (len & 0xFF);
                    txSocket.send(new DatagramPacket(buffer, LENGTH_PREFIX + len, txTarget));
                }
            } catch (IOException e) {
                if (sRunning) Log.e(TAG, "TX thread error", e);
            }
            Log.i(TAG, "TX thread stopped");
        }, "wfb-tun-tx");

        udpToVpnThread.start();
        vpnToUdpThread.start();
    }

    private void teardown() {
        sRunning = false;
        if (rxSocket != null) {
            rxSocket.close();       // unblocks receive()
            rxSocket = null;
        }
        if (vpnInterface != null) {
            try {
                vpnInterface.close(); // unblocks the TUN read()
            } catch (IOException e) {
                Log.w(TAG, "closing TUN failed", e);
            }
            vpnInterface = null;
        }
        if (udpToVpnThread != null) {
            udpToVpnThread.interrupt();
            udpToVpnThread = null;
        }
        if (vpnToUdpThread != null) {
            vpnToUdpThread.interrupt();
            vpnToUdpThread = null;
        }
    }
}
