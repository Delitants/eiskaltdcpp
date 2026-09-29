#include "TabNavigation.h"
#include "ArenaWidget.h"

#include <QApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QRegularExpression>
#include <QScreen>
#include <QScrollBar>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidgetAction>

namespace tab_navigation {

void drawFocusRing(QPainter &painter, const QRect &rect, const QPalette &palette)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(palette.color(QPalette::Highlight), 1.5));
    painter.drawRoundedRect(QRectF(rect).adjusted(1.5, 1.5, -1.5, -1.5), 4, 4);
    painter.restore();
}

namespace {
class CloseButton : public QToolButton {
public:
    explicit CloseButton(QWidget *parent) : QToolButton(parent) {}

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const auto pal = palette();
        if (isEnabled() && (underMouse() || isDown())) {
            QColor fill = pal.color(isDown() ? QPalette::Highlight : QPalette::Midlight);
            fill.setAlpha(isDown() ? 100 : 160);
            painter.setPen(Qt::NoPen);
            painter.setBrush(fill);
            painter.drawRoundedRect(rect().adjusted(2, 2, -2, -2), 4, 4);
        }
        painter.setPen(QPen(pal.color(isEnabled() ? QPalette::Active : QPalette::Disabled,
                                     QPalette::ButtonText), 1.6, Qt::SolidLine, Qt::RoundCap));
        const QPointF center(width() / 2.0, height() / 2.0);
        painter.drawLine(center + QPointF(-4.5, -4.5), center + QPointF(4.5, 4.5));
        painter.drawLine(center + QPointF(-4.5, 4.5), center + QPointF(4.5, -4.5));
        if (hasFocus())
            drawFocusRing(painter, rect(), pal);
    }
};

class NavigationButton : public QToolButton {
public:
    explicit NavigationButton(QWidget *parent) : QToolButton(parent)
    {
        setFocusPolicy(Qt::StrongFocus);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        const auto pal = palette();
        painter.fillRect(rect(), pal.color(QPalette::Window));
        painter.setRenderHint(QPainter::Antialiasing);
        if (underMouse() || isDown()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(pal.color(QPalette::Midlight));
            painter.drawRoundedRect(rect().adjusted(2, 2, -2, -2), 4, 4);
        }
        painter.setPen(QPen(pal.color(isEnabled() ? QPalette::Active : QPalette::Disabled,
                                     QPalette::WindowText), 1.6, Qt::SolidLine, Qt::RoundCap));
        const QPoint center = rect().center();
        if (arrowType() == Qt::NoArrow) {
            for (int y : {-4, 0, 4})
                painter.drawLine(center + QPoint(-6, y), center + QPoint(6, y));
        } else {
            const int direction = arrowType() == Qt::UpArrow ? -1 : 1;
            painter.drawLine(center + QPoint(-4, -2 * direction), center + QPoint(0, 2 * direction));
            painter.drawLine(center + QPoint(0, 2 * direction), center + QPoint(4, -2 * direction));
        }
        if (hasFocus())
            drawFocusRing(painter, rect(), pal);
    }
};
}

QString compactTitle(const QString &title)
{
    QString result = title.trimmed();
    static const QRegularExpression address(
        QStringLiteral("(?:adcs?|nmdcs?|dchubs?)://[^\\s]+"), QRegularExpression::CaseInsensitiveOption);
    const auto match = address.match(result);
    if (!match.hasMatch())
        return result;
    const QUrl url(match.captured());
    QString host = url.host();
    if (host.isEmpty())
        return result;
    if (host.contains(QLatin1Char(':')))
        host = QLatin1Char('[') + host + QLatin1Char(']');
    if (url.port() >= 0)
        host += QLatin1Char(':') + QString::number(url.port());
    result.replace(match.capturedStart(), match.capturedLength(), host);
    return result;
}

