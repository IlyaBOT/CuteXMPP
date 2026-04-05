#include "src/xmpp/xmpp_service.h"

#include "src/logging/app_logger.h"
#include "src/settings/app_settings.h"
#include "src/settings/settings_store.h"
#include "src/ui/theme.h"

#include <QBuffer>
#include <QFileInfo>
#include <QImage>
#include <QMap>
#include <QNetworkProxy>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <QXmlStreamWriter>

#include <QXmppClient.h>
#include <QXmppBookmarkManager.h>
#include <QXmppBookmarkSet.h>
#include <QXmppConfiguration.h>
#include <QXmppCredentials.h>
#include <QXmppDataForm.h>
#include <QXmppDiscoveryIq.h>
#include <QXmppDiscoveryManager.h>
#include <QXmppError.h>
#include <QXmppHttpUploadManager.h>
#include <QXmppLogger.h>
#include <QXmppMamManager.h>
#include <QXmppMessage.h>
#include <QXmppMucForms.h>
#include <QXmppMucManager.h>
#include <QXmppMucManagerV2.h>
#include <QXmppOutOfBandUrl.h>
#include <QXmppPresence.h>
#include <QXmppPubSubManager.h>
#include <QXmppRegisterIq.h>
#include <QXmppRegistrationManager.h>
#include <QXmppResultSet.h>
#include <QXmppRosterIq.h>
#include <QXmppRosterManager.h>
#include <QXmppSasl2UserAgent.h>
#include <QXmppSendResult.h>
#include <QXmppStanza.h>
#include <QXmppTask.h>
#include <QXmppVCardIq.h>
#include <QXmppVCardManager.h>

#include <algorithm>
#include <array>

#ifdef Q_OS_WIN
#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <mutex>
#include <thread>
#endif

namespace CuteXmpp {

#ifdef Q_OS_WIN
class NativeLoopbackRelay
{
public:
    NativeLoopbackRelay(QString remoteHost, quint16 remotePort)
        : m_remoteHost(std::move(remoteHost))
        , m_remotePort(remotePort)
    {
    }

    ~NativeLoopbackRelay()
    {
        stop();
    }

    bool start(QString* errorMessage)
    {
        static const bool winsockReady = []() {
            WSADATA data;
            return WSAStartup(MAKEWORD(2, 2), &data) == 0;
        }();
        if (!winsockReady) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("WSAStartup failed.");
            }
            return false;
        }

        m_listenSocket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (m_listenSocket == INVALID_SOCKET) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Failed to create relay listen socket.");
            }
            return false;
        }

        sockaddr_in address {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        if (::bind(m_listenSocket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Failed to bind relay listen socket.");
            }
            closeSocket(m_listenSocket);
            m_listenSocket = INVALID_SOCKET;
            return false;
        }

        if (::listen(m_listenSocket, 1) == SOCKET_ERROR) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Failed to listen on relay socket.");
            }
            closeSocket(m_listenSocket);
            m_listenSocket = INVALID_SOCKET;
            return false;
        }

        sockaddr_in boundAddress {};
        int boundAddressSize = sizeof(boundAddress);
        if (::getsockname(m_listenSocket, reinterpret_cast<sockaddr*>(&boundAddress), &boundAddressSize) == SOCKET_ERROR) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Failed to determine relay listen port.");
            }
            closeSocket(m_listenSocket);
            m_listenSocket = INVALID_SOCKET;
            return false;
        }

        m_localPort = ntohs(boundAddress.sin_port);
        m_thread = std::thread([this]() { run(); });
        return true;
    }

    void stop()
    {
        m_stopRequested.store(true);
        closeOwnedSockets();
        if (m_thread.joinable()) {
            m_thread.join();
        }
        m_localPort = 0;
    }

    quint16 localPort() const
    {
        return m_localPort;
    }

private:
    static void closeSocket(SOCKET socket)
    {
        if (socket != INVALID_SOCKET) {
            ::closesocket(socket);
        }
    }

    static void enableSocketOptions(SOCKET socket)
    {
        if (socket == INVALID_SOCKET) {
            return;
        }

        const BOOL enabled = TRUE;
        const DWORD timeoutMs = 15000;
        ::setsockopt(socket, SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<const char*>(&enabled), sizeof(enabled));
        ::setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&enabled), sizeof(enabled));
        ::setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
        ::setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
    }

    void closeOwnedSockets()
    {
        std::scoped_lock lock(m_socketMutex);
        closeSocket(m_listenSocket);
        closeSocket(m_localSocket);
        closeSocket(m_remoteSocket);
        m_listenSocket = INVALID_SOCKET;
        m_localSocket = INVALID_SOCKET;
        m_remoteSocket = INVALID_SOCKET;
    }

    SOCKET connectRemote()
    {
        addrinfoW hints {};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        addrinfoW* results = nullptr;
        const QString portString = QString::number(m_remotePort);
        if (::GetAddrInfoW(reinterpret_cast<LPCWSTR>(m_remoteHost.utf16()),
                           reinterpret_cast<LPCWSTR>(portString.utf16()),
                           &hints,
                           &results) != 0) {
            return INVALID_SOCKET;
        }

        SOCKET socket = INVALID_SOCKET;
        for (addrinfoW* current = results; current; current = current->ai_next) {
            socket = ::socket(current->ai_family, current->ai_socktype, current->ai_protocol);
            if (socket == INVALID_SOCKET) {
                continue;
            }
            enableSocketOptions(socket);
            if (::connect(socket, current->ai_addr, static_cast<int>(current->ai_addrlen)) == 0) {
                break;
            }
            closeSocket(socket);
            socket = INVALID_SOCKET;
        }

        ::FreeAddrInfoW(results);
        return socket;
    }

    void forwardLoop(SOCKET source, SOCKET destination, const char* sourceName, const char* destinationName)
    {
        constexpr int bufferSize = 64 * 1024;
        std::array<char, bufferSize> buffer {};
        bool loggedFirstPacket = false;

        while (!m_stopRequested.load()) {
            const int received = ::recv(source, buffer.data(), static_cast<int>(buffer.size()), 0);
            if (received == 0) {
                logDebug(QStringLiteral("XMPP relay: %1 closed the socket.").arg(QString::fromUtf8(sourceName)));
                break;
            }
            if (received < 0) {
                logDebug(QStringLiteral("XMPP relay: recv failed while forwarding %1 -> %2 (WSA=%3)")
                             .arg(QString::fromUtf8(sourceName), QString::fromUtf8(destinationName))
                             .arg(WSAGetLastError()));
                break;
            }

            if (!loggedFirstPacket) {
                loggedFirstPacket = true;
                logDebug(QStringLiteral("XMPP relay: first payload %1 -> %2 (%3 bytes)")
                             .arg(QString::fromUtf8(sourceName), QString::fromUtf8(destinationName))
                             .arg(received));
            }

            int writtenTotal = 0;
            while (writtenTotal < received) {
                const int written = ::send(destination, buffer.data() + writtenTotal, received - writtenTotal, 0);
                if (written <= 0) {
                    logDebug(QStringLiteral("XMPP relay: send failed while forwarding %1 -> %2 (WSA=%3)")
                                 .arg(QString::fromUtf8(sourceName), QString::fromUtf8(destinationName))
                                 .arg(WSAGetLastError()));
                    m_stopRequested.store(true);
                    closeOwnedSockets();
                    return;
                }
                writtenTotal += written;
            }
        }

        m_stopRequested.store(true);
        closeOwnedSockets();
    }

    void run()
    {
        SOCKET acceptedSocket = INVALID_SOCKET;
        SOCKET listenSocket = INVALID_SOCKET;
        {
            std::scoped_lock lock(m_socketMutex);
            listenSocket = m_listenSocket;
        }
        if (listenSocket == INVALID_SOCKET) {
            return;
        }

        acceptedSocket = ::accept(listenSocket, nullptr, nullptr);

        if (acceptedSocket == INVALID_SOCKET || m_stopRequested.load()) {
            closeSocket(acceptedSocket);
            return;
        }
        enableSocketOptions(acceptedSocket);
        logDebug(QStringLiteral("XMPP relay: accepted local connection on 127.0.0.1:%1").arg(m_localPort));

        SOCKET remoteSocket = connectRemote();
        if (remoteSocket == INVALID_SOCKET) {
            logDebug(QStringLiteral("XMPP relay: failed to connect to remote host %1:%2").arg(m_remoteHost).arg(m_remotePort));
            closeSocket(acceptedSocket);
            return;
        }
        logDebug(QStringLiteral("XMPP relay: connected to remote host %1:%2").arg(m_remoteHost).arg(m_remotePort));

        {
            std::scoped_lock lock(m_socketMutex);
            m_localSocket = acceptedSocket;
            m_remoteSocket = remoteSocket;
        }

        std::thread clientToServer([this, acceptedSocket, remoteSocket]() {
            forwardLoop(acceptedSocket, remoteSocket, "client", "server");
        });
        std::thread serverToClient([this, remoteSocket, acceptedSocket]() {
            forwardLoop(remoteSocket, acceptedSocket, "server", "client");
        });

        if (clientToServer.joinable()) {
            clientToServer.join();
        }
        if (serverToClient.joinable()) {
            serverToClient.join();
        }

        closeOwnedSockets();
    }

    QString m_remoteHost;
    quint16 m_remotePort = 0;
    std::atomic_bool m_stopRequested = false;
    std::mutex m_socketMutex;
    std::thread m_thread;
    SOCKET m_listenSocket = INVALID_SOCKET;
    SOCKET m_localSocket = INVALID_SOCKET;
    SOCKET m_remoteSocket = INVALID_SOCKET;
    quint16 m_localPort = 0;
};
#else
class NativeLoopbackRelay
{
public:
    NativeLoopbackRelay(QString, quint16) {}
    bool start(QString*)
    {
        return false;
    }
    void stop() {}
    quint16 localPort() const
    {
        return 0;
    }
};
#endif

