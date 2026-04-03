#include "src/xmpp/xmpp_service.h"

#include "src/logging/app_logger.h"
#include "src/settings/app_settings.h"
#include "src/settings/settings_store.h"
#include "src/ui/theme.h"

#include <QBuffer>
#include <QImage>
#include <QMap>
#include <QNetworkProxy>
#include <QSysInfo>
#include <QTimer>
#include <QUuid>
#include <QXmlStreamReader>
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
#include <QXmppLogger.h>
#include <QXmppMamManager.h>
#include <QXmppMessage.h>
#include <QXmppMucForms.h>
#include <QXmppMucManager.h>
#include <QXmppMucManagerV2.h>
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

namespace CuteXmpp {

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

QString maskedEndpoint(const QString& jid, const QString& server, quint16 port, const QString& password)
{
    return QStringLiteral("jid=%1, server=%2, port=%3, password=%4")
        .arg(jid, server)
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
        return QXmppConfiguration::TLSRequired;
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

QString serializeCredentials(const QXmppCredentials& credentials)
{
    QString xml;
    QXmlStreamWriter writer(&xml);
    credentials.toXml(writer);
    return xml;
}

std::optional<QXmppCredentials> deserializeCredentials(const QString& xml)
{
    if (xml.trimmed().isEmpty()) {
        return std::nullopt;
    }

    QXmlStreamReader reader(xml);
    while (!reader.atEnd() && reader.readNext() != QXmlStreamReader::StartElement) {
    }
    if (reader.atEnd()) {
        return std::nullopt;
    }
    return QXmppCredentials::fromXml(reader);
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
    m_pubSubManager = m_client->addNewExtension<QXmppPubSubManager>();
    m_mucManager = m_client->addNewExtension<QXmppMucManager>();
    m_mucManagerV2 = m_client->addNewExtension<QXmppMucManagerV2>();
    m_rosterManager = m_client->findExtension<QXmppRosterManager>();
    m_vCardManager = m_client->findExtension<QXmppVCardManager>();

    logInfo(QStringLiteral("XMPP service initialized. Settings path: %1").arg(appDataPath()));
    connectSignals();
}

XmppService::~XmppService() = default;

const std::optional<AccountSession>& XmppService::session() const
{
    return m_session;
}

QVector<ChatSummary> XmppService::chats() const
{
    QVector<ChatSummary> chats = m_chats.values().toVector();
    std::sort(chats.begin(), chats.end(), [](const ChatSummary& left, const ChatSummary& right) {
        return left.lastActivity > right.lastActivity;
    });
    return chats;
}

QVector<MessageEntry> XmppService::messages(const QString& chatId) const
{
    return m_messages.value(chatId);
}

bool XmppService::canLoadOlderMessages(const QString& chatId) const
{
    const auto it = m_historyStates.constFind(chatId);
    return it != m_historyStates.cend() && it->initialLoaded && it->hasMore && !it->loading;
}

bool XmppService::isHistoryLoading(const QString& chatId) const
{
    const auto it = m_historyStates.constFind(chatId);
    return it != m_historyStates.cend() && it->loading;
}

void XmppService::login(const LoginRequest& rawRequest)
{
    const QString jid = bareJidOf(rawRequest.jid);
    const QString username = userFromJid(jid);
    const QString server = rawRequest.server.trimmed().isEmpty() ? domainFromJid(jid) : rawRequest.server.trimmed();
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

    resetSessionState();
    m_pendingLogin = request;
    m_pendingOperation = PendingOperation::Login;
    m_pendingRegistration.reset();
    m_registrationManager->setRegisterOnConnectEnabled(false);

    logInfo(QStringLiteral("Starting login with %1, tls=%2, proxy=%3 (%4)")
                .arg(maskedEndpoint(request.jid, request.server, request.port, request.password),
                     tlsModeText(request.tlsMode),
                     proxyModeText(request.proxyMode),
                     proxyDetails(request.proxyMode)));

    QXmppConfiguration configuration;
    configuration.setJid(request.jid);
    configuration.setPassword(request.password);
    configuration.setDomain(request.server);
    configuration.setHost(request.server);
    configuration.setPort(request.port);
    configuration.setResourcePrefix("CuteXMPP");
    configuration.setAutoAcceptSubscriptions(true);
    configuration.setAutoReconnectionEnabled(false);
    configuration.setUseSASLAuthentication(true);
    configuration.setUseSasl2Authentication(true);
    configuration.setUseFastTokenAuthentication(m_settings->ui().rememberTokens);
    configuration.setStreamSecurityMode(streamSecurityModeFor(request.tlsMode));
    configuration.setNetworkProxy(networkProxyFor(request.proxyMode));
    configuration.setSasl2UserAgent(std::optional<QXmppSasl2UserAgent>(QXmppSasl2UserAgent(QUuid(m_settings->deviceId()), "CuteXMPP", QSysInfo::machineHostName())));

    if (m_settings->ui().rememberTokens) {
        const QSettings settingsStore = createSettings();
        const auto credentials = deserializeCredentials(settingsStore.value(credentialKey(jid)).toString());
        if (credentials.has_value()) {
            configuration.setCredentials(*credentials);
            logDebug(QStringLiteral("Loaded cached credentials for %1").arg(jid));
        } else {
            logDebug(QStringLiteral("No cached credentials found for %1").arg(jid));
        }
    }

    QXmppPresence presence(QXmppPresence::Available);
    presence.setAvailableStatusType(QXmppPresence::Online);
    logDebug(QStringLiteral("Connecting to XMPP server: jid=%1, domain=%2, host=%3, port=%4, tls=%5, proxy=%6, sasl2=%7, fast=%8")
                 .arg(configuration.jid(), configuration.domain(), configuration.host())
                 .arg(configuration.port())
                 .arg(tlsModeText(request.tlsMode), proxyDetails(request.proxyMode))
                 .arg(configuration.useSasl2Authentication() ? "on" : "off")
                 .arg(configuration.useFastTokenAuthentication() ? "on" : "off"));
    m_client->connectToServer(configuration, presence);
}

void XmppService::registerAccount(const RegistrationRequest& rawRequest)
{
    const QString username = rawRequest.username.trimmed();
    const QString server = rawRequest.server.trimmed();
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

    resetSessionState();
    m_pendingRegistration = request;
    m_pendingLogin.reset();
    m_pendingOperation = PendingOperation::RegistrationHandshake;
    m_registrationManager->setRegisterOnConnectEnabled(true);

    logInfo(QStringLiteral("Starting registration with username=%1, server=%2, port=%3, tls=%4, proxy=%5 (%6), password=%7")
                .arg(request.username, request.server)
                .arg(request.port)
                .arg(tlsModeText(request.tlsMode),
                     proxyModeText(request.proxyMode),
                     proxyDetails(request.proxyMode),
                     maskSecret(request.password)));

    QXmppConfiguration configuration;
    configuration.setDomain(request.server);
    configuration.setHost(request.server);
    configuration.setPort(request.port);
    configuration.setResourcePrefix("CuteXMPP");
    configuration.setUseSasl2Authentication(true);
    configuration.setUseFastTokenAuthentication(false);
    configuration.setStreamSecurityMode(streamSecurityModeFor(request.tlsMode));
    configuration.setNetworkProxy(networkProxyFor(request.proxyMode));

    logDebug(QStringLiteral("Connecting for in-band registration: domain=%1, host=%2, port=%3, tls=%4, proxy=%5")
                 .arg(configuration.domain(), configuration.host())
                 .arg(configuration.port())
                 .arg(tlsModeText(request.tlsMode), proxyDetails(request.proxyMode)));
    m_client->connectToServer(configuration);
}

void XmppService::disconnectFromServer()
{
    logInfo("Disconnecting from XMPP server.");
    m_pendingOperation = PendingOperation::None;
    m_pendingLogin.reset();
    m_pendingRegistration.reset();
    m_registrationManager->setRegisterOnConnectEnabled(false);
    resetSessionState();
    m_client->disconnectFromServer();
}

void XmppService::sendMessage(const QString& chatId, const QString& text)
{
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

void XmppService::ensureConversationLoaded(const QString& chatId)
{
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
    if (!canLoadOlderMessages(chatId)) {
        return;
    }

    loadConversationFromMam(chatId, 40, true);
}

void XmppService::markChatRead(const QString& chatId)
{
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
    m_requestedRoomInfo.clear();
    m_previewRequestsInFlight.clear();
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

void XmppService::requestContactVCard(const QString& bareJid)
{
    if (!m_vCardManager || bareJid.isEmpty()) {
        return;
    }

    m_vCardManager->fetchVCard(bareJid).then(this, [this, bareJid](QXmppVCardManager::VCardIqResult&& result) {
        if (!std::holds_alternative<QXmppVCardIq>(result) || !m_chats.contains(bareJid)) {
            return;
        }

        const QXmppVCardIq iq = std::get<QXmppVCardIq>(std::move(result));
        ChatSummary& chat = m_chats[bareJid];

        if (chat.title.trimmed().isEmpty()) {
            if (!iq.nickName().trimmed().isEmpty()) {
                chat.title = iq.nickName().trimmed();
            } else if (!iq.fullName().trimmed().isEmpty()) {
                chat.title = iq.fullName().trimmed();
            }
        }

        if (!iq.description().trimmed().isEmpty()) {
            chat.description = iq.description().trimmed();
        } else if (chat.description.trimmed().isEmpty()) {
            chat.description = bareJid;
        }

        if (!iq.photo().isEmpty()) {
            QImage avatar;
            avatar.loadFromData(iq.photo());
            if (!avatar.isNull()) {
                chat.avatar = avatar;
            }
        }
        emit chatsChanged();
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
            if (retrieved.messages.isEmpty()) {
                return;
            }

            const bool isGroupChat = m_chats.value(chatId).isGroupChat || m_mucRooms.contains(chatId);
            const QXmppMessage& message = retrieved.messages.constLast();
            if (message.body().trimmed().isEmpty()) {
                return;
            }

            ChatSummary& chat = m_chats[chatId];
            chat.preview = previewTextForMessage(message.body());
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
                if (message.body().trimmed().isEmpty()) {
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
                    entry.text = message.body().trimmed();
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
                entry.text = message.body().trimmed();
                entry.timestamp = message.stamp().isValid() ? message.stamp() : QDateTime::currentDateTime();
                entry.outgoing = outgoing;
                appendMessageIfMissing(conversation, entry);
            }

            std::sort(conversation.begin(), conversation.end(), [](const MessageEntry& left, const MessageEntry& right) {
                return left.timestamp < right.timestamp;
            });

            trimConversationBuffer(conversation, olderMessages);

            updateConversationPreview(chatId);
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
        if (!m_session.has_value() || message.body().trimmed().isEmpty()) {
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
        entry.text = message.body().trimmed();
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
    if (m_pendingOperation == PendingOperation::RegistrationReconnectLogin && m_pendingRegistration.has_value()) {
        const RegistrationRequest registration = *m_pendingRegistration;
        LoginRequest loginRequest;
        loginRequest.jid = QStringLiteral("%1@%2").arg(registration.username, registration.server);
        loginRequest.password = registration.password;
        loginRequest.server = registration.server;
        loginRequest.port = registration.port;
        loginRequest.proxyMode = registration.proxyMode;
        loginRequest.tlsMode = registration.tlsMode;
        QTimer::singleShot(0, this, [this, loginRequest]() { login(loginRequest); });
        return;
    }
}

void XmppService::handleClientError(const QXmppError& error)
{
    QString message = qxmppErrorText(error);
    const QString socketError = m_client->socketErrorString().trimmed();
    if (!socketError.isEmpty()) {
        message += QStringLiteral(" Socket: %1").arg(socketError);
    }
    logError(QStringLiteral("XMPP error: %1").arg(message));
    if (!m_session.has_value() && m_pendingOperation != PendingOperation::None) {
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
    if (!m_session.has_value() || message.body().trimmed().isEmpty()) {
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
    entry.text = message.body().trimmed();
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
        requestContactVCard(bareJid);
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
    if (!m_settings->ui().rememberTokens) {
        settings.remove(credentialKey(bareJid));
        logDebug(QStringLiteral("Removed cached credentials for %1 because token persistence is disabled.").arg(bareJid));
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
    session.port = loginRequest.port;

    m_session = session;
    m_pendingOperation = PendingOperation::None;
    m_pendingLogin.reset();
    m_pendingRegistration.reset();

    logInfo(QStringLiteral("Authentication succeeded for %1").arg(session.jid));
    m_settings->setLastLoginRequest(loginRequest);
    requestOwnVCard();
    rebuildChatsFromRoster();
    saveCredentialsIfNeeded();

    emit sessionChanged(*m_session);
    emit authenticationSucceeded(*m_session);
}

}  // namespace CuteXmpp
