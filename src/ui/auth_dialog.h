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

    void setBusy(bool busy, const QString& status = {});
    void showError(const QString& message);
    void clearStatus();
    void applyLoginRequest(const LoginRequest& request);

signals:
    void loginSubmitted(const LoginRequest& request);
    void registrationSubmitted(const RegistrationRequest& request);

private:
    QWidget* buildLoginPage();
    QWidget* buildRegisterPage();
    QWidget* buildAuthCard();
    quint16 parsePort(const QLineEdit* edit) const;
    void setAdvancedPanelVisible(QWidget* panel, bool visible, QPushButton* toggleButton);
    void updateWindowHeight();

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
};

}  // namespace CuteXmpp
