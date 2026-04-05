#include "src/ui/main_window.h"

#include "src/settings/app_settings.h"
#include "src/ui/settings_dialog.h"
#include "src/ui/theme.h"
#include "src/ui/widgets.h"
#include "src/xmpp/xmpp_service.h"

#include <QAction>
#include <QAbstractButton>
#include <QApplication>
#include <QButtonGroup>
#include <QCursor>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QToolButton>
#include <QTimer>
#include <QVBoxLayout>

namespace CuteXmpp {

namespace {

QString chatIconText(const ChatSummary& chat)
{
    return !chat.title.trimmed().isEmpty() ? chat.title : chat.id;
}

QString headerMetaText(const ChatSummary& chat)
{
    QStringList parts;
    if (!chat.subtitle.trimmed().isEmpty()) {
        parts.append(chat.subtitle.trimmed());
    }
    if (chat.isGroupChat && !chat.description.trimmed().isEmpty()) {
        parts.append(chat.description.trimmed());
    } else if (parts.isEmpty() && !chat.description.trimmed().isEmpty()) {
        parts.append(chat.description.trimmed());
    }
    if (parts.isEmpty()) {
        parts.append(chat.id);
    }
    return parts.join(QStringLiteral(" • "));
}

}  // namespace

MainWindow::MainWindow(AppSettings* settings, XmppService* service, QWidget* parent)
    : QMainWindow(parent)
    , m_settings(settings)
    , m_service(service)
    , m_theme(themeById(settings->ui().themeId))
{
    setWindowTitle("CuteXMPP");
    resize(1400, 900);

    buildUi();
    applyUiSettings();

    connect(m_searchEdit, &QLineEdit::textChanged, this, [this]() { rebuildChatList(); });
    connect(m_chatListWidget, &QListWidget::currentItemChanged, this, &MainWindow::handleChatSelectionChanged);
    connect(m_sendButton, &QToolButton::clicked, this, &MainWindow::sendCurrentMessage);
    connect(m_messageEdit, &QLineEdit::returnPressed, this, &MainWindow::sendCurrentMessage);
    connect(m_contentSplitter, &QSplitter::splitterMoved, this, [this]() {
        m_settings->setChatListWidth(m_secondarySidebar->width());
    });

    connect(m_service, &XmppService::chatsChanged, this, [this]() {
        rebuildWorkspaceButtons();
        rebuildChatList();
        refreshHeader();
    });
    connect(m_service, &XmppService::messagesChanged, this, [this](const QString& chatId) {
        if (chatId == m_currentChatId) {
            rebuildMessages();
        }
        rebuildChatList();
        refreshHeader();
    });
    connect(m_service, &XmppService::errorMessage, this, [this](const QString& message) {
        statusBar()->showMessage(message, 7000);
    });
    connect(m_service, &XmppService::sessionChanged, this, [this](const AccountSession& session) {
        m_session = session;
        setWindowTitle(QStringLiteral("CuteXMPP - %1").arg(session.displayName));
    });
    connect(m_messageScrollArea->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](const int value) {
        if (!m_suppressHistoryAutoLoad && !m_currentChatId.isEmpty() && value <= 32) {
            m_service->loadOlderMessages(m_currentChatId);
        }
    });
}

void MainWindow::initializeSession(const AccountSession& session)
{
    m_session = session;
    m_currentChatId.clear();
    setWindowTitle(QStringLiteral("CuteXMPP - %1").arg(session.displayName));
    rebuildWorkspaceButtons();
    rebuildChatList();
    refreshHeader();
    rebuildMessages();
}

