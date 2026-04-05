#pragma once

#include "src/models/types.h"

#include <QObject>
#include <QXmppStanza.h>
#include <optional>

class QXmppClient;
class QXmppMamManager;
class QXmppBookmarkManager;
class QXmppDiscoveryManager;
class QXmppHttpUploadManager;
class QXmppPubSubManager;
class QXmppRegistrationManager;
class QXmppRosterManager;
class QXmppVCardManager;
class QXmppMucManager;
class QXmppMucManagerV2;
class QXmppMucRoom;
class QXmppError;
class QXmppMessage;
class QXmppPresence;
class QXmppRegisterIq;
class QXmppBookmarkSet;

namespace CuteXmpp {

class AppSettings;

class XmppService final : public QObject
{
    Q_OBJECT

public:
    explicit XmppService(AppSettings* settings, QObject* parent = nullptr);
    ~XmppService() override;

    const std::optional<AccountSession>& session() const;
    QVector<ChatSummary> chats() const;
    QVector<MessageEntry> messages(const QString& chatId) const;
    bool canLoadOlderMessages(const QString& chatId) const;
    bool isHistoryLoading(const QString& chatId) const;

    void login(const LoginRequest& request);
    void registerAccount(const RegistrationRequest& request);
    void disconnectFromServer();
    void sendMessage(const QString& chatId, const QString& text);
    void sendAttachment(const QString& chatId, const QString& filePath);
    void ensureConversationLoaded(const QString& chatId);
    void loadOlderMessages(const QString& chatId);
    void markChatRead(const QString& chatId);

signals:
    void authenticationSucceeded(const AccountSession& session);
    void authenticationFailed(const QString& message);
    void errorMessage(const QString& message);
    void infoMessage(const QString& message);
    void chatsChanged();
    void messagesChanged(const QString& chatId);
    void sessionChanged(const AccountSession& session);

private:
    enum class PendingOperation
    {
        None,
        Login,
        RegistrationHandshake,
        RegistrationReconnectLogin
    };

    struct ChatHistoryState
    {
        bool initialLoaded = false;
        bool loading = false;
        bool hasMore = true;
        QString oldestCursor;
        QString newestCursor;
    };

    void connectSignals();
    void resetSessionState();
    void rebuildChatsFromRoster();
    void requestContactVCard(const QString& bareJid, bool forceRefresh = false);
    void requestOwnVCard();
    void requestConversationPreview(const QString& chatId);
    void loadConversationFromMam(const QString& chatId, int maxMessages, bool olderMessages = false);
    void updateConversationPreview(const QString& chatId);
    void updateChatPresence(const QString& bareJid);
    void ensureChatExists(const QString& bareJid);
    void ensureGroupChatExists(const QString& roomJid, const QString& title = {}, const QString& nickName = {});
    void requestGroupChatInfo(const QString& roomJid);
    void syncBookmarkSet(const QXmppBookmarkSet& bookmarks);
    void syncMucBookmark(const QString& roomJid, const QString& name, const QString& nickName);
    void registerMucRoom(QXmppMucRoom* room);
    void handleClientConnected();
    void handleClientDisconnected();
    void handleClientError(const QXmppError& error);
    void handleMessageReceived(const QXmppMessage& message);
    void handlePresenceReceived(const QXmppPresence& presence);
    void handleRegistrationForm(const QXmppRegisterIq& iq);
    void handleRegistrationFailed(const QXmppStanza::Error& error);
    void saveCredentialsIfNeeded();
    void finishAuthentication(const QString& displayName);

    AppSettings* m_settings = nullptr;
    QXmppClient* m_client = nullptr;
    QXmppRegistrationManager* m_registrationManager = nullptr;
    QXmppRosterManager* m_rosterManager = nullptr;
    QXmppVCardManager* m_vCardManager = nullptr;
    QXmppMamManager* m_mamManager = nullptr;
    QXmppBookmarkManager* m_bookmarkManager = nullptr;
    QXmppDiscoveryManager* m_discoveryManager = nullptr;
    QXmppHttpUploadManager* m_httpUploadManager = nullptr;
    QXmppPubSubManager* m_pubSubManager = nullptr;
    QXmppMucManager* m_mucManager = nullptr;
    QXmppMucManagerV2* m_mucManagerV2 = nullptr;

    std::optional<AccountSession> m_session;
    QMap<QString, ChatSummary> m_chats;
    QHash<QString, QVector<MessageEntry>> m_messages;
    QHash<QString, ChatHistoryState> m_historyStates;
    QHash<QString, QXmppMucRoom*> m_mucRooms;
    QHash<QString, QString> m_mucNicks;
    QHash<QString, QByteArray> m_contactPhotoHashes;
    QSet<QString> m_requestedRoomInfo;
    QSet<QString> m_previewRequestsInFlight;
    QSet<QString> m_loadedConversationPreviews;
    QSet<QString> m_contactVCardRequestsInFlight;
    QSet<QString> m_loadedContactVCards;
    int m_lastLoggedState = -1;
    PendingOperation m_pendingOperation = PendingOperation::None;
    std::optional<LoginRequest> m_pendingLogin;
    std::optional<RegistrationRequest> m_pendingRegistration;
};

}  // namespace CuteXmpp
