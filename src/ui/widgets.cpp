#include "src/ui/widgets.h"

#include "src/ui/theme.h"

#include <QEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPixmap>
#include <QResizeEvent>
#include <QSet>
#include <QStyle>
#include <QTextCursor>
#include <QTextBrowser>
#include <QToolButton>
#include <QVBoxLayout>

namespace CuteXmpp {

namespace {

QString escapeHtml(const QStringView text)
{
    return text.toString().toHtmlEscaped();
}

bool isIdentifierStart(const QChar ch)
{
    return ch.isLetter() || ch == QChar('_');
}

bool isIdentifierPart(const QChar ch)
{
    return ch.isLetterOrNumber() || ch == QChar('_');
}

bool isUrlBoundaryTerminator(const QChar ch)
{
    return ch.isSpace() || ch == QChar('<') || ch == QChar('>') || ch == QChar('"');
}

QString wrapSpan(const QString& text, const QString& color, bool bold = false)
{
    return QStringLiteral("<span style=\"color:%1;%2\">%3</span>")
        .arg(color, bold ? QStringLiteral("font-weight:600;") : QString(), text);
}

QString detectCodeLanguage(const QString& declaredLanguage, const QString& code)
{
    const QString declared = declaredLanguage.trimmed().toLower();
    if (!declared.isEmpty()) {
        if (declared == "c++" || declared == "cc" || declared == "cxx") {
            return "cpp";
        }
        if (declared == "js" || declared == "ts") {
            return "javascript";
        }
        if (declared == "py") {
            return "python";
        }
        return declared;
    }

    const QString sample = code.trimmed();
    if (sample.contains("#include") || sample.contains("std::") || sample.contains("printf(") || sample.contains("int main")) {
        return "cpp";
    }
    if (sample.contains("def ") || sample.contains("import ") || sample.contains("print(") || sample.contains("self")) {
        return "python";
    }
    if (sample.contains("function ") || sample.contains("const ") || sample.contains("let ") || sample.contains("=>")) {
        return "javascript";
    }
    if ((sample.startsWith('{') || sample.startsWith('[')) && sample.contains("\":")) {
        return "json";
    }
    if (sample.contains("<html") || sample.contains("</") || sample.contains("/>")) {
        return "html";
    }
    return "plain";
}

QString highlightCode(const QString& code, const QString& language)
{
    static const QSet<QString> cppKeywords = {
        "alignas", "alignof", "auto", "bool", "break", "case", "catch", "char", "class", "const", "constexpr",
        "continue", "default", "delete", "do", "double", "else", "enum", "explicit", "false", "float", "for",
        "friend", "if", "inline", "int", "long", "namespace", "new", "noexcept", "nullptr", "operator", "private",
        "protected", "public", "return", "short", "signed", "sizeof", "static", "struct", "switch", "template",
        "this", "throw", "true", "try", "typedef", "typename", "union", "unsigned", "using", "virtual", "void",
        "while"
    };
    static const QSet<QString> pythonKeywords = {
        "and", "as", "assert", "break", "class", "continue", "def", "elif", "else", "except", "False", "finally",
        "for", "from", "if", "import", "in", "is", "lambda", "None", "not", "or", "pass", "raise", "return",
        "True", "try", "while", "with", "yield"
    };
    static const QSet<QString> jsKeywords = {
        "async", "await", "break", "case", "catch", "class", "const", "continue", "default", "delete", "else",
        "export", "extends", "false", "finally", "for", "function", "if", "import", "let", "new", "null", "return",
        "switch", "this", "throw", "true", "try", "typeof", "undefined", "var", "while", "yield"
    };
    static const QSet<QString> jsonKeywords = {"true", "false", "null"};

    const QString lang = detectCodeLanguage(language, code);
    const QSet<QString>* keywords = nullptr;
    QString singleLineComment;

    if (lang == "cpp" || lang == "c") {
        keywords = &cppKeywords;
        singleLineComment = "//";
    } else if (lang == "python") {
        keywords = &pythonKeywords;
        singleLineComment = "#";
    } else if (lang == "javascript" || lang == "typescript" || lang == "js" || lang == "ts") {
        keywords = &jsKeywords;
        singleLineComment = "//";
    } else if (lang == "json") {
        keywords = &jsonKeywords;
    }

    QString html;
    html.reserve(code.size() * 2);
    qsizetype index = 0;

    while (index < code.size()) {
        const QChar ch = code.at(index);

        if (!singleLineComment.isEmpty() && code.mid(index, singleLineComment.size()) == singleLineComment) {
            qsizetype lineEnd = code.indexOf('\n', index);
            if (lineEnd < 0) {
                lineEnd = code.size();
            }
            html += wrapSpan(escapeHtml(QStringView(code).mid(index, lineEnd - index)), "#7ec699");
            index = lineEnd;
            continue;
        }

        if (ch == QChar('"') || ch == QChar('\'')) {
            const QChar quote = ch;
            qsizetype end = index + 1;
            bool escaped = false;
            while (end < code.size()) {
                const QChar current = code.at(end);
                if (current == QChar('\\') && !escaped) {
                    escaped = true;
                    ++end;
                    continue;
                }
                if (current == quote && !escaped) {
                    ++end;
                    break;
                }
                escaped = false;
                ++end;
            }
            html += wrapSpan(escapeHtml(QStringView(code).mid(index, end - index)), "#ffb86c");
            index = end;
            continue;
        }

        if (ch.isDigit()) {
            qsizetype end = index + 1;
            while (end < code.size() && (code.at(end).isLetterOrNumber() || code.at(end) == QChar('.') || code.at(end) == QChar('_'))) {
                ++end;
            }
            html += wrapSpan(escapeHtml(QStringView(code).mid(index, end - index)), "#bd93f9");
            index = end;
            continue;
        }

        if (isIdentifierStart(ch)) {
            qsizetype end = index + 1;
            while (end < code.size() && isIdentifierPart(code.at(end))) {
                ++end;
            }
            const QString token = code.mid(index, end - index);
            if (keywords && keywords->contains(token)) {
                html += wrapSpan(escapeHtml(token), "#79b8ff", true);
            } else {
                html += escapeHtml(token);
            }
            index = end;
            continue;
        }

        html += escapeHtml(QString(ch));
        ++index;
    }

    return html;
}

QString renderInlineMarkup(const QString& text)
{
    QString html;
    html.reserve(text.size() * 2);

    qsizetype index = 0;
    while (index < text.size()) {
        const QChar ch = text.at(index);

        if (ch == QChar('\n')) {
            html += QStringLiteral("<br/>");
            ++index;
            continue;
        }

        if (text.mid(index, 8) == QStringLiteral("https://") || text.mid(index, 7) == QStringLiteral("http://")) {
            qsizetype end = index;
            while (end < text.size() && !isUrlBoundaryTerminator(text.at(end))) {
                ++end;
            }
            QString url = text.mid(index, end - index);
            while (!url.isEmpty() && QStringLiteral(".,!?;:)").contains(url.back())) {
                --end;
                url.chop(1);
            }
            if (!url.isEmpty()) {
                const QString escapedUrl = url.toHtmlEscaped();
                html += QStringLiteral("<a href=\"%1\" style=\"color:#9dc1ff;text-decoration:none;\">%2</a>").arg(escapedUrl, escapedUrl);
                index = end;
                continue;
            }
        }

        if (ch == QChar('`')) {
            const qsizetype end = text.indexOf('`', index + 1);
            if (end > index + 1) {
                html += QStringLiteral("<span style=\"font-family:'Cascadia Mono','Consolas',monospace;background:#1a1522;border-radius:6px;padding:2px 6px;color:#f8f5ff;\">%1</span>")
                    .arg(escapeHtml(QStringView(text).mid(index + 1, end - index - 1)));
                index = end + 1;
                continue;
            }
        }

        if (ch == QChar('*') || ch == QChar('_') || ch == QChar('~')) {
            const qsizetype end = text.indexOf(ch, index + 1);
            if (end > index + 1) {
                const QString inner = renderInlineMarkup(text.mid(index + 1, end - index - 1));
                if (ch == QChar('*')) {
                    html += QStringLiteral("<b>%1</b>").arg(inner);
                } else if (ch == QChar('_')) {
                    html += QStringLiteral("<i>%1</i>").arg(inner);
                } else {
                    html += QStringLiteral("<span style=\"text-decoration:line-through;\">%1</span>").arg(inner);
                }
                index = end + 1;
                continue;
            }
        }

        html += escapeHtml(QString(ch));
        ++index;
    }

    return html;
}

QString formatMessageHtml(const QString& text)
{
    QString html;
    qsizetype position = 0;

    while (position < text.size()) {
        const qsizetype fenceStart = text.indexOf(QStringLiteral("```"), position);
        if (fenceStart < 0) {
            html += renderInlineMarkup(text.mid(position));
            break;
        }

        html += renderInlineMarkup(text.mid(position, fenceStart - position));
        const qsizetype languageEnd = text.indexOf('\n', fenceStart + 3);
        if (languageEnd < 0) {
            html += escapeHtml(text.mid(fenceStart));
            break;
        }

        const QString declaredLanguage = text.mid(fenceStart + 3, languageEnd - fenceStart - 3).trimmed();
        const qsizetype fenceEnd = text.indexOf(QStringLiteral("```"), languageEnd + 1);
        if (fenceEnd < 0) {
            html += escapeHtml(text.mid(fenceStart));
            break;
        }

        const QString code = text.mid(languageEnd + 1, fenceEnd - languageEnd - 1);
        const QString detectedLanguage = detectCodeLanguage(declaredLanguage, code);
        const QString languageLabel = detectedLanguage == "plain" ? QStringLiteral("CODE") : detectedLanguage.left(12).toUpper();
        html += QStringLiteral(
                    "<div style=\"margin:8px 0 4px 0;border-radius:10px;overflow:hidden;border:1px solid #5b4a77;background:#17121f;\">"
                    "<div style=\"padding:4px 10px;background:#4f416a;color:#efe9ff;font-weight:600;font-size:10pt;\">%1</div>"
                    "<pre style=\"margin:0;padding:10px 12px;white-space:pre-wrap;word-break:break-word;font-family:'Cascadia Mono','Consolas',monospace;font-size:11pt;color:#f8f5ff;\">%2</pre>"
                    "</div>")
                    .arg(escapeHtml(languageLabel), highlightCode(code, detectedLanguage));
        position = fenceEnd + 3;
    }

    return html;
}

}  // namespace

QPixmap makeAvatarPixmap(const QImage& image, const QString& fallbackText, const QColor& accent, int size)
{
    QPixmap canvas(size, size);
    canvas.fill(Qt::transparent);

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPainterPath path;
    path.addEllipse(canvas.rect());
    painter.setClipPath(path);

    if (!image.isNull()) {
        const QPixmap scaled = QPixmap::fromImage(image).scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        painter.drawPixmap(0, 0, scaled);
    } else {
        painter.fillPath(path, accent);
        painter.setPen(Qt::white);
        QFont font = painter.font();
        font.setBold(true);
        font.setPointSize(qMax(10, size / 3));
        painter.setFont(font);
        painter.drawText(canvas.rect(), Qt::AlignCenter, initialsForName(fallbackText));
    }

    return canvas;
}

ElidedLabel::ElidedLabel(QWidget* parent)
    : QLabel(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    setWordWrap(false);
}

void ElidedLabel::setFullText(const QString& text)
{
    m_fullText = text;
    setToolTip(text);
    updateElidedText();
}

QString ElidedLabel::fullText() const
{
    return m_fullText;
}

void ElidedLabel::resizeEvent(QResizeEvent* event)
{
    QLabel::resizeEvent(event);
    updateElidedText();
}

void ElidedLabel::updateElidedText()
{
    const QString elided = fontMetrics().elidedText(m_fullText, Qt::ElideRight, qMax(0, contentsRect().width()));
    QLabel::setText(elided);
}

ClickableFrame::ClickableFrame(QWidget* parent)
    : QFrame(parent)
{
    setCursor(Qt::PointingHandCursor);
}

void ClickableFrame::mouseReleaseEvent(QMouseEvent* event)
{
    QFrame::mouseReleaseEvent(event);
    if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint())) {
        emit clicked();
    }
}

