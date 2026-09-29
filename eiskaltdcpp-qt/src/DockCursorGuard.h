#pragma once
#include <QObject>
#include <QMainWindow>
#include <QApplication>
#include <QHoverEvent>
#include <QTimer>

class DockCursorGuard : public QObject {
public:
    explicit DockCursorGuard(QMainWindow *window) : QObject(window), window(window) {
        qApp->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        const auto type = event->type();
        const bool layout = object == window && (type == QEvent::Resize || type == QEvent::LayoutRequest);
        const bool pointer = type == QEvent::Enter || type == QEvent::MouseMove
            || type == QEvent::HoverMove || type == QEvent::MouseButtonRelease;
        if ((!layout && !pointer) || queued || syncing || !isResizeCursor()
            || QApplication::mouseButtons() != Qt::NoButton)
            return false;
        auto *widget = pointer ? qobject_cast<QWidget *>(object) : nullptr;
        if (layout || (widget && widget->window() == window)) {
            queued = true;
            QTimer::singleShot(0, this, [this] { recheck(); });
        }
        return false;
    }

private:
    bool isResizeCursor() const {
        const auto shape = window->cursor().shape();
        return shape == Qt::SplitVCursor || shape == Qt::SplitHCursor;
    }

    void recheck() {
        queued = false;
        if (!window->isVisible() || !isResizeCursor() || QApplication::mouseButtons() != Qt::NoButton)
            return;
        syncing = true;
        const QPoint global = QCursor::pos();
        const QPoint position = window->mapFromGlobal(global);
        // Qt's dock layout retains its separator cursor across geometry changes.
        // Re-hit-test the divider instead of overriding valid child-widget cursors.
        if (window->rect().contains(position)) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 3, 0)
            QHoverEvent hover(QEvent::HoverMove, position, global, position);
#else
            QHoverEvent hover(QEvent::HoverMove, position, position);
#endif
            QApplication::sendEvent(window, &hover);
        } else {
            QEvent leave(QEvent::HoverLeave);
            QApplication::sendEvent(window, &leave);
        }
        syncing = false;
    }

    QMainWindow *window;
    bool queued = false;
    bool syncing = false;
};