namespace {

QString bareJidOf(const QString& jid)
{
    return jid.section('/', 0, 0).trimmed();
}

QString userFromJid(const QString& jid)
{
    return bareJidOf(jid).section('@', 0, 0).trimmed();
}

QString domainFromJid(const QString& jid)
{
    return bareJidOf(jid).section('@', 1, 1).trimmed();
}

QString resourceFromJid(const QString& jid)
{
    return jid.section('/', 1, 1).trimmed();
}

QString credentialKey(const QString& bareJid)
{
    return QStringLiteral("accounts/%1/credentials").arg(bareJid);
}

QString configuredHostText(const QString& host)
{
    return host.trimmed().isEmpty() ? QStringLiteral("<auto>") : host.trimmed();
}

bool isLocalLoopbackHost(const QString& host)
{
    const QString normalized = host.trimmed().toLower();
    return normalized.isEmpty()
        || normalized == QStringLiteral("localhost")
        || normalized == QStringLiteral("127.0.0.1")
        || normalized == QStringLiteral("::1");
}

QString maskedEndpoint(const QString& jid, const QString& domain, const QString& host, quint16 port, const QString& password)
{
    return QStringLiteral("jid=%1, domain=%2, host=%3, port=%4, password=%5")
        .arg(jid, domain, configuredHostText(host))
        .arg(port)
        .arg(maskSecret(password));
}

QString proxyModeText(ProxyMode mode)
{
    switch (mode) {
    case ProxyMode::NoProxy:
        return "no-proxy";
    case ProxyMode::Tor:
        return "tor";
    case ProxyMode::TorBrowser:
        return "tor-browser";
    case ProxyMode::System:
    default:
        return "system";
    }
}

QString tlsModeText(TlsMode mode)
{
    switch (mode) {
    case TlsMode::DirectTls:
        return "direct-tls";
    case TlsMode::Plain:
        return "plain";
    case TlsMode::StartTls:
    default:
        return "starttls";
    }
}

QXmppConfiguration::StreamSecurityMode streamSecurityModeFor(TlsMode mode)
{
    switch (mode) {
    case TlsMode::DirectTls:
        return QXmppConfiguration::LegacySSL;
    case TlsMode::Plain:
        return QXmppConfiguration::TLSDisabled;
    case TlsMode::StartTls:
    default:
        return QXmppConfiguration::TLSEnabled;
    }
}

QNetworkProxy networkProxyFor(ProxyMode mode)
{
    switch (mode) {
    case ProxyMode::NoProxy:
        return QNetworkProxy(QNetworkProxy::NoProxy);
    case ProxyMode::Tor:
        return QNetworkProxy(QNetworkProxy::Socks5Proxy, "localhost", 9050);
    case ProxyMode::TorBrowser:
        return QNetworkProxy(QNetworkProxy::Socks5Proxy, "localhost", 9150);
    case ProxyMode::System:
    default:
        return QNetworkProxy(QNetworkProxy::DefaultProxy);
    }
}

QString proxyDetails(ProxyMode mode)
{
    switch (mode) {
    case ProxyMode::NoProxy:
        return "none";
    case ProxyMode::Tor:
        return "localhost:9050 (SOCKS5)";
    case ProxyMode::TorBrowser:
        return "localhost:9150 (SOCKS5)";
    case ProxyMode::System:
    default:
        return "system";
    }
}

bool shouldUseNativeLoopbackRelay(const QString& host, ProxyMode proxyMode, TlsMode tlsMode)
{
#ifdef Q_OS_WIN
    return !host.trimmed().isEmpty()
        && !isLocalLoopbackHost(host)
        && tlsMode != TlsMode::DirectTls
        && (proxyMode == ProxyMode::System || proxyMode == ProxyMode::NoProxy);
#else
    Q_UNUSED(host);
    Q_UNUSED(proxyMode);
    Q_UNUSED(tlsMode);
    return false;
#endif
}

bool isCallableOnCurrentThread(const QObject* object)
{
    const QThread* objectThread = object ? object->thread() : nullptr;
    return !objectThread || objectThread == QThread::currentThread() || !objectThread->isRunning();
}

QVector<LoginRequest> buildCandidateLoginAttempts(const LoginRequest& request)
{
    QVector<LoginRequest> attempts;
    QSet<QString> seen;

    const auto addAttempt = [&](const LoginRequest& candidate) {
        const QString key = QStringLiteral("%1|%2|%3|%4|%5|%6")
                                .arg(candidate.jid, candidate.server, candidate.connectHost)
                                .arg(candidate.port)
                                .arg(static_cast<int>(candidate.tlsMode))
                                .arg(static_cast<int>(candidate.proxyMode));
        if (seen.contains(key)) {
            return;
        }
        seen.insert(key);
        attempts.push_back(candidate);
    };

    addAttempt(request);

    for (int i = 0; i < 2; ++i) {
        addAttempt(request);
    }

    if (request.proxyMode == ProxyMode::System) {
        LoginRequest noProxy = request;
        noProxy.proxyMode = ProxyMode::NoProxy;
        addAttempt(noProxy);
        addAttempt(noProxy);
    }

    if (request.server.startsWith(QStringLiteral("xmpp."), Qt::CaseInsensitive)) {
        const QString baseDomain = request.server.mid(5).trimmed();
        if (!baseDomain.isEmpty()) {
            LoginRequest baseDomainAttempt = request;
            baseDomainAttempt.jid = QStringLiteral("%1@%2").arg(userFromJid(request.jid), baseDomain);
            baseDomainAttempt.server = baseDomain;
            if (baseDomainAttempt.connectHost.isEmpty()) {
                baseDomainAttempt.connectHost = request.server;
            }
            addAttempt(baseDomainAttempt);

            if (request.proxyMode == ProxyMode::System) {
                LoginRequest baseDomainNoProxy = baseDomainAttempt;
                baseDomainNoProxy.proxyMode = ProxyMode::NoProxy;
                addAttempt(baseDomainNoProxy);
            }
        }
    }

    return attempts;
}

QString serializeCredentials(const QXmppCredentials& credentials)
{
    QString xml;
    QXmlStreamWriter writer(&xml);
    credentials.toXml(writer);
    return xml;
}

PresenceState presenceFromXmpp(const QXmppPresence& presence)
{
    if (presence.type() == QXmppPresence::Unavailable || presence.type() == QXmppPresence::Error) {
        return PresenceState::Offline;
    }

    switch (presence.availableStatusType()) {
    case QXmppPresence::DND:
        return PresenceState::Busy;
    case QXmppPresence::Away:
    case QXmppPresence::XA:
        return PresenceState::Away;
    case QXmppPresence::Online:
    case QXmppPresence::Chat:
    case QXmppPresence::Invisible:
    default:
        return PresenceState::Online;
    }
}

QString stanzaErrorText(const QXmppStanza::Error& error)
{
    if (!error.text().trimmed().isEmpty()) {
        return error.text().trimmed();
    }

    switch (error.condition()) {
    case QXmppStanza::Error::Conflict:
        return "This username already exists on the server.";
    case QXmppStanza::Error::FeatureNotImplemented:
        return "The server does not support in-band registration.";
    case QXmppStanza::Error::JidMalformed:
        return "The server rejected the username format.";
    case QXmppStanza::Error::NotAcceptable:
        return "The server rejected the provided registration data.";
    case QXmppStanza::Error::NotAllowed:
        return "The server does not allow creating accounts right now.";
    case QXmppStanza::Error::NotAuthorized:
        return "Authentication failed.";
    default:
        return "The server returned an XMPP error.";
    }
}

QString qxmppErrorText(const QXmppError& error)
{
    if (!error.description.trimmed().isEmpty()) {
        return error.description.trimmed();
    }
    if (const auto stanza = error.value<QXmppStanza::Error>(); stanza.has_value()) {
        return stanzaErrorText(*stanza);
    }
    return "The XMPP connection failed.";
}

bool appendMessageIfMissing(QVector<MessageEntry>& messages, const MessageEntry& entry)
{
    if (!entry.id.isEmpty()) {
        for (const MessageEntry& existing : messages) {
            if (!existing.id.isEmpty() && existing.id == entry.id) {
                return false;
            }
        }
    } else {
        for (const MessageEntry& existing : messages) {
            if (existing.senderJid == entry.senderJid && existing.text == entry.text && existing.timestamp == entry.timestamp) {
                return false;
            }
        }
    }

    messages.append(entry);
    return true;
}

void trimConversationBuffer(QVector<MessageEntry>& conversation, bool keepOldest)
{
    constexpr int kMaxBufferedMessagesPerChat = 160;
    if (conversation.size() <= kMaxBufferedMessagesPerChat) {
        return;
    }

    if (keepOldest) {
        conversation = conversation.first(kMaxBufferedMessagesPerChat);
    } else {
        conversation = conversation.sliced(conversation.size() - kMaxBufferedMessagesPerChat);
    }
}

QString previewTextForMessage(const QString& text)
{
    QString preview = text.trimmed();
    if (preview.startsWith(QStringLiteral("```"))) {
        return "[Code]";
    }

    preview.replace(QStringLiteral("```"), QString());
    preview.replace(QChar('*'), QString());
    preview.replace(QChar('_'), QString());
    preview.replace(QChar('~'), QString());
    preview.replace(QChar('`'), QString());
    preview.replace(QChar('\n'), QChar(' '));
    preview = preview.simplified();
    return preview.left(160);
}

QString messageDisplayText(const QXmppMessage& message)
{
    const QString body = message.body().trimmed();
    if (!body.isEmpty()) {
        return body;
    }

    const QString outOfBandUrl = message.outOfBandUrl().trimmed();
    if (!outOfBandUrl.isEmpty()) {
        return outOfBandUrl;
    }

    const auto outOfBandUrls = message.outOfBandUrls();
    if (!outOfBandUrls.isEmpty()) {
        return outOfBandUrls.constFirst().url().trimmed();
    }

    return {};
}

}  // namespace

