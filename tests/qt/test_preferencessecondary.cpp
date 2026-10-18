#include "dcpp/stdinc.h"
#include "ApplicationQuitTestFixture.h"
#include "Settings.h"
#include "SettingsUC.h"
#include "dcpp/FavoriteManager.h"
#include "dcpp/UserCommand.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QStyle>
#include <QTreeView>

namespace {
template<class T> T *secondaryChild(QObject *root, const char *name)
{
    auto *widget = root->findChild<T*>(QString::fromLatin1(name));
    REQUIRE(widget);
    return widget;
}

void secondaryLayout(QWidget &widget, int width)
{
    widget.resize(width, 900);
    widget.show();
    if (widget.layout()) widget.layout()->activate();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    QApplication::processEvents();
}

int secondaryLeft(QWidget *widget, QWidget *relativeTo)
{
    return widget->mapTo(relativeTo, QPoint()).x();
}

void secondaryCommands(dcpp::FavoriteManager &manager, int count)
{
    // This manager belongs to the temporary isolated profile, never the user.
    for (const auto &command : manager.getUserCommands())
        manager.removeUserCommand(command.getId());
    for (int row = 0; row < count; ++row)
        manager.addUserCommand(dcpp::UserCommand::TYPE_RAW, dcpp::UserCommand::CONTEXT_HUB, 0,
            "Secondary regression " + std::to_string(row), "fixture-command", "", "adc://fixture.invalid");
}
}

TEST_CASE("Main preference field columns stay adjacent to content-sized labels when widened",
          "[qt][preferences-secondary][preferences-main-columns]")
{
    IsolatedMainWindow fixture;
    ::Settings dialog;
    dialog.navigate(::Settings::Page::Personal);
    auto *page = secondaryChild<QWidget>(&dialog, "UISettingsPersonal");
    auto *speed = secondaryChild<QComboBox>(page, "comboBox_SPEED");
    auto *nmdc = secondaryChild<QComboBox>(page, "comboBox_NMDC_SPEED");
    auto *encoding = secondaryChild<QComboBox>(page, "comboBox_ENC");
    QList<QLabel*> labels{secondaryChild<QLabel>(page, "label_5"),
        secondaryChild<QLabel>(page, "label_NMDC_SPEED"), secondaryChild<QLabel>(page, "label_6")};
    QLabel *tagLabel = nullptr;
    QComboBox *tag = nullptr;
    // Client tag controls currently have no object names. Locate the actual
    // dynamic row without requiring a new production test hook.
    for (auto *label : speed->parentWidget()->findChildren<QLabel*>())
        if (label->text() == QStringLiteral("Client tag")) tagLabel = label;
    for (auto *combo : speed->parentWidget()->findChildren<QComboBox*>())
        if (combo != speed && combo != nmdc && combo != encoding) tag = combo;
    REQUIRE(tagLabel);
    REQUIRE(tag);
    labels.append(tagLabel);
    const QList<QComboBox*> fields{speed, nmdc, encoding, tag};
    const int speedIndex = speed->currentIndex();
    const int encodingIndex = encoding->currentIndex();
    const QVariant tagData = tag->currentData();
    for (bool translated : {false, true}) {
        if (translated) labels[2]->setText("Default encoding for hub connections and private messages");
        for (int width : {980, 1400}) {
            CAPTURE(translated, width);
            secondaryLayout(dialog, width);
            int longest = 0;
            for (auto *label : labels)
                longest = qMax(longest, label->fontMetrics().horizontalAdvance(label->text()));
            const int labelLeft = secondaryLeft(labels.first(), page);
            const int fieldLeft = secondaryLeft(speed, page);
            CHECK(fieldLeft - labelLeft <= longest + 32);
            for (int row = 0; row < fields.size(); ++row) {
                CAPTURE(row);
                CHECK(qAbs(secondaryLeft(fields[row], page) - fieldLeft) <= 2);
                CHECK(labels[row]->geometry().width() <= longest + 24);
                CHECK(fields[row]->isVisible());
            }
            CHECK(speed->currentIndex() == speedIndex);
            CHECK(encoding->currentIndex() == encodingIndex);
            CHECK(tag->currentData() == tagData);
        }
    }
    dialog.reject();
}

