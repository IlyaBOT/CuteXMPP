#include "src/ui/auth_dialog.h"

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace CuteXmpp {

namespace {

constexpr int kAuthWindowWidth = 360;
constexpr int kAuthWindowBaseHeight = 380;
constexpr int kAuthAdvancedExtraPadding = 12;
constexpr int kAuthStatusExtraSpacing = 4;

QString userFromFullJid(const QString& jid)
{
    return jid.section('/', 0, 0).section('@', 0, 0).trimmed();
}

QLabel* createFieldLabel(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setObjectName("AuthFieldLabel");
    return label;
}

QLineEdit* createField(const QString& placeholder, QWidget* parent)
{
    auto* edit = new QLineEdit(parent);
    edit->setPlaceholderText(placeholder);
    edit->setFixedHeight(36);
    edit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return edit;
}

QComboBox* createComboBox(QWidget* parent)
{
    auto* combo = new QComboBox(parent);
    combo->setFixedHeight(36);
    combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return combo;
}

void populateProxyCombo(QComboBox* combo)
{
    combo->addItem("System", static_cast<int>(ProxyMode::System));
    combo->addItem("No Proxy", static_cast<int>(ProxyMode::NoProxy));
    combo->addItem("Tor", static_cast<int>(ProxyMode::Tor));
    combo->addItem("Tor (Browser)", static_cast<int>(ProxyMode::TorBrowser));
}

void populateTlsCombo(QComboBox* combo)
{
    combo->addItem("START TLS", static_cast<int>(TlsMode::StartTls));
    combo->addItem("DIRECT TLS", static_cast<int>(TlsMode::DirectTls));
    combo->addItem("PLAIN", static_cast<int>(TlsMode::Plain));
}

QWidget* createCenteredColumn(QWidget* parent, int maximumWidth, QVBoxLayout** columnLayout)
{
    auto* row = new QWidget(parent);
    row->setObjectName("AuthContent");
    row->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(0);

    auto* column = new QWidget(row);
    column->setObjectName("AuthFormColumn");
    column->setFixedWidth(maximumWidth);
    column->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Maximum);

    auto* layout = new QVBoxLayout(column);
    layout->setContentsMargins(0, 4, 0, 4);
    layout->setSpacing(4);

    rowLayout->addStretch(1);
    rowLayout->addWidget(column, 0, Qt::AlignTop);
    rowLayout->addStretch(1);

    *columnLayout = layout;
    return row;
}

QPushButton* createSmallToggleButton(const QString& text, QWidget* parent)
{
    auto* button = new QPushButton(text, parent);
    button->setObjectName("AuthToggleButton");
    button->setCheckable(true);
    button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    button->setFixedHeight(36);
    return button;
}

}  // namespace

AuthDialog::AuthDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("CuteXMPP");
    setFixedSize(kAuthWindowWidth, kAuthWindowBaseHeight);
    setModal(false);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(8, 8, 8, 8);
    rootLayout->setSpacing(0);

    rootLayout->addWidget(buildAuthCard());
    connect(m_pages, &QStackedWidget::currentChanged, this, [this](int) { updateWindowHeight(); });
    QTimer::singleShot(0, this, &AuthDialog::updateWindowHeight);
}

void AuthDialog::setBusy(bool busy, const QString& status)
{
    if (m_authCard) {
        m_authCard->setEnabled(!busy);
    }
    if (!status.isEmpty()) {
        m_statusLabel->setText(status);
        m_statusLabel->setVisible(true);
        updateWindowHeight();
    }
}

void AuthDialog::showError(const QString& message)
{
    setBusy(false);
    m_statusLabel->setText(message);
    m_statusLabel->setStyleSheet("QLabel { color: #ff8fa8; }");
    m_statusLabel->setVisible(true);
    updateWindowHeight();
}

void AuthDialog::clearStatus()
{
    m_statusLabel->clear();
    m_statusLabel->setStyleSheet({});
    m_statusLabel->setVisible(false);
    updateWindowHeight();
}

void AuthDialog::applyLoginRequest(const LoginRequest& request)
{
    m_loginJidEdit->setText(userFromFullJid(request.jid));
    m_loginPasswordEdit->setText(request.password);
    m_loginServerEdit->setText(request.server);
    m_loginPortEdit->setText(QString::number(request.port));

    auto setComboValue = [](QComboBox* combo, int value) {
        const int index = combo->findData(value);
        if (index >= 0) {
            combo->setCurrentIndex(index);
        }
    };

    setComboValue(m_loginProxyCombo, static_cast<int>(request.proxyMode));
    setComboValue(m_loginTlsCombo, static_cast<int>(request.tlsMode));
}

