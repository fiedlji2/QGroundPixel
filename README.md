# QGroundPixel

A [QGroundControl](https://github.com/mavlink/qgroundcontrol) fork tailored for the
**RadioMaster AX12** (Android 9) that adds **native OpenIPC / WFB‑NG digital FPV video
reception** and connection to the transmitter's **internal radio over serial** — while
keeping the full QGroundControl feature set.

It turns a single Android app into: a full ground control station **+** a wifibroadcast
(WFB‑NG) video receiver, so you get live FPV video, telemetry, and mission control in one
place without a separate goggle/receiver app.

---

## What it does differently from stock QGroundControl

Everything below is additive — no standard QGC functionality is removed.

### 1. Native WFB‑NG video reception (OpenIPC ecosystem)
A vendored, patched copy of PixelPilot's `wfbngrtl8812` module (userspace **RTL8812AU**
driver *devourer* + **wfb‑ng** RX/FEC/decrypt) runs inside the app and outputs the drone's
RTP H.264/H.265 stream to `udp://127.0.0.1:5600` — exactly where QGC's GStreamer video
pipeline already expects it. So QGC "just" plays it as an ordinary UDP video stream, while
the whole wifibroadcast stack works invisibly as a software modem.

Added on top of QGC:
- `custom/` build overlay: `QGroundPixelPlugin` (a `QGCCorePlugin`), `WfbngManager`
  (JNI bridge to the AAR), a **WFB‑NG Video** settings page, and the default `gs.key`.
- `QGCWfbManager.java` — USB attach/detach + permission flow for the RTL8812AU adapter(s).
- Video defaults are pre‑set for this use case: source **UDP h.265**, **low‑latency mode**,
  and **software decoder** (the Android hardware `amcviddec` path renders broken video on
  the AX12; software decode is verified good).
- A display‑queue tweak in `GstVideoReceiver` (time‑bounded instead of a 2‑buffer cap) so
  wfb‑ng's bursty FEC delivery doesn't drop P‑frames.

Configure it under **Application Settings → WFB‑NG Video**: Wi‑Fi channel, bandwidth,
codec, `gs.key` import, TX power, adaptive link, and live link stats.

### 2. Internal radio over serial (RadioMaster AX12)
On Android, QGC is switched from its bundled custom serial stack to the native
**`Qt6::SerialPort`** backend (Qt 6.8+), which can reach the AX12's internal radio module's
serial port. Gated by the CMake option `QGC_USE_QT_SERIAL_ON_ANDROID` (ON by default).
Ported from RadioMaster's `RM_AX12_Serial_Port` branch (commit `9993d95`).

### 3. Fullscreen fix for Android 9 / Qt 6.11
Current QGC master uses `Qt.ExpandedClientAreaHint` for edge‑to‑edge fullscreen. On
Android 9 (API 28) with Qt 6.11, Qt then insets the window's content by `SafeArea.margins`
(the hidden system‑bar areas), leaving unpainted white strips at the top/right of the AX12
panel. QGroundPixel zeroes the `ApplicationWindow` paddings on Android so the UI paints
edge to edge. (Root‑caused and verified on an API 28 1280×720 emulator: 36 px top / 72 px
right → 0 on all edges. This is a stock‑QGC issue, reproduced without any of the changes
above.)

### 4. Distinct application id
Built as `org.qgroundpixel.app` so it can be installed alongside a stock QGroundControl.

---

## Target hardware

- **RadioMaster AX12** (Android 9 / API 28, arm64) — the primary device.
- **RTL8812AU** USB Wi‑Fi adapter for wifibroadcast RX.
- An **OpenIPC** camera/VTX (e.g. RunCam WiFiLink) on the drone.

---

## Building (Android, arm64)

Matches QGC master's toolchain (`.github/build-config.json`):

- **Qt 6.11.1** for Android (`android_arm64_v8a`) **+** a matching host Qt.
- **Android SDK** platform 36, build‑tools 36, **NDK r27c** (27.2.12479018).
- **JDK 21**, CMake ≥ 3.25, Ninja.
- Python with `jinja2` + `defusedxml` (QGC QML page generators).

The `wfbngrtl8812` receive module ships as a prebuilt AAR in
`custom/android/libs/`. To rebuild it from source, clone
[OpenIPC/PixelPilot](https://github.com/OpenIPC/PixelPilot) **with submodules** and apply
the two small patches described in the project history (constructor takes a `Context`; the
`gs.key` path is passed in from Java instead of being hardcoded), then
`gradlew :app:wfbngrtl8812:assembleRelease`.

Configure + build (custom build is auto‑detected from the `custom/` directory):

```bash
export QT_ROOT_DIR=.../Qt/6.11.1/android_arm64_v8a
export QT_HOST_PATH=.../Qt/6.11.1/<host>
export ANDROID_NDK=.../ndk/27.2.12479018

cmake --preset Android \
  -DQGC_ANDROID_PACKAGE_NAME=org.qgroundpixel.app \
  -DQGC_PACKAGE_NAME=org.qgroundpixel.app
cmake --build --preset Android
```

The APK lands in `<build>/Android/android-build/…/*.apk`; sign it with your own key
(`zipalign` + `apksigner`).

---

## AX12 quick setup

1. Install the APK (Settings → allow unknown sources, or `adb install`).
2. Plug in the RTL8812AU adapter → allow the USB permission prompt.
3. Reception starts on the configured channel; video appears in the Fly View.
4. In **WFB‑NG Video** settings, match the **codec** to your camera (H.265 default — a
   mismatch shows no video), set the Wi‑Fi **channel/bandwidth**, and import your `gs.key`
   if you use a non‑default one.

> Disable the OpenIPC camera's audio (majestic.yaml) for now — Opus audio shares UDP 5600
> and only produces warnings.

---

## Licensing & credits

- **QGroundControl** — Dronecode Project, dual Apache‑2.0 / GPLv3.
- **wfb‑ng** ([svpcom/wfb-ng](https://github.com/svpcom/wfb-ng)) — **GPLv3**; combining it
  makes the resulting app GPLv3.
- **devourer** / **PixelPilot** — [OpenIPC](https://github.com/OpenIPC).
- Android serial change ported from
  [RadioMaster‑RC/qgroundcontrol](https://github.com/Radiomaster-RC/qgroundcontrol).

This is an unofficial community fork and is not endorsed by the QGroundControl project,
RadioMaster, or OpenIPC.