XmppService::XmppService(AppSettings* settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_client(new QXmppClient(this))
{
    auto* xmppLogger = new QXmppLogger(this);
    xmppLogger->setLoggingType(QXmppLogger::SignalLogging);
    xmppLogger->setMessageTypes(QXmppLogger::AnyMessage);
    m_client->setLogger(xmppLogger);

    connect(xmppLogger, &QXmppLogger::message, this, [this](QXmppLogger::MessageType type, const QString& text) {
        switch (type) {
        case QXmppLogger::WarningMessage:
            logError(QStringLiteral("QXmpp: %1").arg(text));
            break;
        case QXmppLogger::InformationMessage:
            logInfo(QStringLiteral("QXmpp: %1").arg(text));
            break;
        case QXmppLogger::ReceivedMessage:
        case QXmppLogger::SentMessage:
        case QXmppLogger::DebugMessage:
        case QXmppLogger::NoMessage:
        default:
            logDebug(QStringLiteral("QXmpp: %1").arg(text));
            break;
        }
    });

    m_registrationManager = m_client->addNewExtension<QXmppRegistrationManager>();
    m_mamManager = m_client->addNewExtension<QXmppMamManager>();
    m_bookmarkManager = m_client->addNewExtension<QXmppBookmarkManager>();
    m_discoveryManager = m_client->addNewExtension<QXmppDiscoveryManager>();
    m_httpUploadManager = m_client->addNewExtension<QXmppHttpUploadManager>();
    m_pubSubManager = m_client->addNewExtension<QXmppPubSubManager>();
    m_mucManager = m_client->addNewExtension<QXmppMucManager>();
    m_mucManagerV2 = m_client->addNewExtension<QXmppMucManagerV2>();
    m_rosterManager = m_client->findExtension<QXmppRosterManager>();
    m_vCardManager = m_client->findExtension<QXmppVCardManager>();

    logInfo(QStringLiteral("XMPP service initialized. Settings path: %1").arg(appDataPath()));
    connectSignals();
}

XmppService::~XmppService() = default;

void XmppService::stopConnectionRelay()
{
    if (!m_connectionRelay) {
        return;
    }
    m_connectionRelay->stop();
    m_connectionRelay.reset();
}

std::optional<AccountSession> XmppService::session() const
{
    if (!isCallableOnCurrentThread(this)) {
        std::optional<AccountSession> result;
        QMetaObject::invokeMethod(const_cast<XmppService*>(this), [&]() {
            result = m_session;
        }, Qt::BlockingQueuedConnection);
        return result;
    }

    return m_session;
}

QVector<ChatSummary> XmppService::chats() const
{
    auto buildChats = [this]() {
        QVector<ChatSummary> result = m_chats.values().toVector();
        std::sort(result.begin(), result.end(), [](const ChatSummary& left, const ChatSummary& right) {
            return left.lastActivity > right.lastActivity;
        });
        return result;
    };

    if (!isCallableOnCurrentThread(this)) {
        QVector<ChatSummary> result;
        QMetaObject::invokeMethod(const_cast<XmppService*>(this), [&]() {
            result = buildChats();
        }, Qt::BlockingQueuedConnection);
        return result;
    }

    return buildChats();
}

QVector<MessageEntry> XmppService::messages(const QString& chatId) const
{
    if (!isCallableOnCurrentThread(this)) {
        QVector<MessageEntry> result;
        QMetaObject::invokeMethod(const_cast<XmppService*>(this), [&, chatId]() {
            result = m_messages.value(chatId);
        }, Qt::BlockingQueuedConnection);
        return result;
    }

    return m_messages.value(chatId);
}

bool XmppService::canLoadOlderMessages(const QString& chatId) const
{
    if (!isCallableOnCurrentThread(this)) {
        bool result = false;
        QMetaObject::invokeMethod(const_cast<XmppService*>(this), [&, chatId]() {
            const auto it = m_historyStates.constFind(chatId);
            result = it != m_historyStates.cend() && it->initialLoaded && it->hasMore && !it->loading;
        }, Qt::BlockingQueuedConnection);
        return result;
    }

    const auto it = m_historyStates.constFind(chatId);
    return it != m_historyStates.cend() && it->initialLoaded && it->hasMore && !it->loading;
}

bool XmppService::isHistoryLoading(const QString& chatId) const
{
    if (!isCallableOnCurrentThread(this)) {
        bool result = false;
        QMetaObject::invokeMethod(const_cast<XmppService*>(this), [&, chatId]() {
            const auto it = m_historyStates.constFind(chatId);
            result = it != m_historyStates.cend() && it->loading;
        }, Qt::BlockingQueuedConnection);
        return result;
    }

    const auto it = m_historyStates.constFind(chatId);
    return it != m_historyStates.cend() && it->loading;
}

void XmppService::login(const LoginRequest& rawRequest)
{
    if (!isCallableOnCurrentThread(this)) {
        QMetaObject::invokeMethod(this, [this, rawRequest]() {
            login(rawRequest);
        }, Qt::QueuedConnection);
        return;
    }

    const QString jid = bareJidOf(rawRequest.jid);
    const QString username = userFromJid(jid);
    const QString server = rawRequest.server.trimmed().isEmpty() ? domainFromJid(jid) : rawRequest.server.trimmed();
    const QString connectHost = rawRequest.connectHost.trimmed();
    if (username.isEmpty()) {
        emit authenticationFailed("Username is required.");
        return;
    }
    if (rawRequest.password.isEmpty()) {
        emit authenticationFailed("Password is required.");
        return;
    }
    if (server.isEmpty()) {
        emit authenticationFailed("Server is required.");
        return;
    }

    LoginRequest request = rawRequest;
    request.jid = jid;
    request.server = server;
    request.connectHost = connectHost;

    startLoginSequence(request);
}

void XmppService::startLoginSequence(const LoginRequest& request)
{
    m_loginAttempts = buildLoginAttempts(request);
    m_loginAttemptIndex = 0;
    connectLoginAttempt(m_loginAttempts.constFirst());
}

QVector<LoginRequest> XmppService::buildLoginAttempts(const LoginRequest& request) const
{
    return buildCandidateLoginAttempts(request);
}

void XmppService::connectLoginAttempt(const LoginRequest& request)
{
    const QString jid = bareJidOf(request.jid);
    const QString username = userFromJid(jid);

    stopConnectionRelay();
    resetSessionState();
    m_pendingLogin = request;
    m_pendingOperation = PendingOperation::Login;
    m_pendingRegistration.reset();
    m_registrationManager->setRegisterOnConnectEnabled(false);

    logInfo(QStringLiteral("Starting login with %1, tls=%2, proxy=%3 (%4)")
                .arg(maskedEndpoint(request.jid, request.server, request.connectHost, request.port, request.password),
                     tlsModeText(request.tlsMode),
                     proxyModeText(request.proxyMode),
                     proxyDetails(request.proxyMode)));

    QXmppConfiguration configuration;
    configuration.setUser(username);
    configuration.setPassword(request.password);
    configuration.setDomain(request.server);
    QString socketHost = request.connectHost;
    const QString relayTargetHost = socketHost.isEmpty() ? request.server : socketHost;
    quint16 socketPort = request.port;
    QNetworkProxy proxy = networkProxyFor(request.proxyMode);

    if (shouldUseNativeLoopbackRelay(relayTargetHost, request.proxyMode, request.tlsMode)) {
#ifdef Q_OS_WIN
        auto relay = std::make_unique<NativeLoopbackRelay>(relayTargetHost, socketPort);
        QString relayError;
        if (relay->start(&relayError)) {
            logInfo(QStringLiteral("Routing XMPP connection through local relay: %1:%2 -> 127.0.0.1:%3")
                        .arg(relayTargetHost)
                        .arg(socketPort)
                        .arg(relay->localPort()));
            socketHost = QStringLiteral("127.0.0.1");
            socketPort = relay->localPort();
            proxy = QNetworkProxy(QNetworkProxy::NoProxy);
            m_connectionRelay = std::move(relay);
        } else {
            logError(QStringLiteral("Failed to start local XMPP relay for %1:%2: %3")
                         .arg(relayTargetHost)
                         .arg(socketPort)
                         .arg(relayError));
        }
#endif
    }

    if (!socketHost.isEmpty()) {
        configuration.setHost(socketHost);
    }
    configuration.setPort(socketPort);
    configuration.setResourcePrefix("CuteXMPP");
    configuration.setAutoAcceptSubscriptions(true);
    configuration.setAutoReconnectionEnabled(false);
    configuration.setUseSASLAuthentication(true);
    configuration.setUseSasl2Authentication(false);
    configuration.setUseFastTokenAuthentication(false);
    configuration.setStreamSecurityMode(streamSecurityModeFor(request.tlsMode));
    configuration.setNetworkProxy(proxy);
    configuration.setSasl2UserAgent(std::nullopt);

    {
        QSettings settingsStore = createSettings();
        if (settingsStore.contains(credentialKey(jid))) {
            settingsStore.remove(credentialKey(jid));
            logDebug(QStringLiteral("Removed cached SASL2 credentials for %1 before login fallback.").arg(jid));
        }
    }

    QXmppPresence presence(QXmppPresence::Available);
    presence.setAvailableStatusType(QXmppPresence::Online);
    logDebug(QStringLiteral("Connecting to XMPP server: jid=%1, domain=%2, host=%3, port=%4, tls=%5, proxy=%6, sasl2=%7, fast=%8")
                 .arg(configuration.jid(), configuration.domain(), configuredHostText(configuration.host()))
                 .arg(configuration.port())
                 .arg(tlsModeText(request.tlsMode), proxyDetails(request.proxyMode))
                 .arg(configuration.useSasl2Authentication() ? "on" : "off")
                 .arg(configuration.useFastTokenAuthentication() ? "on" : "off"));
    m_client->connectToServer(configuration, presence);
}

void XmppService::registerAccount(const RegistrationRequest& rawRequest)
{
    if (!isCallableOnCurrentThread(this)) {
        QMetaObject::invokeMethod(this, [this, rawRequest]() {
            registerAccount(rawRequest);
        }, Qt::QueuedConnection);
        return;
    }

    const QString username = rawRequest.username.trimmed();
    const QString server = rawRequest.server.trimmed();
    const QString connectHost = rawRequest.connectHost.trimmed();
    if (username.isEmpty() || username.contains('@') || username.contains('/')) {
        emit authenticationFailed("Choose a username without @ or / characters.");
        return;
    }
    if (server.isEmpty()) {
        emit authenticationFailed("Server is required.");
        return;
    }
    if (rawRequest.password.isEmpty()) {
        emit authenticationFailed("Password is required.");
        return;
    }

    RegistrationRequest request = rawRequest;
    request.username = username;
    request.server = server;
    request.connectHost = connectHost;

    stopConnectionRelay();
    resetSessionState();
    m_loginAttempts.clear();
    m_loginAttemptIndex = -1;
    m_pendingRegistration = request;
    m_pendingLogin.reset();
    m_pendingOperation = PendingOperation::RegistrationHandshake;
    m_registrationManager->setRegisterOnConnectEnabled(true);

    logInfo(QStringLiteral("Starting registration with username=%1, domain=%2, host=%3, port=%4, tls=%5, proxy=%6 (%7), password=%8")
                .arg(request.username, request.server, configuredHostText(request.connectHost))
                .arg(request.port)
                .arg(tlsModeText(request.tlsMode),
                     proxyModeText(request.proxyMode),
                     proxyDetails(request.proxyMode),
                     maskSecret(request.password)));

    QXmppConfiguration configuration;
    configuration.setDomain(request.server);
    QString socketHost = request.connectHost;
    const QString relayTargetHost = socketHost.isEmpty() ? request.server : socketHost;
    quint16 socketPort = request.port;
    QNetworkProxy proxy = networkProxyFor(request.proxyMode);

    if (shouldUseNativeLoopbackRelay(relayTargetHost, request.proxyMode, request.tlsMode)) {
#ifdef Q_OS_WIN
        auto relay = std::make_unique<NativeLoopbackRelay>(relayTargetHost, socketPort);
        QString relayError;
        if (relay->start(&relayError)) {
            logInfo(QStringLiteral("Routing registration connection through local relay: %1:%2 -> 127.0.0.1:%3")
                        .arg(relayTargetHost)
                        .arg(socketPort)
                        .arg(relay->localPort()));
            socketHost = QStringLiteral("127.0.0.1");
            socketPort = relay->localPort();
            proxy = QNetworkProxy(QNetworkProxy::NoProxy);
            m_connectionRelay = std::move(relay);
        } else {
            logError(QStringLiteral("Failed to start local registration relay for %1:%2: %3")
                         .arg(relayTargetHost)
                         .arg(socketPort)
                         .arg(relayError));
        }
#endif
    }

    if (!socketHost.isEmpty()) {
        configuration.setHost(socketHost);
    }
    configuration.setPort(socketPort);
    configuration.setResourcePrefix("CuteXMPP");
    configuration.setUseSasl2Authentication(false);
    configuration.setUseFastTokenAuthentication(false);
    configuration.setStreamSecurityMode(streamSecurityModeFor(request.tlsMode));
    configuration.setNetworkProxy(proxy);

    logDebug(QStringLiteral("Connecting for in-band registration: domain=%1, host=%2, port=%3, tls=%4, proxy=%5")
                 .arg(configuration.domain(), configuredHostText(configuration.host()))
                 .arg(configuration.port())
                 .arg(tlsModeText(request.tlsMode), proxyDetails(request.proxyMode)));
    m_client->connectToServer(configuration);
}

void XmppService::disconnectFromServer()
{
    if (!isCallableOnCurrentThread(this)) {
        QMetaObject::invokeMethod(this, [this]() {
            disconnectFromServer();
        }, Qt::QueuedConnection);
        return;
    }

    logInfo("Disconnecting from XMPP server.");
    m_pendingOperation = PendingOperation::None;
    m_pendingLogin.reset();
    m_pendingRegistration.reset();
    m_loginAttempts.clear();
    m_loginAttemptIndex = -1;
    m_registrationManager->setRegisterOnConnectEnabled(false);
    resetSessionState();
    m_client->disconnectFromServer();
    if (m_client->state() == QXmppClient::DisconnectedState) {
        stopConnectionRelay();
    }
}

void XmppService::sendMessage(const QString& chatId, const QString& text)
{
    if (!isCallableOnCurrentThread(this)) {
        QMetaObject::invokeMethod(this, [this, chatId, text]() {
            sendMessage(chatId, text);
        }, Qt::QueuedConnection);
        return;
    }

    if (!m_session.has_value() || chatId.isEmpty() || text.trimmed().isEmpty()) {
        return;
    }

    const bool isGroupChat = m_chats.value(chatId).isGroupChat || m_mucRooms.contains(chatId);
    if (isGroupChat) {
        QXmppMucRoom* room = m_mucRooms.value(chatId);
        if (!room) {
            logError(QStringLiteral("Cannot send message to group chat %1 because room is not joined.").arg(chatId));
            emit errorMessage("This group chat is not joined yet.");
            return;
        }

        if (!room->sendMessage(text.trimmed())) {
            logError(QStringLiteral("Failed to send group message to %1").arg(chatId));
            emit errorMessage("Failed to send group message.");
            return;
        }

        MessageEntry entry;
        entry.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        entry.chatId = chatId;
        entry.senderJid = m_session->bareJid;
        entry.senderName = room->nickName().trimmed().isEmpty() ? m_session->displayName : room->nickName();
        entry.text = text.trimmed();
        entry.timestamp = QDateTime::currentDateTime();
        entry.outgoing = true;

        appendMessageIfMissing(m_messages[chatId], entry);
        trimConversationBuffer(m_messages[chatId], false);
        updateConversationPreview(chatId);
        emit messagesChanged(chatId);
        emit chatsChanged();
        return;
    }

    QXmppMessage message;
    message.setTo(chatId);
    message.setType(QXmppMessage::Chat);
    message.setBody(text.trimmed());
    message.setReceiptRequested(true);
    message.setOriginId(QUuid::createUuid().toString(QUuid::WithoutBraces));

    m_client->sendSensitive(std::move(message)).then(this, [this, chatId, text](QXmpp::SendResult&& result) {
        if (std::holds_alternative<QXmppError>(result)) {
            const QString errorText = qxmppErrorText(std::get<QXmppError>(result));
            logError(QStringLiteral("Failed to send message to %1: %2").arg(chatId, errorText));
            emit errorMessage(errorText);
            return;
        }

        if (!m_session.has_value()) {
            return;
        }

        MessageEntry entry;
        entry.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        entry.chatId = chatId;
        entry.senderJid = m_session->bareJid;
        entry.senderName = m_session->displayName;
        entry.text = text.trimmed();
        entry.timestamp = QDateTime::currentDateTime();
        entry.outgoing = true;

        appendMessageIfMissing(m_messages[chatId], entry);
        trimConversationBuffer(m_messages[chatId], false);
        logDebug(QStringLiteral("Message sent to %1: %2").arg(chatId, text.left(80)));
        updateConversationPreview(chatId);
        emit messagesChanged(chatId);
        emit chatsChanged();
    });
}

void XmppService::sendAttachment(const QString& chatId, const QString& filePath)
{
    if (!isCallableOnCurrentThread(this)) {
        QMetaObject::invokeMethod(this, [this, chatId, filePath]() {
            sendAttachment(chatId, filePath);
        }, Qt::QueuedConnection);
        return;
    }

    if (!m_session.has_value() || chatId.isEmpty() || filePath.trimmed().isEmpty()) {
        return;
    }
    if (!m_httpUploadManager) {
        emit errorMessage("File upload manager is not available.");
        return;
    }

    const QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        emit errorMessage("The selected file does not exist.");
        return;
    }

    emit infoMessage(QStringLiteral("Uploading %1...").arg(fileInfo.fileName()));
    logInfo(QStringLiteral("Uploading attachment for %1: %2").arg(chatId, fileInfo.filePath()));

    const auto upload = m_httpUploadManager->uploadFile(fileInfo);
    if (!upload) {
        emit errorMessage("The server does not support file uploads.");
        return;
    }
    connect(upload.get(), &QXmppHttpUpload::finished, this, [this, chatId, fileInfo, upload](const QXmppHttpUpload::Result& result) {
        if (const auto* url = std::get_if<QUrl>(&result)) {
            const QString urlText = url->toString();

            QXmppMessage message;
            message.setTo(chatId);
            message.setType((m_chats.value(chatId).isGroupChat || m_mucRooms.contains(chatId)) ? QXmppMessage::GroupChat : QXmppMessage::Chat);
            message.setBody(urlText);
            message.setOutOfBandUrl(urlText);
            message.setOriginId(QUuid::createUuid().toString(QUuid::WithoutBraces));
            if (message.type() == QXmppMessage::Chat) {
                message.setReceiptRequested(true);
            }

            m_client->sendSensitive(std::move(message)).then(this, [this, chatId, fileInfo, urlText](QXmpp::SendResult&& sendResult) {
                if (std::holds_alternative<QXmppError>(sendResult)) {
                    const QString errorText = qxmppErrorText(std::get<QXmppError>(sendResult));
                    logError(QStringLiteral("Failed to send attachment link to %1: %2").arg(chatId, errorText));
                    emit errorMessage(errorText);
                    return;
                }

                if (!m_session.has_value()) {
                    return;
                }

                MessageEntry entry;
                entry.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                entry.chatId = chatId;
                entry.senderJid = m_session->bareJid;
                if (m_chats.value(chatId).isGroupChat || m_mucRooms.contains(chatId)) {
                    QXmppMucRoom* room = m_mucRooms.value(chatId);
                    entry.senderName = room && !room->nickName().trimmed().isEmpty() ? room->nickName() : m_session->displayName;
                } else {
                    entry.senderName = m_session->displayName;
                }
                entry.text = urlText;
                entry.timestamp = QDateTime::currentDateTime();
                entry.outgoing = true;

                appendMessageIfMissing(m_messages[chatId], entry);
                trimConversationBuffer(m_messages[chatId], false);
                updateConversationPreview(chatId);
                emit messagesChanged(chatId);
                emit chatsChanged();
                emit infoMessage(QStringLiteral("Attachment sent: %1").arg(fileInfo.fileName()));
            });
            return;
        }

        if (std::holds_alternative<QXmpp::Cancelled>(result)) {
            emit infoMessage(QStringLiteral("Upload cancelled: %1").arg(fileInfo.fileName()));
            return;
        }

        const QString errorText = qxmppErrorText(std::get<QXmppError>(result));
        logError(QStringLiteral("Attachment upload failed for %1: %2").arg(fileInfo.filePath(), errorText));
        emit errorMessage(errorText);
    });
}

