#pragma once

#include "src/models/types.h"

#include <optional>
#include <QVector>

namespace CuteXmpp {

class AppSettings
{
public:
    AppSettings();

    const UiSettings& ui() const;
    QVector<Workspace> customWorkspaces() const;
    QVector<Workspace> allWorkspaces() const;
    std::optional<LoginRequest> lastLoginRequest() const;

    QString deviceId() const;

    void setThemeId(const QString& themeId);
    void setChatBackgroundImagePath(const QString& path);
    void setChatListWidth(int width);
    void setNotificationsEnabled(bool enabled);
    void setRequireTls(bool enabled);
    void setRememberTokens(bool enabled);
    void setLanguage(const QString& language);
    void setLastLoginRequest(const LoginRequest& request);
    void clearLastLoginRequest();

    QString addWorkspace(const QString& name, const QStringList& chatIds = {});
    void renameWorkspace(const QString& workspaceId, const QString& name);
    void removeWorkspace(const QString& workspaceId);
    void setWorkspaceChatIds(const QString& workspaceId, const QStringList& chatIds);
    void setCustomWorkspaces(const QVector<Workspace>& workspaces);

private:
    void load();
    void saveUi() const;
    void saveWorkspaces() const;
    void saveSession() const;

    UiSettings m_ui;
    QVector<Workspace> m_workspaces;
    QString m_deviceId;
    std::optional<LoginRequest> m_lastLoginRequest;
};

}  // namespace CuteXmpp
