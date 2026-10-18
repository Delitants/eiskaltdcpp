/* Profile-free Search sidebar regression. No SearchFrame, QtContext, hub
 * model, query dispatch or network engine is constructed by this fixture.
 */
#include <catch2/catch_test_macros.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QFont>
#include <QGridLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QPalette>
#include <QStyle>
#include <QStyleOptionButton>
#include <QStyleOptionGroupBox>
#include <QWidget>

#include "MacInputStyle.h"
#include "LineEdit.h"

// Promoted LineEdit requires qtCtx() even before any user interaction. Only
// substitute that type; retain the source grid, policies and native widgets.
// Both generated class names are local variants to avoid cross-TU ODR clashes.
#define LineEdit QLineEdit
#define Ui_SearchFrame ProfileFreeUi_SearchFrame
#define SearchFrame ProfileFreeSearchFrame
#include "ui_UISearchFrame.h"
#undef SearchFrame
#undef Ui_SearchFrame
#undef LineEdit

namespace {

struct SearchLayoutFixture {
    QWidget host;
    Ui::ProfileFreeSearchFrame ui;

    SearchLayoutFixture(bool dark, bool enlarged)
    {
        ui.setupUi(&host);
        REQUIRE(ui.frame_2 != nullptr);
        REQUIRE(ui.gridLayout_3 != nullptr);
        REQUIRE(ui.checkBox_FILTERSLOTS != nullptr);
        REQUIRE(ui.groupBox_2 != nullptr);
        REQUIRE(ui.comboBox_SHARED != nullptr);

        QPalette palette = host.palette();
        palette.setColor(QPalette::Window, dark ? QColor(35, 35, 35) : QColor(245, 245, 245));
        palette.setColor(QPalette::Base, dark ? QColor(24, 24, 24) : QColor(255, 255, 255));
        palette.setColor(QPalette::Text, dark ? QColor(240, 240, 240) : QColor(20, 20, 20));
        palette.setColor(QPalette::WindowText, palette.color(QPalette::Text));
        host.setPalette(palette);
        // Apply the real shared rules locally, never mutate QApplication state.
        host.setStyleSheet(mac_input_style::macInputContrastStyle(palette));
        if (enlarged) {
            QFont font = host.font();
            font.setPointSizeF(font.pointSizeF() > 0 ? font.pointSizeF() * 1.5 : 18);
            host.setFont(font);
        }
        host.ensurePolished();
        for (auto *widget : host.findChildren<QWidget *>())
            widget->ensurePolished();
    }

    void arrange(int sidebarWidth)
    {
        host.resize(1200, 800);
        REQUIRE(host.layout() != nullptr);
        host.layout()->activate();
        // Test the source sidebar at explicit widths without exposing a window
        // or relying on a hidden QSplitter receiving window-system events.
        ui.frame_2->resize(sidebarWidth, 650);
        ui.gridLayout_3->invalidate();
        ui.gridLayout_3->setGeometry(ui.frame_2->rect());
        ui.gridLayout_3->activate();
        REQUIRE(ui.groupBox_2->layout() != nullptr);
        ui.groupBox_2->layout()->activate();
    }

    QRect mapped(QWidget *widget, const QRect &rect) const
    {
        return QRect(widget->mapTo(ui.frame_2, rect.topLeft()), rect.size());
    }

    QRect checkboxPaintBounds() const
    {
        QStyleOptionButton option;
        option.initFrom(ui.checkBox_FILTERSLOTS);
        option.text = ui.checkBox_FILTERSLOTS->text();
        auto *style = ui.checkBox_FILTERSLOTS->style();
        const QRect indicator = style->subElementRect(QStyle::SE_CheckBoxIndicator,
                                                      &option, ui.checkBox_FILTERSLOTS);
        const QRect caption = style->subElementRect(QStyle::SE_CheckBoxContents,
                                                    &option, ui.checkBox_FILTERSLOTS);
        return mapped(ui.checkBox_FILTERSLOTS, indicator.united(caption));
    }