void XmppService::ensureConversationLoaded(const QString& chatId)
{
    if (!isCallableOnCurrentThread(this)) {
        QMetaObject::invokeMethod(this, [this, chatId]() {
            ensureConversationLoaded(chatId);
        }, Qt::QueuedConnection);
        return;
    }

    if (chatId.isEmpty()) {
        return;
    }
    if (m_chats.value(chatId).isGroupChat || m_mucRooms.contains(chatId)) {
        ensureGroupChatExists(chatId, m_chats.value(chatId).title, m_mucNicks.value(chatId));
    } else {
        ensureChatExists(chatId);
        requestContactVCard(chatId);
    }
    if (!m_historyStates.value(chatId).initialLoaded) {
        loadConversationFromMam(chatId, 40, false);
    }
}

void XmppService::loadOlderMessages(const QString& chatId)
{
    if (!isCallableOnCurrentThread(this)) {
        QMetaObject::invokeMethod(this, [this, chatId]() {
            loadOlderMessages(chatId);
        }, Qt::QueuedConnection);
        return;
    }

    if (!canLoadOlderMessages(chatId)) {
        return;
    }

    loadConversationFromMam(chatId, 40, true);
}

void XmppService::markChatRead(const QString& chatId)
{
    if (!isCallableOnCurrentThread(this)) {
        QMetaObject::invokeMethod(this, [this, chatId]() {
            markChatRead(chatId);
        }, Qt::QueuedConnection);
        return;
    }

    if (chatId.isEmpty() || !m_chats.contains(chatId)) {
        return;
    }
    ChatSummary& chat = m_chats[chatId];
    if (chat.unreadCount == 0) {
        return;
    }
    chat.unreadCount = 0;
    emit chatsChanged();
}

