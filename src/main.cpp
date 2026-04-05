#include "src/settings/app_settings.h"
#include "src/logging/app_logger.h"
#include "src/ui/auth_dialog.h"
#include "src/ui/main_window.h"
#include "src/ui/theme.h"
#include "src/xmpp/xmpp_service.h"

#include <QApplication>
#include <QCoreApplication>
#include <QIcon>
#include <QNetworkProxyFactory>
#include <QSet>
#include <QThread>
#include <QTimer>

#include <functional>
#include <optional>

namespace {

using CuteXmpp::LoginRequest;
using CuteXmpp::ProxyMode;
using CuteXmpp::TlsMode;

QString configuredHostText(const QString& host)
{
    return host.trimmed().isEmpty() ? QStringLiteral("<auto>") : host.trimmed();
}

QString proxyModeText(ProxyMode mode)
{
    switch (mode) {
    case ProxyMode::NoProxy:
        return QStringLiteral("no-proxy");
    case ProxyMode::Tor:
        return QStringLiteral("tor");
    case ProxyMode::TorBrowser:
        return QStringLiteral("tor-browser");
    case ProxyMode::System:
    default:
        return QStringLiteral("system");
    }
}

QString tlsModeText(TlsMode mode)
{
    switch (mode) {
    case TlsMode::DirectTls:
        return QStringLiteral("direct-tls");
    case TlsMode::Plain:
        return QStringLiteral("plain");
    case TlsMode::StartTls:
    default:
        return QStringLiteral("starttls");
    }
}

std::optional<TlsMode> parseTlsMode(const QString& text)
{
    const QString normalized = text.trimmed().toLower();
    if (normalized == QStringLiteral("starttls") || normalized == QStringLiteral("start-tls")) {
        return TlsMode::StartTls;
    }
    if (normalized == QStringLiteral("directtls") || normalized == QStringLiteral("direct-tls")) {
        return TlsMode::DirectTls;
    }
    if (normalized == QStringLiteral("plain")) {
        return TlsMode::Plain;
    }
    return std::nullopt;
}

std::optional<ProxyMode> parseProxyMode(const QString& text)
{
    const QString normalized = text.trimmed().toLower();
    if (normalized == QStringLiteral("system")) {
        return ProxyMode::System;
    }
    if (normalized == QStringLiteral("no-proxy") || normalized == QStringLiteral("none")) {
        return ProxyMode::NoProxy;
    }
    if (normalized == QStringLiteral("tor")) {
        return ProxyMode::Tor;
    }
    if (normalized == QStringLiteral("tor-browser")) {
        return ProxyMode::TorBrowser;
    }
    return std::nullopt;
}

QVector<LoginRequest> buildProbeLoginRequests(const QString& username, const QString& domain)
{
    QVector<LoginRequest> requests;
    QSet<QString> seen;

    const QString baseDomain = domain.startsWith(QStringLiteral("xmpp."), Qt::CaseInsensitive)
        ? domain.mid(5)
        : QString();

    const auto addRequest = [&](const QString& jidDomain,
                                const QString& server,
                                const QString& connectHost,
                                quint16 port,
                                TlsMode tlsMode,
                                ProxyMode proxyMode) {
        if (jidDomain.trimmed().isEmpty() || server.trimmed().isEmpty()) {
            return;
        }

        const QString key = QStringLiteral("%1|%2|%3|%4|%5|%6")
                                .arg(jidDomain.trimmed(), server.trimmed(), connectHost.trimmed())
                                .arg(port)
                                .arg(static_cast<int>(tlsMode))
                                .arg(static_cast<int>(proxyMode));
        if (seen.contains(key)) {
            return;
        }
        seen.insert(key);

        LoginRequest request;
        request.jid = QStringLiteral("%1@%2").arg(username, jidDomain.trimmed());
        request.server = server.trimmed();
        request.connectHost = connectHost.trimmed();
        request.port = port;
        request.tlsMode = tlsMode;
        request.proxyMode = proxyMode;
        requests.append(request);
    };

    for (ProxyMode proxyMode : {ProxyMode::System, ProxyMode::NoProxy}) {
        addRequest(domain, domain, QString(), 5222, TlsMode::StartTls, proxyMode);
        addRequest(domain, domain, domain, 5222, TlsMode::StartTls, proxyMode);
        addRequest(domain, domain, domain, 5222, TlsMode::DirectTls, proxyMode);
        addRequest(domain, domain, domain, 5222, TlsMode::Plain, proxyMode);

        if (!baseDomain.isEmpty()) {
            addRequest(baseDomain, baseDomain, domain, 5222, TlsMode::StartTls, proxyMode);
            addRequest(baseDomain, baseDomain, domain, 5222, TlsMode::Plain, proxyMode);
            addRequest(baseDomain, baseDomain, domain, 5222, TlsMode::DirectTls, proxyMode);
        }
    }

    return requests;
}

int runProbeLogin(QApplication& app)
{
    const QStringList args = QCoreApplication::arguments();
    const int probeIndex = args.indexOf(QStringLiteral("--probe-login"));
    if (probeIndex < 0) {
        return -1;
    }
    if (probeIndex + 3 >= args.size()) {
        CuteXmpp::logError(QStringLiteral("Usage: %1 --probe-login <username> <password> <domain> [host] [port] [tls] [proxy]")
                     .arg(args.constFirst()));
        return 2;
    }

    const QString username = args.at(probeIndex + 1).trimmed();
    const QString password = args.at(probeIndex + 2);
    const QString domain = args.at(probeIndex + 3).trimmed();
    if (username.isEmpty() || password.isEmpty() || domain.isEmpty()) {
        CuteXmpp::logError("Probe login requires non-empty username, password and domain.");
        return 2;
    }

    QVector<LoginRequest> attempts;
    if (probeIndex + 4 < args.size()) {
        LoginRequest request;
        request.jid = QStringLiteral("%1@%2").arg(username, domain);
        request.server = domain;
        request.connectHost = args.at(probeIndex + 4).trimmed();
        request.port = probeIndex + 5 < args.size()
            ? static_cast<quint16>(args.at(probeIndex + 5).toUShort())
            : 5222;
        request.tlsMode = probeIndex + 6 < args.size()
            ? parseTlsMode(args.at(probeIndex + 6)).value_or(TlsMode::StartTls)
            : TlsMode::StartTls;
        request.proxyMode = probeIndex + 7 < args.size()
            ? parseProxyMode(args.at(probeIndex + 7)).value_or(ProxyMode::System)
            : ProxyMode::System;
        attempts.append(request);
    } else {
        attempts = buildProbeLoginRequests(username, domain);
    }

    CuteXmpp::AppSettings settings;
    CuteXmpp::XmppService service(&settings);
    if (attempts.isEmpty()) {
        CuteXmpp::logError("Probe login did not generate any connection attempts.");
        return 2;
    }

    QTimer attemptTimeout;
    attemptTimeout.setSingleShot(true);

    bool finished = false;
    int currentAttempt = -1;
    std::function<void()> startNextAttempt;

    QObject::connect(&service, &CuteXmpp::XmppService::authenticationSucceeded, &app, [&](const CuteXmpp::AccountSession& session) {
        finished = true;
        attemptTimeout.stop();
        CuteXmpp::logInfo(QStringLiteral("Probe connected successfully: jid=%1, server=%2, host=%3, port=%4")
                    .arg(session.jid, session.server, configuredHostText(session.connectHost))
                    .arg(session.port));
        service.disconnectFromServer();
        app.exit(0);
    });

    QObject::connect(&service, &CuteXmpp::XmppService::authenticationFailed, &app, [&](const QString& message) {
        if (finished) {
            return;
        }
        attemptTimeout.stop();
        CuteXmpp::logError(QStringLiteral("Probe attempt failed: %1").arg(message));
        QTimer::singleShot(250, &app, startNextAttempt);
    });

    QObject::connect(&attemptTimeout, &QTimer::timeout, &app, [&]() {
        if (finished) {
            return;
        }
        CuteXmpp::logError(QStringLiteral("Probe attempt timed out after %1 seconds.").arg(attemptTimeout.interval() / 1000));
        service.disconnectFromServer();
        QTimer::singleShot(250, &app, startNextAttempt);
    });

    startNextAttempt = [&]() {
        if (finished) {
            return;
        }

        ++currentAttempt;
        if (currentAttempt >= attempts.size()) {
            finished = true;
            CuteXmpp::logError("Probe exhausted all connection attempts without success.");
            app.exit(1);
            return;
        }

        const LoginRequest& attempt = attempts.at(currentAttempt);
        LoginRequest request = attempt;
        request.password = password;

        CuteXmpp::logInfo(QStringLiteral("Probe attempt %1/%2: jid=%3, server=%4, host=%5, port=%6, tls=%7, proxy=%8")
                    .arg(currentAttempt + 1)
                    .arg(attempts.size())
                    .arg(request.jid, request.server, configuredHostText(request.connectHost))
                    .arg(request.port)
                    .arg(tlsModeText(request.tlsMode), proxyModeText(request.proxyMode)));

        attemptTimeout.start(15'000);
        service.login(request);
    };

    QTimer::singleShot(0, &app, startNextAttempt);
    return app.exec();
}

}  // namespace

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
    qRegisterMetaType<CuteXmpp::LoginRequest>();
    qRegisterMetaType<CuteXmpp::RegistrationRequest>();

