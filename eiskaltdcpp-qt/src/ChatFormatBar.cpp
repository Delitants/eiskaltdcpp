#include "ChatFormatBar.h"

#include <QAction>
#include <QEvent>
#include <QHBoxLayout>
#include <QIconEngine>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QStyle>
#include <QTextEdit>
#include <QToolButton>

namespace {
constexpr int targetSize = 32;
constexpr int iconSize = 22;
constexpr int buttonGap = 4;

enum class Symbol { Bold, Italic, Underline, Strike, Palette, Link, Code, Image, Smile, More };

Symbol buttonSymbol(const QString &name)
{
    if (name == "toolButton_BOLD") return Symbol::Bold;
    if (name == "toolButton_ITALIC") return Symbol::Italic;
    if (name == "toolButton_UNDERLINE") return Symbol::Underline;
    if (name == "toolButton_STRIKE") return Symbol::Strike;
    if (name == "toolButton_COLOR") return Symbol::Palette;
    if (name == "toolButton_LINK") return Symbol::Link;
    if (name == "toolButton_CODE") return Symbol::Code;
    if (name == "toolButton_IMAGE") return Symbol::Image;
    if (name == "toolButton_SMILE") return Symbol::Smile;
    return Symbol::More;
}

// Paths, rather than font glyphs or cached 1x bitmaps, stay sharp on every screen.
class FormatIconEngine final : public QIconEngine
{
public:
    FormatIconEngine(Symbol symbol, const QPalette &palette) : symbol(symbol), palette(palette) {}
    QIconEngine *clone() const override { return new FormatIconEngine(*this); }

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State) override
    {
        const auto group = mode == QIcon::Disabled ? QPalette::Disabled : palette.currentColorGroup();
        const auto role = mode == QIcon::Selected ? QPalette::HighlightedText : QPalette::ButtonText;
        const QColor color = palette.color(group, role);
        const qreal side = qMin(rect.width(), rect.height());
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->translate(rect.x() + (rect.width() - side) / 2,
                           rect.y() + (rect.height() - side) / 2);
        painter->scale(side / iconSize, side / iconSize);
        painter->setPen(QPen(color, symbol == Symbol::Bold ? 2.6 : 2.0,
                            Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->setBrush(Qt::NoBrush);
        QPainterPath path;
        const auto dot = [painter, color](qreal x, qreal y, qreal radius = 1.2) {
            painter->save();
            painter->setPen(Qt::NoPen);
            painter->setBrush(color);
            painter->drawEllipse(QPointF(x, y), radius, radius);
            painter->restore();
        };
        switch (symbol) {
        case Symbol::Bold:
            path.moveTo(6, 11);
            path.lineTo(12, 11);
            path.cubicTo(18, 11, 18, 18, 12, 18);
            path.lineTo(6, 18);
            path.lineTo(6, 4);
            path.lineTo(11.5, 4);
            path.cubicTo(17, 4, 17, 11, 12, 11);
            break;
        case Symbol::Italic:
            path.moveTo(9, 4); path.lineTo(16, 4);
            path.moveTo(13, 4); path.lineTo(9, 18);
            path.moveTo(6, 18); path.lineTo(13, 18);
            break;
        case Symbol::Underline:
            path.moveTo(5, 3); path.lineTo(5, 11);
            path.cubicTo(5, 19, 17, 19, 17, 11);
            path.lineTo(17, 3);
            path.moveTo(4, 20); path.lineTo(18, 20);
            break;
        case Symbol::Strike:
            path.moveTo(16, 6);
            path.cubicTo(14, 2, 6, 3, 6, 7);
            path.cubicTo(6, 10, 10, 10.5, 12, 11.5);
            path.cubicTo(20, 14, 15, 22, 6, 17);
            path.moveTo(3, 11); path.lineTo(19, 11);
            break;
        case Symbol::Palette:
            path.moveTo(11, 3);
            path.cubicTo(6, 3, 3, 6.5, 3, 11);
            path.cubicTo(3, 16, 6.5, 19, 11, 19);
            path.cubicTo(13, 19, 14, 17.5, 12.5, 16);
            path.cubicTo(11, 14.5, 12, 13, 14, 13);
            path.lineTo(16, 13);
            path.cubicTo(21.5, 13, 19.5, 3, 11, 3);
            path.closeSubpath();
            dot(7, 8); dot(11, 6); dot(15, 8); dot(6, 12);
            break;
        case Symbol::Link:
            path.moveTo(9, 6); path.lineTo(11, 4);
            path.cubicTo(15, 0, 22, 7, 18, 11);
            path.lineTo(16, 13);
            path.moveTo(6, 9); path.lineTo(4, 11);
            path.cubicTo(0, 15, 7, 22, 11, 18);
            path.lineTo(13, 16);
            path.moveTo(8, 14); path.lineTo(14, 8);
            break;
        case Symbol::Code:
            path.moveTo(6, 6); path.lineTo(2, 11); path.lineTo(6, 16);
            path.moveTo(16, 6); path.lineTo(20, 11); path.lineTo(16, 16);
            path.moveTo(13, 4); path.lineTo(9, 18);
            break;
        case Symbol::Image:
            path.addRoundedRect(QRectF(3, 4, 16, 14), 1.5, 1.5);
            path.moveTo(3, 15); path.lineTo(8, 10); path.lineTo(12, 14);
            path.lineTo(15, 11); path.lineTo(19, 15);
            dot(14, 8, 1.4);
            break;
        case Symbol::Smile:
            path.addEllipse(QPointF(11, 11), 8, 8);
            path.moveTo(7, 13); path.cubicTo(9, 17, 13, 17, 15, 13);
            dot(8, 8.5); dot(14, 8.5);
            break;
        case Symbol::More:
            dot(5, 11, 1.5); dot(11, 11, 1.5); dot(17, 11, 1.5);
            break;
        }
        painter->drawPath(path);
        painter->restore();
    }

    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        QPixmap result(size);
        result.fill(Qt::transparent);
        QPainter painter(&result);
        paint(&painter, QRect(QPoint(), size), mode, state);
        return result;
    }

    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state, qreal scale) override
    {
        auto result = pixmap(size * scale, mode, state);
        result.setDevicePixelRatio(scale);
        return result;
    }

private:
    Symbol symbol;
    QPalette palette;
};