void MainWindow::applyUiSettings()
{
    m_theme = themeById(m_settings->ui().themeId);
    m_chatBackground->setOverlayColor(m_theme.id == "daybreak" ? QColor(255, 255, 255, 50) : QColor(255, 255, 255, 22));
    m_chatBackground->setBackgroundAppearance(m_theme.bubbleIncoming, m_settings->ui().chatBackgroundImagePath);

    const int chatListWidth = m_settings->ui().chatListWidth;
    m_contentSplitter->setSizes({chatListWidth, width() - chatListWidth});

    rebuildWorkspaceButtons();
    rebuildChatList();
    refreshHeader();
    rebuildMessages();
}

void MainWindow::buildUi()
{
    auto* central = new QWidget(this);
    auto* rootLayout = new QHBoxLayout(central);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto* primarySidebar = new QFrame(central);
    primarySidebar->setObjectName("PrimarySidebar");
    primarySidebar->setFixedWidth(100);

    auto* primaryLayout = new QVBoxLayout(primarySidebar);
    primaryLayout->setContentsMargins(12, 14, 12, 14);
    primaryLayout->setSpacing(14);

    auto* toggleListButton = new QToolButton(primarySidebar);
    toggleListButton->setText("Menu");
    toggleListButton->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    toggleListButton->setIcon(style()->standardIcon(QStyle::SP_TitleBarMenuButton));
    connect(toggleListButton, &QToolButton::clicked, this, [this]() {
        m_secondarySidebar->setVisible(!m_secondarySidebar->isVisible());
    });

    m_workspaceButtons = new QButtonGroup(this);
    m_workspaceButtons->setExclusive(true);
    auto* workspaceContainer = new QWidget(primarySidebar);
    m_workspaceLayout = new QVBoxLayout(workspaceContainer);
    m_workspaceLayout->setContentsMargins(0, 0, 0, 0);
    m_workspaceLayout->setSpacing(10);

    auto* addWorkspaceButton = new QToolButton(primarySidebar);
    addWorkspaceButton->setText("Folder");
    addWorkspaceButton->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    addWorkspaceButton->setIcon(style()->standardIcon(QStyle::SP_FileDialogNewFolder));
    connect(addWorkspaceButton, &QToolButton::clicked, this, [this]() {
        const QString name = QInputDialog::getText(this, "New folder", "Folder name");
        if (name.trimmed().isEmpty()) {
            return;
        }
        const QStringList initialChats = m_currentChatId.isEmpty() ? QStringList{} : QStringList{m_currentChatId};
        const QString newId = m_settings->addWorkspace(name.trimmed(), initialChats);
        if (!newId.isEmpty()) {
            m_currentWorkspaceId = newId;
            rebuildWorkspaceButtons();
            rebuildChatList();
        }
    });

    auto* settingsButton = new QToolButton(primarySidebar);
    settingsButton->setText("Settings");
    settingsButton->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    settingsButton->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
    connect(settingsButton, &QToolButton::clicked, this, &MainWindow::openSettings);

    primaryLayout->addWidget(toggleListButton, 0, Qt::AlignCenter);
    primaryLayout->addWidget(workspaceContainer, 0, Qt::AlignTop);
    primaryLayout->addStretch(1);
    primaryLayout->addWidget(addWorkspaceButton, 0, Qt::AlignCenter);
    primaryLayout->addWidget(settingsButton, 0, Qt::AlignCenter);

    m_contentSplitter = new QSplitter(Qt::Horizontal, central);
    m_contentSplitter->setChildrenCollapsible(false);

    m_secondarySidebar = new QFrame(m_contentSplitter);
    m_secondarySidebar->setObjectName("SecondarySidebar");
    auto* secondaryLayout = new QVBoxLayout(m_secondarySidebar);
    secondaryLayout->setContentsMargins(8, 8, 8, 8);
    secondaryLayout->setSpacing(8);

    m_searchEdit = new QLineEdit(m_secondarySidebar);
    m_searchEdit->setPlaceholderText("Search chats");

    m_chatListWidget = new QListWidget(m_secondarySidebar);
    m_chatListWidget->setObjectName("ChatListWidget");
    m_chatListWidget->setSpacing(8);
    m_chatListWidget->setFrameShape(QFrame::NoFrame);
    m_chatListWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_chatListWidget->setSelectionRectVisible(false);

    secondaryLayout->addWidget(m_searchEdit);
    secondaryLayout->addWidget(m_chatListWidget, 1);

    auto* rightPane = new QWidget(m_contentSplitter);
    auto* rightLayout = new QVBoxLayout(rightPane);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);

    auto* headerFrame = new QFrame(rightPane);
    headerFrame->setObjectName("HeaderFrame");
    auto* headerLayout = new QHBoxLayout(headerFrame);
    headerLayout->setContentsMargins(18, 14, 18, 14);
    headerLayout->setSpacing(12);

    m_headerInfoButton = new ClickableFrame(headerFrame);
    m_headerInfoButton->setObjectName("HeaderInfoButton");
    auto* infoLayout = new QHBoxLayout(m_headerInfoButton);
    infoLayout->setContentsMargins(10, 6, 10, 6);
    infoLayout->setSpacing(12);

    m_chatAvatarLabel = new QLabel(m_headerInfoButton);
    m_chatAvatarLabel->setFixedSize(44, 44);

    auto* headerText = new QWidget(m_headerInfoButton);
    auto* headerTextLayout = new QVBoxLayout(headerText);
    headerTextLayout->setContentsMargins(0, 0, 0, 0);
    headerTextLayout->setSpacing(4);

    m_chatTitleLabel = new ElidedLabel(headerText);
    m_chatTitleLabel->setObjectName("HeaderTitle");
    m_chatTitleLabel->setFullText("No chat selected");

    m_chatMetaLabel = new ElidedLabel(headerText);
    m_chatMetaLabel->setObjectName("HeaderMeta");
    m_chatMetaLabel->setFullText("Select a contact from your roster.");

    headerTextLayout->addWidget(m_chatTitleLabel);
    headerTextLayout->addWidget(m_chatMetaLabel);
    infoLayout->addWidget(m_chatAvatarLabel);
    infoLayout->addWidget(headerText, 1);

    auto* searchButton = new QToolButton(headerFrame);
    searchButton->setIcon(style()->standardIcon(QStyle::SP_FileDialogContentsView));
    connect(searchButton, &QToolButton::clicked, this, [this]() { m_searchEdit->setFocus(); });

    auto* moreButton = new QToolButton(headerFrame);
    moreButton->setIcon(style()->standardIcon(QStyle::SP_TitleBarUnshadeButton));
    connect(moreButton, &QToolButton::clicked, this, &MainWindow::openCurrentChatMenu);

    headerLayout->addWidget(m_headerInfoButton, 1);
    headerLayout->addWidget(searchButton);
    headerLayout->addWidget(moreButton);
    connect(m_headerInfoButton, &ClickableFrame::clicked, this, &MainWindow::openCurrentChatInfo);

    m_chatBackground = new ChatBackgroundFrame(rightPane);
    auto* backgroundLayout = new QVBoxLayout(m_chatBackground);
    backgroundLayout->setContentsMargins(18, 18, 18, 18);
    backgroundLayout->setSpacing(0);

    m_messageScrollArea = new QScrollArea(m_chatBackground);
    m_messageScrollArea->setObjectName("MessageScrollArea");
    m_messageScrollArea->setWidgetResizable(true);
    m_messageScrollArea->setFrameShape(QFrame::NoFrame);

    m_messageContainer = new QWidget(m_messageScrollArea);
    m_messageContainer->setObjectName("MessageContainer");
    m_messageLayout = new QVBoxLayout(m_messageContainer);
    m_messageLayout->setContentsMargins(0, 0, 0, 0);
    m_messageLayout->setSpacing(10);
    m_messageLayout->addStretch(1);

    m_messageScrollArea->setWidget(m_messageContainer);
    backgroundLayout->addWidget(m_messageScrollArea, 1);

    auto* composerFrame = new QFrame(rightPane);
    composerFrame->setObjectName("ComposerFrame");
    auto* composerLayout = new QHBoxLayout(composerFrame);
    composerLayout->setContentsMargins(16, 14, 16, 14);
    composerLayout->setSpacing(12);

    auto* attachButton = new QToolButton(composerFrame);
    attachButton->setIcon(style()->standardIcon(QStyle::SP_FileDialogNewFolder));
    attachButton->setToolTip("Attachments will be added later.");

    m_messageEdit = new QLineEdit(composerFrame);
    m_messageEdit->setPlaceholderText("Write a message...");

    m_sendButton = new QToolButton(composerFrame);
    m_sendButton->setIcon(style()->standardIcon(QStyle::SP_ArrowForward));

    composerLayout->addWidget(attachButton);
    composerLayout->addWidget(m_messageEdit, 1);
    composerLayout->addWidget(m_sendButton);

    rightLayout->addWidget(headerFrame);
    rightLayout->addWidget(m_chatBackground, 1);
    rightLayout->addWidget(composerFrame);

    m_contentSplitter->addWidget(m_secondarySidebar);
    m_contentSplitter->addWidget(rightPane);

    rootLayout->addWidget(primarySidebar);
    rootLayout->addWidget(m_contentSplitter, 1);

    setCentralWidget(central);
    m_chatInfoOverlay = new ChatInfoOverlay(central);
    m_chatInfoOverlay->hide();
    menuBar()->hide();
    statusBar()->showMessage("Ready");
}

