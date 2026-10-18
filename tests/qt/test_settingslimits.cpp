#include <catch2/catch_test_macros.hpp>

// setupUi constructs every Connection tab, including promoted LineEdits outside
// Limits. Those require qtCtx()->wulforUtil(); this fixture has no application
// services. Substitute only those unrelated fields, not any Limits controls.
#include "LineEdit.h"
#define LineEdit QLineEdit
// Keep this altered generated type distinct from the production UI across TUs.
#define Ui_UISettingsConnection Ui_LimitsFixtureConnection
#define UISettingsConnection LimitsFixtureConnection
#include "ui_UISettingsConnection.h"
#undef UISettingsConnection
#undef Ui_UISettingsConnection
#undef LineEdit

#include <QApplication>
#include <QFont>
#include <QFontMetrics>
#include <QLayout>
#include <QRect>
#include <QVBoxLayout>
#include <array>
#include <utility>

namespace {
struct LimitsForm {
    QWidget generatedRoot;
    Ui::LimitsFixtureConnection ui;
    QWidget host;

    LimitsForm()
    {
        ui.setupUi(&generatedRoot);
        // Isolate this tab from the minimum sizes of unrelated Connection pages.
        ui.tabWidget->removeTab(ui.tabWidget->indexOf(ui.tab_2));
        auto *layout = new QVBoxLayout(&host);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(ui.tab_2);
        REQUIRE(ui.tab_2->parentWidget() == &host);
        REQUIRE(layout->indexOf(ui.tab_2) == 0);
        REQUIRE(ui.tabWidget->indexOf(ui.tab_2) == -1);
    }

    void arrange(int width, int height)
    {
        host.resize(width, height);
        host.show();
        // Removing an inactive QTabWidget page leaves it explicitly hidden.
        // Showing the new host alone does not lay out that hidden subtree.
        ui.tab_2->show();
        QApplication::sendPostedEvents();
        QApplication::processEvents();
        host.layout()->activate();
        REQUIRE(host.isVisible());
        REQUIRE(ui.tab_2->isVisible());
        REQUIRE_FALSE(ui.tab_2->isHidden());
        REQUIRE(ui.tab_2->parentWidget() == &host);
    }

    QRect rect(const QWidget *widget) const
    {
        return {widget->mapTo(&host, QPoint(0, 0)), widget->size()};
    }

    std::array<QSpinBox *, 7> editors() const
    {
        return {{ui.spinBox_DOWN_LIMIT_NORMAL, ui.spinBox_UP_LIMIT_NORMAL,
                 ui.spinBox_DOWN_LIMIT_TIME, ui.spinBox_UP_LIMIT_TIME,
                 ui.spinBox_BANDWIDTH_LIMIT_START, ui.spinBox_BANDWIDTH_LIMIT_END,
                 ui.spinBox_ALTERNATE_SLOTS}};
    }
};
}

TEST_CASE("Limits retains every original widget identity and class",
          "[settings-limits-geometry]")
{
    LimitsForm f;
    struct Contract { const char *id; const char *type; };
    // Frozen pre-reflow contract; new section wrappers are allowed.
    const Contract contract[] = {
        {"tab_2", "QWidget"}, {"groupBox_3", "QGroupBox"},
        {"checkBox_THROTTLE_ENABLE", "QCheckBox"}, {"frame_3", "QFrame"},
        {"frameBasicLimits", "QFrame"}, {"label_11", "QLabel"},
        {"spinBox_DOWN_LIMIT_NORMAL", "QSpinBox"}, {"label_14", "QLabel"},
        {"label_12", "QLabel"}, {"spinBox_UP_LIMIT_NORMAL", "QSpinBox"},
        {"label_15", "QLabel"}, {"frameOptionalLimits", "QWidget"},
        {"checkBox_TIME_DEPENDENT_THROTTLE", "QCheckBox"},
        {"frameOptionalLimitsPeriod", "QFrame"},
        {"spinBox_BANDWIDTH_LIMIT_START", "QSpinBox"}, {"label_13", "QLabel"},
        {"spinBox_BANDWIDTH_LIMIT_END", "QSpinBox"}, {"label_19", "QLabel"},
        {"frameOptionalLimitsValues", "QFrame"}, {"label_9", "QLabel"},
        {"spinBox_DOWN_LIMIT_TIME", "QSpinBox"}, {"label_10", "QLabel"},
        {"spinBox_UP_LIMIT_TIME", "QSpinBox"}, {"label_17", "QLabel"},
        {"spinBox_ALTERNATE_SLOTS", "QSpinBox"}, {"label_21", "QLabel"},
        {"label_16", "QLabel"}
    };
    for (const auto &entry : contract) {
        CAPTURE(entry.id);
        const auto matches = f.host.findChildren<QWidget *>(QString::fromLatin1(entry.id));
        REQUIRE(matches.size() == 1);
        CHECK(QString::fromLatin1(matches.front()->metaObject()->className()) ==
              QString::fromLatin1(entry.type));
    }
}