void updateIcon(QToolButton *button)
{
    button->setIcon(QIcon(new FormatIconEngine(buttonSymbol(button->objectName()), button->palette())));
}

QString buttonLabel(const QToolButton *button)
{
    return button->toolTip().isEmpty() ? button->text() : button->toolTip();
}

int purposeGroup(const QString &name)
{
    if (name == "toolButton_BOLD" || name == "toolButton_ITALIC" ||
        name == "toolButton_UNDERLINE" || name == "toolButton_STRIKE")
        return 0;
    if (name == "toolButton_COLOR" || name == "toolButton_LINK" || name == "toolButton_CODE")
        return 1;
    return 2;
}
}

ChatFormatBar::ChatFormatBar(QHBoxLayout *row, QTextEdit *input)
    : QWidget(row->parentWidget()), editor(input),
      overflowButton(new QToolButton(this)), overflowMenu(new QMenu(this))
{
    setObjectName("chatFormatBar");
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMinimumWidth(targetSize);

    QList<QLayoutItem *> remaining;
    while (auto *layoutItem = row->takeAt(0)) {
        auto *button = qobject_cast<QToolButton *>(layoutItem->widget());
        if (!button) {
            if (layoutItem->spacerItem())
                delete layoutItem;
            else
                remaining << layoutItem;
            continue;
        }
        delete layoutItem;
        button->setParent(this);
        QSizePolicy policy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        // Keep semantic changes observable even for hidden overflow controls.
        policy.setRetainSizeWhenHidden(true);
        button->setSizePolicy(policy);
        button->setStyleSheet(QString());
        button->setFixedSize(targetSize, targetSize);
        button->setIconSize(QSize(iconSize, iconSize));
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::TabFocus);
        const auto name = button->objectName();
        updateIcon(button);
        const bool automaticAccessibleName = button->accessibleName().isEmpty();
        if (automaticAccessibleName)
            button->setAccessibleName(buttonLabel(button));
        button->installEventFilter(this);
        items.append({button, purposeGroup(name), automaticAccessibleName});
    }

    overflowButton->setObjectName("chatFormatOverflowButton");
    overflowButton->setText(QStringLiteral("..."));
    overflowButton->setToolTip(tr("More formatting"));
    overflowButton->setAccessibleName(overflowButton->toolTip());
    overflowButton->setFixedSize(targetSize, targetSize);
    overflowButton->setIconSize(QSize(iconSize, iconSize));
    overflowButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    updateIcon(overflowButton);
    overflowButton->installEventFilter(this);
    overflowButton->setAutoRaise(true);
    overflowButton->setFocusPolicy(Qt::TabFocus);
    overflowMenu->setObjectName("chatFormatOverflowMenu");
    connect(overflowButton, &QToolButton::clicked, this, &ChatFormatBar::showOverflow);
    connect(overflowMenu, &QMenu::aboutToHide, this, [this]() {
        if (editor)
            editor->setFocus(Qt::OtherFocusReason);
    });
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    row->addWidget(this, 1);
    for (auto *item : remaining)
        row->addItem(item);
    arrangeButtons();
}