void XmppService::connectSignals()
{
    connect(m_client, &QXmppClient::connected, this, &XmppService::handleClientConnected);
    connect(m_client, &QXmppClient::disconnected, this, &XmppService::handleClientDisconnected);
    connect(m_client, &QXmppClient::errorOccurred, this, &XmppService::handleClientError);
    connect(m_client, &QXmppClient::stateChanged, this, [this](QXmppClient::State state) {
        if (m_lastLoggedState == static_cast<int>(state)) {
            return;
        }
        m_lastLoggedState = static_cast<int>(state);
        QString stateText = "unknown";
        switch (state) {
        case QXmppClient::DisconnectedState:
            stateText = "disconnected";
            break;
        case QXmppClient::ConnectingState:
            stateText = "connecting";
            break;
        case QXmppClient::ConnectedState:
            stateText = "connected";
            break;
        }
        logDebug(QStringLiteral("XMPP client state changed: %1").arg(stateText));
    });
    connect(m_client, &QXmppClient::messageReceived, this, &XmppService::handleMessageReceived);
    connect(m_client, &QXmppClient::presenceReceived, this, &XmppService::handlePresenceReceived);
    connect(m_client, &QXmppClient::credentialsChanged, this, &XmppService::saveCredentialsIfNeeded);

    connect(m_registrationManager, &QXmppRegistrationManager::registrationFormReceived, this, &XmppService::handleRegistrationForm);
    connect(m_registrationManager, &QXmppRegistrationManager::registrationFailed, this, &XmppService::handleRegistrationFailed);
    connect(m_registrationManager, &QXmppRegistrationManager::registrationSucceeded, this, [this]() {
        m_registrationManager->setRegisterOnConnectEnabled(false);
        m_pendingOperation = PendingOperation::RegistrationReconnectLogin;
        m_client->disconnectFromServer();
    });

    if (m_bookmarkManager) {
        connect(m_bookmarkManager, &QXmppBookmarkManager::bookmarksReceived, this, &XmppService::syncBookmarkSet);
    }

    if (m_mucManager) {
        connect(m_mucManager, &QXmppMucManager::roomAdded, this, &XmppService::registerMucRoom);
        connect(m_mucManager, &QXmppMucManager::invitationReceived, this, [this](const QString& roomJid, const QString&, const QString&) {
            ensureGroupChatExists(roomJid, userFromJid(roomJid));
            emit chatsChanged();
        });
    }

    if (m_mucManagerV2) {
        connect(m_mucManagerV2, &QXmppMucManagerV2::bookmarksReset, this, [this]() {
            if (!m_mucManagerV2 || !m_mucManagerV2->bookmarks().has_value()) {
                return;
            }
            for (const QXmppMucBookmark& bookmark : *m_mucManagerV2->bookmarks()) {
                syncMucBookmark(bookmark.jid(), bookmark.name(), bookmark.nick());
            }
            emit chatsChanged();
        });
        connect(m_mucManagerV2, &QXmppMucManagerV2::bookmarksAdded, this, [this](const QList<QXmppMucBookmark>& bookmarks) {
            for (const QXmppMucBookmark& bookmark : bookmarks) {
                syncMucBookmark(bookmark.jid(), bookmark.name(), bookmark.nick());
            }
            emit chatsChanged();
        });
        connect(m_mucManagerV2, &QXmppMucManagerV2::bookmarksChanged, this, [this](const QList<QXmppMucManagerV2::BookmarkChange>& changes) {
            for (const auto& change : changes) {
                syncMucBookmark(change.newBookmark.jid(), change.newBookmark.name(), change.newBookmark.nick());
            }
            emit chatsChanged();
        });
        connect(m_mucManagerV2, &QXmppMucManagerV2::bookmarksRemoved, this, [this](const QList<QString>& removedBookmarkJids) {
            for (const QString& roomJid : removedBookmarkJids) {
                m_mucRooms.remove(roomJid);
                m_mucNicks.remove(roomJid);
                if (m_chats.contains(roomJid) && m_chats.value(roomJid).isGroupChat) {
                    m_chats.remove(roomJid);
                    m_messages.remove(roomJid);
                    m_historyStates.remove(roomJid);
                }
            }
            emit chatsChanged();
        });
    }

    if (m_rosterManager) {
        connect(m_rosterManager, &QXmppRosterManager::rosterReceived, this, &XmppService::rebuildChatsFromRoster);
        connect(m_rosterManager, &QXmppRosterManager::itemAdded, this, [this](const QString&) { rebuildChatsFromRoster(); });
        connect(m_rosterManager, &QXmppRosterManager::itemChanged, this, [this](const QString&) { rebuildChatsFromRoster(); });
        connect(m_rosterManager, &QXmppRosterManager::itemRemoved, this, [this](const QString& bareJid) {
            m_chats.remove(bareJid);
            m_messages.remove(bareJid);
            m_historyStates.remove(bareJid);
            emit chatsChanged();
        });
        connect(m_rosterManager, &QXmppRosterManager::presenceChanged, this, [this](const QString& bareJid, const QString&) {
            updateChatPresence(bareJid);
            emit chatsChanged();
        });
    }
}

void XmppService::resetSessionState()
{
    m_session.reset();
    m_chats.clear();
    m_messages.clear();
    m_historyStates.clear();
    m_mucRooms.clear();
    m_mucNicks.clear();
    m_contactPhotoHashes.clear();
    m_requestedRoomInfo.clear();
    m_previewRequestsInFlight.clear();
    m_loadedConversationPreviews.clear();
    m_contactVCardRequestsInFlight.clear();
    m_loadedContactVCards.clear();
    emit chatsChanged();
}

void XmppService::rebuildChatsFromRoster()
{
    if (!m_rosterManager) {
        return;
    }

    const QStringList bareJids = m_rosterManager->getRosterBareJids();
    for (const QString& bareJid : bareJids) {
        ensureChatExists(bareJid);
        requestContactVCard(bareJid);
        requestConversationPreview(bareJid);
    }
    emit chatsChanged();
}