void MainWindow::createMenus()
{
    auto* accountMenu = menuBar()->addMenu("Account");
    QAction* settingsAction = accountMenu->addAction("Settings...");
    connect(settingsAction, &QAction::triggered, this, &MainWindow::openSettings);

    QAction* logoutAction = accountMenu->addAction("Logout");
    connect(logoutAction, &QAction::triggered, this, [this]() { emit logoutRequested(); });

    QAction* quitAction = accountMenu->addAction("Quit");
    connect(quitAction, &QAction::triggered, this, &QWidget::close);
}

void MainWindow::rebuildWorkspaceButtons()
{
    if (!m_workspaceLayout) {
        return;
    }

    while (QLayoutItem* item = m_workspaceLayout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            m_workspaceButtons->removeButton(qobject_cast<QAbstractButton*>(widget));
            widget->deleteLater();
        }
        delete item;
    }

    const QVector<Workspace> workspaces = m_settings->allWorkspaces();
    bool currentExists = false;

    for (const Workspace& workspace : workspaces) {
        auto* button = new QToolButton(this);
        button->setObjectName("WorkspaceButton");
        button->setCheckable(true);
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setText(workspace.name);
        button->setIcon(style()->standardIcon(workspace.builtIn ? QStyle::SP_FileDialogDetailedView : QStyle::SP_DirIcon));
        button->setChecked(workspace.id == m_currentWorkspaceId);
        button->setProperty("workspaceId", workspace.id);
        m_workspaceButtons->addButton(button);
        m_workspaceLayout->addWidget(button);

        if (workspace.id == m_currentWorkspaceId) {
            currentExists = true;
        }

        connect(button, &QToolButton::clicked, this, [this, workspace]() {
            m_currentWorkspaceId = workspace.id;
            rebuildChatList();
        });
    }

    if (!currentExists) {
        m_currentWorkspaceId = QString::fromUtf8(kAllChatsWorkspaceId);
        for (QAbstractButton* button : m_workspaceButtons->buttons()) {
            if (button->property("workspaceId").toString() == m_currentWorkspaceId) {
                button->setChecked(true);
                break;
            }
        }
    }

    m_workspaceLayout->addStretch(1);
}