void ChatFormatBar::bindEditorAction(QToolButton *button, const std::function<void()> &handler)
{
    connect(button, &QToolButton::clicked, this, [this, handler]() {
        // Focus restoration and dispatch are one connection in hubs, PMs and tests.
        // Do not restore focus after the handler: it may open the emoji picker.
        if (editor) {
            editor->setFocus(Qt::OtherFocusReason);
            handler();
        }
    });
}

QSize ChatFormatBar::sizeHint() const
{
    return QSize(qMax(0, int(items.size()) * (targetSize + buttonGap) - buttonGap), targetSize);
}

QSize ChatFormatBar::minimumSizeHint() const
{
    return QSize(targetSize, targetSize);
}

void ChatFormatBar::arrangeButtons()
{
    if (arranging)
        return;
    arranging = true;
    const int height = targetSize;
    setFixedHeight(height);
    const bool overflowing = sizeHint().width() > width();
    const int available = overflowing ? width() - targetSize - buttonGap : width();
    int x = 0;
    bool full = false;
    for (const auto &item : items) {
        if (item.automaticAccessibleName)
            item.button->setAccessibleName(buttonLabel(item.button));
        full = full || x + targetSize > available;
        item.button->setVisible(!full);
        if (!full) {
            const QRect logical(x, 0, targetSize, height);
            item.button->setGeometry(QStyle::visualRect(layoutDirection(), rect(), logical));
            x += targetSize + buttonGap;
        }
    }
    overflowButton->setVisible(overflowing);
    const QRect overflowRect(qMax(0, width() - targetSize), 0, targetSize, height);
    overflowButton->setGeometry(QStyle::visualRect(layoutDirection(), rect(), overflowRect));
    for (const auto &item : items) {
        // Existing image/emoji dialogs still use the original button as an anchor.
        if (item.button->isHidden())
            item.button->setGeometry(overflowButton->geometry());
    }
    arranging = false;
}

void ChatFormatBar::showOverflow()
{
    overflowMenu->clear();
    int previousGroup = -1;
    for (const auto &item : items) {
        auto *button = item.button;
        if (!button->isHidden())
            continue;
        if (previousGroup != -1 && previousGroup != item.group)
            overflowMenu->addSeparator();
        auto *action = overflowMenu->addAction(button->icon(), buttonLabel(button));
        action->setObjectName(button->objectName());
        action->setEnabled(button->isEnabled());
        action->setCheckable(button->isCheckable());
        action->setChecked(button->isChecked());
        if (button->menu())
            action->setMenu(button->menu());
        connect(action, &QAction::triggered, button, &QToolButton::click);
        previousGroup = item.group;
    }
    if (!overflowMenu->isEmpty())
        overflowMenu->popup(overflowButton->mapToGlobal(QPoint(0, overflowButton->height())));
}

bool ChatFormatBar::event(QEvent *event)
{
    const bool handled = QWidget::event(event);
    if (event->type() == QEvent::LayoutRequest)
        arrangeButtons();
    return handled;
}

bool ChatFormatBar::eventFilter(QObject *object, QEvent *event)
{
    if (event->type() == QEvent::ToolTipChange) {
        for (const auto &item : items) {
            if (item.button == object && item.automaticAccessibleName)
                item.button->setAccessibleName(buttonLabel(item.button));
        }
    }
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange) {
        if (auto *button = qobject_cast<QToolButton *>(object)) {
            updateIcon(button);
            // Open menus hold QIcon copies, so refresh those without rebuilding actions.
            for (auto *action : overflowMenu->actions()) {
                if (action->objectName() == button->objectName())
                    action->setIcon(button->icon());
            }
        }
    }
    if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange)
        arrangeButtons();
    return QWidget::eventFilter(object, event);
}

void ChatFormatBar::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    arrangeButtons();
}

void ChatFormatBar::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        overflowButton->setToolTip(tr("More formatting"));
        overflowButton->setAccessibleName(overflowButton->toolTip());
    }
    arrangeButtons();
}
