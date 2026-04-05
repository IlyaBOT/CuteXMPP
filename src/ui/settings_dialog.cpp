#include "src/ui/settings_dialog.h"

#include "src/settings/app_settings.h"
#include "src/ui/theme.h"
#include "src/ui/widgets.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QCheckBox>
#include <QUuid>

namespace CuteXmpp {

namespace {

QWidget* wrapPageContent(QWidget* content)
{
    auto* wrapper = new QWidget;
    auto* layout = new QVBoxLayout(wrapper);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(content);
    layout->addStretch(1);
    return wrapper;
}

}  // namespace

SettingsDialog::SettingsDialog(AppSettings* settings,
                               const std::optional<AccountSession>& session,
                               const QVector<ChatSummary>& chats,
                               QWidget* parent)
    : QDialog(parent)
    , m_settings(settings)
    , m_session(session)
    , m_chats(chats)
    , m_workspaces(settings->customWorkspaces())
    , m_backgroundImagePath(settings->ui().chatBackgroundImagePath)
{
    setWindowTitle("Settings");
    resize(880, 620);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(18, 18, 18, 18);
    rootLayout->setSpacing(12);

    auto* card = new QFrame(this);
    card->setObjectName("SettingsCard");
    auto* cardLayout = new QHBoxLayout(card);
    cardLayout->setContentsMargins(0, 0, 0, 0);
    cardLayout->setSpacing(0);

    auto* sidebar = new QFrame(card);
    sidebar->setObjectName("SettingsSidebar");
    sidebar->setFixedWidth(240);
    auto* sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(14, 14, 14, 14);
    sidebarLayout->setSpacing(10);

    auto* settingsTitle = new QLabel("Settings", sidebar);
    settingsTitle->setObjectName("SettingsTitle");

    m_categoryList = new QListWidget(sidebar);
    m_categoryList->setSpacing(6);

    sidebarLayout->addWidget(settingsTitle);
    sidebarLayout->addWidget(m_categoryList, 1);

    auto* content = new QWidget(card);
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(18, 18, 18, 18);
    contentLayout->setSpacing(12);

    m_pages = new QStackedWidget(content);
    addCategory("My Account", buildMyAccountPage());
    addCategory("Main Settings", buildMainSettingsPage());
    addCategory("Notifications", buildNotificationsPage());
    addCategory("Security", buildSecurityPage());
    addCategory("Appearance", buildAppearancePage());
    addCategory("Language", buildLanguagePage());
    addCategory("Folders", buildFoldersPage());

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, content);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);

    contentLayout->addWidget(m_pages, 1);
    contentLayout->addWidget(buttons);

    cardLayout->addWidget(sidebar);
    cardLayout->addWidget(content, 1);
    rootLayout->addWidget(card, 1);

    connect(m_categoryList, &QListWidget::currentRowChanged, m_pages, &QStackedWidget::setCurrentIndex);
    m_categoryList->setCurrentRow(0);
}

void SettingsDialog::accept()
{
    m_settings->setThemeId(m_themeCombo->currentData().toString());
    m_settings->setChatListWidth(m_chatListWidthSpin->value());
    m_settings->setNotificationsEnabled(m_notificationsCheck->isChecked());
    m_settings->setRequireTls(m_requireTlsCheck->isChecked());
    m_settings->setRememberTokens(m_rememberTokensCheck->isChecked());
    m_settings->setLanguage(m_languageCombo->currentText());
    m_settings->setChatBackgroundImagePath(m_backgroundImagePath);
    m_settings->setCustomWorkspaces(m_workspaces);
    QDialog::accept();
}