void MainWindow::rebuildChatList()
{
    const QVector<ChatSummary> chats = filteredChats();
    const QString selectedChatId = m_currentChatId;
    QString targetChatId = selectedChatId;

    {
        const QSignalBlocker blocker(m_chatListWidget);
        m_chatListWidget->clear();
        for (const ChatSummary& chat : chats) {
            auto* item = new QListWidgetItem;
            item->setData(Qt::UserRole, chat.id);
            item->setSizeHint(QSize(0, 80));

            auto* widget = new ChatListItemWidget(m_chatListWidget);
            widget->setChat(chat, m_theme, false);
            m_chatListWidget->addItem(item);
            m_chatListWidget->setItemWidget(item, widget);
        }

        QListWidgetItem* targetItem = nullptr;
        if (!selectedChatId.isEmpty()) {
            for (int row = 0; row < m_chatListWidget->count(); ++row) {
                auto* item = m_chatListWidget->item(row);
                if (item->data(Qt::UserRole).toString() == selectedChatId) {
                    targetItem = item;
                    break;
                }
            }
        }

        if (!targetItem && m_chatListWidget->count() > 0) {
            targetItem = m_chatListWidget->item(0);
            targetChatId = targetItem->data(Qt::UserRole).toString();
        } else if (!targetItem) {
            targetChatId.clear();
        }

        m_chatListWidget->setCurrentItem(targetItem);
    }

    if (targetChatId != m_currentChatId) {
        m_currentChatId = targetChatId;
        if (!m_currentChatId.isEmpty()) {
            m_service->markChatRead(m_currentChatId);
            m_scrollToBottomOnNextRender = true;
            m_service->ensureConversationLoaded(m_currentChatId);
        }
        refreshChatListWidgets(chats);
        refreshHeader();
        rebuildMessages();
    } else if (m_currentChatId.isEmpty()) {
        refreshChatListWidgets(chats);
        refreshHeader();
        rebuildMessages();
    } else {
        refreshChatListWidgets(chats);
    }
}