void XmppService::requestContactVCard(const QString& bareJid, bool forceRefresh)
{
    if (!m_vCardManager || bareJid.isEmpty()) {
        return;
    }
    if (m_contactVCardRequestsInFlight.contains(bareJid)) {
        return;
    }
    if (!forceRefresh && m_loadedContactVCards.contains(bareJid)) {
        return;
    }

    m_contactVCardRequestsInFlight.insert(bareJid);
    m_vCardManager->fetchVCard(bareJid).then(this, [this, bareJid](QXmppVCardManager::VCardIqResult&& result) {
        m_contactVCardRequestsInFlight.remove(bareJid);
        if (!std::holds_alternative<QXmppVCardIq>(result) || !m_chats.contains(bareJid)) {
            return;
        }

        const QXmppVCardIq iq = std::get<QXmppVCardIq>(std::move(result));
        ChatSummary& chat = m_chats[bareJid];
        bool changed = false;

        if (chat.title.trimmed().isEmpty()) {
            if (!iq.nickName().trimmed().isEmpty()) {
                chat.title = iq.nickName().trimmed();
                changed = true;
            } else if (!iq.fullName().trimmed().isEmpty()) {
                chat.title = iq.fullName().trimmed();
                changed = true;
            }
        }

        if (!iq.description().trimmed().isEmpty()) {
            const QString description = iq.description().trimmed();
            if (chat.description != description) {
                chat.description = description;
                changed = true;
            }
        } else if (chat.description.trimmed().isEmpty()) {
            chat.description = bareJid;
            changed = true;
        }

        if (!iq.photo().isEmpty()) {
            QImage avatar;
            avatar.loadFromData(iq.photo());
            if (!avatar.isNull()) {
                if (chat.avatar != avatar) {
                    chat.avatar = avatar;
                    changed = true;
                }
            }
        } else if (!chat.avatar.isNull()) {
            chat.avatar = QImage();
            changed = true;
        }

        m_loadedContactVCards.insert(bareJid);
        if (changed) {
            emit chatsChanged();
        }
    });
}

void XmppService::requestOwnVCard()
{
    if (!m_vCardManager || !m_session.has_value()) {
        return;
    }

    const QString bareJid = m_session->bareJid;
    m_vCardManager->fetchVCard(bareJid).then(this, [this, bareJid](QXmppVCardManager::VCardIqResult&& result) {
        if (!std::holds_alternative<QXmppVCardIq>(result) || !m_session.has_value() || m_session->bareJid != bareJid) {
            return;
        }

        const QXmppVCardIq iq = std::get<QXmppVCardIq>(std::move(result));
        if (!iq.photo().isEmpty()) {
            QImage avatar;
            avatar.loadFromData(iq.photo());
            if (!avatar.isNull()) {
                m_session->avatar = avatar;
                emit sessionChanged(*m_session);
            }
        }
    });
}

void XmppService::requestConversationPreview(const QString& chatId)
{
    if (!m_mamManager || !m_session.has_value() || chatId.isEmpty() || m_previewRequestsInFlight.contains(chatId)) {
        return;
    }
    if (m_loadedConversationPreviews.contains(chatId) || !m_messages.value(chatId).isEmpty()) {
        return;
    }

    m_previewRequestsInFlight.insert(chatId);

    QXmppResultSetQuery query;
    query.setMax(1);
    query.setBefore(QStringLiteral(""));

    m_mamManager->retrieveMessages(QString(), QString(), chatId, QDateTime(), QDateTime(), query)
        .then(this, [this, chatId](QXmppMamManager::RetrieveResult&& result) {
            m_previewRequestsInFlight.remove(chatId);

            if (!std::holds_alternative<QXmppMamManager::RetrievedMessages>(result) || !m_session.has_value() || !m_chats.contains(chatId)) {
                return;
            }

            const QXmppMamManager::RetrievedMessages retrieved = std::get<QXmppMamManager::RetrievedMessages>(std::move(result));
            m_loadedConversationPreviews.insert(chatId);
            if (retrieved.messages.isEmpty()) {
                return;
            }

            const bool isGroupChat = m_chats.value(chatId).isGroupChat || m_mucRooms.contains(chatId);
            const QXmppMessage& message = retrieved.messages.constLast();
            const QString displayText = messageDisplayText(message);
            if (displayText.isEmpty()) {
                return;
            }

            ChatSummary& chat = m_chats[chatId];
            chat.preview = previewTextForMessage(displayText);
            chat.lastActivity = message.stamp().isValid() ? message.stamp() : QDateTime::currentDateTime();

            if (isGroupChat) {
                const QString senderNick = resourceFromJid(message.from());
                if (!senderNick.isEmpty()) {
                    chat.preview = QStringLiteral("%1: %2").arg(senderNick, chat.preview);
                }
            }

            emit chatsChanged();
        });
}

void XmppService::loadConversationFromMam(const QString& chatId, int maxMessages, bool olderMessages)
{
    if (!m_mamManager || !m_session.has_value() || chatId.isEmpty()) {
        return;
    }

    ChatHistoryState& history = m_historyStates[chatId];
    if (history.loading) {
        return;
    }
    if (!olderMessages && history.initialLoaded) {
        return;
    }
    if (olderMessages && (!history.initialLoaded || !history.hasMore || history.oldestCursor.trimmed().isEmpty())) {
        return;
    }

    history.loading = true;

    QXmppResultSetQuery query;
    query.setMax(maxMessages);
    if (olderMessages) {
        query.setBefore(history.oldestCursor);
    } else {
        query.setBefore(QStringLiteral(""));
    }

    m_mamManager->retrieveMessages(QString(), QString(), chatId, QDateTime(), QDateTime(), query)
        .then(this, [this, chatId, olderMessages](QXmppMamManager::RetrieveResult&& result) {
            ChatHistoryState& history = m_historyStates[chatId];
            history.loading = false;

            if (!std::holds_alternative<QXmppMamManager::RetrievedMessages>(result) || !m_session.has_value()) {
                return;
            }

            const QXmppMamManager::RetrievedMessages retrieved = std::get<QXmppMamManager::RetrievedMessages>(std::move(result));
            const QXmppResultSetReply reply = retrieved.result.resultSetReply();
            history.initialLoaded = true;
            history.hasMore = reply.index() > 0;
            if (!reply.first().trimmed().isEmpty()) {
                history.oldestCursor = reply.first().trimmed();
            }
            if (!reply.last().trimmed().isEmpty()) {
                history.newestCursor = reply.last().trimmed();
            }

            QVector<MessageEntry>& conversation = m_messages[chatId];
            const bool isGroupChat = m_chats.value(chatId).isGroupChat || m_mucRooms.contains(chatId);

            for (const QXmppMessage& message : retrieved.messages) {
                const QString displayText = messageDisplayText(message);
                if (displayText.isEmpty()) {
                    continue;
                }

                const QString bareFrom = bareJidOf(message.from());
                const QString bareTo = bareJidOf(message.to());
                if (isGroupChat) {
                    if (bareFrom != chatId && bareTo != chatId) {
                        continue;
                    }

                    ensureGroupChatExists(chatId, m_chats.value(chatId).title, m_mucNicks.value(chatId));
                    const QString senderNick = resourceFromJid(message.from());
                    const QString roomNick = m_mucNicks.value(chatId);
                    const bool outgoing = !senderNick.isEmpty() && senderNick == roomNick;

                    MessageEntry entry;
                    entry.id = !message.originId().isEmpty() ? message.originId() : message.id();
                    entry.chatId = chatId;
                    entry.senderJid = outgoing ? m_session->bareJid : chatId;
                    entry.senderName = outgoing
                        ? (roomNick.isEmpty() ? m_session->displayName : roomNick)
                        : (senderNick.isEmpty() ? m_chats.value(chatId).title : senderNick);
                    entry.text = displayText;
                    entry.timestamp = message.stamp().isValid() ? message.stamp() : QDateTime::currentDateTime();
                    entry.outgoing = outgoing;
                    appendMessageIfMissing(conversation, entry);
                    continue;
                }

                const bool outgoing = bareFrom == m_session->bareJid;
                const QString peerJid = outgoing ? bareTo : bareFrom;
                if (peerJid != chatId) {
                    continue;
                }

                ensureChatExists(chatId);

                MessageEntry entry;
                entry.id = !message.originId().isEmpty() ? message.originId() : message.id();
                entry.chatId = chatId;
                entry.senderJid = outgoing ? m_session->bareJid : chatId;
                entry.senderName = outgoing ? m_session->displayName : m_chats.value(chatId).title;
                entry.text = displayText;
                entry.timestamp = message.stamp().isValid() ? message.stamp() : QDateTime::currentDateTime();
                entry.outgoing = outgoing;
                appendMessageIfMissing(conversation, entry);
            }

            std::sort(conversation.begin(), conversation.end(), [](const MessageEntry& left, const MessageEntry& right) {
                return left.timestamp < right.timestamp;
            });

            trimConversationBuffer(conversation, olderMessages);

            updateConversationPreview(chatId);
            m_loadedConversationPreviews.insert(chatId);
            emit messagesChanged(chatId);
            emit chatsChanged();

            if (!olderMessages && history.hasMore && conversation.isEmpty()) {
                history.hasMore = false;
            }
        });
}

void XmppService::updateConversationPreview(const QString& chatId)
{
    if (!m_chats.contains(chatId)) {
        return;
    }

    ChatSummary& chat = m_chats[chatId];
    const QVector<MessageEntry>& conversation = m_messages[chatId];
    if (conversation.isEmpty()) {
        return;
    }

    const MessageEntry& lastMessage = conversation.constLast();
    chat.preview = previewTextForMessage(lastMessage.text);
    chat.lastActivity = lastMessage.timestamp;
}

void XmppService::updateChatPresence(const QString& bareJid)
{
    if (!m_rosterManager || !m_chats.contains(bareJid)) {
        return;
    }

    if (m_chats.value(bareJid).isGroupChat) {
        return;
    }

    PresenceState state = PresenceState::Offline;
    QString statusText;

    for (const QString& resource : m_rosterManager->getResources(bareJid)) {
        const QXmppPresence presence = m_rosterManager->getPresence(bareJid, resource);
        const PresenceState candidate = presenceFromXmpp(presence);
        if (candidate == PresenceState::Busy) {
            state = candidate;
        } else if (candidate == PresenceState::Away && state != PresenceState::Busy) {
            state = candidate;
        } else if (candidate == PresenceState::Online && state == PresenceState::Offline) {
            state = candidate;
        }

        if (!presence.statusText().trimmed().isEmpty()) {
            statusText = presence.statusText().trimmed();
        }
    }

    ChatSummary& chat = m_chats[bareJid];
    chat.presence = state;
    chat.subtitle = statusText.isEmpty() ? presenceText(state) : statusText;
}

