#include "src/settings/app_settings.h"
#include "src/settings/settings_store.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

namespace CuteXmpp {

namespace {

QVector<Workspace> parseWorkspaces(const QString& json)
{
    QVector<Workspace> workspaces;
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8());
    if (!document.isArray()) {
        return workspaces;
    }

    const QJsonArray array = document.array();
    workspaces.reserve(array.size());
    for (const QJsonValue& value : array) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject object = value.toObject();
        Workspace workspace;
        workspace.id = object.value("id").toString();
        workspace.name = object.value("name").toString();
        const QJsonArray chatIds = object.value("chatIds").toArray();
        for (const QJsonValue& chatId : chatIds) {
            workspace.chatIds.append(chatId.toString());
        }
        if (!workspace.id.isEmpty() && !workspace.name.trimmed().isEmpty()) {
            workspaces.append(workspace);
        }
    }

    return workspaces;
}

QString serializeWorkspaces(const QVector<Workspace>& workspaces)
{
    QJsonArray array;
    for (const Workspace& workspace : workspaces) {
        if (workspace.id.isEmpty() || workspace.name.trimmed().isEmpty()) {
            continue;
        }

        QJsonObject object;
        object.insert("id", workspace.id);
        object.insert("name", workspace.name);

        QJsonArray chatIds;
        for (const QString& chatId : workspace.chatIds) {
            chatIds.append(chatId);
        }
        object.insert("chatIds", chatIds);
        array.append(object);
    }

    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

}  // namespace

AppSettings::AppSettings()
{
    load();
}

const UiSettings& AppSettings::ui() const
{
    return m_ui;
}

QVector<Workspace> AppSettings::customWorkspaces() const
{
    return m_workspaces;
}

QVector<Workspace> AppSettings::allWorkspaces() const
{
    QVector<Workspace> workspaces;
    workspaces.reserve(m_workspaces.size() + 1);
    workspaces.append({QString::fromUtf8(kAllChatsWorkspaceId), "All Chats", {}, true});
    for (const Workspace& workspace : m_workspaces) {
        workspaces.append(workspace);
    }
    return workspaces;
}

std::optional<LoginRequest> AppSettings::lastLoginRequest() const
{
    return m_lastLoginRequest;
}

QString AppSettings::deviceId() const
{
    return m_deviceId;
}

void AppSettings::setThemeId(const QString& themeId)
{
    if (themeId.isEmpty() || m_ui.themeId == themeId) {
        return;
    }
    m_ui.themeId = themeId;
    saveUi();
}

void AppSettings::setChatBackgroundImagePath(const QString& path)
{
    if (m_ui.chatBackgroundImagePath == path) {
        return;
    }
    m_ui.chatBackgroundImagePath = path;
    saveUi();
}

void AppSettings::setChatListWidth(int width)
{
    width = qMax(260, width);
    if (m_ui.chatListWidth == width) {
        return;
    }
    m_ui.chatListWidth = width;
    saveUi();
}

void AppSettings::setNotificationsEnabled(bool enabled)
{
    if (m_ui.notificationsEnabled == enabled) {
        return;
    }
    m_ui.notificationsEnabled = enabled;
    saveUi();
}

void AppSettings::setRequireTls(bool enabled)
{
    if (m_ui.requireTls == enabled) {
        return;
    }
    m_ui.requireTls = enabled;
    saveUi();
}

void AppSettings::setRememberTokens(bool enabled)
{
    if (m_ui.rememberTokens == enabled) {
        return;
    }
    m_ui.rememberTokens = enabled;
    saveUi();
}

void AppSettings::setLanguage(const QString& language)
{
    if (language.isEmpty() || m_ui.language == language) {
        return;
    }
    m_ui.language = language;
    saveUi();
}

void AppSettings::setLastLoginRequest(const LoginRequest& request)
{
    if (request.jid.trimmed().isEmpty() || request.server.trimmed().isEmpty()) {
        return;
    }

    m_lastLoginRequest = request;
    saveSession();
}

void AppSettings::clearLastLoginRequest()
{
    if (!m_lastLoginRequest.has_value()) {
        return;
    }

    m_lastLoginRequest.reset();
    saveSession();
}

QString AppSettings::addWorkspace(const QString& name, const QStringList& chatIds)
{
    const QString trimmedName = name.trimmed();
    if (trimmedName.isEmpty()) {
        return {};
    }

    Workspace workspace;
    workspace.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    workspace.name = trimmedName;
    workspace.chatIds = chatIds;
    m_workspaces.append(workspace);
    saveWorkspaces();
    return workspace.id;
}