quint64 Registry::add(ArenaWidget *widget)
{
    if (!widget || !widget->getWidget())
        return 0;
    if (const quint64 existing = idFor(widget))
        return existing;
    QObject *owner = dynamic_cast<QObject*>(widget);
    const quint64 id = ++nextId;
    targets.insert(id, {widget, owner ? owner : widget->getWidget(), widget->getWidget()});
    return id;
}

void Registry::remove(ArenaWidget *widget)
{
    for (auto it = targets.begin(); it != targets.end();) {
        if (it->arena == widget)
            it = targets.erase(it);
        else
            ++it;
    }
}

quint64 Registry::idFor(ArenaWidget *widget) const
{
    for (auto it = targets.cbegin(); it != targets.cend(); ++it) {
        if (it->arena == widget && it->owner && it->widget)
            return it.key();
    }
    return 0;
}

ArenaWidget *Registry::resolve(quint64 id) const
{
    const auto it = targets.constFind(id);
    return it != targets.cend() && it->owner && it->widget ? it->arena : nullptr;
}

AllTabsMenu::AllTabsMenu(QWidget *parent) : QMenu(parent)
{
    setObjectName(QStringLiteral("allTabsMenu"));
    setAttribute(Qt::WA_WindowPropagation);
    auto *panel = new QWidget(this);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);
    search = new QLineEdit(panel);
    search->setObjectName(QStringLiteral("tabSearch"));
    search->setPlaceholderText(tr("Search tabs by title or hostname"));
    search->setAccessibleName(search->placeholderText());
    search->setClearButtonEnabled(true);
    list = new QListWidget(panel);
    list->setObjectName(QStringLiteral("tabSearchResults"));
    list->setAccessibleName(tr("All Tabs"));
    list->setTextElideMode(Qt::ElideRight);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list->setIconSize(QSize(16, 16));
    list->setMinimumSize(160, 120);
    empty = new QLabel(tr("No matching tabs"), panel);
    layout->addWidget(search);
    layout->addWidget(list);
    layout->addWidget(empty);
    auto *action = new QWidgetAction(this);
    action->setDefaultWidget(panel);
    addAction(action);
    search->installEventFilter(this);
    list->installEventFilter(this);
    connect(search, &QLineEdit::textChanged, this, &AllTabsMenu::filter);
    connect(search, &QLineEdit::returnPressed, this, &AllTabsMenu::activateCurrent);
    connect(list, &QListWidget::itemClicked, this, &AllTabsMenu::activateCurrent);
    connect(this, &QMenu::aboutToShow, this, [this, panel]() {
        const QRect available = screen()->availableGeometry();
        panel->setFixedWidth(qMax(180, qMin(440, available.width() - 32)));
        list->setFixedHeight(qMax(120, qMin(360, available.height() - 140)));
        search->clear();
        filter();
        QTimer::singleShot(0, search, [this]() {
            // Run after the owning view has supplied its current tab snapshot.
            list->setCurrentItem(nullptr);
            filter();
            if (list->currentItem())
                list->scrollToItem(list->currentItem());
            search->setFocus();
        });
    });
    filter();
}

void AllTabsMenu::setEntries(const QList<Entry> &updated)
{
    entries = updated;
    filter();
}

void AllTabsMenu::filter()
{
    const quint64 previous = list->currentItem() ? list->currentItem()->data(Qt::UserRole).toULongLong() : 0;
    const int scroll = list->verticalScrollBar()->value();
    const auto terms = search->text().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    list->clear();
    QListWidgetItem *selection = nullptr;
    for (const Entry &entry : entries) {
        const QString haystack = entry.title + QLatin1Char('\n') + entry.details;
        bool matches = true;
        for (const QString &term : terms)
            matches = matches && haystack.contains(term, Qt::CaseInsensitive);
        if (!matches)
            continue;
        auto *item = new QListWidgetItem(entry.icon, entry.title, list);
        item->setData(Qt::UserRole, QVariant::fromValue(entry.id));
        item->setToolTip(entry.details.toHtmlEscaped());
        QFont font = list->font();
        font.setBold(entry.active);
        item->setFont(font);
        item->setSizeHint(QSize(0, qMax(30, list->fontMetrics().height() + 10)));
        if (entry.id == previous || (!selection && entry.active))
            selection = item;
    }
    if (!selection && list->count())
        selection = list->item(0);
    list->setCurrentItem(selection);
    list->verticalScrollBar()->setValue(scroll);
    empty->setVisible(list->count() == 0);
}