ChatBackgroundFrame::ChatBackgroundFrame(QWidget* parent)
    : QFrame(parent)
{
    setFrameShape(QFrame::NoFrame);
}

void ChatBackgroundFrame::setBackgroundAppearance(const QColor& color, const QString& imagePath)
{
    m_color = color.isValid() ? color : QColor("#f5f3f9");
    m_imagePath = imagePath;
    update();
}

void ChatBackgroundFrame::setOverlayColor(const QColor& color)
{
    m_overlay = color;
    update();
}

void ChatBackgroundFrame::paintEvent(QPaintEvent* event)
{
    QFrame::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), m_color);

    if (!m_imagePath.isEmpty() && QFileInfo::exists(m_imagePath)) {
        const QPixmap background(m_imagePath);
        if (!background.isNull()) {
            painter.drawPixmap(rect(), background.scaled(size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
            painter.fillRect(rect(), m_overlay);
        }
    }
}

ChatListItemWidget::ChatListItemWidget(QWidget* parent)
    : QFrame(parent)
{
    setObjectName("ChatListItemCard");

    auto* rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(8, 8, 8, 8);
    rootLayout->setSpacing(8);

    m_avatarLabel = new QLabel(this);
    m_avatarLabel->setFixedSize(64, 64);

    auto* textColumn = new QWidget(this);
    textColumn->setObjectName("ChatListTextColumn");
    textColumn->setAttribute(Qt::WA_StyledBackground, true);
    auto* textLayout = new QVBoxLayout(textColumn);
    textLayout->setContentsMargins(0, 8, 0, 8);
    textLayout->setSpacing(0);

    auto* topRow = new QHBoxLayout;
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->setSpacing(8);

    m_titleLabel = new ElidedLabel(textColumn);
    m_titleLabel->setObjectName("ChatListTitle");
    QFont titleFont = m_titleLabel->font();
    titleFont.setBold(true);
    titleFont.setPointSize(13);
    m_titleLabel->setFont(titleFont);

    m_timeLabel = new QLabel(textColumn);
    m_timeLabel->setObjectName("ChatListTime");
    m_timeLabel->setAlignment(Qt::AlignTop | Qt::AlignRight);

    topRow->addWidget(m_titleLabel, 1);
    topRow->addWidget(m_timeLabel, 0, Qt::AlignTop);

    m_previewLabel = new ElidedLabel(textColumn);
    m_previewLabel->setObjectName("ChatListPreview");
    m_previewLabel->setAlignment(Qt::AlignLeft | Qt::AlignBottom);
    QFont previewFont = m_previewLabel->font();
    previewFont.setPointSize(10);
    previewFont.setBold(false);
    m_previewLabel->setFont(previewFont);

    textLayout->addLayout(topRow);
    textLayout->addStretch(1);
    textLayout->addWidget(m_previewLabel);

    rootLayout->addWidget(m_avatarLabel, 0, Qt::AlignTop);
    rootLayout->addWidget(textColumn, 1);
}

void ChatListItemWidget::setChat(const ChatSummary& chat, const ThemePalette& theme, bool selected)
{
    m_avatarLabel->setPixmap(makeAvatarPixmap(chat.avatar, chat.title, theme.accent, 64));
    m_titleLabel->setFullText(chat.title);
    const QString preview = chat.preview.isEmpty() ? chat.subtitle : chat.preview;
    m_previewLabel->setFullText(preview);
    m_timeLabel->setText(formattedTimestamp(chat.lastActivity));
    applySelection(theme, selected);
}

void ChatListItemWidget::applySelection(const ThemePalette& theme, bool selected)
{
    Q_UNUSED(theme);
    const QString background = selected ? QStringLiteral("#564875") : QStringLiteral("transparent");
    setStyleSheet(QStringLiteral("QFrame#ChatListItemCard { background: %1; border: none; border-radius: 14px; }").arg(background));
}

MessageBubbleWidget::MessageBubbleWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(10);

    m_avatarLabel = new QLabel(this);
    m_avatarLabel->setFixedSize(36, 36);

    m_bubbleFrame = new QFrame(this);
    auto* bubbleLayout = new QVBoxLayout(m_bubbleFrame);
    bubbleLayout->setContentsMargins(14, 12, 14, 12);
    bubbleLayout->setSpacing(6);

    m_senderLabel = new QLabel(m_bubbleFrame);
    m_senderLabel->setObjectName("MessageSenderLabel");
    QFont senderFont = m_senderLabel->font();
    senderFont.setBold(true);
    senderFont.setPointSize(11);
    m_senderLabel->setFont(senderFont);

    m_bodyLabel = new QLabel(m_bubbleFrame);
    m_bodyLabel->setObjectName("MessageBodyLabel");
    m_bodyLabel->setWordWrap(true);
    m_bodyLabel->setTextFormat(Qt::RichText);
    m_bodyLabel->setOpenExternalLinks(true);
    m_bodyLabel->setTextInteractionFlags(Qt::TextBrowserInteraction | Qt::TextSelectableByMouse);
    QFont bodyFont = m_bodyLabel->font();
    bodyFont.setPointSize(14);
    bodyFont.setBold(false);
    m_bodyLabel->setFont(bodyFont);

    m_timeLabel = new QLabel(m_bubbleFrame);
    m_timeLabel->setObjectName("MessageTimeLabel");
    m_timeLabel->setAlignment(Qt::AlignRight);

    bubbleLayout->addWidget(m_senderLabel);
    bubbleLayout->addWidget(m_bodyLabel);
    bubbleLayout->addWidget(m_timeLabel);

    rootLayout->addWidget(m_avatarLabel, 0, Qt::AlignBottom);
    rootLayout->addWidget(m_bubbleFrame, 0, Qt::AlignTop);
    rootLayout->addStretch(1);
}

