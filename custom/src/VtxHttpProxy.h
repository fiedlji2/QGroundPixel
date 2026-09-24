#pragma once

#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtNetwork/QTcpServer>

Q_DECLARE_LOGGING_CATEGORY(VtxHttpProxyLog)

/// Minimal HTTP relay for the in-app VTX settings browser.
///
/// The VTX's web UI (OpenIPC Majestic) protects itself with HTTP Basic auth, and the
/// Android WebView behind Qt WebView has no way to answer the 401 challenge (it reports
/// net::ERR_HTTP_RESPONSE_CODE_FAILURE instead). This relay listens on 127.0.0.1 and, for
/// every client connection, rewrites the first request head — sets the Host header, adds
/// `Authorization: Basic …` — then pipes bytes verbatim in both directions. Nothing else is
/// interpreted, so chunked responses, streaming and WebSocket upgrades work unchanged.
/// Absolute `Location:` redirects to the VTX are rewritten back to the relay address so
/// the browser never leaves it.
class VtxHttpProxy : public QObject
{
    Q_OBJECT

public:
    explicit VtxHttpProxy(QObject *parent = nullptr);

    /// Start listening (idempotent; restarts when the target or credentials changed).
    bool start(const QString &targetHost, quint16 targetPort, const QString &user, const QString &password);
    void stop();

    bool isListening() const { return _server.isListening(); }
    quint16 port() const { return _server.serverPort(); }
    /// URL the WebView should load, e.g. "http://127.0.0.1:38412/".
    QString baseUrl() const;

private:
    void _onNewConnection();

    QTcpServer _server;
    QString _targetHost;
    quint16 _targetPort = 80;
    QByteArray _authHeaderValue;   // "Basic base64(user:pass)", empty when no credentials
};