void MainWindow::rebuildMessages()
{
    m_suppressHistoryAutoLoad = true;
    auto* scrollBar = m_messageScrollArea->verticalScrollBar();
    m_previousScrollValue = scrollBar->value();
    m_previousScrollMaximum = scrollBar->maximum();

    clearLayout(m_messageLayout);
    m_messageLayout->addStretch(1);

    const auto chat = currentChat();
    if (!chat.has_value()) {
        auto* placeholder = new QLabel("Select a chat to view messages from the server archive.", m_messageContainer);
        placeholder->setObjectName("HeaderMeta");
        placeholder->setAlignment(Qt::AlignCenter);
        m_messageLayout->insertWidget(0, placeholder);
        m_suppressHistoryAutoLoad = false;
        return;
    }

    m_renderChat = *chat;
    m_renderQueue = m_service->messages(chat->id);
    m_renderIndex = 0;
    m_renderBubbleWidth = qMax(300, m_messageScrollArea->viewport()->width() - 220);

    if (m_renderQueue.isEmpty()) {
        auto* placeholder = new QLabel(m_service->isHistoryLoading(chat->id) ? "Loading messages..." : "No archived messages loaded yet.", m_messageContainer);
        placeholder->setObjectName("HeaderMeta");
        placeholder->setAlignment(Qt::AlignCenter);
        m_messageLayout->insertWidget(0, placeholder);
        m_suppressHistoryAutoLoad = false;
        return;
    }

    QTimer::singleShot(0, this, &MainWindow::renderNextMessageChunk);
}

