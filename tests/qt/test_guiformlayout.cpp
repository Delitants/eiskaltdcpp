/* Profile-free geometry contract for SettingsGUI's Chat (extended) page.
 * Generated Designer widgets are real; Chat pictures reproduces the runtime
 * construction in SettingsGUI::init(), without QtContext, icons or settings IO.
 * The production layout helper is shared; no test-only layout correction.
 */
#include <catch2/catch_test_macros.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QFont>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QStringList>
#include <QStyle>
#include <QToolButton>
#include <QWidget>

#include <array>
#include <cstdlib>

#include "ExtendedChatLayout.h"

// The unrelated Basic-page promoted LineEdit dereferences qtCtx() in its
// constructor. Substitute only that promoted type in this generated test UI.
// Rename the generated class too, avoiding an ODR clash with the real UI class
// when this file is linked into the full-GUI test target.
#include "LineEdit.h"
#define LineEdit QLineEdit
#define UISettingsGUI ProfileFreeUISettingsGUI
#include "ui_UISettingsGUI.h"
#undef UISettingsGUI
#undef LineEdit

namespace {

void activateTree(QLayout *layout)
{
    REQUIRE(layout != nullptr);
    layout->invalidate();
    layout->activate();
    for (int i = 0; i < layout->count(); ++i) {
        auto *item = layout->itemAt(i);
        REQUIRE(item != nullptr);
        if (auto *child = item->layout())
            activateTree(child);
        else if (auto *widget = item->widget()) {
            if (widget->layout())
                activateTree(widget->layout());
        }
    }
}

struct ExtendedChatFixture {
    QWidget host;
    Ui::ProfileFreeUISettingsGUI ui;
    QGroupBox *pictures;
    QLineEdit *folder;
    QCheckBox *autoClean;
    QSpinBox *days;

    ExtendedChatFixture()
    {
        ui.setupUi(&host);
        REQUIRE(ui.tab_5 != nullptr);
        REQUIRE(ui.verticalLayout_5 != nullptr);
        for (auto *label : labels())
            REQUIRE(label != nullptr);
        for (auto *editor : editors())
            REQUIRE(editor != nullptr);
        ui.tabWidget->setCurrentWidget(ui.tab_5);
        pictures = new QGroupBox(QStringLiteral("Chat pictures"), ui.tab_5);
        auto *layout = new QGridLayout(pictures);
        auto *labelDir = new QLabel(QStringLiteral("Folder"), pictures);
        folder = new QLineEdit(pictures);
        auto *browse = new QToolButton(pictures);
        autoClean = new QCheckBox(QStringLiteral("Auto-clean files older than"), pictures);
        days = new QSpinBox(pictures);
        days->setRange(1, 365);
        days->setSuffix(QStringLiteral(" days"));
        days->setMaximumWidth(100);
        layout->addWidget(labelDir, 0, 0);
        layout->addWidget(folder, 0, 1);
        layout->addWidget(browse, 0, 2);
        layout->addWidget(autoClean, 1, 0, 1, 2);
        layout->addWidget(days, 1, 2);
        layout->setColumnStretch(1, 1);
        ui.verticalLayout_5->insertWidget(ui.verticalLayout_5->count() - 1, pictures);
        QObject::connect(autoClean, &QCheckBox::toggled, days, &QWidget::setEnabled);
        days->setEnabled(autoClean->isChecked());
        ChatExtendedLayout::applyExtendedChatLayout(
            ui.tab_5, ui.gridLayout_8, ui.gridLayout_2,
            labels(), editors(), autoClean, days);
    }

    std::array<QLabel *, 8> labels() const
    {
        return {{ui.label_8, ui.label_9, ui.label_12, ui.label_10,
                 ui.label_11, ui.label, ui.label_6, ui.label_7}};
    }

    std::array<QWidget *, 8> editors() const
    {
        return {{ui.comboBox_DBL_CLICK, ui.comboBox_MDL_CLICK,
                 ui.comboBox_DEF_MAGNET_ACTION, ui.comboBox_APP_UNIT_BASE,
                 ui.lineEdit_TIMESTAMP, ui.spinBox_OUT_IN_HIST,
                 ui.spinBox_PARAGRAPHS, ui.comboBox_CHAT_SEPARATOR}};
    }

    void arrange(int width, bool largeFont = false)
    {
        if (largeFont) {
            QFont font = host.font();
            font.setPointSizeF(font.pointSizeF() > 0 ? font.pointSizeF() * 1.5 : 18);
            host.setFont(font);
        }
        host.ensurePolished();
        // Deliver the shared helper's post-font/style native-hint refresh,
        // without showing a window or dispatching unrelated input events.
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        host.resize(width + 80, 1100);
        activateTree(host.layout());
        // Explicit page geometry avoids dependence on exposure/window-manager events.
        ui.tab_5->resize(width, 1000);
        ui.verticalLayout_5->setGeometry(ui.tab_5->rect());
        activateTree(ui.verticalLayout_5);
    }

    int left(QWidget *widget) const
    {
        return widget->mapTo(ui.tab_5, QPoint(0, 0)).x();
    }
};

} // namespace

TEST_CASE("Chat extended labels are leading and do not consume surplus width",
          "[qt][guiformlayout]")
{
    ExtendedChatFixture fixture;
    fixture.arrange(1000);
    for (auto *label : fixture.labels()) {
        INFO(label->objectName().toStdString());
        CHECK_FALSE(label->sizePolicy().expandingDirections().testFlag(Qt::Horizontal));
        const auto visual = QStyle::visualAlignment(label->layoutDirection(), label->alignment());
        CHECK(visual.testFlag(Qt::AlignLeft));
        CHECK_FALSE(visual.testFlag(Qt::AlignRight));
    }
}