TEST_CASE("History numeric fields and clear actions stay beside their labels instead of the far edge",
          "[qt][preferences-secondary][preferences-history-rows]")
{
    IsolatedMainWindow fixture;
    ::Settings dialog;
    dialog.navigate(::Settings::Page::History);
    auto *page = secondaryChild<QWidget>(&dialog, "UISettingsHistory");
    const QList<QLabel*> labels{secondaryChild<QLabel>(page, "label"), secondaryChild<QLabel>(page, "label_2")};
    const QList<QSpinBox*> fields{secondaryChild<QSpinBox>(page, "spinBox_SearchHistory"),
        secondaryChild<QSpinBox>(page, "spinBox_DirectoriesHistory")};
    const QList<QCheckBox*> checks{secondaryChild<QCheckBox>(page, "checkBox_SearchHistory"),
        secondaryChild<QCheckBox>(page, "checkBox_DirectoriesHistory")};
    const QList<QPushButton*> clear{secondaryChild<QPushButton>(page, "pushButton_ClearSearchHistory"),
        secondaryChild<QPushButton>(page, "pushButton_ClearDirectoriesHistory")};
    for (int width : {980, 1400}) {
        CAPTURE(width);
        secondaryLayout(dialog, width);
        const int origin = secondaryLeft(checks.first(), page);
        int longestLabel = 0, longestCheck = 0;
        for (auto *label : labels)
            longestLabel = qMax(longestLabel, label->fontMetrics().horizontalAdvance(label->text()));
        for (auto *check : checks) {
            const int indicator = check->style()->pixelMetric(QStyle::PM_IndicatorWidth, nullptr, check);
            const int spacing = check->style()->pixelMetric(QStyle::PM_CheckBoxLabelSpacing, nullptr, check);
            longestCheck = qMax(longestCheck, check->fontMetrics().horizontalAdvance(check->text()) + indicator + spacing);
        }
        for (int row = 0; row < fields.size(); ++row) {
            CAPTURE(row);
            CHECK_FALSE(labels[row]->alignment().testFlag(Qt::AlignRight));
            CHECK(qAbs(secondaryLeft(labels[row], page) - origin) <= 16);
            CHECK(secondaryLeft(fields[row], page) - origin <= longestLabel + 32);
            CHECK(fields[row]->width() <= qMax(110, fields[row]->sizeHint().width()) + 16);
            CHECK(secondaryLeft(clear[row], page) - origin <= longestCheck + 32);
            CHECK(secondaryLeft(clear[row], page) >= secondaryLeft(checks[row], page)
                + checks[row]->fontMetrics().horizontalAdvance(checks[row]->text()));
            CHECK(qAbs(clear[row]->mapTo(page, clear[row]->rect().center()).y()
                - checks[row]->mapTo(page, checks[row]->rect().center()).y()) <= 4);
        }
        CHECK(qAbs(secondaryLeft(fields[0], page) - secondaryLeft(fields[1], page)) <= 2);
        CHECK(qAbs(secondaryLeft(clear[0], page) - secondaryLeft(clear[1], page)) <= 2);
    }
    dialog.reject();
}

TEST_CASE("User Commands actions are disabled for empty selection even with a stale current index",
          "[qt][preferences-secondary][preferences-uc-selection]")
{
    IsolatedMainWindow fixture;
    auto &manager = *fixture.core->getFavoriteManager();
    for (int rows : {0, 3}) {
        CAPTURE(rows);
        secondaryCommands(manager, rows);
        SettingsUC page;
        auto *tree = secondaryChild<QTreeView>(&page, "treeView");
        REQUIRE(tree->model()->rowCount() == rows);
        secondaryLayout(page, 980);
        if (rows) tree->setCurrentIndex(tree->model()->index(1, 0));
        tree->selectionModel()->clearSelection();
        QApplication::processEvents();
        REQUIRE(tree->selectionModel()->selectedRows().isEmpty());
        QSignalSpy changed(&page, &SettingsUC::changeUC);
        QSignalSpy removed(&page, &SettingsUC::remUC);
        QSignalSpy up(&page, &SettingsUC::upUC);
        QSignalSpy down(&page, &SettingsUC::downUC);
        CHECK(secondaryChild<QPushButton>(&page, "pushButton_ADD")->isEnabled());
        for (const char *name : {"pushButton_CH", "pushButton_REM", "pushButton_UP", "pushButton_DOWN"}) {
            CAPTURE(name);
            auto *button = secondaryChild<QPushButton>(&page, name);
            CHECK_FALSE(button->isEnabled());
            button->click();
        }
        CHECK(changed.count() == 0);
        CHECK(removed.count() == 0);
        CHECK(up.count() == 0);
        CHECK(down.count() == 0);
        CHECK(tree->model()->rowCount() == rows);
    }
}

