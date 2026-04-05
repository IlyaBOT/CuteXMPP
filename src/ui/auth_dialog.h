#pragma once

#include "src/models/types.h"

#include <QDialog>

class QLabel;
class QLineEdit;
class QPushButton;
class QFrame;
class QStackedWidget;
class QWidget;
class QComboBox;

namespace CuteXmpp {

class AuthDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit AuthDialog(QWidget* parent = nullptr);

    void setBusy(bool busy, const QString& status = {}, int busyPageIndex = -1);
    void showError(const QString& message);
    void clearStatus();
    void applyLoginRequest(const LoginRequest& request);

signals:
    void loginSubmitted(const LoginRequest& request);
    void registrationSubmitted(const RegistrationRequest& request);
    void authenticationCancelled();

private:
    QWidget* buildLoginPage();
    QWidget* buildRegisterPage();
    QWidget* buildAuthCard();
    quint16 parsePort(const QLineEdit* edit) const;
    void setAdvancedPanelVisible(QWidget* panel, bool visible, QPushButton* toggleButton);
    void updateWindowHeight();
    void updateBusyState();

    QStackedWidget* m_pages = nullptr;
    QLabel* m_statusLabel = nullptr;
    QFrame* m_frame = nullptr;
    QWidget* m_loginAdvancedPanel = nullptr;
    QWidget* m_registerAdvancedPanel = nullptr;
    QWidget* m_authCard = nullptr;
    QLineEdit* m_loginJidEdit = nullptr;
    QLineEdit* m_loginPasswordEdit = nullptr;
    QLineEdit* m_loginServerEdit = nullptr;
    QLineEdit* m_loginConnectHostEdit = nullptr;
    QLineEdit* m_loginPortEdit = nullptr;
    QComboBox* m_loginProxyCombo = nullptr;
    QComboBox* m_loginTlsCombo = nullptr;
    QLineEdit* m_registerUsernameEdit = nullptr;
    QLineEdit* m_registerServerEdit = nullptr;
    QLineEdit* m_registerPasswordEdit = nullptr;
    QLineEdit* m_registerConnectHostEdit = nullptr;
    QLineEdit* m_registerPortEdit = nullptr;
    QComboBox* m_registerProxyCombo = nullptr;
    QComboBox* m_registerTlsCombo = nullptr;
    QPushButton* m_loginPrimaryButton = nullptr;
    QPushButton* m_loginSecondaryButton = nullptr;
    QPushButton* m_registerPrimaryButton = nullptr;
    QPushButton* m_registerSecondaryButton = nullptr;
    QVector<QWidget*> m_loginBusyWidgets;
    QVector<QWidget*> m_registerBusyWidgets;
    bool m_busy = false;
    int m_busyPageIndex = -1;
};

}  // namespace CuteXmpp
