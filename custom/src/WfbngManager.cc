#include "WfbngManager.h"

#include "AppSettings.h"
#include "QGCLoggingCategory.h"
#include "SettingsManager.h"
#include "VideoManager.h"
#include "VtxHttpProxy.h"

#include <QtCore/QApplicationStatic>
#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QSettings>
#include <QtCore/QTimer>

#ifdef Q_OS_ANDROID
#include <QtCore/QJniEnvironment>
#include <QtCore/QJniObject>
#include <QtCore/private/qandroidextras_p.h>
#endif

QGC_LOGGING_CATEGORY(WfbngManagerLog, "QGroundPixel.WfbngManager")

Q_APPLICATION_STATIC(WfbngManager, _wfbngManagerInstance);

namespace {
constexpr const char *kSettingsGroup = "QGroundPixelWfbng";
constexpr const char *kDefaultKeyResource = ":/Custom/wfb/gs.key";
constexpr const char *kJavaManagerClass = "org/mavlink/qgroundcontrol/wfb/QGCWfbManager";
constexpr const char *kJavaVpnClass = "org/mavlink/qgroundcontrol/wfb/QGCWfbVpnService";
constexpr int kVpnConsentRequestCode = 0x5746;   // 'WF'
constexpr int kAndroidResultOk = -1;             // Activity.RESULT_OK

#ifdef Q_OS_ANDROID
QJniObject s_javaManager;

void jniAdapters(JNIEnv *, jclass, jint count)
{
    QMetaObject::invokeMethod(WfbngManager::instance(), [count]() {
        WfbngManager::instance()->updateAdapterCount(count);
    }, Qt::QueuedConnection);
}

void jniStats(JNIEnv *, jclass, jint rssi, jint ok, jint recovered, jint lost)
{
    QMetaObject::invokeMethod(WfbngManager::instance(), [rssi, ok, recovered, lost]() {
        WfbngManager::instance()->updateStats(rssi, ok, recovered, lost);
    }, Qt::QueuedConnection);
}

bool registerNatives()
{
    QJniEnvironment env;
    jclass cls = env.findClass(kJavaManagerClass);
    if (!cls) {
        qCWarning(WfbngManagerLog) << "QGCWfbManager Java class not found";
        return false;
    }

    static const JNINativeMethod methods[] = {
        {"nativeAdapters", "(I)V", reinterpret_cast<void *>(jniAdapters)},
        {"nativeStats", "(IIII)V", reinterpret_cast<void *>(jniStats)},
    };
    if (env->RegisterNatives(cls, methods, sizeof(methods) / sizeof(methods[0])) != JNI_OK) {
        qCWarning(WfbngManagerLog) << "RegisterNatives failed for QGCWfbManager";
        return false;
    }
    return true;
}
#endif
} // namespace

WfbngManager::WfbngManager(QObject *parent)
    : QObject(parent)
{
    QSettings settings;
    settings.beginGroup(kSettingsGroup);
    _enabled = settings.value("enabled", true).toBool();
    _channel = settings.value("channel", 161).toInt();
    _bandwidth = settings.value("bandwidth", 20).toInt();
    _txPower = settings.value("txPower", 20).toInt();
    _adaptiveLink = settings.value("adaptiveLink", false).toBool();
    settings.endGroup();
}

WfbngManager *WfbngManager::instance()
{
    return _wfbngManagerInstance();
}

bool WfbngManager::supported() const
{
#ifdef Q_OS_ANDROID
    return true;
#else
    return false;
#endif
}

QString WfbngManager::_gsKeyPath() const
{
#ifdef Q_OS_ANDROID
    // Must match what WfbNgLink.java passes to the native layer:
    // context.getFilesDir() + "/gs.key"
    QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (context.isValid()) {
        const QJniObject filesDir = context.callObjectMethod("getFilesDir", "()Ljava/io/File;");
        if (filesDir.isValid()) {
            return filesDir.callObjectMethod("getAbsolutePath", "()Ljava/lang/String;").toString()
                   + QStringLiteral("/gs.key");
        }
    }
#endif
    return QString();
}

