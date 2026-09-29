#include "ActionToolBar.h"

#include <QActionEvent>
#include <QApplication>
#include <QFontMetrics>
#include <QIconEngine>
#include <QKeyEvent>
#include <QLayout>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QProxyStyle>
#include <QStyleOptionToolButton>
#include <QTextLayout>
#include <QToolButton>

namespace {
class OverflowIcon final : public QIconEngine
{
public:
    explicit OverflowIcon(const QPalette &palette) : palette(palette) {}
    QIconEngine *clone() const override { return new OverflowIcon(palette); }
    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State) override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->translate(rect.center() - QPoint(7, 7));
        painter->setPen(QPen(palette.color(mode == QIcon::Disabled ? QPalette::Disabled : QPalette::Active,
            QPalette::ButtonText), 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->drawPolyline(QPolygonF{QPointF(3, 3), QPointF(7, 7), QPointF(3, 11)});
        painter->drawPolyline(QPolygonF{QPointF(8, 3), QPointF(12, 7), QPointF(8, 11)});
        painter->restore();
    }
    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        QPixmap result(size);
        result.fill(Qt::transparent);
        QPainter painter(&result);
        painter.scale(size.width() / 16.0, size.height() / 16.0);
        paint(&painter, QRect(0, 0, 16, 16), mode, state);
        return result;
    }
private:
    QPalette palette;
};

QString caption(QString text)
{
    text.replace(QStringLiteral("&&"), QString(QChar(0x001f)));
    text.remove(QLatin1Char('&'));
    return text.replace(QChar(0x001f), QLatin1Char('&'));
}

class ToolbarButtonStyle final : public QProxyStyle
{
public:
    ToolbarButtonStyle() : QProxyStyle(QStringLiteral("Fusion")) {}

    QSize sizeFromContents(ContentsType type, const QStyleOption *option,
                           const QSize &size, const QWidget *widget) const override
    {
        const auto *button = qstyleoption_cast<const QStyleOptionToolButton*>(option);
        if (type != CT_ToolButton || !button)
            return QProxyStyle::sizeFromContents(type, option, size, widget);
        const QFontMetrics metrics(button->font);
        const int textWidth = metrics.horizontalAdvance(caption(button->text));
        const int side = button->iconSize.width();
        const int rowHeight = qMax(40, qMax(side, metrics.height()) + 12);
        const int menu = button->features.testFlag(QStyleOptionToolButton::MenuButtonPopup) ? 16 : 0;
        switch (button->toolButtonStyle) {
        case Qt::ToolButtonTextOnly:
            return QSize(qBound(64, textWidth + 24, 152) + menu, rowHeight);
        case Qt::ToolButtonTextBesideIcon:
            return QSize(qBound(88, textWidth + side + 30, 192) + menu, rowHeight);
        case Qt::ToolButtonTextUnderIcon:
            return QSize(qBound(72, textWidth + 20, 120) + menu,
                         side + metrics.height() * 2 + 16);
        default:
            return QSize(side + 16 + menu, rowHeight);
        }
    }

