#include "VtxHttpProxy.h"

#include "QGCLoggingCategory.h"

#include <QtCore/QList>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QTcpSocket>

QGC_LOGGING_CATEGORY(VtxHttpProxyLog, "QGroundPixel.VtxHttpProxy")

namespace {

constexpr int kMaxHeadBytes = 64 * 1024;

/// One browser connection relayed to the VTX. Owns both sockets; deletes itself when
/// either side closes.
class ProxyConnection : public QObject
{
public:
    ProxyConnection(QTcpSocket *client, const QString &targetHost, quint16 targetPort,
                    const QByteArray &authHeaderValue, const QByteArray &relayBase, QObject *parent)
        : QObject(parent)
        , _client(client)
        , _target(new QTcpSocket(this))
        , _targetHost(targetHost)
        , _targetPort(targetPort)
        , _authHeaderValue(authHeaderValue)
        , _relayBase(relayBase)
    {
        _client->setParent(this);

        (void) connect(_client, &QTcpSocket::readyRead, this, &ProxyConnection::_onClientData);
        (void) connect(_client, &QTcpSocket::disconnected, this, &ProxyConnection::_finish);
        (void) connect(_target, &QTcpSocket::readyRead, this, &ProxyConnection::_onTargetData);
        (void) connect(_target, &QTcpSocket::disconnected, this, &ProxyConnection::_finish);
        (void) connect(_target, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
            qCDebug(VtxHttpProxyLog) << "target socket error:" << _target->errorString();
            _finish();
        });
        (void) connect(_target, &QTcpSocket::connected, this, [this]() {
            _targetConnected = true;
            if (!_pendingToTarget.isEmpty()) {
                (void) _target->write(_pendingToTarget);
                _pendingToTarget.clear();
            }
        });
    }

private:
    void _onClientData()
    {
        QByteArray data = _client->readAll();
        if (!_headRewritten) {
            _head.append(data);
            const qsizetype end = _head.indexOf("\r\n\r\n");
            if (end < 0) {
                if (_head.size() > kMaxHeadBytes) {
                    qCWarning(VtxHttpProxyLog) << "request head too large, dropping connection";
                    _finish();
                }
                return;
            }
            const QByteArray rest = _head.mid(end + 4);
            data = _rewriteRequestHead(_head.left(end)) + "\r\n\r\n" + rest;
            _head.clear();
            _headRewritten = true;
            _target->connectToHost(_targetHost, _targetPort);
        }
        if (_targetConnected) {
            (void) _target->write(data);
        } else {
            _pendingToTarget.append(data);
        }
    }

    void _onTargetData()
    {
        QByteArray data = _target->readAll();
        if (!_responseHeadSeen) {
            _responseHead.append(data);
            const qsizetype end = _responseHead.indexOf("\r\n\r\n");
            if (end < 0) {
                if (_responseHead.size() > kMaxHeadBytes) {
                    // Not HTTP-shaped at all; just pass everything through from here on.
                    _responseHeadSeen = true;
                    data = _responseHead;
                    _responseHead.clear();
                } else {
                    return;
                }
            } else {
                data = _rewriteResponseHead(_responseHead.left(end)) + _responseHead.mid(end);
                _responseHead.clear();
                _responseHeadSeen = true;
            }
        }
        (void) _client->write(data);
    }

    QByteArray _rewriteRequestHead(const QByteArray &head) const
    {
        QList<QByteArray> lines = head.split('\n');
        for (QByteArray &line : lines) {
            if (line.endsWith('\r')) line.chop(1);
        }
        if (lines.isEmpty()) {
            return head;
        }

        bool upgrade = false;
        QList<QByteArray> out;
        out.append(lines.first());   // request line, untouched (origin-form target)
        for (qsizetype i = 1; i < lines.size(); ++i) {
            const QByteArray &line = lines.at(i);
            const qsizetype colon = line.indexOf(':');
            if (colon <= 0) {
                continue;
            }
            const QByteArray name = line.left(colon).trimmed().toLower();
            if (name == "upgrade") {
                upgrade = true;
            }
            if (name == "host" || name == "authorization" || name == "proxy-authorization"
                || name == "keep-alive" || (name == "connection" && !upgrade)) {
                continue;
            }
            out.append(line);
        }
        // "connection: upgrade" (WebSocket) must survive; anything else becomes one request
        // per TCP connection so only the first head ever needs rewriting.
        if (upgrade) {
            for (qsizetype i = 1; i < lines.size(); ++i) {
                if (lines.at(i).left(11).toLower() == "connection:" && !out.contains(lines.at(i))) {
                    out.append(lines.at(i));
                }
            }
        } else {
            out.append("Connection: close");
        }
        out.append("Host: " + _targetHostHeader());
        if (!_authHeaderValue.isEmpty()) {
            out.append("Authorization: " + _authHeaderValue);
        }
        return out.join("\r\n");
    }