void MessageBubbleWidget::setMessage(const MessageEntry& message, const ChatSummary& chat, const ThemePalette& theme, int bubbleWidth)
{
    auto* rootLayout = qobject_cast<QHBoxLayout*>(layout());
    if (!rootLayout) {
        return;
    }

    while (QLayoutItem* item = rootLayout->takeAt(0)) {
        delete item;
    }

    const QPixmap avatar = makeAvatarPixmap(message.outgoing ? QImage() : chat.avatar, message.senderName, theme.accent, 36);
    m_avatarLabel->setPixmap(avatar);
    m_senderLabel->setText(message.senderName);
    m_bodyLabel->setText(formatMessageHtml(message.text));
    m_bodyLabel->setMaximumWidth(bubbleWidth);
    m_timeLabel->setText(formattedTimestamp(message.timestamp));

    m_bubbleFrame->setObjectName(message.outgoing ? "BubbleOutgoing" : "BubbleIncoming");
    m_bubbleFrame->style()->unpolish(m_bubbleFrame);
    m_bubbleFrame->style()->polish(m_bubbleFrame);

    if (message.outgoing) {
        rootLayout->addStretch(1);
        rootLayout->addWidget(m_bubbleFrame, 0, Qt::AlignTop);
        rootLayout->addWidget(m_avatarLabel, 0, Qt::AlignBottom);
    } else {
        rootLayout->addWidget(m_avatarLabel, 0, Qt::AlignBottom);
        rootLayout->addWidget(m_bubbleFrame, 0, Qt::AlignTop);
        rootLayout->addStretch(1);
    }
}