TEST_CASE("Limits generated connections preserve both enablement gates and values",
          "[settings-limits-geometry]")
{
    LimitsForm f;
    const auto editors = f.editors();
    const std::array<int, 7> values{{12345, 23456, 34567, 45678, 22, 3, 17}};
    for (std::size_t i = 0; i < editors.size(); ++i) editors[i]->setValue(values[i]);
    CHECK_FALSE(f.ui.frameBasicLimits->isEnabled());
    CHECK_FALSE(f.ui.frameOptionalLimits->isEnabled());
    CHECK_FALSE(f.ui.frameOptionalLimitsPeriod->isEnabled());
    CHECK_FALSE(f.ui.frameOptionalLimitsValues->isEnabled());

    // Reverse and repeat transitions to catch stale explicit child disablement.
    for (bool throttle : {true, false, true}) {
        f.ui.checkBox_THROTTLE_ENABLE->setChecked(throttle);
        for (bool scheduled : {true, false, true, false}) {
            f.ui.checkBox_TIME_DEPENDENT_THROTTLE->setChecked(scheduled);
            CAPTURE(throttle, scheduled);
            CHECK(f.ui.frameBasicLimits->isEnabled() == throttle);
            CHECK(f.ui.frameOptionalLimits->isEnabled() == throttle);
            CHECK(f.ui.checkBox_TIME_DEPENDENT_THROTTLE->isEnabled() == throttle);
            CHECK(f.ui.frameOptionalLimitsPeriod->isEnabled() == (throttle && scheduled));
            CHECK(f.ui.frameOptionalLimitsValues->isEnabled() == (throttle && scheduled));
            for (std::size_t i = 0; i < editors.size(); ++i) {
                CHECK(editors[i]->isEnabled() == (i < 2 ? throttle : throttle && scheduled));
                CHECK(editors[i]->value() == values[i]);
            }
        }
    }
}

TEST_CASE("Limits retains numeric ranges and separate unit labels",
          "[settings-limits-geometry]")
{
    LimitsForm f;
    const auto editors = f.editors();
    const std::array<int, 7> maxima{{125000, 125000, 125000, 125000, 23, 23, 99}};
    for (std::size_t i = 0; i < editors.size(); ++i) {
        CAPTURE(editors[i]->objectName());
        CHECK(editors[i]->minimum() == 0);
        CHECK(editors[i]->maximum() == maxima[i]);
        CHECK(editors[i]->singleStep() == 1);
        CHECK(editors[i]->suffix().isEmpty());
        editors[i]->setValue(maxima[i]);
        CHECK(editors[i]->value() == maxima[i]);
        editors[i]->setValue(0);
        CHECK(editors[i]->value() == 0);
    }
    for (auto *unit : {f.ui.label_14, f.ui.label_15, f.ui.label_16, f.ui.label_17})
        CHECK(unit->text() == QStringLiteral("KiB/s"));
    CHECK(f.ui.label_19->text() == QStringLiteral("hour(s)"));
}