QWidget* SettingsDialog::buildMyAccountPage()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setSpacing(16);

    auto* title = new QLabel("My Account", page);
    title->setObjectName("SettingsTitle");

    auto* profileRow = new QHBoxLayout;
    profileRow->setContentsMargins(0, 0, 0, 0);
    profileRow->setSpacing(16);

    m_accountAvatarLabel = new QLabel(page);
    m_accountAvatarLabel->setFixedSize(84, 84);

    auto* labelColumn = new QVBoxLayout;
    labelColumn->setContentsMargins(0, 0, 0, 0);
    labelColumn->setSpacing(6);

    m_accountNameLabel = new QLabel(page);
    QFont nameFont = m_accountNameLabel->font();
    nameFont.setPointSize(18);
    nameFont.setBold(true);
    m_accountNameLabel->setFont(nameFont);

    m_accountJidLabel = new QLabel(page);
    m_accountJidLabel->setObjectName("MutedLabel");
    m_accountServerLabel = new QLabel(page);
    m_accountServerLabel->setObjectName("MutedLabel");

    if (m_session.has_value()) {
        m_accountAvatarLabel->setPixmap(makeAvatarPixmap(m_session->avatar, m_session->displayName, QColor("#9a7cff"), 84));
        m_accountNameLabel->setText(m_session->displayName);
        m_accountJidLabel->setText(m_session->jid);
        if (m_session->connectHost.trimmed().isEmpty()) {
            m_accountServerLabel->setText(QStringLiteral("Server: %1:%2").arg(m_session->server).arg(m_session->port));
        } else {
            m_accountServerLabel->setText(QStringLiteral("Server: %1:%2 via %3").arg(m_session->server).arg(m_session->port).arg(m_session->connectHost));
        }
    } else {
        m_accountAvatarLabel->setPixmap(makeAvatarPixmap({}, "?", QColor("#9a7cff"), 84));
        m_accountNameLabel->setText("No active account");
        m_accountJidLabel->setText("Sign in to see account information.");
        m_accountServerLabel->clear();
    }

    labelColumn->addWidget(m_accountNameLabel);
    labelColumn->addWidget(m_accountJidLabel);
    labelColumn->addWidget(m_accountServerLabel);
    labelColumn->addStretch(1);

    profileRow->addWidget(m_accountAvatarLabel);
    profileRow->addLayout(labelColumn, 1);

    auto* actionsLayout = new QHBoxLayout;
    actionsLayout->setContentsMargins(0, 0, 0, 0);
    actionsLayout->setSpacing(10);

    auto* logoutButton = new QPushButton("Log out", page);
    logoutButton->setObjectName("DangerButton");
    logoutButton->setEnabled(m_session.has_value());
    connect(logoutButton, &QPushButton::clicked, this, [this]() { emit logoutRequested(); });

    actionsLayout->addWidget(logoutButton, 0, Qt::AlignLeft);
    actionsLayout->addStretch(1);

    layout->addWidget(title);
    layout->addLayout(profileRow);
    layout->addLayout(actionsLayout);
    layout->addStretch(1);
    return wrapPageContent(page);
}

QWidget* SettingsDialog::buildMainSettingsPage()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setSpacing(16);

    auto* title = new QLabel("Main Settings", page);
    title->setObjectName("SettingsTitle");

    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setLabelAlignment(Qt::AlignLeft);

    m_chatListWidthSpin = new QSpinBox(page);
    m_chatListWidthSpin->setRange(260, 520);
    m_chatListWidthSpin->setValue(m_settings->ui().chatListWidth);

    form->addRow("Default chat list width", m_chatListWidthSpin);

    layout->addWidget(title);
    layout->addLayout(form);
    layout->addStretch(1);
    return wrapPageContent(page);
}

QWidget* SettingsDialog::buildNotificationsPage()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setSpacing(16);

    auto* title = new QLabel("Notifications", page);
    title->setObjectName("SettingsTitle");

    m_notificationsCheck = new QCheckBox("Enable desktop notifications", page);
    m_notificationsCheck->setChecked(m_settings->ui().notificationsEnabled);

    layout->addWidget(title);
    layout->addWidget(m_notificationsCheck);
    layout->addStretch(1);
    return wrapPageContent(page);
}