ChatInfoOverlay::ChatInfoOverlay(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("ChatInfoOverlay");
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_StyledBackground, false);
    setAttribute(Qt::WA_DeleteOnClose, false);
    hide();

    if (parentWidget()) {
        parentWidget()->installEventFilter(this);
    }

    m_card = new QFrame(this);
    m_card->setObjectName("ChatInfoCard");
    m_card->setMaximumWidth(470);

    auto* cardLayout = new QVBoxLayout(m_card);
    cardLayout->setContentsMargins(0, 0, 0, 0);
    cardLayout->setSpacing(0);

    auto* topSection = new QWidget(m_card);
    auto* topLayout = new QHBoxLayout(topSection);
    topLayout->setContentsMargins(24, 18, 16, 18);
    topLayout->setSpacing(16);

    m_avatarLabel = new QLabel(topSection);
    m_avatarLabel->setFixedSize(72, 72);

    auto* titleColumn = new QWidget(topSection);
    auto* titleLayout = new QVBoxLayout(titleColumn);
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(4);

    m_titleLabel = new ElidedLabel(titleColumn);
    m_titleLabel->setObjectName("ChatInfoTitle");
    QFont titleFont = m_titleLabel->font();
    titleFont.setBold(true);
    titleFont.setPointSize(18);
    m_titleLabel->setFont(titleFont);

    m_metaLabel = new QLabel(titleColumn);
    m_metaLabel->setObjectName("ChatInfoMeta");

    titleLayout->addStretch(1);
    titleLayout->addWidget(m_titleLabel);
    titleLayout->addWidget(m_metaLabel);
    titleLayout->addStretch(1);

    m_closeButton = new QToolButton(topSection);
    m_closeButton->setObjectName("ChatInfoCloseButton");
    m_closeButton->setText(QStringLiteral("×"));
    m_closeButton->setAutoRaise(true);
    connect(m_closeButton, &QToolButton::clicked, this, &QWidget::hide);

    topLayout->addWidget(m_avatarLabel, 0, Qt::AlignTop);
    topLayout->addWidget(titleColumn, 1);
    topLayout->addWidget(m_closeButton, 0, Qt::AlignTop);

    auto* separator = new QFrame(m_card);
    separator->setObjectName("ChatInfoSeparator");
    separator->setFixedHeight(1);

    m_descriptionView = new QTextBrowser(m_card);
    m_descriptionView->setObjectName("ChatInfoDescription");
    m_descriptionView->setOpenExternalLinks(true);
    m_descriptionView->setFrameShape(QFrame::NoFrame);
    m_descriptionView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_descriptionView->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_descriptionView->setReadOnly(true);
    m_descriptionView->setUndoRedoEnabled(false);

    cardLayout->addWidget(topSection);
    cardLayout->addWidget(separator);
    cardLayout->addWidget(m_descriptionView);
}

