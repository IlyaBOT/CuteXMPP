#pragma once

#include "src/models/types.h"

#include <QFrame>
#include <QLabel>
#include <QWidget>

class QEvent;
class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QShowEvent;
class QTextBrowser;
class QToolButton;

namespace CuteXmpp {

QPixmap makeAvatarPixmap(const QImage& image, const QString& fallbackText, const QColor& accent, int size);

class ElidedLabel final : public QLabel
{
    Q_OBJECT

public:
    explicit ElidedLabel(QWidget* parent = nullptr);

    void setFullText(const QString& text);
    QString fullText() const;

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void updateElidedText();

    QString m_fullText;
};

class ClickableFrame : public QFrame
{
    Q_OBJECT

public:
    explicit ClickableFrame(QWidget* parent = nullptr);

signals:
    void clicked();

protected:
    void mouseReleaseEvent(QMouseEvent* event) override;
};

class ChatBackgroundFrame final : public QFrame
{
public:
    explicit ChatBackgroundFrame(QWidget* parent = nullptr);

    void setBackgroundAppearance(const QColor& color, const QString& imagePath);
    void setOverlayColor(const QColor& color);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QColor m_color = QColor("#f5f3f9");
    QColor m_overlay = QColor(255, 255, 255, 22);
    QString m_imagePath;
};

class ChatListItemWidget final : public QFrame
{
public:
    explicit ChatListItemWidget(QWidget* parent = nullptr);

    void setChat(const ChatSummary& chat, const ThemePalette& theme, bool selected);

private:
    void applySelection(const ThemePalette& theme, bool selected);

    QLabel* m_avatarLabel = nullptr;
    ElidedLabel* m_titleLabel = nullptr;
    ElidedLabel* m_previewLabel = nullptr;
    QLabel* m_timeLabel = nullptr;
};

class MessageBubbleWidget final : public QWidget
{
public:
    explicit MessageBubbleWidget(QWidget* parent = nullptr);

    void setMessage(const MessageEntry& message, const ChatSummary& chat, const ThemePalette& theme, int bubbleWidth);

private:
    QLabel* m_avatarLabel = nullptr;
    QFrame* m_bubbleFrame = nullptr;
    QLabel* m_senderLabel = nullptr;
    QLabel* m_bodyLabel = nullptr;
    QLabel* m_timeLabel = nullptr;
};

class ChatInfoOverlay final : public QWidget
{
    Q_OBJECT

public:
    explicit ChatInfoOverlay(QWidget* parent = nullptr);

    void showChatInfo(const ChatSummary& chat, const ThemePalette& theme);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void updateOverlayGeometry();
    void layoutCard();

    QFrame* m_card = nullptr;
    QLabel* m_avatarLabel = nullptr;
    ElidedLabel* m_titleLabel = nullptr;
    QLabel* m_metaLabel = nullptr;
    QTextBrowser* m_descriptionView = nullptr;
    QToolButton* m_closeButton = nullptr;
};

}  // namespace CuteXmpp
