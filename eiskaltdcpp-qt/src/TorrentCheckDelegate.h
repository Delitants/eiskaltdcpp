#pragma once

#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyledItemDelegate>

// Cocoa's item-view indicator can be painted at the viewport origin. Keep
// painting and hit testing in the same row coordinates on every platform.
class TorrentCheckDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QWidget *createEditor(QWidget *, const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        return nullptr;
    }
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        const bool checked = opt.checkState == Qt::Checked;
        const auto *style = opt.widget ? opt.widget->style() : QApplication::style();
        const QString text = opt.text;
        const QIcon icon = opt.icon;
        const QSize iconSize = opt.decorationSize.isValid() ? opt.decorationSize : QSize(16, 16);
        opt.features &= ~(QStyleOptionViewItem::HasCheckIndicator | QStyleOptionViewItem::HasDecoration);
        opt.text.clear();
        opt.icon = QIcon();
        painter->save();
        painter->setClipRect(opt.rect);
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);
        const bool selected = opt.state & QStyle::State_Selected;
        const auto foreground = selected ? opt.palette.highlightedText().color() : opt.palette.text().color();
        const QRect box = QStyle::visualRect(opt.direction, opt.rect,
            QRect(opt.rect.left() + 5, opt.rect.center().y() - 7, 14, 14));
        painter->setFont(opt.font);
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(QPen(foreground, 1));
        painter->setBrush(checked ? opt.palette.highlight() : opt.palette.base());
        painter->drawRoundedRect(box.adjusted(0, 0, -1, -1), 2, 2);
        if (checked) {
            painter->setPen(QPen(opt.palette.highlightedText().color(), 2));
            QPainterPath tick;
            tick.moveTo(box.left() + 3, box.top() + 7);
            tick.lineTo(box.left() + 6, box.top() + 10);
            tick.lineTo(box.left() + 11, box.top() + 3);
            painter->drawPath(tick);
        }
        int textOffset = 27;
        if (!icon.isNull()) {
            const auto iconRect = QStyle::visualRect(opt.direction, opt.rect,
                QRect(opt.rect.left() + textOffset, opt.rect.center().y() - iconSize.height() / 2,
                      iconSize.width(), iconSize.height()));
            const auto mode = !(opt.state & QStyle::State_Enabled) ? QIcon::Disabled
                : selected ? QIcon::Selected : QIcon::Normal;
            icon.paint(painter, iconRect, Qt::AlignCenter, mode);
            textOffset += iconSize.width() + 6;
        }
        painter->setPen(foreground);
        const auto textRect = QStyle::visualRect(opt.direction, opt.rect, opt.rect.adjusted(textOffset, 0, -4, 0));
        const auto alignment = QStyle::visualAlignment(opt.direction, Qt::AlignVCenter | Qt::AlignLeft);
        painter->drawText(textRect, alignment,
                         opt.fontMetrics.elidedText(text, opt.textElideMode, qMax(0, textRect.width())));
        painter->restore();
    }
    bool editorEvent(QEvent *event, QAbstractItemModel *model,
                     const QStyleOptionViewItem &option, const QModelIndex &index) override
    {
        if (!(index.flags() & Qt::ItemIsEnabled) || !(index.flags() & Qt::ItemIsUserCheckable))
            return false;
        if (event->type() == QEvent::MouseButtonRelease) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() != Qt::LeftButton || !option.rect.contains(mouse->position().toPoint()))
                return false;
        } else if (event->type() == QEvent::KeyPress) {
            const auto key = static_cast<QKeyEvent *>(event)->key();
            if (key != Qt::Key_Space && key != Qt::Key_Select) return false;
        } else {
            return false;
        }
        return model->setData(index, index.data(Qt::CheckStateRole).toInt() == Qt::Checked
                             ? Qt::Unchecked : Qt::Checked, Qt::CheckStateRole);
    }
};
