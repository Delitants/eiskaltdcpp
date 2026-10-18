#pragma once

#include <QAbstractItemView>
#include <QAbstractItemModel>
#include <QApplication>
#include <QCheckBox>
#include <QGroupBox>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QRadioButton>
#include <QRegion>
#include <QStyledItemDelegate>
#include <QStyleOption>
#include <cmath>
#include <typeinfo>

// Retain Cocoa's native indicator artwork, but paint it in local coordinates
// and align the actual artwork with the label ink. No application style swap.
namespace preferences_check_indicators {
inline QString visibleText(QString text)
{
    text.replace("&&", QString(QChar(1)));
    text.remove('&');
    text.replace(QChar(1), '&');
    return text;
}

inline int textInkCenter(const QRect &rect, const QFontMetrics &metrics, const QString &text)
{
    const QString plain = visibleText(text);
    if (plain.contains('\n')) return rect.center().y();
    const QRect ink = metrics.tightBoundingRect(plain);
    return rect.center().y() + (metrics.ascent() - metrics.descent()) / 2 + ink.center().y();
}

inline void drawIndicator(QPainter *painter, const QStyle *style, QStyleOptionButton option,
                          QRect target, const QWidget *widget, bool radio = false)
{
    const qreal ratio = painter->device()->devicePixelRatioF();
    const int padding = 8;
    const QSize canvas = target.size() + QSize(padding * 2, padding * 2);
    QImage image(QSize(int(std::ceil(canvas.width() * ratio)),
                       int(std::ceil(canvas.height() * ratio))), QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(ratio);
    image.fill(Qt::transparent);
    option.rect = QRect(QPoint(padding, padding), target.size());
    option.state &= ~QStyle::State_HasFocus;
    auto render = [&] {
        QPainter local(&image);
        style->drawPrimitive(radio ? QStyle::PE_IndicatorRadioButton : QStyle::PE_IndicatorCheckBox,
                             &option, &local, widget);
    };
    auto bounds = [&] {
        QRect ink;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                if (qAlpha(image.pixel(x, y)) > 16) ink = ink.united(QRect(x, y, 1, 1));
        return ink;
    };
    const QColor background = option.palette.color(QPalette::Window);
    auto contrast = [&] {
        int maximum = 0;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                const QColor pixel = image.pixelColor(x, y);
                const int alpha = pixel.alpha();
                maximum = qMax(maximum, qMax(qAbs(pixel.red() - background.red()),
                    qMax(qAbs(pixel.green() - background.green()),
                         qAbs(pixel.blue() - background.blue()))) * alpha / 255);
            }
        }
        return maximum;
    };
    const bool disabled = !(option.state & QStyle::State_Enabled);
    render();
    QRect ink = bounds();
    bool disabledFallback = disabled && contrast() < 24;
    if (disabledFallback) {
        // Some Cocoa disabled glyphs composite almost invisibly. Reuse the
        // enabled native on/off/mixed artwork, dimmed without changing state.
        option.state |= QStyle::State_Enabled;
        image.fill(Qt::transparent);
        render();
        ink = bounds();
    }
    if (contrast() < 24 && !(option.state & (QStyle::State_On | QStyle::State_NoChange))) {
        // Unchecked Cocoa artwork can itself be near-transparent white. Boost
        // its existing alpha silhouette, retaining native edges and geometry.
        int maximumAlpha = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                maximumAlpha = qMax(maximumAlpha, qAlpha(image.pixel(x, y)));
        QColor foreground = option.palette.color(disabled ? QPalette::Disabled : QPalette::Active,
                                                  QPalette::WindowText);
        const auto separation = [&](QColor color) {
            return qMax(qAbs(color.red() - background.red()),
                qMax(qAbs(color.green() - background.green()), qAbs(color.blue() - background.blue())));
        };
        if (separation(foreground) < 80)
            foreground = background.lightness() < 128 ? QColor(Qt::white) : QColor(Qt::black);
        if (maximumAlpha > 0) {
            for (int y = 0; y < image.height(); ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    foreground.setAlpha(qAlpha(image.pixel(x, y)) * (disabled ? 96 : 128) / maximumAlpha);
                    image.setPixelColor(x, y, foreground);
                }
            }
            ink = bounds();
            disabledFallback = false;
        }
    }
    if (ink.isEmpty()) return;
    const QSizeF size(ink.width() / ratio, ink.height() / ratio);
    painter->save();
    if (disabledFallback) painter->setOpacity(painter->opacity() * 0.55);
    painter->drawImage(QRectF(QPointF(target.center()) - QPointF((size.width() - 1) / 2,
                                                               (size.height() - 1) / 2), size), image, ink);
    painter->restore();
}