    QByteArray _rewriteResponseHead(const QByteArray &head) const
    {
        // Rewrite absolute redirects to the VTX back to the relay so the browser stays on it.
        QList<QByteArray> lines = head.split('\n');
        for (QByteArray &line : lines) {
            if (line.endsWith('\r')) line.chop(1);
            if (line.left(9).toLower() == "location:") {
                const QByteArray value = line.mid(9).trimmed();
                const QByteArray hostPrefix = "http://" + _targetHostHeader();
                const QByteArray bareHostPrefix = "http://" + _targetHost.toUtf8();
                if (value.startsWith(hostPrefix)) {
                    line = "Location: " + _relayBase + value.mid(hostPrefix.size());
                } else if (value.startsWith(bareHostPrefix)) {
                    line = "Location: " + _relayBase + value.mid(bareHostPrefix.size());
                }
            }
        }
        return lines.join("\r\n");
    }

    QByteArray _targetHostHeader() const
    {
        return (_targetPort == 80) ? _targetHost.toUtf8() : (_targetHost.toUtf8() + ":" + QByteArray::number(_targetPort));
    }

    void _finish()
    {
        if (_finished) {
            return;
        }
        _finished = true;
        // Flush what the other side still has before tearing down.
        if (_client->state() == QAbstractSocket::ConnectedState) {
            _client->disconnectFromHost();
        }
        if (_target->state() == QAbstractSocket::ConnectedState) {
            _target->disconnectFromHost();
        }
        deleteLater();
    }

    QTcpSocket *_client = nullptr;
    QTcpSocket *_target = nullptr;
    QString _targetHost;
    quint16 _targetPort = 80;
    QByteArray _authHeaderValue;
    QByteArray _relayBase;
    QByteArray _head;
    QByteArray _responseHead;
    QByteArray _pendingToTarget;
    bool _headRewritten = false;
    bool _responseHeadSeen = false;
    bool _targetConnected = false;
    bool _finished = false;
};

} // namespace

VtxHttpProxy::VtxHttpProxy(QObject *parent)
    : QObject(parent)
{
    (void) connect(&_server, &QTcpServer::newConnection, this, &VtxHttpProxy::_onNewConnection);
}

bool VtxHttpProxy::start(const QString &targetHost, quint16 targetPort, const QString &user, const QString &password)
{
    const QByteArray auth = (user.isEmpty() && password.isEmpty())
        ? QByteArray()
        : ("Basic " + (user.toUtf8() + ":" + password.toUtf8()).toBase64());

    if (_server.isListening() && (targetHost == _targetHost) && (targetPort == _targetPort) && (auth == _authHeaderValue)) {
        return true;
    }
    stop();

    _targetHost = targetHost;
    _targetPort = targetPort;
    _authHeaderValue = auth;

    if (!_server.listen(QHostAddress::LocalHost, 0)) {
        qCWarning(VtxHttpProxyLog) << "listen failed:" << _server.errorString();
        return false;
    }
    qCDebug(VtxHttpProxyLog) << "relaying" << baseUrl() << "->" << targetHost << ":" << targetPort
                             << (auth.isEmpty() ? "(no auth)" : "(basic auth)");
    return true;
}

void VtxHttpProxy::stop()
{
    if (_server.isListening()) {
        _server.close();
    }
    // Live connections are the only children of this object (the server is a member);
    // drop them so nothing keeps piping.
    const QObjectList liveConnections = children();
    for (QObject *connection : liveConnections) {
        connection->deleteLater();
    }
}

QString VtxHttpProxy::baseUrl() const
{
    return _server.isListening() ? QStringLiteral("http://127.0.0.1:%1/").arg(_server.serverPort()) : QString();
}

void VtxHttpProxy::_onNewConnection()
{
    while (QTcpSocket *client = _server.nextPendingConnection()) {
        const QByteArray relayBase = QStringLiteral("http://127.0.0.1:%1").arg(_server.serverPort()).toUtf8();
        (void) new ProxyConnection(client, _targetHost, _targetPort, _authHeaderValue, relayBase, this);
    }
}