void AllTabsMenu::activateCurrent()
{
    if (!list->currentItem())
        return;
    const quint64 id = list->currentItem()->data(Qt::UserRole).toULongLong();
    hide();
    emit selected(id);
}

bool AllTabsMenu::eventFilter(QObject *object, QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape) {
            hide();
            return true;
        }
        if (object == search && (key->key() == Qt::Key_Down || key->key() == Qt::Key_Up)) {
            const int direction = key->key() == Qt::Key_Down ? 1 : -1;
            if (list->count())
                list->setCurrentRow(qBound(0, list->currentRow() + direction, list->count() - 1));
            return true;
        }
        if (object == list && (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)) {
            activateCurrent();
            return true;
        }
    }
    return QMenu::eventFilter(object, event);
}

QToolButton *makeAllTabsButton(AllTabsMenu *menu, QWidget *parent)
{
    auto *button = new NavigationButton(parent);
    button->setObjectName(QStringLiteral("allTabsButton"));
    const auto title = AllTabsMenu::tr("All Tabs");
    button->setText(title);
    button->setToolTip(title);
    button->setAccessibleName(title);
    button->setFixedSize(30, 30);
    button->setAutoRaise(true);
    button->setMenu(menu);
    QObject::connect(button, &QToolButton::clicked, menu, [button, menu]() {
        menu->popup(button->mapToGlobal(QPoint(0, button->height())));
    });
    return button;
}

QToolButton *makeScrollButton(Qt::ArrowType direction, QWidget *parent)
{
    auto *button = new NavigationButton(parent);
    button->setArrowType(direction);
    button->setFixedSize(30, 24);
    button->setAutoRaise(true);
    return button;
}

QToolButton *makeCloseButton(QWidget *parent, const QString &label)
{
    auto *button = new CloseButton(parent);
    button->setObjectName(QStringLiteral("tabCloseButton"));
    button->setFixedSize(CloseSize, CloseSize);
    button->setIconSize(QSize(14, 14));
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::StrongFocus);
    button->setToolTip(label);
    button->setAccessibleName(label);
    // Override application/ancestor QSS backgrounds without native button chrome.
    button->setStyleSheet(QStringLiteral(
        "QToolButton#tabCloseButton { background: transparent; border: none; padding: 0px; margin: 0px; }"));
    return button;
}

BoundedTabBar::BoundedTabBar(QWidget *parent) : QTabBar(parent)
{
    setExpanding(false);
    setElideMode(Qt::ElideRight);
    setUsesScrollButtons(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

QSize BoundedTabBar::minimumSizeHint() const
{
    return QSize(80, sizeHint().height());
}

QSize BoundedTabBar::tabSizeHint(int index) const
{
    QSize size = QTabBar::tabSizeHint(index);
    const int arrows = 2 * style()->pixelMetric(QStyle::PM_TabBarScrollButtonWidth, nullptr, this) + 4;
    const int limit = qMax(80, qMin(MaximumWidth, width() - arrows));
    size.setWidth(qMin(limit, qMax(MinimumWidth, size.width())));
    return size;
}

QSize BoundedTabBar::minimumTabSizeHint(int index) const
{
    // Retain readable tabs and scroll instead of squeezing forty titles to zero.
    return tabSizeHint(index);
}

void BoundedTabBar::paintEvent(QPaintEvent *event)
{
    QTabBar::paintEvent(event);
    if (currentIndex() < 0)
        return;
    QPainter painter(this);
    QRect active = tabRect(currentIndex()).adjusted(6, 0, -6, -2);
    painter.fillRect(QRect(active.left(), active.bottom() - 2, active.width(), 3),
                     palette().color(QPalette::Highlight));
}

}