TEST_CASE("User Commands selection and row boundaries update actions after moves and removal",
          "[qt][preferences-secondary][preferences-uc-selection]")
{
    IsolatedMainWindow fixture;
    secondaryCommands(*fixture.core->getFavoriteManager(), 3);
    SettingsUC page;
    auto *tree = secondaryChild<QTreeView>(&page, "treeView");
    auto *change = secondaryChild<QPushButton>(&page, "pushButton_CH");
    auto *remove = secondaryChild<QPushButton>(&page, "pushButton_REM");
    auto *up = secondaryChild<QPushButton>(&page, "pushButton_UP");
    auto *down = secondaryChild<QPushButton>(&page, "pushButton_DOWN");
    secondaryLayout(page, 980);
    const auto select = [&](int row) {
        const QModelIndex index = tree->model()->index(row, 0);
        REQUIRE(index.isValid());
        tree->selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        QApplication::processEvents();
    };
    for (int row : {0, 1, 2}) {
        CAPTURE(row);
        select(row);
        CHECK(change->isEnabled());
        CHECK(remove->isEnabled());
        CHECK(up->isEnabled() == (row > 0));
        CHECK(down->isEnabled() == (row < 2));
    }
    const QString selected = tree->model()->index(2, 0).data().toString();
    up->click();
    QApplication::processEvents();
    REQUIRE(tree->selectionModel()->selectedRows().size() == 1);
    CHECK(tree->selectionModel()->selectedRows().first().data().toString() == selected);
    CHECK(up->isEnabled());
    CHECK(down->isEnabled());
    select(0);
    remove->click();
    QApplication::processEvents();
    REQUIRE(tree->model()->rowCount() == 2);
    tree->selectionModel()->clearSelection();
    CHECK_FALSE(change->isEnabled());
    CHECK_FALSE(remove->isEnabled());
    CHECK_FALSE(up->isEnabled());
    CHECK_FALSE(down->isEnabled());
}