bool WfbngManager::_provisionDefaultKey(bool overwrite)
{
    const QString keyPath = _gsKeyPath();
    if (keyPath.isEmpty()) {
        return false;
    }
    if (QFile::exists(keyPath)) {
        if (!overwrite) {
            return true;
        }
        (void) QFile::remove(keyPath);
    }
    if (!QFile::copy(kDefaultKeyResource, keyPath)) {
        qCWarning(WfbngManagerLog) << "Failed to provision default gs.key to" << keyPath;
        return false;
    }
    (void) QFile::setPermissions(keyPath, QFile::ReadOwner | QFile::WriteOwner);
    qCDebug(WfbngManagerLog) << "Default gs.key provisioned to" << keyPath;
    return true;
}

void WfbngManager::_applyKeyStatus()
{
    QSettings settings;
    settings.beginGroup(kSettingsGroup);
    const bool customKey = settings.value("customKey", false).toBool();
    settings.endGroup();

    _keyStatus = customKey ? tr("Custom key imported") : tr("Default OpenIPC key");
    emit keyStatusChanged();
}

void WfbngManager::init()
{
    if (_initialized || !supported()) {
        _applyKeyStatus();
        return;
    }

#ifdef Q_OS_ANDROID
    // The native wfb-ng layer aborts if the key file is missing when the
    // aggregators are constructed, so the key MUST be on disk before the Java
    // manager (and with it WfbNgLink) is created.
    if (!_provisionDefaultKey(false)) {
        qCWarning(WfbngManagerLog) << "gs.key provisioning failed; wfb-ng disabled";
        return;
    }

    if (!registerNatives()) {
        return;
    }

    QJniObject context = QNativeInterface::QAndroidApplication::context();
    s_javaManager = QJniObject(kJavaManagerClass, "(Landroid/content/Context;)V", context.object());
    if (!s_javaManager.isValid()) {
        qCWarning(WfbngManagerLog) << "Failed to construct QGCWfbManager";
        return;
    }

    s_javaManager.callMethod<void>("configure", "(IIZIZ)V",
                                   _channel, _bandwidth, _adaptiveLink, _txPower, _enabled);
    s_javaManager.callMethod<void>("refreshAdapters");

    _initialized = true;
    qCDebug(WfbngManagerLog) << "WFB-NG initialized: channel" << _channel
                             << "bandwidth" << _bandwidth << "enabled" << _enabled;

    if (tunnelEnabled()) {
        startTunnel();
    } else {
        _refreshTunnelState();
    }
#endif

    _applyKeyStatus();
}

bool WfbngManager::tunnelEnabled() const
{
    QSettings settings;
    settings.beginGroup(kSettingsGroup);
    return settings.value("tunnelEnabled", true).toBool();
}

void WfbngManager::setTunnelEnabled(bool enabled)
{
    if (tunnelEnabled() == enabled) {
        return;
    }
    QSettings settings;
    settings.beginGroup(kSettingsGroup);
    settings.setValue("tunnelEnabled", enabled);
    settings.endGroup();
    emit tunnelEnabledChanged();

    if (enabled) {
        startTunnel();
    } else {
        stopTunnel();
    }
}