TEST_CASE("Limits rows stay compact aligned and contained as the page grows",
          "[settings-limits-geometry]")
{
    for (int width : {640, 1120}) {
        for (bool enlarged : {false, true}) {
            CAPTURE(width, enlarged);
            LimitsForm f;
            if (enlarged) {
                QFont font = f.host.font();
                if (font.pointSizeF() > 0) font.setPointSizeF(font.pointSizeF() * 1.25);
                else font.setPixelSize(qRound(font.pixelSize() * 1.25));
                f.host.setFont(font);
            }
            f.ui.checkBox_THROTTLE_ENABLE->setChecked(true);
            f.ui.checkBox_TIME_DEPENDENT_THROTTLE->setChecked(true);
            f.arrange(width, 640);
            const auto editors = f.editors();
            std::array<QRect, 7> before;
            for (std::size_t i = 0; i < editors.size(); ++i) before[i] = f.rect(editors[i]);
            f.arrange(width, 960);
            CHECK(f.host.width() == width);
            for (auto *label : f.ui.tab_2->findChildren<QLabel *>()) {
                CAPTURE(label->objectName());
                CHECK(f.host.rect().contains(f.rect(label)));
                CHECK(label->width() >= label->sizeHint().width());
            }
            for (auto *checkbox : {f.ui.checkBox_THROTTLE_ENABLE, f.ui.checkBox_TIME_DEPENDENT_THROTTLE}) {
                CHECK(f.host.rect().contains(f.rect(checkbox)));
                CHECK(checkbox->width() >= checkbox->sizeHint().width());
            }
            for (std::size_t i = 0; i < editors.size(); ++i) {
                CAPTURE(editors[i]->objectName());
                const auto r = f.rect(editors[i]);
                CHECK(editors[i]->isVisible());
                CHECK(f.host.rect().contains(r));
                CHECK(r.width() >= editors[i]->minimumSizeHint().width());
                CHECK(qAbs(r.top() - before[i].top()) <= 2);
                for (std::size_t j = i + 1; j < editors.size(); ++j)
                    CHECK_FALSE(r.intersects(f.rect(editors[j])));
            }
            const std::array<QSpinBox *, 5> rows{{editors[0], editors[1], editors[2], editors[3], editors[6]}};
            const std::array<QLabel *, 5> labels{{f.ui.label_11, f.ui.label_12, f.ui.label_9,
                                                 f.ui.label_10, f.ui.label_21}};
            const int rowGapLimit = 2 * f.host.fontMetrics().height();
            for (std::size_t i = 0; i < rows.size(); ++i) {
                const auto r = f.rect(rows[i]);
                const auto label = f.rect(labels[i]);
                CHECK(qAbs(r.left() - f.rect(rows[0]).left()) <= 2);
                CHECK(qAbs(r.width() - f.rect(rows[0]).width()) <= 2);
                CHECK(label.right() < r.left());
                CHECK(qAbs(label.center().y() - r.center().y()) <= 2);
                CHECK(f.host.rect().contains(label));
                CHECK(labels[i]->width() >= labels[i]->sizeHint().width());
            }
            // Download/Upload and alternate Upload/Slots must be adjacent rows.
            for (const auto pair : {std::pair<int, int>{0, 1}, {2, 3}, {3, 4}}) {
                const auto previous = f.rect(rows[pair.first]);
                const auto next = f.rect(rows[pair.second]);
                CHECK(next.top() > previous.bottom());
                CHECK(next.top() - previous.bottom() <= rowGapLimit);
            }
            CHECK(f.rect(f.ui.spinBox_BANDWIDTH_LIMIT_START).bottom() < f.rect(editors[2]).top());
            CHECK(qAbs(f.rect(editors[4]).center().y() - f.rect(editors[5]).center().y()) <= 2);
            CHECK(f.rect(editors[4]).right() < f.rect(editors[5]).left());
        }
    }
}

TEST_CASE("Limits separates default and scheduled controls into named sections",
          "[settings-limits-geometry]")
{
    LimitsForm f;
    const auto sections = f.ui.tab_2->findChildren<QGroupBox *>();
    QGroupBox *defaults = nullptr;
    QGroupBox *scheduled = nullptr;
    for (auto *section : sections) {
        if (section->title() == QStringLiteral("Default Limits")) {
            REQUIRE(defaults == nullptr);
            defaults = section;
        }
        if (section->title() == QStringLiteral("Scheduled Alternate")) {
            REQUIRE(scheduled == nullptr);
            scheduled = section;
        }
    }
    REQUIRE(defaults != nullptr);
    REQUIRE(scheduled != nullptr);
    CHECK_FALSE(defaults->isAncestorOf(scheduled));
    CHECK_FALSE(scheduled->isAncestorOf(defaults));
    CHECK(defaults->isAncestorOf(f.ui.frameBasicLimits));
    CHECK(scheduled->isAncestorOf(f.ui.checkBox_TIME_DEPENDENT_THROTTLE));
    CHECK(scheduled->isAncestorOf(f.ui.frameOptionalLimitsPeriod));
    CHECK(scheduled->isAncestorOf(f.ui.frameOptionalLimitsValues));
    CHECK_FALSE(defaults->isAncestorOf(f.ui.checkBox_THROTTLE_ENABLE));
    CHECK_FALSE(scheduled->isAncestorOf(f.ui.checkBox_THROTTLE_ENABLE));
}