TEST_CASE("Queue priority and drop numeric columns remain compact through resize and Cancel",
          "[qt][preferences-secondary][preferences-queue-columns]")
{
    IsolatedMainWindow fixture;
    using SM = dcpp::SettingsManager;
    struct NumericRow {
        const char *group;
        const char *label;
        const char *field;
        int pair;
        SM::IntSetting setting;
        int value;
        int maximum;
    };
    const NumericRow rows[] = {
        {"groupBox_4", "label_7", "spinBox_HTPMAX", 0, SM::PRIO_HIGHEST_SIZE, 64, 10000},
        {"groupBox_4", "label_8", "spinBox_NPMAX", 0, SM::PRIO_NORMAL_SIZE, 256, 10000},
        {"groupBox_4", "label_9", "spinBox_HPMAX", 1, SM::PRIO_HIGH_SIZE, 128, 10000},
        {"groupBox_4", "label_10", "spinBox_LPMAX", 1, SM::PRIO_LOW_SIZE, 512, 10000},
        {"groupBox_5", "label_11", "spinBox_DROPSB", 0, SM::AUTODROP_SPEED, 512, 1000000},
        {"groupBox_5", "label_13", "spinBox_MINELAPSED", 0, SM::AUTODROP_ELAPSED, 23, 1000},
        {"groupBox_5", "label_15", "spinBox_MINSRCONLINE", 0, SM::AUTODROP_MINSOURCES, 4, 1000},
        {"groupBox_5", "label_12", "spinBox_CHECKEVERY", 1, SM::AUTODROP_INTERVAL, 17, 1000},
        {"groupBox_5", "label_14", "spinBox_MAXINACT", 1, SM::AUTODROP_INACTIVITY, 19, 1000},
        {"groupBox_5", "label_16", "spinBox_MINFSZ", 1, SM::AUTODROP_FILESIZE, 128, 100000}
    };
    auto *settings = fixture.core->getSettingsManager();
    for (const auto &row : rows) settings->set(row.setting, row.value);
    for (int fontGrowth : {0, 3}) {
        CAPTURE(fontGrowth);
        ::Settings dialog;
        dialog.navigate(::Settings::Page::Downloads, 1);
        auto *page = secondaryChild<QWidget>(&dialog, "UISettingsDownloads");
        QFont font = dialog.font();
        if (fontGrowth) {
            if (font.pixelSize() > 0) font.setPixelSize(font.pixelSize() + fontGrowth);
            else font.setPointSizeF(qMax(1.0, font.pointSizeF()) + fontGrowth);
            dialog.setFont(font);
        }
        QList<QPoint> labelOrigins, fieldOrigins;
        for (int width : {980, 1400}) {
            CAPTURE(width);
            secondaryLayout(dialog, width);
            int rowIndex = 0;
            for (const auto &row : rows) {
                CAPTURE(row.group, row.label, row.field);
                auto *group = secondaryChild<QGroupBox>(page, row.group);
                auto *label = secondaryChild<QLabel>(group, row.label);
                auto *spin = secondaryChild<QSpinBox>(group, row.field);
                auto *grid = qobject_cast<QGridLayout*>(group->layout());
                REQUIRE(grid);
                int longestLabel = 0;
                for (const auto &peer : rows) {
                    if (QString::fromLatin1(peer.group) != QLatin1String(row.group) || peer.pair != row.pair) continue;
                    auto *caption = secondaryChild<QLabel>(group, peer.label);
                    longestLabel = qMax(longestLabel, caption->fontMetrics().horizontalAdvance(caption->text()));
                }
                const QPoint labelOrigin = label->mapTo(group, QPoint());
                const QPoint fieldOrigin = spin->mapTo(group, QPoint());
                const QRect labelRect(labelOrigin, label->size());
                const QRect fieldRect(fieldOrigin, spin->size());
                INFO("Queue width=" << width << " actual=" << dialog.width()
                     << " font=" << label->font().toString().toStdString()
                     << " label=" << labelRect.x() << ',' << labelRect.y() << ' '
                     << labelRect.width() << 'x' << labelRect.height()
                     << " field=" << fieldRect.x() << ',' << fieldRect.y() << ' '
                     << fieldRect.width() << 'x' << fieldRect.height()
                     << " maximum caption advance=" << longestLabel);
                CHECK(label->isVisible());
                CHECK(spin->isVisible());
                CHECK(label->width() <= longestLabel + 8);
                CHECK(fieldOrigin.x() - labelOrigin.x() <= longestLabel + 24);
                CHECK(fieldRect.left() > labelRect.right());
                CHECK(qAbs(labelRect.center().y() - fieldRect.center().y()) <= 4);
                CHECK(spin->minimum() == 0);
                CHECK(spin->maximum() == row.maximum);
                CHECK(spin->value() == row.value);
                if (width == 980) {
                    labelOrigins.append(labelOrigin);
                    fieldOrigins.append(fieldOrigin);
                } else {
                    CHECK(qAbs(labelOrigin.x() - labelOrigins[rowIndex].x()) <= 2);
                    CHECK(qAbs(labelOrigin.y() - labelOrigins[rowIndex].y()) <= 2);
                    CHECK(qAbs(fieldOrigin.x() - fieldOrigins[rowIndex].x()) <= 2);
                    CHECK(qAbs(fieldOrigin.y() - fieldOrigins[rowIndex].y()) <= 2);
                }
                ++rowIndex;
            }
            for (const char *name : {"groupBox_4", "groupBox_5"}) {
                auto *group = secondaryChild<QGroupBox>(page, name);
                auto *grid = qobject_cast<QGridLayout*>(group->layout());
                REQUIRE(grid);
                auto *leftLabelItem = grid->itemAtPosition(0, 0);
                auto *leftFieldItem = grid->itemAtPosition(0, 1);
                auto *rightLabelItem = grid->itemAtPosition(0, 3);
                auto *rightFieldItem = grid->itemAtPosition(0, 4);
                REQUIRE(leftLabelItem); REQUIRE(leftFieldItem); REQUIRE(rightLabelItem); REQUIRE(rightFieldItem);
                auto *leftLabel = qobject_cast<QLabel*>(leftLabelItem->widget());
                auto *leftField = qobject_cast<QSpinBox*>(leftFieldItem->widget());
                auto *rightLabel = qobject_cast<QLabel*>(rightLabelItem->widget());
                auto *rightField = qobject_cast<QSpinBox*>(rightFieldItem->widget());
                REQUIRE(leftLabel); REQUIRE(leftField); REQUIRE(rightLabel); REQUIRE(rightField);
                CHECK(secondaryLeft(leftLabel, group) - grid->contentsRect().left() <= 12);
                CHECK(leftLabel->mapTo(group, QPoint()).y() - grid->contentsRect().top() <= 12);
                CHECK(qAbs(leftField->mapTo(group, leftField->rect().center()).y()
                    - rightField->mapTo(group, rightField->rect().center()).y()) <= 2);
                CHECK(secondaryLeft(rightLabel, group)
                    - (secondaryLeft(leftField, group) + leftField->width()) <= 24);
            }
        }
        for (const auto &row : rows) {
            auto *spin = secondaryChild<QSpinBox>(page, row.field);
            spin->setValue(row.value + 1);
            CHECK(spin->minimum() == 0);
            CHECK(spin->maximum() == row.maximum);
        }
        secondaryChild<QDialogButtonBox>(&dialog, "buttonBox")->button(QDialogButtonBox::Cancel)->click();
        CHECK(dialog.result() == QDialog::Rejected);
        for (const auto &row : rows) CHECK(settings->get(row.setting) == row.value);
    }
}