    void drawComplexControl(ComplexControl control, const QStyleOptionComplex *option,
                            QPainter *painter, const QWidget *widget) const override
    {
        const auto *button = qstyleoption_cast<const QStyleOptionToolButton*>(option);
        if (control != CC_ToolButton || !button) {
            QProxyStyle::drawComplexControl(control, option, painter, widget);
            return;
        }
        // Retain native button states, menu hit targets, and focus indicators;
        // only the icon/caption layout needs to differ from Qt's unbounded label.
        QStyleOptionToolButton chrome(*button);
        chrome.text.clear();
        chrome.icon = QIcon();
        chrome.toolButtonStyle = Qt::ToolButtonIconOnly;
        QProxyStyle::drawComplexControl(control, &chrome, painter, widget);
        QRect content = subControlRect(CC_ToolButton, button, SC_ToolButton, widget).adjusted(8, 6, -8, -6);
        const QSize iconSize = button->iconSize;
        const auto mode = !(button->state & State_Enabled) ? QIcon::Disabled
            : button->state & State_MouseOver ? QIcon::Active : QIcon::Normal;
        const auto state = button->state & State_On ? QIcon::On : QIcon::Off;
        const auto style = button->toolButtonStyle;
        painter->save();
        painter->setClipRect(content);
        painter->setFont(button->font);
        painter->setPen(button->palette.color(button->state & State_Enabled
            ? QPalette::Active : QPalette::Disabled, QPalette::ButtonText));
        QRect textRect = content;
        if (style != Qt::ToolButtonTextOnly) {
            QRect iconRect(QPoint(), iconSize);
            if (style == Qt::ToolButtonTextBesideIcon) {
                iconRect.moveTopLeft(QPoint(content.left(), content.center().y() - iconSize.height() / 2 + 1));
                textRect.setLeft(iconRect.right() + 7);
                iconRect = visualRect(button->direction, content, iconRect);
                textRect = visualRect(button->direction, content, textRect);
            } else if (style == Qt::ToolButtonTextUnderIcon) {
                iconRect.moveTopLeft(QPoint(content.center().x() - iconSize.width() / 2 + 1, content.top()));
                textRect.setTop(iconRect.bottom() + 5);
            } else {
                iconRect.moveCenter(content.center());
            }
            button->icon.paint(painter, iconRect, Qt::AlignCenter, mode, state);
        }
        if (style == Qt::ToolButtonTextUnderIcon) {
            const QString text = caption(button->text);
            QTextLayout layout(text, button->font);
            QTextOption textOption(Qt::AlignHCenter);
            textOption.setTextDirection(button->direction);
            textOption.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
            layout.setTextOption(textOption);
            layout.beginLayout();
            auto first = layout.createLine();
            if (first.isValid()) {
                first.setLineWidth(textRect.width());
                first.setPosition(QPointF(0, 0));
            }
            auto second = layout.createLine();
            layout.endLayout();
            if (first.isValid()) {
                first.draw(painter, textRect.topLeft());
                if (second.isValid()) {
                    const QFontMetrics metrics(button->font);
                    const QString remainder = metrics.elidedText(text.mid(second.textStart()), Qt::ElideRight, textRect.width());
                    painter->drawText(textRect.adjusted(0, metrics.height(), 0, 0),
                        Qt::AlignTop | Qt::AlignHCenter | Qt::TextSingleLine, remainder);
                }
            }
        } else if (style == Qt::ToolButtonTextOnly || style == Qt::ToolButtonTextBesideIcon) {
            const auto text = QFontMetrics(button->font).elidedText(caption(button->text), Qt::ElideRight, textRect.width());
            painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignHCenter | Qt::TextSingleLine, text);
        }
        painter->restore();
    }
};
}

ActionToolBar::ActionToolBar(QWidget *parent) : QToolBar(parent)
{
    buttonStyle = new ToolbarButtonStyle;
    buttonStyle->setParent(this);
    setFont(QApplication::font());
    qApp->installEventFilter(this);
    setIconSize(QSize(28, 28));
    setMovable(false);
    setFloatable(false);
    setAllowedAreas(Qt::TopToolBarArea);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    overflowMenu = new QMenu(this);
    overflowMenu->setObjectName(QStringLiteral("actionToolbarOverflow"));
    if (auto *overflow = findChild<QToolButton*>(QStringLiteral("qt_toolbar_ext_button"))) {
        overflow->setFocusPolicy(Qt::StrongFocus);
        // Qt's docked toolbar extension expands over the tabs. Physical input
        // uses our popup below; retain the same behavior for accessible clicks.
        connect(overflow, &QToolButton::pressed, this, [this, overflow] {
            disconnect(overflow, nullptr, layout(), nullptr);
        });
        connect(overflow, &QToolButton::clicked, this, [this, overflow] {
            overflow->setChecked(false);
            showOverflow(overflow);
        });
    }
    connect(this, &QToolBar::toolButtonStyleChanged, this, &ActionToolBar::refreshButtons);
    connect(this, &QToolBar::iconSizeChanged, this, &ActionToolBar::refreshButtons);
}

ActionToolBar::~ActionToolBar()
{
    // The shared style is owned by this toolbar, not by its tool buttons.
    for (auto *button : findChildren<QToolButton*>())
        button->setStyle(nullptr);
}

