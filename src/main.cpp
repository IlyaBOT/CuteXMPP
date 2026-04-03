#include "src/settings/app_settings.h"
#include "src/ui/auth_dialog.h"
#include "src/ui/main_window.h"
#include "src/ui/theme.h"
#include "src/xmpp/xmpp_service.h"

#include <QApplication>
#include <QIcon>
#include <QNetworkProxyFactory>
#include <QTimer>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setOrganizationName("CuteXMPP");
    app.setApplicationName("CuteXMPP");
    QNetworkProxyFactory::setUseSystemConfiguration(true);
    const QIcon appIcon(":/icons/cutexmpp-logo.png");
    app.setWindowIcon(appIcon);

    qRegisterMetaType<CuteXmpp::AccountSession>();
    qRegisterMetaType<CuteXmpp::ChatSummary>();
    qRegisterMetaType<CuteXmpp::MessageEntry>();

    CuteXmpp::AppSettings settings;
    app.setStyleSheet(CuteXmpp::buildApplicationStyleSheet(CuteXmpp::themeById(settings.ui().themeId)));

    CuteXmpp::XmppService service(&settings);
    CuteXmpp::AuthDialog authDialog;
    CuteXmpp::MainWindow mainWindow(&settings, &service);
    authDialog.setWindowIcon(appIcon);
    mainWindow.setWindowIcon(appIcon);

    if (const auto lastLogin = settings.lastLoginRequest(); lastLogin.has_value()) {
        authDialog.applyLoginRequest(*lastLogin);
    }

    QObject::connect(&authDialog, &CuteXmpp::AuthDialog::loginSubmitted, &service, [&authDialog, &service](const CuteXmpp::LoginRequest& request) {
        authDialog.setBusy(true, "Signing in...");
        service.login(request);
    });
    QObject::connect(&authDialog, &CuteXmpp::AuthDialog::registrationSubmitted, &service, [&authDialog, &service](const CuteXmpp::RegistrationRequest& request) {
        authDialog.setBusy(true, "Creating account...");
        service.registerAccount(request);
    });

    QObject::connect(&service, &CuteXmpp::XmppService::authenticationSucceeded, &mainWindow, [&authDialog, &mainWindow](const CuteXmpp::AccountSession& session) {
        authDialog.setBusy(false);
        authDialog.clearStatus();
        mainWindow.initializeSession(session);
        authDialog.hide();
        mainWindow.show();
        mainWindow.raise();
        mainWindow.activateWindow();
    });
    QObject::connect(&service, &CuteXmpp::XmppService::authenticationFailed, &authDialog, [&authDialog](const QString& message) {
        authDialog.showError(message);
        authDialog.raise();
        authDialog.activateWindow();
    });
    QObject::connect(&mainWindow, &CuteXmpp::MainWindow::logoutRequested, &service, [&authDialog, &mainWindow, &service]() {
        service.disconnectFromServer();
        mainWindow.hide();
        authDialog.setBusy(false);
        authDialog.clearStatus();
        authDialog.show();
        authDialog.raise();
        authDialog.activateWindow();
    });

    QObject::connect(&mainWindow, &CuteXmpp::MainWindow::logoutRequested, &authDialog, [&settings]() {
        settings.clearLastLoginRequest();
    });

    authDialog.show();
    authDialog.raise();
    authDialog.activateWindow();

    if (const auto lastLogin = settings.lastLoginRequest(); lastLogin.has_value()) {
        QTimer::singleShot(0, &authDialog, [&authDialog, &service, lastLogin]() {
            authDialog.setBusy(true, "Restoring previous session...");
            service.login(*lastLogin);
        });
    }

    return app.exec();
}