void WfbngManager::startTunnel()
{
#ifdef Q_OS_ANDROID
    QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid()) {
        _refreshTunnelState(tr("No Android context"));
        return;
    }

    const QJniObject consentIntent = QJniObject::callStaticObjectMethod(
        kJavaVpnClass, "prepareIntent", "(Landroid/content/Context;)Landroid/content/Intent;", context.object());

    if (!consentIntent.isValid()) {
        // Consent already granted (or not required): start straight away.
        QJniObject::callStaticMethod<void>(kJavaVpnClass, "startService", "(Landroid/content/Context;)V", context.object());
        qCDebug(WfbngManagerLog) << "wfb-ng tunnel start requested";
        QTimer::singleShot(500, this, [this]() { _refreshTunnelState(); });
        return;
    }

    _refreshTunnelState(tr("Waiting for VPN permission…"));
    qCDebug(WfbngManagerLog) << "Requesting Android VPN consent for the wfb-ng tunnel";
    QtAndroidPrivate::startActivity(consentIntent, kVpnConsentRequestCode,
                                    [this](int requestCode, int resultCode, const QJniObject &data) {
        Q_UNUSED(data);
        if (requestCode != kVpnConsentRequestCode) {
            return;
        }
        if (resultCode != kAndroidResultOk) {
            qCWarning(WfbngManagerLog) << "VPN consent denied; wfb-ng tunnel not started";
            _refreshTunnelState(tr("VPN permission denied"));
            return;
        }
        QJniObject ctx = QNativeInterface::QAndroidApplication::context();
        QJniObject::callStaticMethod<void>(kJavaVpnClass, "startService", "(Landroid/content/Context;)V", ctx.object());
        qCDebug(WfbngManagerLog) << "VPN consent granted; wfb-ng tunnel start requested";
        QTimer::singleShot(500, this, [this]() { _refreshTunnelState(); });
    });
#else
    _refreshTunnelState(tr("Only available on Android"));
#endif
}

void WfbngManager::stopTunnel()
{
#ifdef Q_OS_ANDROID
    QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (context.isValid()) {
        QJniObject::callStaticMethod<void>(kJavaVpnClass, "stopService", "(Landroid/content/Context;)V", context.object());
        qCDebug(WfbngManagerLog) << "wfb-ng tunnel stop requested";
    }
    QTimer::singleShot(500, this, [this]() { _refreshTunnelState(); });
#endif
}

void WfbngManager::_refreshTunnelState(const QString &statusOverride)
{
    bool active = false;
#ifdef Q_OS_ANDROID
    active = QJniObject::callStaticMethod<jboolean>(kJavaVpnClass, "isRunning", "()Z");
#endif
    QString status = statusOverride;
    if (status.isEmpty()) {
        status = active ? tr("Tunnel up: 10.5.0.3/24 → VTX 10.5.0.10")
                        : (tunnelEnabled() ? tr("Tunnel down") : tr("Tunnel off"));
    }
    if ((active != _tunnelActive) || (status != _tunnelStatus)) {
        _tunnelActive = active;
        _tunnelStatus = status;
        emit tunnelActiveChanged();
    }
}

void WfbngManager::setEnabled(bool enabled)
{
    if (_enabled == enabled) {
        return;
    }
    _enabled = enabled;

    QSettings settings;
    settings.beginGroup(kSettingsGroup);
    settings.setValue("enabled", enabled);
    settings.endGroup();

#ifdef Q_OS_ANDROID
    if (_initialized) {
        s_javaManager.callMethod<void>("setEnabled", "(Z)V", enabled);
    }
#endif
    emit enabledChanged();
}

void WfbngManager::setChannel(int channel)
{
    if (_channel == channel) {
        return;
    }
    _channel = channel;

    QSettings settings;
    settings.beginGroup(kSettingsGroup);
    settings.setValue("channel", channel);
    settings.endGroup();

#ifdef Q_OS_ANDROID
    if (_initialized) {
        s_javaManager.callMethod<void>("setChannel", "(I)V", channel);
    }
#endif
    emit channelChanged();
}

void WfbngManager::setBandwidth(int bandwidth)
{
    if (_bandwidth == bandwidth) {
        return;
    }
    _bandwidth = bandwidth;

    QSettings settings;
    settings.beginGroup(kSettingsGroup);
    settings.setValue("bandwidth", bandwidth);
    settings.endGroup();

#ifdef Q_OS_ANDROID
    if (_initialized) {
        s_javaManager.callMethod<void>("setBandwidth", "(I)V", bandwidth);
    }
#endif
    emit bandwidthChanged();
}

