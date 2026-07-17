#include "WfbngManager.h"

#include "QGCLoggingCategory.h"

#include <QtCore/QApplicationStatic>
#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <QtCore/QSettings>

#ifdef Q_OS_ANDROID
#include <QtCore/QJniEnvironment>
#include <QtCore/QJniObject>
#endif

QGC_LOGGING_CATEGORY(WfbngManagerLog, "QGroundPixel.WfbngManager")

Q_APPLICATION_STATIC(WfbngManager, _wfbngManagerInstance);

namespace {
constexpr const char *kSettingsGroup = "QGroundPixelWfbng";
constexpr const char *kDefaultKeyResource = ":/Custom/wfb/gs.key";
constexpr const char *kJavaManagerClass = "org/mavlink/qgroundcontrol/wfb/QGCWfbManager";

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
#endif

    _applyKeyStatus();
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
}