    if (QCoreApplication::arguments().contains(QStringLiteral("--probe-login"))) {
        return runProbeLogin(app);
    }

    CuteXmpp::AppSettings settings;
    app.setStyleSheet(CuteXmpp::buildApplicationStyleSheet(CuteXmpp::themeById(settings.ui().themeId)));

    QThread xmppThread;
    xmppThread.setObjectName("XmppServiceThread");

    auto* service = new CuteXmpp::XmppService(&settings);
    service->moveToThread(&xmppThread);
    xmppThread.start();

    CuteXmpp::AuthDialog authDialog;
    CuteXmpp::MainWindow mainWindow(&settings, service);
    authDialog.setWindowIcon(appIcon);
    mainWindow.setWindowIcon(appIcon);

    if (const auto lastLogin = settings.lastLoginRequest(); lastLogin.has_value()) {
        authDialog.applyLoginRequest(*lastLogin);
    }

    QObject::connect(&authDialog, &CuteXmpp::AuthDialog::loginSubmitted, &authDialog, [&authDialog, service](const CuteXmpp::LoginRequest& request) {
        authDialog.setBusy(true, "Signing in...", 0);
        service->login(request);
    });
    QObject::connect(&authDialog, &CuteXmpp::AuthDialog::registrationSubmitted, &authDialog, [&authDialog, service](const CuteXmpp::RegistrationRequest& request) {
        authDialog.setBusy(true, "Creating account...", 1);
        service->registerAccount(request);
    });
    QObject::connect(&authDialog, &CuteXmpp::AuthDialog::authenticationCancelled, &authDialog, [&authDialog, service]() {
        authDialog.setBusy(false);
        authDialog.clearStatus();
        service->disconnectFromServer();
    });

