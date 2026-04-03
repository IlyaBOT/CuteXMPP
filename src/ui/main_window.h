#pragma once

#include "src/models/types.h"

#include <QMainWindow>

class QButtonGroup;
class QLayout;
class QFrame;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QMenu;
class QScrollArea;
class QSplitter;
class QToolButton;
class QVBoxLayout;

namespace CuteXmpp {

class AppSettings;
class ChatBackgroundFrame;
class ChatInfoOverlay;
class ClickableFrame;
class ElidedLabel;
class XmppService;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(AppSettings* settings, XmppService* service, QWidget* parent = nullptr);

    void initializeSession(const AccountSession& session);
    void applyUiSettings();

signals:
    void logoutRequested();

private:
    void buildUi();
    void createMenus();
    void rebuildWorkspaceButtons();
    void rebuildChatList();
    void rebuildMessages();
    void renderNextMessageChunk();
    void clearLayout(QLayout* layout);
    void handleChatSelectionChanged(QListWidgetItem* current, QListWidgetItem* previous);
    void sendCurrentMessage();
    void refreshHeader();
    void selectFirstVisibleChat();
    void selectChatById(const QString& chatId);
    QVector<ChatSummary> filteredChats() const;
    std::optional<ChatSummary> currentChat() const;
    QString currentWorkspaceName() const;
    void openSettings();
    void openCurrentChatInfo();
    void openCurrentChatMenu();

    AppSettings* m_settings = nullptr;
    XmppService* m_service = nullptr;
    AccountSession m_session;
    ThemePalette m_theme;
    QString m_currentWorkspaceId = QString::fromUtf8(kAllChatsWorkspaceId);
    QString m_currentChatId;

    QFrame* m_secondarySidebar = nullptr;
    QSplitter* m_contentSplitter = nullptr;
    QButtonGroup* m_workspaceButtons = nullptr;
    QVBoxLayout* m_workspaceLayout = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QListWidget* m_chatListWidget = nullptr;
    ClickableFrame* m_headerInfoButton = nullptr;
    QLabel* m_chatAvatarLabel = nullptr;
    ElidedLabel* m_chatTitleLabel = nullptr;
    ElidedLabel* m_chatMetaLabel = nullptr;
    ChatBackgroundFrame* m_chatBackground = nullptr;
    QScrollArea* m_messageScrollArea = nullptr;
    QWidget* m_messageContainer = nullptr;
    QVBoxLayout* m_messageLayout = nullptr;
    QLineEdit* m_messageEdit = nullptr;
    QToolButton* m_sendButton = nullptr;
    QMenu* m_chatMenu = nullptr;
    ChatInfoOverlay* m_chatInfoOverlay = nullptr;
    QVector<MessageEntry> m_renderQueue;
    ChatSummary m_renderChat;
    QString m_lastRenderedChatId;
    int m_renderIndex = 0;
    int m_renderBubbleWidth = 0;
    int m_previousScrollValue = 0;
    int m_previousScrollMaximum = 0;
    int m_lastRenderedMessageCount = 0;
    bool m_scrollToBottomOnNextRender = false;
    bool m_suppressHistoryAutoLoad = false;
};

}  // namespace CuteXmpp