inline void drawLabel(QPainter *painter, const QStyle *style, const QStyleOptionButton &option,
                      const QRect &rect, const QWidget *widget)
{
    int flags = QStyle::visualAlignment(option.direction, Qt::AlignLeft | Qt::AlignVCenter)
        | Qt::TextShowMnemonic;
    if (!style->styleHint(QStyle::SH_UnderlineShortcut, &option, widget)) flags |= Qt::TextHideMnemonic;
    style->drawItemText(painter, rect, flags, option.palette, option.state & QStyle::State_Enabled,
                        option.text, QPalette::WindowText);
    if (option.state & QStyle::State_HasFocus) {
        QStyleOptionFocusRect focus;
        focus.QStyleOption::operator=(option);
        focus.rect = rect.adjusted(-1, 1, 1, -1);
        focus.backgroundColor = option.palette.window().color();
        style->drawPrimitive(QStyle::PE_FrameFocusRect, &focus, painter, widget);
    }
}

class RowDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    static QRect indicatorRect(const QStyleOptionViewItem &option)
    {
        const auto *style = option.widget ? option.widget->style() : QApplication::style();
        const int width = style->pixelMetric(QStyle::PM_IndicatorWidth, &option, option.widget);
        const int height = style->pixelMetric(QStyle::PM_IndicatorHeight, &option, option.widget);
        return QStyle::visualRect(option.direction, option.rect,
            QRect(option.rect.left() + 3, option.rect.center().y() - height / 2, width, height));
    }
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        if (!index.data(Qt::CheckStateRole).isValid()) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        const auto *style = opt.widget ? opt.widget->style() : QApplication::style();
        QRect box = indicatorRect(opt);
        const QRect nativeText = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, opt.widget);
        QStyleOptionViewItem content(opt);
        content.features &= ~QStyleOptionViewItem::HasCheckIndicator;
        const QRect bareText = style->subElementRect(QStyle::SE_ItemViewItemText, &content, content.widget);
        if (opt.direction == Qt::LeftToRight)
            content.rect.setLeft(content.rect.left() + qMax(0, nativeText.left() - bareText.left()));
        else
            content.rect.setRight(content.rect.right() - qMax(0, bareText.right() - nativeText.right()));
        // Keep Qt's normal role-aware content rendering, but remove only the
        // faulty indicator primitive. Preserve its reserved text/icon spacing.
        painter->save();
        painter->setClipRect(opt.rect, Qt::IntersectClip);
        QStyleOptionViewItem background(opt);
        background.features &= ~(QStyleOptionViewItem::HasCheckIndicator | QStyleOptionViewItem::HasDecoration
                                  | QStyleOptionViewItem::HasDisplay);
        background.text.clear();
        background.icon = QIcon();
        style->drawControl(QStyle::CE_ItemViewItem, &background, painter, opt.widget);
        style->drawControl(QStyle::CE_ItemViewItem, &content, painter, opt.widget);
        painter->restore();
        QStyleOptionButton check;
        if (opt.widget) check.initFrom(opt.widget);
        check.palette = opt.palette;
        check.direction = opt.direction;
        check.state = opt.state & ~(QStyle::State_On | QStyle::State_Off | QStyle::State_NoChange);
        check.state |= opt.checkState == Qt::PartiallyChecked ? QStyle::State_NoChange
            : opt.checkState == Qt::Checked ? QStyle::State_On : QStyle::State_Off;
        painter->save();
        painter->setClipRect(opt.rect, Qt::IntersectClip);
        drawIndicator(painter, style, check, box, opt.widget);
        painter->restore();
    }
    bool editorEvent(QEvent *event, QAbstractItemModel *model,
                     const QStyleOptionViewItem &option, const QModelIndex &index) override
    {
        if (!index.data(Qt::CheckStateRole).isValid())
            return QStyledItemDelegate::editorEvent(event, model, option, index);
        if (!(index.flags() & Qt::ItemIsEnabled) || !(index.flags() & Qt::ItemIsUserCheckable)
            || !(option.state & QStyle::State_Enabled)) return false;
        if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease
            || event->type() == QEvent::MouseButtonDblClick) {
            const auto *mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() != Qt::LeftButton
                || !indicatorRect(option).adjusted(-2, -2, 2, 2).contains(mouse->position().toPoint())) return false;
            if (event->type() != QEvent::MouseButtonRelease) return true;
        } else if (event->type() == QEvent::KeyPress) {
            const int key = static_cast<QKeyEvent*>(event)->key();
            if (key != Qt::Key_Space && key != Qt::Key_Select) return false;
        } else return false;
        const int state = index.data(Qt::CheckStateRole).toInt();
        return model->setData(index, index.flags() & Qt::ItemIsUserTristate ? (state + 1) % 3
            : state == Qt::Checked ? Qt::Unchecked : Qt::Checked, Qt::CheckStateRole);
    }
};