void MainWindow::renderNextMessageChunk()
{
    if (m_renderIndex >= m_renderQueue.size()) {
        auto* scrollBar = m_messageScrollArea->verticalScrollBar();
        if (m_scrollToBottomOnNextRender) {
            scrollBar->setValue(scrollBar->maximum());
        } else if (m_lastRenderedChatId == m_renderChat.id && m_previousScrollValue <= 32 && m_renderQueue.size() > m_lastRenderedMessageCount) {
            scrollBar->setValue(qMin(scrollBar->maximum(), scrollBar->maximum() - m_previousScrollMaximum + m_previousScrollValue));
        } else {
            scrollBar->setValue(qMin(m_previousScrollValue, scrollBar->maximum()));
        }
        m_suppressHistoryAutoLoad = false;

        m_lastRenderedChatId = m_renderChat.id;
        m_lastRenderedMessageCount = static_cast<int>(m_renderQueue.size());
        m_scrollToBottomOnNextRender = false;
        return;
    }

    constexpr int kMessagesPerChunk = 16;
    int remaining = kMessagesPerChunk;
    while (m_renderIndex < m_renderQueue.size() && remaining-- > 0) {
        auto* widget = new MessageBubbleWidget(m_messageContainer);
        widget->setMessage(m_renderQueue.at(m_renderIndex), m_renderChat, m_theme, m_renderBubbleWidth);
        m_messageLayout->insertWidget(m_renderIndex, widget);
        ++m_renderIndex;
    }

    QTimer::singleShot(0, this, &MainWindow::renderNextMessageChunk);
}

void MainWindow::refreshChatListWidgets(const QVector<ChatSummary>& chats)
{
    for (int row = 0; row < m_chatListWidget->count(); ++row) {
        QListWidgetItem* item = m_chatListWidget->item(row);
        auto* widget = static_cast<ChatListItemWidget*>(m_chatListWidget->itemWidget(item));
        if (!widget) {
            continue;
        }

        const QString chatId = item->data(Qt::UserRole).toString();
        for (const ChatSummary& chat : chats) {
            if (chat.id == chatId) {
                widget->setChat(chat, m_theme, chatId == m_currentChatId);
                break;
            }
        }
    }
}

void MainWindow::clearLayout(QLayout* layout)
{
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        }
        if (QLayout* childLayout = item->layout()) {
            clearLayout(childLayout);
            delete childLayout;
        }
        delete item;
    }
}

void MainWindow::handleChatSelectionChanged(QListWidgetItem* current, QListWidgetItem* previous)
{
    Q_UNUSED(previous);

    if (!current) {
        m_currentChatId.clear();
        refreshHeader();
        rebuildMessages();
        return;
    }

    m_currentChatId = current->data(Qt::UserRole).toString();
    m_service->markChatRead(m_currentChatId);
    m_scrollToBottomOnNextRender = true;
    m_service->ensureConversationLoaded(m_currentChatId);
    refreshHeader();
    rebuildMessages();

    refreshChatListWidgets(filteredChats());
}

void MainWindow::sendCurrentMessage()
{
    const QString text = m_messageEdit->text().trimmed();
    if (text.isEmpty() || m_currentChatId.isEmpty()) {
        return;
    }

    m_scrollToBottomOnNextRender = true;
    m_service->sendMessage(m_currentChatId, text);
    m_messageEdit->clear();
}

void MainWindow::refreshHeader()
{
    const auto chat = currentChat();
    if (!chat.has_value()) {
        m_chatAvatarLabel->setPixmap(makeAvatarPixmap({}, "?", m_theme.accent, 44));
        m_chatTitleLabel->setFullText("No chat selected");
        m_chatMetaLabel->setFullText("Select a contact from your roster.");
        m_messageEdit->setEnabled(false);
        m_sendButton->setEnabled(false);
        return;
    }

    m_chatAvatarLabel->setPixmap(makeAvatarPixmap(chat->avatar, chatIconText(*chat), m_theme.accent, 44));
    m_chatTitleLabel->setFullText(chat->title);
    m_chatMetaLabel->setFullText(headerMetaText(*chat));
    m_messageEdit->setEnabled(true);
    m_sendButton->setEnabled(true);
}

void MainWindow::selectFirstVisibleChat()
{
    if (m_chatListWidget->count() == 0) {
        m_currentChatId.clear();
        refreshHeader();
        rebuildMessages();
        return;
    }
    m_chatListWidget->setCurrentRow(0);
}