void XmppService::ensureChatExists(const QString& bareJid)
{
    if (bareJid.isEmpty()) {
        return;
    }

    ChatSummary& chat = m_chats[bareJid];
    chat.id = bareJid;
    if (chat.title.trimmed().isEmpty()) {
        QString title = userFromJid(bareJid);
        if (m_rosterManager) {
            const QXmppRosterIq::Item item = m_rosterManager->getRosterEntry(bareJid);
            if (!item.name().trimmed().isEmpty()) {
                title = item.name().trimmed();
            }
        }
        chat.title = title;
    }

    if (chat.description.trimmed().isEmpty()) {
        chat.description = bareJid;
    }
    updateChatPresence(bareJid);
    if (!chat.lastActivity.isValid()) {
        chat.lastActivity = QDateTime::fromMSecsSinceEpoch(0);
    }
}

void XmppService::ensureGroupChatExists(const QString& roomJid, const QString& title, const QString& nickName)
{
    if (roomJid.isEmpty()) {
        return;
    }

    ChatSummary& chat = m_chats[roomJid];
    chat.id = roomJid;
    chat.isGroupChat = true;
    if (!title.trimmed().isEmpty()) {
        chat.title = title.trimmed();
    } else if (chat.title.trimmed().isEmpty()) {
        chat.title = userFromJid(roomJid);
    }
    if (!nickName.trimmed().isEmpty()) {
        m_mucNicks[roomJid] = nickName.trimmed();
    }
    if (chat.subtitle.trimmed().isEmpty()) {
        chat.subtitle = "Group chat";
    }
    if (chat.description.trimmed().isEmpty()) {
        chat.description = roomJid;
    }
    chat.presence = PresenceState::Online;
    if (!chat.lastActivity.isValid()) {
        chat.lastActivity = QDateTime::fromMSecsSinceEpoch(0);
    }
    requestGroupChatInfo(roomJid);
}

void XmppService::requestGroupChatInfo(const QString& roomJid)
{
    if (!m_discoveryManager || roomJid.trimmed().isEmpty() || m_requestedRoomInfo.contains(roomJid)) {
        return;
    }

    m_requestedRoomInfo.insert(roomJid);
    m_discoveryManager->info(roomJid).then(this, [this, roomJid](auto&& result) {
        if (!std::holds_alternative<QXmppDiscoInfo>(result) || !m_chats.contains(roomJid)) {
            m_requestedRoomInfo.remove(roomJid);
            return;
        }

        const QXmppDiscoInfo info = std::get<QXmppDiscoInfo>(std::move(result));
        ChatSummary& chat = m_chats[roomJid];

        for (const QXmppDiscoIdentity& identity : info.identities()) {
            if (!identity.name().trimmed().isEmpty()) {
                chat.title = identity.name().trimmed();
                break;
            }
        }

        if (const auto roomInfo = info.dataForm<QXmppMucRoomInfo>(); roomInfo.has_value()) {
            if (roomInfo->occupants().has_value()) {
                chat.participantCount = static_cast<int>(*roomInfo->occupants());
                chat.subtitle = QStringLiteral("%1 members").arg(chat.participantCount);
            }

            if (!roomInfo->description().trimmed().isEmpty()) {
                chat.description = roomInfo->description().trimmed();
            } else if (!roomInfo->subject().trimmed().isEmpty()) {
                chat.description = roomInfo->subject().trimmed();
            }
        }

        emit chatsChanged();
    });
}

void XmppService::syncBookmarkSet(const QXmppBookmarkSet& bookmarks)
{
    if (!m_session.has_value() || !m_mucManager) {
        return;
    }

    for (const QXmppBookmarkConference& conference : bookmarks.conferences()) {
        if (conference.jid().trimmed().isEmpty()) {
            continue;
        }

        const QString nickName = conference.nickName().trimmed().isEmpty()
            ? m_session->displayName
            : conference.nickName().trimmed();
        syncMucBookmark(conference.jid(), conference.name(), nickName);
    }

    emit chatsChanged();
}

void XmppService::syncMucBookmark(const QString& roomJid, const QString& name, const QString& nickName)
{
    if (!m_session.has_value() || !m_mucManager || roomJid.trimmed().isEmpty()) {
        return;
    }

    const QString resolvedNick = nickName.trimmed().isEmpty() ? m_session->displayName : nickName.trimmed();

    ensureGroupChatExists(roomJid, name, resolvedNick);

    QXmppMucRoom* room = m_mucRooms.value(roomJid);
    if (!room) {
        room = m_mucManager->addRoom(roomJid);
        registerMucRoom(room);
    }

    room->setNickName(resolvedNick);
    if (!room->isJoined()) {
        room->join();
    }
}

void XmppService::registerMucRoom(QXmppMucRoom* room)
{
    if (!room) {
        return;
    }

    const QString roomJid = room->jid().trimmed();
    if (roomJid.isEmpty()) {
        return;
    }

    if (m_mucRooms.contains(roomJid) && m_mucRooms.value(roomJid) == room) {
        return;
    }

    m_mucRooms.insert(roomJid, room);
    ensureGroupChatExists(roomJid, room->name(), room->nickName());

    connect(room, &QXmppMucRoom::joined, this, [this, room]() {
        const QString roomJid = room->jid();
        ensureGroupChatExists(roomJid, room->name(), room->nickName());
        if (!room->subject().trimmed().isEmpty()) {
            m_chats[roomJid].description = room->subject().trimmed();
        }
        emit chatsChanged();
        requestConversationPreview(roomJid);
    });

    connect(room, &QXmppMucRoom::nameChanged, this, [this, room](const QString& name) {
        ensureGroupChatExists(room->jid(), name, room->nickName());
        emit chatsChanged();
    });

    connect(room, &QXmppMucRoom::subjectChanged, this, [this, room](const QString& subject) {
        ensureGroupChatExists(room->jid(), room->name(), room->nickName());
        if (!subject.trimmed().isEmpty()) {
            m_chats[room->jid()].description = subject.trimmed();
        }
        emit chatsChanged();
    });

    connect(room, &QXmppMucRoom::participantsChanged, this, [this, room]() {
        ensureGroupChatExists(room->jid(), room->name(), room->nickName());
        const int participants = static_cast<int>(room->participants().size());
        m_chats[room->jid()].participantCount = participants;
        m_chats[room->jid()].subtitle = participants > 0
            ? QStringLiteral("%1 members").arg(participants)
            : QStringLiteral("Group chat");
        emit chatsChanged();
    });

    connect(room, &QXmppMucRoom::messageReceived, this, [this, room](const QXmppMessage& message) {
        const QString displayText = messageDisplayText(message);
        if (!m_session.has_value() || displayText.isEmpty()) {
            return;
        }

        const QString roomJid = room->jid();
        ensureGroupChatExists(roomJid, room->name(), room->nickName());

        const QString senderNick = resourceFromJid(message.from());
        if (!senderNick.isEmpty() && senderNick == room->nickName()) {
            return;
        }

        MessageEntry entry;
        entry.id = !message.originId().isEmpty() ? message.originId() : message.id();
        entry.chatId = roomJid;
        entry.senderJid = roomJid;
        entry.senderName = senderNick.isEmpty() ? m_chats.value(roomJid).title : senderNick;
        entry.text = displayText;
        entry.timestamp = message.stamp().isValid() ? message.stamp() : QDateTime::currentDateTime();
        entry.outgoing = false;

        if (appendMessageIfMissing(m_messages[roomJid], entry)) {
            trimConversationBuffer(m_messages[roomJid], false);
            ChatSummary& chat = m_chats[roomJid];
            chat.unreadCount += 1;
            updateConversationPreview(roomJid);
            emit messagesChanged(roomJid);
            emit chatsChanged();
        }
    });
}

void XmppService::handleClientConnected()
{
    logInfo("XMPP connection established.");
    if (m_pendingOperation == PendingOperation::Login && m_pendingLogin.has_value()) {
        finishAuthentication(userFromJid(m_pendingLogin->jid));
    }

    if (m_bookmarkManager && m_bookmarkManager->areBookmarksReceived()) {
        syncBookmarkSet(m_bookmarkManager->bookmarks());
    }
    if (m_mucManagerV2 && m_mucManagerV2->bookmarks().has_value()) {
        for (const QXmppMucBookmark& bookmark : *m_mucManagerV2->bookmarks()) {
            syncMucBookmark(bookmark.jid(), bookmark.name(), bookmark.nick());
        }
    }
}

void XmppService::handleClientDisconnected()
{
    logInfo("XMPP connection closed.");
    stopConnectionRelay();
    if (m_pendingOperation == PendingOperation::RegistrationReconnectLogin && m_pendingRegistration.has_value()) {
        const RegistrationRequest registration = *m_pendingRegistration;
        LoginRequest loginRequest;
        loginRequest.jid = QStringLiteral("%1@%2").arg(registration.username, registration.server);
        loginRequest.password = registration.password;
        loginRequest.server = registration.server;
        loginRequest.connectHost = registration.connectHost;
        loginRequest.port = registration.port;
        loginRequest.proxyMode = registration.proxyMode;
        loginRequest.tlsMode = registration.tlsMode;
        QTimer::singleShot(0, this, [this, loginRequest]() { login(loginRequest); });
        return;
    }
}