QWidget* AuthDialog::buildAuthCard()
{
    m_authCard = new QWidget(this);
    auto* cardLayout = new QVBoxLayout(m_authCard);
    cardLayout->setContentsMargins(0, 0, 0, 0);

    m_frame = new QFrame(m_authCard);
    m_frame->setObjectName("AuthCard");

    auto* frameLayout = new QVBoxLayout(m_frame);
    frameLayout->setContentsMargins(8, 8, 8, 8);
    frameLayout->setSpacing(4);

    m_pages = new QStackedWidget(m_frame);
    m_pages->setObjectName("AuthPages");
    m_pages->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_pages->addWidget(buildLoginPage());
    m_pages->addWidget(buildRegisterPage());

    m_statusLabel = new QLabel(m_frame);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setObjectName("MutedLabel");
    m_statusLabel->setVisible(false);

    frameLayout->addWidget(m_pages);
    frameLayout->addWidget(m_statusLabel);

    cardLayout->addWidget(m_frame);
    return m_authCard;
}

QWidget* AuthDialog::buildLoginPage()
{
    auto* page = new QWidget;
    page->setObjectName("AuthPage");
    page->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    QVBoxLayout* formLayout = nullptr;
    QWidget* formColumn = createCenteredColumn(page, 328, &formLayout);

    auto* heroTitle = new QLabel("Welcome to CuteXMPP!", page);
    heroTitle->setObjectName("AuthHeroTitle");
    heroTitle->setAlignment(Qt::AlignCenter);
    heroTitle->setWordWrap(false);
    heroTitle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto* heroSubtitle = new QLabel("Sign in to your account or create a new one.", page);
    heroSubtitle->setObjectName("AuthHeroSubtitle");
    heroSubtitle->setAlignment(Qt::AlignCenter);
    heroSubtitle->setWordWrap(false);
    heroSubtitle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto* usernameLabel = createFieldLabel("Username", page);
    m_loginJidEdit = createField("ilya", page);

    auto* serverLabel = createFieldLabel("Server", page);
    m_loginServerEdit = createField("xmpp.domain.com", page);
    m_loginServerEdit->setText("xmpp.ilyabot.space");

    auto* passwordLabel = createFieldLabel("Password", page);
    m_loginPasswordEdit = createField("Password", page);
    m_loginPasswordEdit->setEchoMode(QLineEdit::Password);

    auto* advancedToggle = createSmallToggleButton("Advanced settings", page);

    m_loginAdvancedPanel = new QWidget(page);
    m_loginAdvancedPanel->setObjectName("AuthContent");
    auto* advancedLayout = new QVBoxLayout(m_loginAdvancedPanel);
    advancedLayout->setContentsMargins(0, 0, 0, 0);
    advancedLayout->setSpacing(4);

    auto* portLabel = createFieldLabel("Port", m_loginAdvancedPanel);
    m_loginPortEdit = createField("5222", m_loginAdvancedPanel);
    m_loginPortEdit->setText("5222");
    m_loginPortEdit->setValidator(new QIntValidator(1, 65535, m_loginPortEdit));

    auto* proxyLabel = createFieldLabel("Proxy", m_loginAdvancedPanel);
    m_loginProxyCombo = createComboBox(m_loginAdvancedPanel);
    populateProxyCombo(m_loginProxyCombo);

    auto* tlsLabel = createFieldLabel("TLS", m_loginAdvancedPanel);
    m_loginTlsCombo = createComboBox(m_loginAdvancedPanel);
    populateTlsCombo(m_loginTlsCombo);

    advancedLayout->addWidget(portLabel);
    advancedLayout->addWidget(m_loginPortEdit);
    advancedLayout->addWidget(proxyLabel);
    advancedLayout->addWidget(m_loginProxyCombo);
    advancedLayout->addWidget(tlsLabel);
    advancedLayout->addWidget(m_loginTlsCombo);
    m_loginAdvancedPanel->setVisible(false);

    auto* signInButton = new QPushButton("Sign in", page);
    signInButton->setObjectName("PrimaryButton");
    signInButton->setFixedHeight(36);
    signInButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto* registerButton = new QPushButton("Register", page);
    registerButton->setFixedHeight(36);
    registerButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto* actionRow = new QHBoxLayout;
    actionRow->setContentsMargins(0, 0, 0, 0);
    actionRow->setSpacing(8);
    actionRow->addWidget(signInButton, 1);
    actionRow->addWidget(registerButton, 1);

    formLayout->addWidget(heroTitle);
    formLayout->addWidget(heroSubtitle);
    formLayout->addSpacing(4);
    formLayout->addWidget(usernameLabel);
    formLayout->addWidget(m_loginJidEdit);
    formLayout->addWidget(serverLabel);
    formLayout->addWidget(m_loginServerEdit);
    formLayout->addWidget(passwordLabel);
    formLayout->addWidget(m_loginPasswordEdit);
    formLayout->addSpacing(4);
    formLayout->addWidget(advancedToggle, 0, Qt::AlignHCenter);
    formLayout->addWidget(m_loginAdvancedPanel);
    formLayout->addSpacing(4);
    formLayout->addLayout(actionRow);

    layout->addSpacing(4);
    layout->addWidget(formColumn, 0, Qt::AlignTop);
    layout->addSpacing(4);

    connect(advancedToggle, &QPushButton::toggled, this, [this, advancedToggle](bool visible) {
        setAdvancedPanelVisible(m_loginAdvancedPanel, visible, advancedToggle);
    });
    connect(registerButton, &QPushButton::clicked, this, [this]() { m_pages->setCurrentIndex(1); });
    connect(signInButton, &QPushButton::clicked, this, [this]() {
        clearStatus();
        const QString username = m_loginJidEdit->text().trimmed();
        const QString server = m_loginServerEdit->text().trimmed();

        LoginRequest request;
        request.jid = QStringLiteral("%1@%2").arg(username, server);
        request.password = m_loginPasswordEdit->text();
        request.server = server;
        request.port = parsePort(m_loginPortEdit);
        request.proxyMode = static_cast<ProxyMode>(m_loginProxyCombo->currentData().toInt());
        request.tlsMode = static_cast<TlsMode>(m_loginTlsCombo->currentData().toInt());
        emit loginSubmitted(request);
    });

    return page;
}

