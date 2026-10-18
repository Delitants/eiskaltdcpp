#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMetaObject>
#include <QObject>
#include <QScopedValueRollback>
#include <QSpinBox>
#include <QWidget>

#include <array>

namespace ChatExtendedLayout {

// Page-owned and profile-free so the generated-UI fixture uses the same policy.
class Controller final : public QObject {
public:
    Controller(QWidget *page, QGridLayout *actions, QGridLayout *history,
               const std::array<QLabel *, 8> &labels,
               const std::array<QWidget *, 8> &editors,
               QCheckBox *autoClean, QSpinBox *days)
        : QObject(page), grids_{{actions, history}}, labels_(labels),
          editors_(editors), days_(days)
    {
        for (auto *grid : grids_) {
            grid->setContentsMargins(0, 0, 0, 0);
            grid->setHorizontalSpacing(8);
            grid->setVerticalSpacing(6);
            grid->setColumnStretch(0, 0);
            grid->setColumnStretch(1, 0);
            grid->setColumnStretch(2, 1);
            grid->addItem(new QSpacerItem(0, 0, QSizePolicy::Expanding,
                                         QSizePolicy::Minimum), 0, 2);
        }
        for (auto *label : labels_) {
            label->setAttribute(Qt::WA_LayoutUsesWidgetRect);
            label->setAlignment(Qt::AlignLeading | Qt::AlignVCenter);
            label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
            label->installEventFilter(this);
        }
        for (auto *editor : editors_) {
            // Cocoa layout-item optical margins differ for combo boxes,
            // line edits and spins. This form aligns actual widget bounds.
            editor->setAttribute(Qt::WA_LayoutUsesWidgetRect);
            for (auto *grid : grids_) {
                if (grid->indexOf(editor) >= 0)
                    grid->setAlignment(editor, Qt::AlignLeading | Qt::AlignVCenter);
            }
            if (auto *combo = qobject_cast<QComboBox *>(editor))
                combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
            editor->installEventFilter(this);
        }

        // Keep the folder row elastic, but give cleanup its own compact row.
        autoClean->setAttribute(Qt::WA_LayoutUsesWidgetRect);
        days->setAttribute(Qt::WA_LayoutUsesWidgetRect);
        auto *pictureGrid = qobject_cast<QGridLayout *>(autoClean->parentWidget()->layout());
        if (pictureGrid) {
            pictureGrid->removeWidget(autoClean);
            pictureGrid->removeWidget(days);
            auto *row = new QWidget(pictureGrid->parentWidget());
            auto *layout = new QHBoxLayout(row);
            layout->setContentsMargins(0, 0, 0, 0);
            layout->setSpacing(8);
            autoClean->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
            layout->addWidget(autoClean, 0, Qt::AlignVCenter);
            layout->addWidget(days, 0, Qt::AlignVCenter);
            layout->addStretch(1);
            pictureGrid->addWidget(row, 1, 0, 1, 3);
        }
        days_->installEventFilter(this);
        page->installEventFilter(this);
        refresh();
    }

protected:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        switch (event->type()) {
        case QEvent::FontChange:
        case QEvent::ApplicationFontChange:
        case QEvent::StyleChange:
        case QEvent::Polish:
            refresh();
            // Filters run before the widget invalidates its native hint cache.
            // Refit once more after the complete font/style event is handled.
            if (!refreshPending_) {
                refreshPending_ = true;
                QMetaObject::invokeMethod(this, [this] {
                    refreshPending_ = false;
                    refresh();
                }, Qt::QueuedConnection);
            }
            break;
        default:
            break;
        }
        return QObject::eventFilter(object, event);
    }

private:
    void refresh()
    {
        if (refreshing_)
            return;
        QScopedValueRollback<bool> guard(refreshing_, true);
        int columnWidth = 0;
        for (auto *label : labels_) {
            const QFontMetrics metrics(label->font());
            // Bound the common caption column by font units; long captions wrap
            // instead of moving the editors towards the right edge of the page.
            const int preferred = qMin(metrics.horizontalAdvance(label->text()),
                                       36 * metrics.averageCharWidth());
            columnWidth = qMax(columnWidth,
                               qMax(preferred, label->minimumSizeHint().width()));
        }
        for (auto *grid : grids_) {
            grid->setColumnMinimumWidth(0, columnWidth);
            grid->invalidate();
        }
        for (auto *editor : editors_) {
            if (auto *spin = qobject_cast<QSpinBox *>(editor))
                fitSpin(spin);
        }
        fitSpin(days_);
    }

    static void fitSpin(QSpinBox *spin)
    {
        // Native hints account for the range, prefix/suffix, buttons and style.
        // Replace the former 100px cap without changing any numeric semantics.
        spin->setFixedWidth(spin->sizeHint().expandedTo(spin->minimumSizeHint()).width());
    }

    std::array<QGridLayout *, 2> grids_;
    std::array<QLabel *, 8> labels_;
    std::array<QWidget *, 8> editors_;
    QSpinBox *days_;
    bool refreshing_ = false;
    bool refreshPending_ = false;
};

inline void applyExtendedChatLayout(QWidget *page, QGridLayout *actions,
                                    QGridLayout *history,
                                    const std::array<QLabel *, 8> &labels,
                                    const std::array<QWidget *, 8> &editors,
                                    QCheckBox *autoClean, QSpinBox *days)
{
    new Controller(page, actions, history, labels, editors, autoClean, days);
}

} // namespace ChatExtendedLayout
