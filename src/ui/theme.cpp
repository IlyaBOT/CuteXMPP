#include "src/ui/theme.h"

#include <QDate>

namespace CuteXmpp {

QList<ThemePalette> availableThemes()
{
    return {
        {"nocturne", "Nocturne", QColor("#1f1a28"), QColor("#251f31"), QColor("#2e263b"), QColor("#2b2435"), QColor("#f5f3f9"), QColor("#3b3147"), QColor("#9a7cff"), QColor("#b59dff"), QColor("#f8f5ff"), QColor("#b6aacd"), QColor("#4a405a"), QColor("#31293d"), QColor("#564875"), QColor("#4a3d61")},
        {"slate", "Slate", QColor("#182028"), QColor("#1f2932"), QColor("#293540"), QColor("#23313a"), QColor("#f1f6f8"), QColor("#30414d"), QColor("#5bc8a7"), QColor("#7dd8bd"), QColor("#f4f8fb"), QColor("#a4bac7"), QColor("#40515d"), QColor("#294047"), QColor("#356070"), QColor("#3b5661")},
        {"daybreak", "Daybreak", QColor("#e9eef5"), QColor("#d9e0ea"), QColor("#edf2f8"), QColor("#f7f9fc"), QColor("#ffffff"), QColor("#ffffff"), QColor("#3b6ff4"), QColor("#2c5dd4"), QColor("#1f2834"), QColor("#647284"), QColor("#c8d3e0"), QColor("#ffffff"), QColor("#e6eef9"), QColor("#c7d8f4")}
    };
}

ThemePalette themeById(const QString& id)
{
    const QList<ThemePalette> themes = availableThemes();
    for (const ThemePalette& theme : themes) {
        if (theme.id == id) {
            return theme;
        }
    }
    return themes.front();
}

QString buildApplicationStyleSheet(const ThemePalette& theme)
{
    return QStringLiteral(
               "QWidget {"
               "  background: %1;"
               "  color: %2;"
               "  font-family: 'Segoe UI', 'Noto Sans', sans-serif;"
               "  font-size: 10pt;"
               "}"
               "QMainWindow, QDialog { background: %1; }"
               "QFrame#PrimarySidebar { background: %3; border: none; }"
               "QFrame#SecondarySidebar, QFrame#SettingsSidebar { background: %4; border: none; }"
               "QFrame#HeaderFrame, QFrame#ComposerFrame { background: %3; border: none; }"
               "QFrame#AuthCard, QFrame#SettingsCard { background: %4; border: 1px solid %8; border-radius: 18px; }"
               "QFrame#HeaderInfoButton:hover { background: rgba(255, 255, 255, 0.04); border-radius: 14px; }"
               "QStackedWidget#AuthPages, QWidget#AuthPage, QWidget#AuthContent, QWidget#AuthFormColumn { background: transparent; border: none; }"
               "QFrame#BubbleIncoming { background: %10; border: none; border-radius: 18px; }"
               "QFrame#BubbleOutgoing { background: %11; border: none; border-radius: 18px; }"
               "QLineEdit, QSpinBox, QComboBox, QListWidget, QScrollArea { background: %7; border: 1px solid %8; border-radius: 12px; }"
               "QLineEdit, QSpinBox, QComboBox { padding: 8px 12px; selection-background-color: %5; }"
               "QScrollArea { border: none; }"
               "QListWidget { outline: none; padding: 6px; }"
               "QListWidget#ChatListWidget { background: transparent; border: none; border-radius: 0; padding: 0; }"
               "QListWidget#ChatListWidget::item { background: transparent; border: none; margin: 0; padding: 0; }"
               "QListWidget#ChatListWidget::item:selected { background: transparent; border: none; }"
               "QListWidget#ChatListWidget::item:hover { background: transparent; border: none; }"
               "QComboBox::drop-down { border: none; width: 24px; }"
               "QPushButton { background: %7; color: %2; border: 1px solid %8; border-radius: 12px; padding: 8px 14px; }"
               "QPushButton:hover { border-color: %5; }"
               "QPushButton#PrimaryButton { background: %5; color: white; border-color: %5; }"
               "QPushButton#PrimaryButton:hover { background: %6; }"
               "QPushButton#AuthToggleButton { padding: 4px 10px; border-radius: 10px; }"
               "QPushButton#DangerButton { background: transparent; color: #ff7f99; border-color: #7b4455; }"
               "QToolButton { background: transparent; border: none; padding: 8px; color: %2; }"
               "QToolButton:hover { background: rgba(255, 255, 255, 0.08); border-radius: 10px; }"
               "QToolButton#WorkspaceButton { border: 1px solid transparent; border-radius: 14px; padding: 10px 8px; }"
               "QToolButton#WorkspaceButton:checked { background: %5; color: white; border-color: %5; }"
               "QWidget#AuthPage QLineEdit { padding: 6px 10px; min-height: 20px; }"
               "QWidget#AuthPage QPushButton { padding: 5px 12px; min-height: 28px; }"
               "QWidget#AuthPage QPushButton#AuthToggleButton { padding: 3px 10px; min-height: 24px; }"
               "QLabel { background: transparent; }"
               "QWidget#ChatListTextColumn { background: transparent; border: none; }"
               "QLabel#MutedLabel { color: %9; }"
               "QLabel#ChatListTitle { color: #ffffff; background: transparent; }"
               "QLabel#ChatListPreview { color: %9; background: transparent; }"
               "QLabel#ChatListTime { color: %9; background: transparent; font-size: 9pt; }"
               "QLabel#SectionTitle { font-size: 16pt; font-weight: 700; }"
               "QLabel#AuthHeroTitle { font-size: 22pt; font-weight: 700; }"
               "QLabel#AuthHeroSubtitle { color: %9; font-size: 10pt; }"
               "QLabel#AuthFieldLabel { color: %2; font-size: 9pt; font-weight: 600; }"
               "QLabel#SettingsTitle { font-size: 20pt; font-weight: 700; }"
               "QLabel#HeaderTitle { font-size: 17pt; font-weight: 700; }"
               "QLabel#HeaderMeta { color: %9; font-size: 10pt; }"
               "QLabel#MessageSenderLabel { color: %6; background: transparent; }"
               "QLabel#MessageBodyLabel { color: #ffffff; background: transparent; }"
               "QLabel#MessageTimeLabel { color: %9; background: transparent; font-size: 9pt; }"
               "QScrollArea#MessageScrollArea, QWidget#MessageContainer { background: transparent; border: none; }"
               "QWidget#ChatInfoOverlay { background: transparent; }"
               "QFrame#ChatInfoCard { background: %4; border: 1px solid %8; border-radius: 16px; }"
               "QFrame#ChatInfoSeparator { background: %5; border: none; min-height: 1px; max-height: 1px; }"
               "QLabel#ChatInfoTitle { color: #ffffff; background: transparent; }"
               "QLabel#ChatInfoMeta { color: %9; background: transparent; font-size: 11pt; }"
               "QTextBrowser#ChatInfoDescription { background: transparent; border: none; color: #ffffff; padding: 16px 24px 20px 24px; font-size: 12pt; }"
               "QToolButton#ChatInfoCloseButton { color: %2; background: transparent; border: none; font-size: 18pt; padding: 0; }"
               "QToolButton#ChatInfoCloseButton:hover { background: rgba(255, 255, 255, 0.08); border-radius: 12px; }"
               "QCheckBox { background: transparent; color: %2; spacing: 10px; }"
               "QCheckBox::indicator { width: 18px; height: 18px; border: 1px solid %8; border-radius: 5px; background: %7; }"
               "QCheckBox::indicator:checked { background: %5; border-color: %5; }"
               "QMenuBar { background: %1; color: %2; }"
               "QMenuBar::item:selected { background: %3; }"
               "QMenu { background: %4; border: 1px solid %8; }"
               "QMenu::item:selected { background: %5; color: white; }")
        .arg(theme.window.name(),
             theme.textPrimary.name(),
             theme.sidebar.name(),
             theme.panel.name(),
             theme.accent.name(),
             theme.accentStrong.name(),
             theme.input.name(),
             theme.border.name(),
             theme.textSecondary.name(),
             theme.bubbleIncoming.name(),
             theme.bubbleOutgoing.name(),
             theme.bubbleBorder.name());
}

QString formattedTimestamp(const QDateTime& timestamp)
{
    if (!timestamp.isValid()) {
        return {};
    }
    if (timestamp.date() == QDate::currentDate()) {
        return timestamp.time().toString("HH:mm");
    }
    return timestamp.date().toString("dd MMM");
}

QString presenceText(PresenceState state)
{
    switch (state) {
    case PresenceState::Online:
        return "Online";
    case PresenceState::Away:
        return "Away";
    case PresenceState::Busy:
        return "Do not disturb";
    case PresenceState::Offline:
    default:
        return "Offline";
    }
}

QString initialsForName(const QString& value)
{
    const QStringList parts = value.split(' ', Qt::SkipEmptyParts);
    QString result;
    for (const QString& part : parts) {
        result.append(part.left(1).toUpper());
        if (result.size() == 2) {
            break;
        }
    }
    return result.isEmpty() ? value.left(1).toUpper() : result;
}

}  // namespace CuteXmpp