inline void attachDefaultDelegate(QWidget *widget)
{
    auto *view = qobject_cast<QAbstractItemView*>(widget);
    auto *delegate = view ? view->itemDelegate() : nullptr;
    if (delegate && typeid(*delegate) == typeid(QStyledItemDelegate)) {
        view->setItemDelegate(new RowDelegate(view));
        view->viewport()->update();
    }
}

class PaintFilter final : public QObject {
public:
    explicit PaintFilter(QObject *parent, bool applicationWide = false)
        : QObject(parent), applicationWide(applicationWide) { }
protected:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (applicationWide && event->type() == QEvent::Polish)
            attachDefaultDelegate(qobject_cast<QWidget*>(object));
        // Custom controls may own text layout and their complete paint path.
        if (object->property("eiskaltPreserveCustomCheckPainting").toBool())
            return QObject::eventFilter(object, event);
        auto *button = qobject_cast<QAbstractButton*>(object);
        auto *group = qobject_cast<QGroupBox*>(object);
        const bool radio = qobject_cast<QRadioButton*>(object);
        if (event->type() == QEvent::Paint && button
            && (radio || qobject_cast<QCheckBox*>(object))) {
            QStyleOptionButton option;
            option.initFrom(button);
            option.text = button->text();
            option.state &= ~(QStyle::State_On | QStyle::State_Off | QStyle::State_NoChange);
            const auto *checkbox = qobject_cast<QCheckBox*>(button);
            option.state |= checkbox && checkbox->checkState() == Qt::PartiallyChecked ? QStyle::State_NoChange
                : button->isChecked() ? QStyle::State_On : QStyle::State_Off;
            if (button->isDown()) option.state |= QStyle::State_Sunken;
            const auto *style = button->style();
            QRect label = style->subElementRect(radio ? QStyle::SE_RadioButtonContents
                : QStyle::SE_CheckBoxContents, &option, button);
            QRect box = style->subElementRect(radio ? QStyle::SE_RadioButtonIndicator
                : QStyle::SE_CheckBoxIndicator, &option, button);
            QPainter painter(button);
            painter.setFont(button->font());
            style->drawPrimitive(QStyle::PE_Widget, &option, &painter, button);
            if (!button->icon().isNull()) {
                const QRect iconRect = QStyle::alignedRect(option.direction, Qt::AlignLeading | Qt::AlignVCenter,
                    button->iconSize(), label);
                button->icon().paint(&painter, iconRect, Qt::AlignCenter,
                    button->isEnabled() ? QIcon::Normal : QIcon::Disabled,
                    button->isChecked() ? QIcon::On : QIcon::Off);
                if (option.direction == Qt::LeftToRight) label.setLeft(iconRect.right() + 5);
                else label.setRight(iconRect.left() - 5);
            }
            box.moveCenter(QPoint(box.center().x(), textInkCenter(label, option.fontMetrics, option.text)));
            drawLabel(&painter, style, option, label, button);
            drawIndicator(&painter, style, option, box, button, radio);
            return true;
        }
        if (!group || !group->isCheckable()) return QObject::eventFilter(object, event);
        if (event->type() != QEvent::Paint && event->type() != QEvent::MouseButtonPress
            && event->type() != QEvent::MouseButtonRelease && event->type() != QEvent::MouseButtonDblClick)
            return QObject::eventFilter(object, event);
        QStyleOptionGroupBox option;
        option.initFrom(group);
        option.text = group->title();
        option.textAlignment = group->alignment();
        option.lineWidth = 1;
        if (group->isFlat()) option.features |= QStyleOptionFrame::Flat;
        option.subControls = QStyle::SC_GroupBoxFrame | QStyle::SC_GroupBoxLabel | QStyle::SC_GroupBoxCheckBox;
        option.state |= group->isChecked() ? QStyle::State_On : QStyle::State_Off;
        const auto *style = group->style();
        const QRect label = style->subControlRect(QStyle::CC_GroupBox, &option, QStyle::SC_GroupBoxLabel, group);
        QRect box = style->subControlRect(QStyle::CC_GroupBox, &option, QStyle::SC_GroupBoxCheckBox, group);
        box.moveCenter(QPoint(box.center().x(), textInkCenter(label, option.fontMetrics, option.text)));
        if (event->type() == QEvent::Paint) {
            QPainter painter(group);
            painter.setFont(group->font());
            painter.save();
            QRegion frameClip(group->rect());
            frameClip -= label.united(box).adjusted(-2, -2, 2, 2);
            painter.setClipRegion(frameClip);
            option.subControls = QStyle::SC_GroupBoxFrame;
            style->drawComplexControl(QStyle::CC_GroupBox, &option, &painter, group);
            painter.restore();
            QStyleOptionButton check;
            check.QStyleOption::operator=(option);
            check.text = group->title();
            drawLabel(&painter, style, check, label, group);
            drawIndicator(&painter, style, check, box, group);
            return true;
        }
        if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease
            || event->type() == QEvent::MouseButtonDblClick) {
            const auto *mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() != Qt::LeftButton || !group->isEnabled()) return false;
            const bool hit = label.united(box).contains(mouse->position().toPoint());
            if (event->type() == QEvent::MouseButtonPress) {
                pressedGroup = hit ? group : nullptr;
                if (hit && group->focusPolicy() != Qt::NoFocus) group->setFocus(Qt::MouseFocusReason);
            } else if (event->type() == QEvent::MouseButtonRelease) {
                const bool activate = pressedGroup == group && hit;
                pressedGroup.clear();
                if (activate) {
                    group->setChecked(!group->isChecked());
                    emit group->clicked(group->isChecked());
                }
            }
            return true;
        }
        return QObject::eventFilter(object, event);
    }