void MainWindow::selectChatById(const QString& chatId)
{
    for (int row = 0; row < m_chatListWidget->count(); ++row) {
        QListWidgetItem* item = m_chatListWidget->item(row);
        if (item->data(Qt::UserRole).toString() == chatId) {
            m_chatListWidget->setCurrentItem(item);
            return;
        }
    }
}

QVector<ChatSummary> MainWindow::filteredChats() const
{
    QVector<ChatSummary> chats = m_service->chats();
    const QString search = m_searchEdit->text().trimmed();

    QStringList workspaceChatIds;
    const bool filterByWorkspace = m_currentWorkspaceId != QString::fromUtf8(kAllChatsWorkspaceId);
    if (filterByWorkspace) {
        for (const Workspace& workspace : m_settings->customWorkspaces()) {
            if (workspace.id == m_currentWorkspaceId) {
                workspaceChatIds = workspace.chatIds;
                break;
            }
        }
    }

    QVector<ChatSummary> filtered;
    filtered.reserve(chats.size());
    for (const ChatSummary& chat : chats) {
        if (filterByWorkspace && !workspaceChatIds.contains(chat.id)) {
            continue;
        }
        if (!search.isEmpty()) {
            const QString haystack = QStringLiteral("%1 %2 %3 %4").arg(chat.title, chat.subtitle, chat.preview, chat.description);
            if (!haystack.contains(search, Qt::CaseInsensitive)) {
                continue;
            }
        }
        filtered.append(chat);
    }
    return filtered;
}

std::optional<ChatSummary> MainWindow::currentChat() const
{
    const QVector<ChatSummary> chats = m_service->chats();
    for (const ChatSummary& chat : chats) {
        if (chat.id == m_currentChatId) {
            return chat;
        }
    }
    return std::nullopt;
}

QString MainWindow::currentWorkspaceName() const
{
    for (const Workspace& workspace : m_settings->allWorkspaces()) {
        if (workspace.id == m_currentWorkspaceId) {
            return workspace.name;
        }
    }
    return "All Chats";
}

void MainWindow::openSettings()
{
    SettingsDialog dialog(m_settings, m_service->session(), m_service->chats(), this);
    bool requestedLogout = false;
    connect(&dialog, &SettingsDialog::logoutRequested, this, [&dialog, &requestedLogout]() {
        requestedLogout = true;
        dialog.done(QDialog::Rejected);
    });

    if (dialog.exec() == QDialog::Accepted) {
        qApp->setStyleSheet(buildApplicationStyleSheet(themeById(m_settings->ui().themeId)));
        applyUiSettings();
        return;
    }

    if (requestedLogout) {
        emit logoutRequested();
    }
}

void MainWindow::openCurrentChatInfo()
{
    const auto chat = currentChat();
    if (!chat.has_value() || !m_chatInfoOverlay) {
        return;
    }

    m_chatInfoOverlay->showChatInfo(*chat, m_theme);
}

void MainWindow::openCurrentChatMenu()
{
    if (!m_chatMenu) {
        m_chatMenu = new QMenu(this);
        QAction* viewInfo = m_chatMenu->addAction("View contact info");
        connect(viewInfo, &QAction::triggered, this, &MainWindow::openCurrentChatInfo);

        QAction* reloadHistory = m_chatMenu->addAction("Reload history");
        connect(reloadHistory, &QAction::triggered, this, [this]() {
            if (!m_currentChatId.isEmpty()) {
                m_service->ensureConversationLoaded(m_currentChatId);
            }
        });

        QAction* logoutAction = m_chatMenu->addAction("Log out");
        connect(logoutAction, &QAction::triggered, this, [this]() {
            emit logoutRequested();
        });
    }

    m_chatMenu->exec(QCursor::pos());
}

}  // namespace CuteXmpp