TEST_CASE("Chat extended forms share a stable leading editor column",
          "[qt][guiformlayout]")
{
    for (const bool largeFont : {false, true}) {
        ExtendedChatFixture fixture;
        fixture.arrange(720, largeFont);
        const int initialLeft = fixture.left(fixture.ui.comboBox_DBL_CLICK);
        for (const int width : {720, 1000, 1280}) {
            fixture.arrange(width);
            INFO("page width=" << width << ", enlarged font=" << largeFont);
            for (auto *editor : fixture.editors()) {
                INFO(editor->objectName().toStdString());
                CHECK(editor->testAttribute(Qt::WA_LayoutUsesWidgetRect));
                CHECK(std::abs(fixture.left(editor) - initialLeft) <= 2);
                CHECK(editor->geometry().width() > 0);
            }
        }
    }
}

TEST_CASE("Chat pictures auto-clean caption and days remain adjacent",
          "[qt][guiformlayout]")
{
    for (const bool largeFont : {false, true}) {
        ExtendedChatFixture fixture;
        fixture.arrange(1000, largeFont);
        for (const int width : {1000, 1280}) {
            fixture.arrange(width);
            INFO("page width=" << width << ", enlarged font=" << largeFont);
            CHECK(fixture.autoClean->testAttribute(Qt::WA_LayoutUsesWidgetRect));
            CHECK(fixture.days->testAttribute(Qt::WA_LayoutUsesWidgetRect));
            // QWidget width includes the stretched empty checkbox cell. Compare
            // against the native caption indicator size, not that empty area.
            const int captionEnd = fixture.left(fixture.autoClean)
                + fixture.autoClean->sizeHint().width();
            const int gap = fixture.left(fixture.days) - captionEnd;
            CHECK(gap >= 0);
            CHECK(gap <= 16);
            const int captionCenter = fixture.autoClean->mapTo(fixture.pictures, QPoint()).y()
                + fixture.autoClean->height() / 2;
            const int daysCenter = fixture.days->mapTo(fixture.pictures, QPoint()).y()
                + fixture.days->height() / 2;
            CHECK(std::abs(captionCenter - daysCenter) <= 2);
        }
    }
}

TEST_CASE("Chat extended numeric editors are compact without clipping native hints",
          "[qt][guiformlayout]")
{
    for (const bool largeFont : {false, true}) {
        ExtendedChatFixture fixture;
        fixture.arrange(1280, largeFont);
        for (auto *spin : {fixture.ui.spinBox_OUT_IN_HIST,
                           fixture.ui.spinBox_PARAGRAPHS, fixture.days}) {
            INFO(spin->objectName().toStdString());
            const int needed = spin->sizeHint().expandedTo(spin->minimumSizeHint()).width();
            CHECK(spin->width() >= needed);
            CHECK(spin->width() <= needed + 8);
        }
    }
}

TEST_CASE("Chat extended layout preserves Designer identity and public value semantics",
          "[qt][guiformlayout]")
{
    ExtendedChatFixture fixture;
    const auto editors = fixture.editors();
    QStringList names;
    for (auto *editor : editors)
        names.append(editor->objectName());
    fixture.ui.spinBox_OUT_IN_HIST->setValue(500);
    fixture.ui.spinBox_PARAGRAPHS->setValue(1000000);
    fixture.ui.lineEdit_TIMESTAMP->setText(QStringLiteral("hh:mm:ss"));
    fixture.days->setValue(365);
    fixture.arrange(1280, true);
    fixture.arrange(720);
    for (std::size_t i = 0; i < editors.size(); ++i) {
        CHECK(fixture.editors()[i] == editors[i]);
        CHECK(editors[i]->objectName() == names.at(static_cast<int>(i)));
    }
    CHECK(fixture.ui.spinBox_OUT_IN_HIST->minimum() == 0);
    CHECK(fixture.ui.spinBox_OUT_IN_HIST->maximum() == 500);
    CHECK(fixture.ui.spinBox_OUT_IN_HIST->value() == 500);
    CHECK(fixture.ui.spinBox_PARAGRAPHS->maximum() == 1000000);
    CHECK(fixture.ui.spinBox_PARAGRAPHS->value() == 1000000);
    CHECK(fixture.ui.lineEdit_TIMESTAMP->text() == QStringLiteral("hh:mm:ss"));
    CHECK(fixture.ui.comboBox_DBL_CLICK->count() == 3);
    CHECK(fixture.ui.comboBox_MDL_CLICK->count() == 3);
    CHECK(fixture.ui.comboBox_DEF_MAGNET_ACTION->count() == 3);
    CHECK(fixture.ui.comboBox_APP_UNIT_BASE->count() == 3);
    CHECK(fixture.ui.comboBox_CHAT_SEPARATOR->count() == 2);
    CHECK(fixture.folder->text().isEmpty());
    CHECK(fixture.days->minimum() == 1);
    CHECK(fixture.days->maximum() == 365);
    CHECK(fixture.days->value() == 365);
    CHECK(fixture.days->suffix() == QStringLiteral(" days"));
    fixture.autoClean->setChecked(true);
    CHECK(fixture.days->isEnabled());
    fixture.autoClean->setChecked(false);
    CHECK_FALSE(fixture.days->isEnabled());
}