void WfbngManager::setTxPower(int txPower)
{
    if (_txPower == txPower) {
        return;
    }
    _txPower = txPower;

    QSettings settings;
    settings.beginGroup(kSettingsGroup);
    settings.setValue("txPower", txPower);
    settings.endGroup();

#ifdef Q_OS_ANDROID
    if (_initialized) {
        s_javaManager.callMethod<void>("setTxPower", "(I)V", txPower);
    }
#endif
    emit txPowerChanged();
}

void WfbngManager::setAdaptiveLink(bool adaptiveLink)
{
    if (_adaptiveLink == adaptiveLink) {
        return;
    }
    _adaptiveLink = adaptiveLink;

    QSettings settings;
    settings.beginGroup(kSettingsGroup);
    settings.setValue("adaptiveLink", adaptiveLink);
    settings.endGroup();

#ifdef Q_OS_ANDROID
    if (_initialized) {
        s_javaManager.callMethod<void>("setAdaptiveLink", "(Z)V", adaptiveLink);
    }
#endif
    emit adaptiveLinkChanged();
}

void WfbngManager::importGsKey(const QUrl &fileUrl)
{
    const QString source = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    QFile sourceFile(source);
    if (!sourceFile.open(QIODevice::ReadOnly)) {
        qCWarning(WfbngManagerLog) << "Cannot open key file" << source << sourceFile.errorString();
        return;
    }
    const QByteArray keyData = sourceFile.readAll();
    sourceFile.close();
    if (keyData.isEmpty()) {
        qCWarning(WfbngManagerLog) << "Key file is empty:" << source;
        return;
    }

    const QString keyPath = _gsKeyPath();
    if (keyPath.isEmpty()) {
        return;
    }
    QFile target(keyPath);
    if (!target.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qCWarning(WfbngManagerLog) << "Cannot write" << keyPath << target.errorString();
        return;
    }
    (void) target.write(keyData);
    target.close();

    QSettings settings;
    settings.beginGroup(kSettingsGroup);
    settings.setValue("customKey", true);
    settings.endGroup();

#ifdef Q_OS_ANDROID
    if (_initialized) {
        s_javaManager.callMethod<void>("refreshKeyAndRestart");
    }
#endif
    _applyKeyStatus();
    qCDebug(WfbngManagerLog) << "Imported gs.key (" << keyData.size() << "bytes ) from" << source;
}

void WfbngManager::resetGsKey()
{
    if (!_provisionDefaultKey(true)) {
        return;
    }

    QSettings settings;
    settings.beginGroup(kSettingsGroup);
    settings.setValue("customKey", false);
    settings.endGroup();

#ifdef Q_OS_ANDROID
    if (_initialized) {
        s_javaManager.callMethod<void>("refreshKeyAndRestart");
    }
#endif
    _applyKeyStatus();
}

void WfbngManager::restart()
{
#ifdef Q_OS_ANDROID
    if (_initialized) {
        s_javaManager.callMethod<void>("restart");
    }
#endif
}

namespace {
constexpr const char *kRtpCaptureKey = "VideoDebug/rtpCaptureEnabled";
}

bool WfbngManager::rtpCapture() const
{
    QSettings settings;
    return settings.value(QLatin1String(kRtpCaptureKey), false).toBool();
}

void WfbngManager::setRtpCapture(bool enabled)
{
    if (rtpCapture() == enabled) {
        return;
    }

    QSettings settings;
    settings.setValue(QLatin1String(kRtpCaptureKey), enabled);
    emit rtpCaptureChanged();

    // VideoManager reads the flag when it (re)starts a receiver, so bounce the pipeline.
    // stopVideo() is asynchronous; mirror VideoManager's own 1 s restart cadence.
    VideoManager::instance()->stopVideo();
    QTimer::singleShot(1000, VideoManager::instance(), []() { VideoManager::instance()->startVideo(); });
    qCDebug(WfbngManagerLog) << "RTP capture" << (enabled ? "enabled" : "disabled") << "- restarting video";
}

