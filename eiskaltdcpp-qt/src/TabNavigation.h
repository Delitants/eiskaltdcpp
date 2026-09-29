#pragma once

#include <QHash>
#include <QIcon>
#include <QMenu>
#include <QPointer>
#include <QTabBar>

class ArenaWidget;
class QLineEdit;
class QListWidget;
class QLabel;
class QToolButton;
class QPainter;

namespace tab_navigation {

constexpr int MaximumWidth = 240;
constexpr int MinimumWidth = 96;
constexpr int CloseSize = 24;
constexpr int EndInset = 8;
constexpr int ContentGap = 6;

QString compactTitle(const QString &title);

// Menu actions carry monotonic IDs, never ArenaWidget pointers or mutable indexes.
class Registry {
public:
    quint64 add(ArenaWidget *widget);
    void remove(ArenaWidget *widget);
    quint64 idFor(ArenaWidget *widget) const;
    ArenaWidget *resolve(quint64 id) const;

private:
    struct Target {
        ArenaWidget *arena;
        QPointer<QObject> owner;
        QPointer<QWidget> widget;
    };
    QHash<quint64, Target> targets;
    quint64 nextId = 0;
};

struct Entry {
    quint64 id;
    QString title;
    QString details;
    QIcon icon;
    bool active = false;
};

class AllTabsMenu : public QMenu {
    Q_OBJECT
public:
    explicit AllTabsMenu(QWidget *parent = nullptr);
    void setEntries(const QList<Entry> &entries);

signals:
    void selected(quint64 id);

protected:
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    void filter();
    void activateCurrent();
    QList<Entry> entries;
    QLineEdit *search;
    QListWidget *list;
    QLabel *empty;
};

QToolButton *makeAllTabsButton(AllTabsMenu *menu, QWidget *parent);
QToolButton *makeScrollButton(Qt::ArrowType direction, QWidget *parent);
QToolButton *makeCloseButton(QWidget *parent, const QString &label);
void drawFocusRing(QPainter &painter, const QRect &rect, const QPalette &palette);

class BoundedTabBar : public QTabBar {
public:
    explicit BoundedTabBar(QWidget *parent = nullptr);
    QSize minimumSizeHint() const override;

protected:
    QSize tabSizeHint(int index) const override;
    QSize minimumTabSizeHint(int index) const override;
    void paintEvent(QPaintEvent *event) override;
};

}