QWidget* SettingsDialog::buildSecurityPage()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setSpacing(16);

    auto* title = new QLabel("Security", page);
    title->setObjectName("SettingsTitle");

    m_requireTlsCheck = new QCheckBox("Require TLS when connecting to the server", page);
    m_requireTlsCheck->setChecked(m_settings->ui().requireTls);

    m_rememberTokensCheck = new QCheckBox("Remember authentication tokens when the server supports SASL2/FAST", page);
    m_rememberTokensCheck->setChecked(m_settings->ui().rememberTokens);

    layout->addWidget(title);
    layout->addWidget(m_requireTlsCheck);
    layout->addWidget(m_rememberTokensCheck);
    layout->addStretch(1);
    return wrapPageContent(page);
}

QWidget* SettingsDialog::buildAppearancePage()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setSpacing(16);

    auto* title = new QLabel("Appearance", page);
    title->setObjectName("SettingsTitle");

    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    m_themeCombo = new QComboBox(page);
    const QList<ThemePalette> themes = availableThemes();
    for (const ThemePalette& theme : themes) {
        m_themeCombo->addItem(theme.name, theme.id);
    }
    const int themeIndex = m_themeCombo->findData(m_settings->ui().themeId);
    if (themeIndex >= 0) {
        m_themeCombo->setCurrentIndex(themeIndex);
    }

    auto* imageButton = new QPushButton("Choose background image", page);
    auto* clearImageButton = new QPushButton("Clear background image", page);

    connect(imageButton, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(this,
                                                          "Choose background image",
                                                          {},
                                                          "Images (*.png *.jpg *.jpeg *.bmp *.webp)");
        if (!path.isEmpty()) {
            m_backgroundImagePath = path;
        }
    });
    connect(clearImageButton, &QPushButton::clicked, this, [this]() { m_backgroundImagePath.clear(); });

    form->addRow("Theme", m_themeCombo);
    form->addRow(imageButton);
    form->addRow(clearImageButton);

    layout->addWidget(title);
    layout->addLayout(form);
    layout->addStretch(1);
    return wrapPageContent(page);
}

QWidget* SettingsDialog::buildLanguagePage()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setSpacing(16);

    auto* title = new QLabel("Language", page);
    title->setObjectName("SettingsTitle");

    m_languageCombo = new QComboBox(page);
    m_languageCombo->addItems({"English", "Russian"});
    const int languageIndex = m_languageCombo->findText(m_settings->ui().language);
    if (languageIndex >= 0) {
        m_languageCombo->setCurrentIndex(languageIndex);
    }

    layout->addWidget(title);
    layout->addWidget(m_languageCombo);
    layout->addStretch(1);
    return wrapPageContent(page);
}

