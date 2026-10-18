#include <catch2/catch_test_macros.hpp>
#include "MacInputStyle.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QMenu>
#include <QSpinBox>
#include <QStyleOptionButton>
#include <QStyleOptionComboBox>

namespace {
QPalette compactPalette(bool dark)
{
    QPalette pal = QApplication::palette();
    pal.setColor(QPalette::Window, dark ? QColor(35, 35, 35) : QColor(240, 240, 240));
    pal.setColor(QPalette::Base, dark ? QColor(28, 28, 28) : QColor(Qt::white));
    pal.setColor(QPalette::Text, dark ? QColor(235, 235, 235) : QColor(24, 24, 24));
    pal.setColor(QPalette::WindowText, pal.color(QPalette::Text));
    return pal;
}
}

TEST_CASE("Compact shared fields have native sized hints in both appearances", "[qt][compactstyle]")
{
    for (bool dark : {false, true}) {
        CAPTURE(dark);
        QWidget host;
        host.setPalette(compactPalette(dark));
        host.setStyleSheet(mac_input_style::macInputContrastStyle(host.palette()));
        QLineEdit edit(&host);
        QComboBox combo(&host);
        QSpinBox spin(&host);
        combo.addItem("Automatic");
        const QList<QWidget *> fields{&edit, &combo, &spin};
        for (QWidget *field : fields) {
            field->ensurePolished();
            CAPTURE(field->metaObject()->className(), field->sizeHint().height());
            CHECK(field->sizeHint().height() >= 24);
            CHECK(field->sizeHint().height() <= 28);
        }
    }
}

TEST_CASE("Compact checkboxes keep full labels and native indicators", "[qt][compactstyle]")
{
    for (bool dark : {false, true}) {
        QWidget host;
        host.setPalette(compactPalette(dark));
        host.setStyleSheet(mac_input_style::macInputContrastStyle(host.palette()));
        QCheckBox check("Require encrypted connections for all transfers", &host);
        check.ensurePolished();
        check.resize(check.sizeHint());
        for (bool enabled : {false, true}) {
            check.setEnabled(enabled);
            for (bool checked : {false, true}) {
                check.setChecked(checked);
                QStyleOptionButton option;
                option.initFrom(&check);
                option.text = check.text();
                option.state |= checked ? QStyle::State_On : QStyle::State_Off;
                const QRect text = check.style()->subElementRect(QStyle::SE_CheckBoxContents, &option, &check);
                const QRect indicator = check.style()->subElementRect(QStyle::SE_CheckBoxIndicator, &option, &check);
                CHECK(text.width() >= check.fontMetrics().horizontalAdvance(check.text()));
                CHECK(indicator.width() > 0);
                CHECK(check.rect().contains(indicator));
                CHECK_FALSE(check.grab().isNull());
            }
        }
    }
}

TEST_CASE("Compact style preserves native popup policy and unclipped menu rows", "[qt][compactstyle]")
{
    QComboBox nativeCombo;
    const int nativePopup = nativeCombo.style()->styleHint(QStyle::SH_ComboBox_Popup, nullptr, &nativeCombo);
    for (bool dark : {false, true}) {
        QWidget host;
        host.setPalette(compactPalette(dark));
        host.setStyleSheet(mac_input_style::macInputContrastStyle(host.palette()));
        QComboBox combo(&host);
        combo.addItems({"Automatic", "A longer connection profile name"});
        combo.ensurePolished();
        CHECK(combo.style()->styleHint(QStyle::SH_ComboBox_Popup, nullptr, &combo) == nativePopup);
        QMenu menu(&host);
        menu.addAction("Open connection preferences")->setCheckable(true);
        menu.actions().first()->setChecked(true);
        menu.addAction("Unavailable action")->setEnabled(false);
        menu.addMenu("More connection options")->addAction("Automatic");
        menu.ensurePolished();
        menu.popup(QPoint(20, 20));
        QApplication::processEvents();
        for (QAction *action : menu.actions()) {
            const QRect row = menu.actionGeometry(action);
            CHECK(menu.rect().contains(row));
            CHECK(row.height() >= menu.fontMetrics().height());
            CHECK(row.width() >= menu.fontMetrics().horizontalAdvance(action->text()) + 24);
        }
        CHECK_FALSE(menu.grab().isNull());
        menu.hide();
    }
}

TEST_CASE("Settings sidebar is compact but grows for translated labels", "[qt][compactstyle]")
{
    QListWidget sidebar;
    sidebar.setIconSize(QSize(22, 22));
    sidebar.addItems({"Main", "GUI", "Downloads", "User Commands"});
    const int compact = mac_input_style::settingsSidebarWidth(&sidebar);
    CHECK(compact >= 160);
    CHECK(compact < 210);
    const QString longLabel = QStringLiteral("Connection configuration and encrypted transfer preferences");
    sidebar.addItem(longLabel);
    const int expanded = mac_input_style::settingsSidebarWidth(&sidebar);
    CHECK(expanded >= sidebar.fontMetrics().horizontalAdvance(longLabel) + 22 + 48);
    CHECK(expanded > compact);
    CHECK(mac_input_style::settingsSidebarWidth(nullptr) == 160);
}