void AppSettings::renameWorkspace(const QString& workspaceId, const QString& name)
{
    const QString trimmedName = name.trimmed();
    if (workspaceId.isEmpty() || trimmedName.isEmpty()) {
        return;
    }

    for (Workspace& workspace : m_workspaces) {
        if (workspace.id == workspaceId) {
            workspace.name = trimmedName;
            saveWorkspaces();
            return;
        }
    }
}

void AppSettings::removeWorkspace(const QString& workspaceId)
{
    if (workspaceId.isEmpty()) {
        return;
    }

    for (int index = 0; index < m_workspaces.size(); ++index) {
        if (m_workspaces.at(index).id == workspaceId) {
            m_workspaces.removeAt(index);
            saveWorkspaces();
            return;
        }
    }
}

void AppSettings::setWorkspaceChatIds(const QString& workspaceId, const QStringList& chatIds)
{
    for (Workspace& workspace : m_workspaces) {
        if (workspace.id == workspaceId) {
            workspace.chatIds = chatIds;
            workspace.chatIds.removeDuplicates();
            saveWorkspaces();
            return;
        }
    }
}

void AppSettings::setCustomWorkspaces(const QVector<Workspace>& workspaces)
{
    m_workspaces = workspaces;
    saveWorkspaces();
}

void AppSettings::load()
{
    QSettings settings = createSettings();
    m_ui.themeId = settings.value("ui/theme", m_ui.themeId).toString();
    m_ui.chatBackgroundImagePath = settings.value("ui/chatBackgroundImage").toString();
    m_ui.chatListWidth = settings.value("ui/chatListWidth", m_ui.chatListWidth).toInt();
    m_ui.notificationsEnabled = settings.value("ui/notificationsEnabled", m_ui.notificationsEnabled).toBool();
    m_ui.requireTls = settings.value("ui/requireTls", m_ui.requireTls).toBool();
    m_ui.rememberTokens = settings.value("ui/rememberTokens", m_ui.rememberTokens).toBool();
    m_ui.language = settings.value("ui/language", m_ui.language).toString();

    m_workspaces = parseWorkspaces(settings.value("workspaces/custom").toString());

    m_deviceId = settings.value("xmpp/deviceId").toString();
    if (m_deviceId.isEmpty()) {
        m_deviceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        settings.setValue("xmpp/deviceId", m_deviceId);
    }

    const QString lastJid = settings.value("session/lastJid").toString().trimmed();
    const QString lastServer = settings.value("session/lastServer").toString().trimmed();
    if (!lastJid.isEmpty() && !lastServer.isEmpty()) {
        LoginRequest request;
        request.jid = lastJid;
        request.password = settings.value("session/lastPassword").toString();
        request.server = lastServer;
        request.connectHost = settings.value("session/lastConnectHost").toString().trimmed();
        request.port = static_cast<quint16>(settings.value("session/lastPort", 5222).toUInt());
        request.proxyMode = static_cast<ProxyMode>(settings.value("session/lastProxyMode", static_cast<int>(ProxyMode::System)).toInt());
        request.tlsMode = static_cast<TlsMode>(settings.value("session/lastTlsMode", static_cast<int>(TlsMode::StartTls)).toInt());
        m_lastLoginRequest = request;
    }
}

void AppSettings::saveUi() const
{
    QSettings settings = createSettings();
    settings.setValue("ui/theme", m_ui.themeId);
    settings.remove("ui/chatBackgroundColor");
    settings.setValue("ui/chatBackgroundImage", m_ui.chatBackgroundImagePath);
    settings.setValue("ui/chatListWidth", m_ui.chatListWidth);
    settings.setValue("ui/notificationsEnabled", m_ui.notificationsEnabled);
    settings.setValue("ui/requireTls", m_ui.requireTls);
    settings.setValue("ui/rememberTokens", m_ui.rememberTokens);
    settings.setValue("ui/language", m_ui.language);
    settings.setValue("xmpp/deviceId", m_deviceId);
}

void AppSettings::saveWorkspaces() const
{
    QSettings settings = createSettings();
    settings.setValue("workspaces/custom", serializeWorkspaces(m_workspaces));
}

void AppSettings::saveSession() const
{
    QSettings settings = createSettings();
    if (!m_lastLoginRequest.has_value()) {
        settings.remove("session");
        return;
    }

    settings.setValue("session/lastJid", m_lastLoginRequest->jid);
    settings.setValue("session/lastPassword", m_lastLoginRequest->password);
    settings.setValue("session/lastServer", m_lastLoginRequest->server);
    settings.setValue("session/lastConnectHost", m_lastLoginRequest->connectHost);
    settings.setValue("session/lastPort", m_lastLoginRequest->port);
    settings.setValue("session/lastProxyMode", static_cast<int>(m_lastLoginRequest->proxyMode));
    settings.setValue("session/lastTlsMode", static_cast<int>(m_lastLoginRequest->tlsMode));
}

}  // namespace CuteXmpp
