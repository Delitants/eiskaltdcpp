#pragma once

#include <QAction>
#include <QCoreApplication>
#include <QEvent>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>

// Visibility is transient UI state, never part of a saved proxy profile.
class PasswordRevealAction final : public QObject {
public:
    explicit PasswordRevealAction(QLineEdit *edit) : QObject(edit), field(edit)
    {
        action = new QAction(edit);
        action->setObjectName(QStringLiteral("passwordRevealAction"));
        action->setCheckable(true);
        edit->setEchoMode(QLineEdit::Password);
        edit->addAction(action, QLineEdit::TrailingPosition);
        connect(action, &QAction::toggled, this, [this](bool visible) {
            field->setEchoMode(visible ? QLineEdit::Normal : QLineEdit::Password);
            refresh();
        });
        edit->installEventFilter(this);
        refresh();
    }

    static void mask(QLineEdit *edit)
    {
        if (auto *action = edit->findChild<QAction *>(QStringLiteral("passwordRevealAction")))
            action->setChecked(false);
        edit->setEchoMode(QLineEdit::Password);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (action && (event->type() == QEvent::Hide
                       || (event->type() == QEvent::EnabledChange && !field->isEnabled())))
            mask(field);
        if (action && (event->type() == QEvent::PaletteChange
                       || event->type() == QEvent::LanguageChange))
            refresh();
        return QObject::eventFilter(watched, event);
    }

private:
    void refresh()
    {
        const bool visible = action->isChecked();
        const auto label = visible
            ? QCoreApplication::translate("PasswordRevealAction", "Hide password")
            : QCoreApplication::translate("PasswordRevealAction", "Show password");
        action->setText(label);
        action->setToolTip(label);
        QIcon icon;
        for (int scale : {1, 2, 3}) {
            QPixmap pixmap(24 * scale, 24 * scale);
            pixmap.setDevicePixelRatio(scale);
            pixmap.fill(Qt::transparent);
            QPainter painter(&pixmap);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(QPen(field->palette().color(QPalette::Text), 1.8,
                                Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            QPainterPath outline;
            outline.moveTo(2, 12);
            outline.cubicTo(7, 4, 17, 4, 22, 12);
            outline.cubicTo(17, 20, 7, 20, 2, 12);
            painter.drawPath(outline);
            painter.drawEllipse(QPointF(12, 12), 3, 3);
            if (visible)
                painter.drawLine(QPointF(4, 4), QPointF(20, 20));
            painter.end();
            icon.addPixmap(pixmap);
        }
        action->setIcon(icon);
    }

    QLineEdit *field;
    QPointer<QAction> action;
};