    QRect sharedTitleBounds() const
    {
        QStyleOptionGroupBox option;
        option.initFrom(ui.groupBox_2);
        option.text = ui.groupBox_2->title();
        option.textAlignment = ui.groupBox_2->alignment();
        option.subControls = QStyle::SC_GroupBoxFrame | QStyle::SC_GroupBoxContents
            | QStyle::SC_GroupBoxLabel;
        if (ui.groupBox_2->isFlat())
            option.features |= QStyleOptionFrame::Flat;
        option.lineWidth = ui.groupBox_2->style()->pixelMetric(
            QStyle::PM_DefaultFrameWidth, &option, ui.groupBox_2);
        return mapped(ui.groupBox_2, ui.groupBox_2->style()->subControlRect(
            QStyle::CC_GroupBox, &option, QStyle::SC_GroupBoxLabel, ui.groupBox_2));
    }
};

} // namespace

TEST_CASE("Search free-slots checkbox and shared-files group have separate bounds",
          "[qt][searchlayout]")
{
    for (const bool dark : {false, true}) {
        for (const bool enlarged : {false, true}) {
            SearchLayoutFixture fixture(dark, enlarged);
            for (const int width : {220, 260, 290}) {
                INFO("sidebar=" << width << ", dark=" << dark << ", enlarged=" << enlarged);
                fixture.arrange(width);
                const QRect checkbox = fixture.mapped(fixture.ui.checkBox_FILTERSLOTS,
                                                       fixture.ui.checkBox_FILTERSLOTS->rect());
                const QRect group = fixture.mapped(fixture.ui.groupBox_2,
                                                    fixture.ui.groupBox_2->rect());
                INFO("checkbox bottom=" << checkbox.bottom() << ", group top=" << group.top());
                CHECK_FALSE(checkbox.intersects(group));
                CHECK(group.top() - checkbox.bottom() - 1 >= 4);
                CHECK(fixture.ui.checkBox_FILTERSLOTS->height()
                      >= fixture.ui.checkBox_FILTERSLOTS->minimumSizeHint().height());
            }
        }
    }
}

TEST_CASE("Search native checkbox painting clears the shared-files title",
          "[qt][searchlayout]")
{
    for (const bool dark : {false, true}) {
        for (const bool enlarged : {false, true}) {
            SearchLayoutFixture fixture(dark, enlarged);
            for (const int width : {220, 260, 290}) {
                INFO("sidebar=" << width << ", dark=" << dark << ", enlarged=" << enlarged);
                fixture.arrange(width);
                const QRect checkbox = fixture.checkboxPaintBounds();
                const QRect title = fixture.sharedTitleBounds();
                REQUIRE(checkbox.isValid());
                REQUIRE(title.isValid());
                INFO("paint bottom=" << checkbox.bottom() << ", title top=" << title.top());
                CHECK_FALSE(checkbox.intersects(title));
                CHECK(title.top() - checkbox.bottom() - 1 >= 4);
            }
        }
    }
}

TEST_CASE("Search sidebar layout preserves public filter controls without requests",
          "[qt][searchlayout]")
{
    SearchLayoutFixture fixture(false, false);
    auto *freeSlots = fixture.ui.checkBox_FILTERSLOTS;
    auto *shared = fixture.ui.comboBox_SHARED;
    freeSlots->setChecked(true);
    shared->setCurrentIndex(2);
    fixture.ui.lineEdit_SIZE->setText(QStringLiteral("123"));
    fixture.arrange(220);
    fixture.arrange(290);
    CHECK(fixture.ui.checkBox_FILTERSLOTS == freeSlots);
    CHECK(freeSlots->objectName() == QStringLiteral("checkBox_FILTERSLOTS"));
    CHECK(freeSlots->isChecked());
    CHECK(fixture.ui.comboBox_SHARED == shared);
    CHECK(shared->count() == 3);
    CHECK(shared->currentIndex() == 2);
    CHECK(fixture.ui.comboBox_SIZE->count() == 4);
    CHECK(fixture.ui.comboBox_SIZETYPE->count() == 3);
    CHECK(fixture.ui.lineEdit_SIZE->text() == QStringLiteral("123"));
    CHECK(fixture.ui.lineEdit_SEARCHSTR->text().isEmpty());
    CHECK(fixture.ui.treeView_HUBS->model() == nullptr);
    CHECK(fixture.ui.treeView_RESULTS->model() == nullptr);
}