void ActionToolBar::refreshButtons()
{
    if (!buttonStyle)
        return;
    for (auto *action : actions()) {
        if (auto *button = qobject_cast<QToolButton*>(widgetForAction(action))) {
            if (button->style() != buttonStyle)
                button->setStyle(buttonStyle);
            button->setFont(font());
            button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            button->updateGeometry();
            button->update();
        }
    }
    if (auto *overflow = findChild<QToolButton*>(QStringLiteral("qt_toolbar_ext_button"))) {
        overflow->setIcon(QIcon(new OverflowIcon(palette())));
        overflow->setToolTip(tr("More actions"));
        overflow->setAccessibleName(tr("More actions"));
    }
    updateGeometry();
}

void ActionToolBar::actionEvent(QActionEvent *event)
{
    QToolBar::actionEvent(event);
    refreshButtons();
}

void ActionToolBar::changeEvent(QEvent *event)
{
    QToolBar::changeEvent(event);
    if (event->type() == QEvent::FontChange ||
        event->type() == QEvent::LanguageChange || event->type() == QEvent::PaletteChange)
        refreshButtons();
}

void ActionToolBar::showOverflow(QToolButton *button)
{
    overflowMenu->clear();
    for (auto *action : actions()) {
        auto *widget = widgetForAction(action);
        if (!action->isVisible() || !widget || !widget->isHidden())
            continue;
        auto *toolButton = qobject_cast<QToolButton*>(widget);
        auto *menu = toolButton ? toolButton->menu() : nullptr;
        if (menu && menu != action->menu()) {
            auto *entry = overflowMenu->addMenu(menu);
            entry->setText(action->text());
            entry->setIcon(action->icon());
            entry->setEnabled(action->isEnabled());
            connect(menu, &QMenu::aboutToShow, this, &ActionToolBar::prepareSplitMenu, Qt::UniqueConnection);
        } else {
            overflowMenu->addAction(action);
        }
    }
    if (!overflowMenu->isEmpty()) {
        const int x = layoutDirection() == Qt::RightToLeft ? 0 : button->width() - overflowMenu->sizeHint().width();
        overflowMenu->popup(button->mapToGlobal(QPoint(x, button->height())));
    }
}

void ActionToolBar::prepareSplitMenu()
{
    auto *menu = qobject_cast<QMenu*>(sender());
    if (!menu)
        return;
    // Run after the button's lazy menu population, so its primary action is
    // reachable in overflow as well as its saved-hub (or plugin) choices.
    for (auto *action : actions()) {
        auto *button = qobject_cast<QToolButton*>(widgetForAction(action));
        if (!button || button->menu() != menu || action->menu() == menu || menu->actions().contains(action))
            continue;
        auto *first = menu->actions().value(0, nullptr);
        menu->insertAction(first, action);
        if (first)
            menu->insertSeparator(first);
        break;
    }
}

bool ActionToolBar::eventFilter(QObject *object, QEvent *event)
{
    if (event->type() == QEvent::ApplicationFontChange && (object == qApp || object == window())) {
        setFont(QApplication::font());
        return false;
    }
    if (object->parent() != this)
        return false;
    auto *button = qobject_cast<QToolButton*>(object);
    if (button && button->objectName() == QStringLiteral("qt_toolbar_ext_button")) {
        if ((event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease) &&
            static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
            if (event->type() == QEvent::MouseButtonPress)
                showOverflow(button);
            return true;
        }
        if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
            const int key = static_cast<QKeyEvent*>(event)->key();
            if (key == Qt::Key_Space || key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Down) {
                if (event->type() == QEvent::KeyPress)
                    showOverflow(button);
                return true;
            }
        }
    }
    return QToolBar::eventFilter(object, event);
}

void action_toolbar::separateRows(QMainWindow &window, QToolBar *actions, QToolBar *tabs, QToolBar *search)
{
    // Normalize even restored layouts: older profiles placed all three bars
    // in one row, leaving only a few pixels for the hub tabs.
    QList<QPair<QToolBar*, bool>> bars;
    for (auto *bar : {actions, search, tabs}) {
        if (!bar)
            continue;
        bars.append({bar, !bar->isHidden()});
        window.removeToolBarBreak(bar);
        window.removeToolBar(bar);
        bar->setAllowedAreas(Qt::TopToolBarArea);
        bar->setMovable(false);
        bar->setFloatable(false);
    }
    for (const auto &entry : bars) {
        if (entry.first == tabs)
            window.addToolBarBreak(Qt::TopToolBarArea);
        window.addToolBar(Qt::TopToolBarArea, entry.first);
        entry.first->setVisible(entry.second);
    }
    if (tabs)
        tabs->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}