QWidget* SettingsDialog::buildFoldersPage()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setSpacing(16);

    auto* title = new QLabel("Folders", page);
    title->setObjectName("SettingsTitle");

    auto* body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(12);

    auto* folderColumn = new QVBoxLayout;
    folderColumn->setContentsMargins(0, 0, 0, 0);
    folderColumn->setSpacing(10);

    m_folderList = new QListWidget(page);

    auto* folderButtons = new QHBoxLayout;
    folderButtons->setContentsMargins(0, 0, 0, 0);
    folderButtons->setSpacing(8);

    auto* addButton = new QPushButton("Add", page);
    auto* renameButton = new QPushButton("Rename", page);
    auto* removeButton = new QPushButton("Remove", page);

    folderButtons->addWidget(addButton);
    folderButtons->addWidget(renameButton);
    folderButtons->addWidget(removeButton);

    folderColumn->addWidget(m_folderList, 1);
    folderColumn->addLayout(folderButtons);

    auto* chatsColumn = new QVBoxLayout;
    chatsColumn->setContentsMargins(0, 0, 0, 0);
    chatsColumn->setSpacing(10);

    auto* chatsLabel = new QLabel("Chats in selected folder", page);
    chatsLabel->setObjectName("AuthFieldLabel");
    m_folderChatsList = new QListWidget(page);

    chatsColumn->addWidget(chatsLabel);
    chatsColumn->addWidget(m_folderChatsList, 1);

    body->addLayout(folderColumn, 1);
    body->addLayout(chatsColumn, 1);

    layout->addWidget(title);
    layout->addLayout(body, 1);

    populateFolderList();

    connect(m_folderList, &QListWidget::currentRowChanged, this, [this](int row) {
        populateFolderChatList(row);
        updateFolderButtons();
    });
    connect(m_folderChatsList, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        if (m_updatingFolderChats) {
            return;
        }
        const int folderIndex = m_folderList->currentRow();
        if (folderIndex < 0 || folderIndex >= m_workspaces.size()) {
            return;
        }

        Workspace& workspace = m_workspaces[folderIndex];
        const QString chatId = item->data(Qt::UserRole).toString();
        if (item->checkState() == Qt::Checked) {
            if (!workspace.chatIds.contains(chatId)) {
                workspace.chatIds.append(chatId);
            }
        } else {
            workspace.chatIds.removeAll(chatId);
        }
    });

    connect(addButton, &QPushButton::clicked, this, [this]() {
        const QString name = QInputDialog::getText(this, "New folder", "Folder name");
        if (name.trimmed().isEmpty()) {
            return;
        }
        Workspace workspace;
        workspace.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        workspace.name = name.trimmed();
        m_workspaces.append(workspace);
        populateFolderList();
        m_folderList->setCurrentRow(static_cast<int>(m_workspaces.size()) - 1);
    });
    connect(renameButton, &QPushButton::clicked, this, [this]() {
        const int row = m_folderList->currentRow();
        if (row < 0 || row >= m_workspaces.size()) {
            return;
        }
        const QString name = QInputDialog::getText(this, "Rename folder", "Folder name", QLineEdit::Normal, m_workspaces[row].name);
        if (name.trimmed().isEmpty()) {
            return;
        }
        m_workspaces[row].name = name.trimmed();
        populateFolderList();
        m_folderList->setCurrentRow(row);
    });
    connect(removeButton, &QPushButton::clicked, this, [this]() {
        const int row = m_folderList->currentRow();
        if (row < 0 || row >= m_workspaces.size()) {
            return;
        }
        m_workspaces.removeAt(row);
        populateFolderList();
    });

    updateFolderButtons();
    return wrapPageContent(page);
}

void SettingsDialog::addCategory(const QString& title, QWidget* page)
{
    m_categoryList->addItem(title);
    m_pages->addWidget(page);
}

void SettingsDialog::populateFolderList()
{
    m_folderList->clear();
    for (const Workspace& workspace : m_workspaces) {
        auto* item = new QListWidgetItem(workspace.name, m_folderList);
        item->setData(Qt::UserRole, workspace.id);
    }
    if (!m_workspaces.isEmpty()) {
        m_folderList->setCurrentRow(qMin(m_folderList->currentRow(), static_cast<int>(m_workspaces.size()) - 1));
        if (m_folderList->currentRow() < 0) {
            m_folderList->setCurrentRow(0);
        }
    }
    populateFolderChatList(m_folderList->currentRow());
}

void SettingsDialog::populateFolderChatList(int workspaceIndex)
{
    m_updatingFolderChats = true;
    m_folderChatsList->clear();

    if (workspaceIndex >= 0 && workspaceIndex < m_workspaces.size()) {
        const Workspace& workspace = m_workspaces.at(workspaceIndex);
        for (const ChatSummary& chat : m_chats) {
            auto* item = new QListWidgetItem(chat.title, m_folderChatsList);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setData(Qt::UserRole, chat.id);
            item->setCheckState(workspace.chatIds.contains(chat.id) ? Qt::Checked : Qt::Unchecked);
        }
    }

    m_updatingFolderChats = false;
}

void SettingsDialog::updateFolderButtons()
{
    const bool hasSelection = m_folderList->currentRow() >= 0 && m_folderList->currentRow() < m_workspaces.size();
    m_folderChatsList->setEnabled(hasSelection);
}

}  // namespace CuteXmpp