QWidget* AuthDialog::buildRegisterPage()
{
    auto* page = new QWidget;
    page->setObjectName("AuthPage");
    page->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    QVBoxLayout* formLayout = nullptr;
    QWidget* formColumn = createCenteredColumn(page, 328, &formLayout);

    auto* heroTitle = new QLabel("Create Account", page);
    heroTitle->setObjectName("AuthHeroTitle");
    heroTitle->setAlignment(Qt::AlignCenter);
    heroTitle->setWordWrap(false);
    heroTitle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto* heroSubtitle = new QLabel("Choose a username, server and password.", page);
    heroSubtitle->setObjectName("AuthHeroSubtitle");
    heroSubtitle->setAlignment(Qt::AlignCenter);
    heroSubtitle->setWordWrap(false);
    heroSubtitle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto* usernameLabel = createFieldLabel("Username", page);
    m_registerUsernameEdit = createField("ilya", page);

    auto* serverLabel = createFieldLabel("Server", page);
    m_registerServerEdit = createField("xmpp.domain.com", page);
    m_registerServerEdit->setText("xmpp.ilyabot.space");

    auto* passwordLabel = createFieldLabel("Password", page);
    m_registerPasswordEdit = createField("Password", page);
    m_registerPasswordEdit->setEchoMode(QLineEdit::Password);

    auto* advancedToggle = createSmallToggleButton("Advanced settings", page);

    m_registerAdvancedPanel = new QWidget(page);
    m_registerAdvancedPanel->setObjectName("AuthContent");
    auto* advancedLayout = new QVBoxLayout(m_registerAdvancedPanel);
    advancedLayout->setContentsMargins(0, 0, 0, 0);
    advancedLayout->setSpacing(4);

    auto* portLabel = createFieldLabel("Port", m_registerAdvancedPanel);
    m_registerPortEdit = createField("5222", m_registerAdvancedPanel);
    m_registerPortEdit->setText("5222");
    m_registerPortEdit->setValidator(new QIntValidator(1, 65535, m_registerPortEdit));

    auto* proxyLabel = createFieldLabel("Proxy", m_registerAdvancedPanel);
    m_registerProxyCombo = createComboBox(m_registerAdvancedPanel);
    populateProxyCombo(m_registerProxyCombo);

    auto* tlsLabel = createFieldLabel("TLS", m_registerAdvancedPanel);
    m_registerTlsCombo = createComboBox(m_registerAdvancedPanel);
    populateTlsCombo(m_registerTlsCombo);

    advancedLayout->addWidget(portLabel);
    advancedLayout->addWidget(m_registerPortEdit);
    advancedLayout->addWidget(proxyLabel);
    advancedLayout->addWidget(m_registerProxyCombo);
    advancedLayout->addWidget(tlsLabel);
    advancedLayout->addWidget(m_registerTlsCombo);
    m_registerAdvancedPanel->setVisible(false);

    auto* buttonRow = new QHBoxLayout;
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->setSpacing(8);

    auto* backButton = new QPushButton("Back", page);
    backButton->setFixedHeight(36);
    backButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* registerButton = new QPushButton("Register", page);
    registerButton->setObjectName("PrimaryButton");
    registerButton->setFixedHeight(36);
    registerButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    buttonRow->addWidget(backButton, 1);
    buttonRow->addWidget(registerButton, 1);

    formLayout->addWidget(heroTitle);
    formLayout->addWidget(heroSubtitle);
    formLayout->addSpacing(4);
    formLayout->addWidget(usernameLabel);
    formLayout->addWidget(m_registerUsernameEdit);
    formLayout->addWidget(serverLabel);
    formLayout->addWidget(m_registerServerEdit);
    formLayout->addWidget(passwordLabel);
    formLayout->addWidget(m_registerPasswordEdit);
    formLayout->addSpacing(4);
    formLayout->addWidget(advancedToggle, 0, Qt::AlignHCenter);
    formLayout->addWidget(m_registerAdvancedPanel);
    formLayout->addSpacing(4);
    formLayout->addLayout(buttonRow);

    layout->addSpacing(4);
    layout->addWidget(formColumn, 0, Qt::AlignTop);
    layout->addSpacing(4);

    connect(advancedToggle, &QPushButton::toggled, this, [this, advancedToggle](bool visible) {
        setAdvancedPanelVisible(m_registerAdvancedPanel, visible, advancedToggle);
    });
    connect(backButton, &QPushButton::clicked, this, [this]() { m_pages->setCurrentIndex(0); });
    connect(registerButton, &QPushButton::clicked, this, [this]() {
        clearStatus();
        RegistrationRequest request;
        request.username = m_registerUsernameEdit->text().trimmed();
        request.server = m_registerServerEdit->text().trimmed();
        request.password = m_registerPasswordEdit->text();
        request.port = parsePort(m_registerPortEdit);
        request.proxyMode = static_cast<ProxyMode>(m_registerProxyCombo->currentData().toInt());
        request.tlsMode = static_cast<TlsMode>(m_registerTlsCombo->currentData().toInt());
        emit registrationSubmitted(request);
    });

    return page;
}