    QObject::connect(service, &CuteXmpp::XmppService::authenticationSucceeded, &mainWindow, [&authDialog, &mainWindow](const CuteXmpp::AccountSession& session) {
        authDialog.setBusy(false);
        authDialog.clearStatus();
        mainWindow.initializeSession(session);
        authDialog.hide();
        mainWindow.show();
        mainWindow.raise();
        mainWindow.activateWindow();
    });
    QObject::connect(service, &CuteXmpp::XmppService::authenticationFailed, &authDialog, [&authDialog](const QString& message) {
        authDialog.showError(message);
        authDialog.raise();
        authDialog.activateWindow();
    });
    QObject::connect(&mainWindow, &CuteXmpp::MainWindow::logoutRequested, &mainWindow, [&authDialog, &mainWindow, service]() {
        service->disconnectFromServer();
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

    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [&xmppThread, service]() {
        if (!service || !xmppThread.isRunning()) {
            return;
        }

        QMetaObject::invokeMethod(service, [service]() {
            service->disconnectFromServer();
            service->moveToThread(QCoreApplication::instance()->thread());
        }, Qt::BlockingQueuedConnection);

        xmppThread.quit();
        xmppThread.wait();
        delete service;
    });

    authDialog.show();
    authDialog.raise();
    authDialog.activateWindow();

    if (const auto lastLogin = settings.lastLoginRequest(); lastLogin.has_value()) {
        QTimer::singleShot(0, &authDialog, [&authDialog, service, lastLogin]() {
            authDialog.setBusy(true, "Restoring previous session...", 0);
            service->login(*lastLogin);
        });
    }

    return app.exec();
}