bool XmppService::tryNextLoginAttempt(const QString& message)
{
    if (m_pendingOperation != PendingOperation::Login || !m_pendingLogin.has_value() || m_loginAttempts.isEmpty()) {
        return false;
    }

    const QString lowered = message.toLower();
    const bool shouldRetry =
        lowered.contains(QStringLiteral("remote host closed")) ||
        lowered.contains(QStringLiteral("timed out")) ||
        lowered.contains(QStringLiteral("unknown error")) ||
        lowered.contains(QStringLiteral("no supported sasl mechanism")) ||
        lowered.contains(QStringLiteral("plain is disabled")) ||
        lowered.contains(QStringLiteral("socket:"));
    if (!shouldRetry) {
        return false;
    }

    if (m_loginAttemptIndex + 1 >= m_loginAttempts.size()) {
        return false;
    }

    ++m_loginAttemptIndex;
    const LoginRequest nextAttempt = m_loginAttempts.at(m_loginAttemptIndex);
    logInfo(QStringLiteral("Retrying login with fallback %1/%2: jid=%3, server=%4, host=%5, port=%6, tls=%7, proxy=%8")
                .arg(m_loginAttemptIndex + 1)
                .arg(m_loginAttempts.size())
                .arg(nextAttempt.jid, nextAttempt.server, configuredHostText(nextAttempt.connectHost))
                .arg(nextAttempt.port)
                .arg(tlsModeText(nextAttempt.tlsMode), proxyModeText(nextAttempt.proxyMode)));

    m_client->disconnectFromServer();
    QTimer::singleShot(1500, this, [this, nextAttempt]() {
        if (m_session.has_value() || m_pendingOperation != PendingOperation::Login) {
            return;
        }
        connectLoginAttempt(nextAttempt);
    });
    return true;
}

void XmppService::handleClientError(const QXmppError& error)
{
    QString message = qxmppErrorText(error);
    if (message.contains(QStringLiteral("No supported SASL mechanism available"), Qt::CaseInsensitive)
        || message.contains(QStringLiteral("PLAIN is disabled"), Qt::CaseInsensitive)) {
        message += QStringLiteral(" The server only offered mechanisms that require a different TLS mode.");
    }
    const QString socketError = m_client->socketErrorString().trimmed();
    if (!socketError.isEmpty()) {
        message += QStringLiteral(" Socket: %1").arg(socketError);
    }
    logError(QStringLiteral("XMPP error: %1").arg(message));
    if (!m_session.has_value() && m_pendingOperation != PendingOperation::None) {
        if (tryNextLoginAttempt(message)) {
            return;
        }
        m_loginAttempts.clear();
        m_loginAttemptIndex = -1;
        m_pendingOperation = PendingOperation::None;
        m_pendingLogin.reset();
        m_pendingRegistration.reset();
        m_registrationManager->setRegisterOnConnectEnabled(false);
        m_client->disconnectFromServer();
        emit authenticationFailed(message);
        return;
    }
    emit errorMessage(message);
}

void XmppService::handleMessageReceived(const QXmppMessage& message)
{
    const QString displayText = messageDisplayText(message);
    if (!m_session.has_value() || displayText.isEmpty()) {
        return;
    }

    if (message.type() == QXmppMessage::GroupChat || m_mucRooms.contains(bareJidOf(message.from()))) {
        return;
    }

    const QString bareFrom = bareJidOf(message.from());
    const QString bareTo = bareJidOf(message.to());
    if (bareFrom == m_session->bareJid) {
        return;
    }

    const QString chatId = !bareFrom.isEmpty() ? bareFrom : bareTo;
    if (chatId.isEmpty()) {
        return;
    }

    ensureChatExists(chatId);

    MessageEntry entry;
    entry.id = !message.originId().isEmpty() ? message.originId() : message.id();
    entry.chatId = chatId;
    entry.senderJid = chatId;
    entry.senderName = m_chats.value(chatId).title;
    entry.text = displayText;
    entry.timestamp = message.stamp().isValid() ? message.stamp() : QDateTime::currentDateTime();
    entry.outgoing = false;

    if (appendMessageIfMissing(m_messages[chatId], entry)) {
        trimConversationBuffer(m_messages[chatId], false);
        logDebug(QStringLiteral("Message received from %1: %2").arg(chatId, entry.text.left(80)));
        ChatSummary& chat = m_chats[chatId];
        chat.unreadCount += 1;
        updateConversationPreview(chatId);
        emit messagesChanged(chatId);
        emit chatsChanged();
    }
}

void XmppService::handlePresenceReceived(const QXmppPresence& presence)
{
    const QString bareJid = bareJidOf(presence.from());
    if (bareJid.isEmpty()) {
        return;
    }

    if (m_mucRooms.contains(bareJid)) {
        ensureGroupChatExists(bareJid, m_chats.value(bareJid).title, m_mucNicks.value(bareJid));
        return;
    }

    ensureChatExists(bareJid);
    updateChatPresence(bareJid);
    if (presence.vCardUpdateType() == QXmppPresence::VCardUpdateValidPhoto) {
        const QByteArray photoHash = presence.photoHash();
        const bool photoChanged = !photoHash.isEmpty() && m_contactPhotoHashes.value(bareJid) != photoHash;
        if (photoChanged) {
            m_contactPhotoHashes.insert(bareJid, photoHash);
        }
        if (photoChanged || !m_loadedContactVCards.contains(bareJid)) {
            requestContactVCard(bareJid, photoChanged);
        }
    } else if (presence.vCardUpdateType() == QXmppPresence::VCardUpdateNoPhoto) {
        m_contactPhotoHashes.remove(bareJid);
        if (!m_chats.value(bareJid).avatar.isNull()) {
            m_chats[bareJid].avatar = QImage();
        }
    }
    logDebug(QStringLiteral("Presence update from %1: %2").arg(bareJid, m_chats.value(bareJid).subtitle));
    emit chatsChanged();
}

void XmppService::handleRegistrationForm(const QXmppRegisterIq& iq)
{
    if (!m_pendingRegistration.has_value()) {
        emit authenticationFailed("The server sent a registration form but no registration request is pending.");
        return;
    }

    const RegistrationRequest request = *m_pendingRegistration;
    const QXmppDataForm originalForm = iq.form();
    logDebug(QStringLiteral("Registration form received from server %1. Data form present: %2")
                 .arg(request.server, originalForm.isNull() ? "no" : "yes"));
    if (!originalForm.isNull()) {
        QXmppDataForm form = originalForm;
        form.setType(QXmppDataForm::Submit);
        QList<QXmppDataForm::Field> fields = form.constFields();
        for (QXmppDataForm::Field& field : fields) {
            const QString key = field.key().trimmed().toLower();
            if (key == "username") {
                field.setValue(request.username);
            } else if (key == "password") {
                field.setValue(request.password);
            } else if (key == "email" && field.isRequired()) {
                field.setValue(QStringLiteral("%1@%2").arg(request.username, request.server));
            } else if (key == "name" && field.isRequired()) {
                field.setValue(request.username);
            }
        }
        form.setFields(fields);
        m_registrationManager->setRegistrationFormToSend(form);
    } else {
        QXmppRegisterIq filled = iq;
        filled.setUsername(request.username);
        filled.setPassword(request.password);
        m_registrationManager->setRegistrationFormToSend(filled);
    }

    m_registrationManager->sendCachedRegistrationForm();
}

void XmppService::handleRegistrationFailed(const QXmppStanza::Error& error)
{
    logError(QStringLiteral("Registration failed: %1").arg(stanzaErrorText(error)));
    m_pendingOperation = PendingOperation::None;
    m_pendingLogin.reset();
    m_pendingRegistration.reset();
    m_registrationManager->setRegisterOnConnectEnabled(false);
    m_client->disconnectFromServer();
    emit authenticationFailed(stanzaErrorText(error));
}

void XmppService::saveCredentialsIfNeeded()
{
    if (!m_pendingLogin.has_value() && !m_session.has_value()) {
        return;
    }

    const QString bareJid = m_session.has_value() ? m_session->bareJid : bareJidOf(m_pendingLogin->jid);
    if (bareJid.isEmpty()) {
        return;
    }

    QSettings settings = createSettings();
    if (!m_settings->ui().rememberTokens
        || !m_client->configuration().useSasl2Authentication()
        || !m_client->configuration().useFastTokenAuthentication()) {
        settings.remove(credentialKey(bareJid));
        logDebug(QStringLiteral("Removed cached credentials for %1 because token persistence is unavailable for the current login mode.").arg(bareJid));
        return;
    }

    settings.setValue(credentialKey(bareJid), serializeCredentials(m_client->configuration().credentials()));
    logDebug(QStringLiteral("Saved cached credentials for %1").arg(bareJid));
}

void XmppService::finishAuthentication(const QString& displayName)
{
    if (!m_pendingLogin.has_value()) {
        return;
    }

    const LoginRequest loginRequest = *m_pendingLogin;
    AccountSession session;
    session.jid = loginRequest.jid;
    session.bareJid = bareJidOf(loginRequest.jid);
    session.username = userFromJid(loginRequest.jid);
    session.displayName = displayName.trimmed().isEmpty() ? session.username : displayName.trimmed();
    session.server = loginRequest.server;
    session.connectHost = loginRequest.connectHost;
    session.port = loginRequest.port;

    m_session = session;
    m_pendingOperation = PendingOperation::None;
    m_pendingLogin.reset();
    m_pendingRegistration.reset();
    m_loginAttempts.clear();
    m_loginAttemptIndex = -1;

    logInfo(QStringLiteral("Authentication succeeded for %1").arg(session.jid));
    m_settings->setLastLoginRequest(loginRequest);
    requestOwnVCard();
    rebuildChatsFromRoster();
    saveCredentialsIfNeeded();

    emit sessionChanged(*m_session);
    emit authenticationSucceeded(*m_session);
}

}  // namespace CuteXmpp
