#pragma once

#include "src/models/types.h"

#include <QDialog>
#include <optional>

class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QSpinBox;
class QStackedWidget;

namespace CuteXmpp {

class AppSettings;

class SettingsDialog final : public QDialog
{
    Q_OBJECT

public:
    SettingsDialog(AppSettings* settings,
                   const std::optional<AccountSession>& session,
                   const QVector<ChatSummary>& chats,
                   QWidget* parent = nullptr);

signals:
    void logoutRequested();

protected:
    void accept() override;

private:
    QWidget* buildMyAccountPage();
    QWidget* buildMainSettingsPage();
    QWidget* buildNotificationsPage();
    QWidget* buildSecurityPage();
    QWidget* buildAppearancePage();
    QWidget* buildLanguagePage();
    QWidget* buildFoldersPage();
    void addCategory(const QString& title, QWidget* page);
    void populateFolderList();
    void populateFolderChatList(int workspaceIndex);
    void updateFolderButtons();

    AppSettings* m_settings = nullptr;
    std::optional<AccountSession> m_session;
    QVector<ChatSummary> m_chats;
    QVector<Workspace> m_workspaces;
    bool m_updatingFolderChats = false;
    QString m_backgroundImagePath;

    QListWidget* m_categoryList = nullptr;
    QStackedWidget* m_pages = nullptr;
    QLabel* m_accountAvatarLabel = nullptr;
    QLabel* m_accountNameLabel = nullptr;
    QLabel* m_accountJidLabel = nullptr;
    QLabel* m_accountServerLabel = nullptr;
    QSpinBox* m_chatListWidthSpin = nullptr;
    QCheckBox* m_notificationsCheck = nullptr;
    QCheckBox* m_requireTlsCheck = nullptr;
    QCheckBox* m_rememberTokensCheck = nullptr;
    QComboBox* m_themeCombo = nullptr;
    QComboBox* m_languageCombo = nullptr;
    QListWidget* m_folderList = nullptr;
    QListWidget* m_folderChatsList = nullptr;
};

}  // namespace CuteXmpp