quint16 AuthDialog::parsePort(const QLineEdit* edit) const
{
    bool ok = false;
    const int port = edit->text().trimmed().toInt(&ok);
    return ok ? static_cast<quint16>(port) : 5222;
}

void AuthDialog::setAdvancedPanelVisible(QWidget* panel, bool visible, QPushButton* toggleButton)
{
    panel->setVisible(visible);
    toggleButton->setText(visible ? "Hide advanced settings" : "Advanced settings");
    QTimer::singleShot(0, this, &AuthDialog::updateWindowHeight);
}

void AuthDialog::updateWindowHeight()
{
    if (!m_pages) {
        return;
    }

    if (QWidget* currentPage = m_pages->currentWidget()) {
        if (QLayout* pageLayout = currentPage->layout()) {
            pageLayout->invalidate();
            pageLayout->activate();
        }
        m_pages->setFixedHeight(currentPage->sizeHint().height());
    }

    QWidget* advancedPanel = m_pages->currentIndex() == 0 ? m_loginAdvancedPanel : m_registerAdvancedPanel;
    int dialogHeight = kAuthWindowBaseHeight;

    if (advancedPanel && advancedPanel->isVisible()) {
        dialogHeight += advancedPanel->sizeHint().height() + kAuthAdvancedExtraPadding;
    }

    if (m_statusLabel && m_statusLabel->isVisible()) {
        dialogHeight += m_statusLabel->sizeHint().height() + kAuthStatusExtraSpacing;
    }

    setFixedHeight(dialogHeight);
}

}  // namespace CuteXmpp