void ChatInfoOverlay::showChatInfo(const ChatSummary& chat, const ThemePalette& theme)
{
    m_avatarLabel->setPixmap(makeAvatarPixmap(chat.avatar, chat.title, theme.accent, 72));
    m_titleLabel->setFullText(chat.title);
    m_metaLabel->setText(chat.subtitle.trimmed().isEmpty() ? chat.id : chat.subtitle);

    QStringList blocks;
    if (!chat.id.trimmed().isEmpty()) {
        blocks.append(chat.id.trimmed());
    }
    if (!chat.description.trimmed().isEmpty() && chat.description.trimmed() != chat.id.trimmed()) {
        blocks.append(chat.description.trimmed());
    }
    if (blocks.isEmpty()) {
        blocks.append(chat.subtitle.trimmed());
    }
    m_descriptionView->setPlainText(blocks.join(QStringLiteral("\n\n")));
    m_descriptionView->moveCursor(QTextCursor::Start);

    updateOverlayGeometry();
    show();
    raise();
    setFocus(Qt::OtherFocusReason);
}

bool ChatInfoOverlay::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parentWidget() && (event->type() == QEvent::Resize || event->type() == QEvent::Move || event->type() == QEvent::Show)) {
        updateOverlayGeometry();
    }
    return QWidget::eventFilter(watched, event);
}

void ChatInfoOverlay::mousePressEvent(QMouseEvent* event)
{
    if (!m_card->geometry().contains(event->position().toPoint())) {
        hide();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void ChatInfoOverlay::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        hide();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void ChatInfoOverlay::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor(0, 0, 0, 150));
}

void ChatInfoOverlay::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    updateOverlayGeometry();
}

void ChatInfoOverlay::updateOverlayGeometry()
{
    if (!parentWidget()) {
        return;
    }

    setGeometry(parentWidget()->rect());
    layoutCard();
}

void ChatInfoOverlay::layoutCard()
{
    if (!m_card) {
        return;
    }

    const int cardWidth = qMin(width() - 24, 470);
    const int cardHeight = qMin(height() - 24, 320);
    const QSize cardSize(qMax(280, cardWidth), qMax(220, cardHeight));
    const QPoint topLeft((width() - cardSize.width()) / 2, (height() - cardSize.height()) / 2);
    m_card->setGeometry(QRect(topLeft, cardSize));
}

}  // namespace CuteXmpp
