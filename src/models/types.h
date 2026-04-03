#pragma once

#include <QColor>
#include <QDateTime>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QVector>

namespace CuteXmpp {

inline constexpr auto kAllChatsWorkspaceId = "all-chats";

struct ThemePalette
{
    QString id;
    QString name;
    QColor window;
    QColor sidebar;
    QColor sidebarAlt;
    QColor panel;
    QColor surface;
    QColor input;
    QColor accent;
    QColor accentStrong;
    QColor textPrimary;
    QColor textSecondary;
    QColor border;
    QColor bubbleIncoming;
    QColor bubbleOutgoing;
    QColor bubbleBorder;
};

struct UiSettings
{
    QString themeId = "nocturne";
    QColor chatBackgroundColor = QColor("#f5f3f9");
    QString chatBackgroundImagePath;
    int chatListWidth = 360;
    bool notificationsEnabled = true;
    bool requireTls = true;
    bool rememberTokens = true;
    QString language = "English";
};

struct Workspace
{
    QString id;
    QString name;
    QStringList chatIds;
    bool builtIn = false;
};

enum class PresenceState
{
    Offline,
    Online,
    Away,
    Busy
};

enum class ProxyMode
{
    System,
    NoProxy,
    Tor,
    TorBrowser
};

enum class TlsMode
{
    StartTls,
    DirectTls,
    Plain
};

struct AccountSession
{
    QString jid;
    QString bareJid;
    QString username;
    QString displayName;
    QString server;
    quint16 port = 5222;
    QImage avatar;
};

struct LoginRequest
{
    QString jid;
    QString password;
    QString server;
    quint16 port = 5222;
    ProxyMode proxyMode = ProxyMode::System;
    TlsMode tlsMode = TlsMode::StartTls;
};

struct RegistrationRequest
{
    QString username;
    QString server;
    QString password;
    quint16 port = 5222;
    ProxyMode proxyMode = ProxyMode::System;
    TlsMode tlsMode = TlsMode::StartTls;
};

struct MessageEntry
{
    QString id;
    QString chatId;
    QString senderJid;
    QString senderName;
    QString text;
    QDateTime timestamp;
    bool outgoing = false;
};

struct ChatSummary
{
    QString id;
    QString title;
    QString subtitle;
    QString description;
    QString preview;
    QDateTime lastActivity;
    int unreadCount = 0;
    int participantCount = -1;
    PresenceState presence = PresenceState::Offline;
    QImage avatar;
    bool isGroupChat = false;
};

}  // namespace CuteXmpp

Q_DECLARE_METATYPE(CuteXmpp::AccountSession)
Q_DECLARE_METATYPE(CuteXmpp::ChatSummary)
Q_DECLARE_METATYPE(CuteXmpp::MessageEntry)