QString WfbngManager::rtpCaptureDir() const
{
    return QDir(SettingsManager::instance()->appSettings()->savePath()->rawValue().toString())
        .filePath(QStringLiteral("RtpCapture"));
}

bool WfbngManager::nativeDecoder() const
{
    QSettings settings;
    settings.beginGroup(kSettingsGroup);
    return settings.value("nativeDecoder", false).toBool();
}

QString WfbngManager::_vtxSetting(const char *key, const QString &defaultValue) const
{
    QSettings settings;
    settings.beginGroup(kSettingsGroup);
    return settings.value(QLatin1String(key), defaultValue).toString();
}

void WfbngManager::_setVtxSetting(const char *key, const QString &value)
{
    if (_vtxSetting(key, QString()) == value) {
        return;
    }
    QSettings settings;
    settings.beginGroup(kSettingsGroup);
    settings.setValue(QLatin1String(key), value);
    settings.endGroup();
    emit vtxSettingsChanged();
    // A live relay keeps the old target/credentials until restarted; the page restarts it.
    stopVtxProxy();
}

QString WfbngManager::vtxHost() const { return _vtxSetting("vtxHost", QStringLiteral("10.5.0.10")); }
void WfbngManager::setVtxHost(const QString &host)
{
    const QString trimmed = host.trimmed();
    if (!trimmed.isEmpty()) {
        _setVtxSetting("vtxHost", trimmed);
    }
}
QString WfbngManager::vtxUser() const { return _vtxSetting("vtxUser", QStringLiteral("root")); }
void WfbngManager::setVtxUser(const QString &user) { _setVtxSetting("vtxUser", user.trimmed()); }
QString WfbngManager::vtxPassword() const { return _vtxSetting("vtxPassword", QStringLiteral("12345")); }
void WfbngManager::setVtxPassword(const QString &password) { _setVtxSetting("vtxPassword", password); }

QString WfbngManager::startVtxProxy()
{
    if (!_vtxProxy) {
        _vtxProxy = new VtxHttpProxy(this);
    }
    QString host = vtxHost();
    quint16 port = 80;
    const int colon = host.lastIndexOf(':');
    if (colon > 0) {
        bool ok = false;
        const int parsed = host.mid(colon + 1).toInt(&ok);
        if (ok && (parsed > 0) && (parsed < 65536)) {
            port = static_cast<quint16>(parsed);
            host = host.left(colon);
        }
    }
    if (!_vtxProxy->start(host, port, vtxUser(), vtxPassword())) {
        return QString();
    }
    return _vtxProxy->baseUrl();
}

void WfbngManager::stopVtxProxy()
{
    if (_vtxProxy) {
        _vtxProxy->stop();
    }
}

void WfbngManager::setNativeDecoder(bool enabled)
{
    if (nativeDecoder() == enabled) {
        return;
    }
    QSettings settings;
    settings.beginGroup(kSettingsGroup);
    settings.setValue("nativeDecoder", enabled);
    settings.endGroup();
    emit nativeDecoderChanged();
    qCDebug(WfbngManagerLog) << "Native decoder" << (enabled ? "enabled" : "disabled") << "- takes effect after app restart";
}

void WfbngManager::updateAdapterCount(int count)
{
    if (_adapterCount != count) {
        _adapterCount = count;
        emit linkStatsChanged();
    }
}

void WfbngManager::updateStats(int rssi, int ok, int recovered, int lost)
{
    _rssi = rssi;
    _packetsOk = ok;
    _packetsRecovered = recovered;
    _packetsLost = lost;
    emit linkStatsChanged();
    // Stats arrive about once a second while an adapter runs — cheap place to keep the
    // tunnel indicator honest (the service can be revoked by the system at any time).
    _refreshTunnelState();
}