private:
    const bool applicationWide;
    QPointer<QGroupBox> pressedGroup;
};

// App ownership intentionally outlives QtContext and its core managers. The
// filter captures no context, profile, model, or service pointers.
inline void installApplication(QApplication *application)
{
#ifdef Q_OS_MAC
    if (!application || application->property("eiskaltCheckIndicatorFilterInstalled").toBool()) return;
    auto *filter = new PaintFilter(application, true);
    filter->setObjectName("eiskaltApplicationCheckIndicatorFilter");
    application->installEventFilter(filter);
    application->setProperty("eiskaltCheckIndicatorFilterInstalled", true);
    for (auto *widget : QApplication::allWidgets()) {
        attachDefaultDelegate(widget);
        widget->update();
    }
#else
    Q_UNUSED(application);
#endif
}

// Call after the Preferences pages have been constructed. Does not replace
// specialized delegates, per-row/per-column delegates, or application styles.
inline void install(QWidget *root)
{
#ifdef Q_OS_MAC
    if (!root) return;
    if (qApp && qApp->property("eiskaltCheckIndicatorFilterInstalled").toBool()) return;
    auto *filter = new PaintFilter(root);
    auto widgets = root->findChildren<QWidget*>();
    widgets.prepend(root);
    for (auto *widget : widgets) {
        if (!widget->property("preferencesCheckPaintInstalled").toBool()
            && (qobject_cast<QCheckBox*>(widget) || qobject_cast<QRadioButton*>(widget)
                || qobject_cast<QGroupBox*>(widget))) {
            widget->installEventFilter(filter);
            widget->setProperty("preferencesCheckPaintInstalled", true);
            widget->update();
        }
        attachDefaultDelegate(widget);
    }
#else
    Q_UNUSED(root);
#endif
}
} // namespace preferences_check_indicators
