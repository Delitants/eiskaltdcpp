#pragma once

#include <QEvent>
#include <QList>
#include <QObject>
#include <QPointer>

// Retain prepared owners without pumping their queued work or deletion.
class RetainedGuiInputFence final : public QObject {
public:
    RetainedGuiInputFence(const QList<QObject*>& owners, QObject* parent) : QObject(parent) {
        for (auto* owner : owners) roots.append(owner);
    }
protected:
    bool eventFilter(QObject* receiver, QEvent* event) override {
        switch (event->type()) {
        case QEvent::MetaCall:
        case QEvent::FutureCallOut:
        case QEvent::DeferredDelete:
        case QEvent::Timer:
            for (auto* owner = receiver; owner; owner = owner->parent())
                for (const auto& root : roots)
                    if (root && root == owner) return true;
            break;
        default: break;
        }
        return false;
    }
private:
    QList<QPointer<QObject>> roots;
};
