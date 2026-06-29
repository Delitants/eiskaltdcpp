/***************************************************************************
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 3 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
***************************************************************************/
/*
 * Copyright (C) 2026 Joe Rivera <transfix@sublevels.net>
 */

#include "Settings.h"
#include "QtContextAware.h"
#include "QtContext.h"
#include "dcpp/DCPlusPlus.h"
#include "SettingsPersonal.h"
#include "SettingsConnection.h"
#include "SettingsDownloads.h"
#include "SettingsSharing.h"
#include "SettingsGUI.h"
#include "SettingsNotification.h"
#include "SettingsLog.h"
#include "SettingsUC.h"
#include "SettingsShortcuts.h"
#include "SettingsHistory.h"
#include "SettingsAdvanced.h"
#include <QGroupBox>
#include <QLayout>
#include <QFormLayout>
#include <QGridLayout>
#include <QCheckBox>
#include <QRadioButton>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QApplication>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QAbstractButton>
#include <QDir>
#include <QFileInfo>
#include <QFontMetrics>
#include <QSplitter>
#include <QSize>
#include <QEvent>

#include "WulforUtil.h"

#include <QTableWidget>
#include <QTabWidget>
#include <QTabBar>
#include <QFrame>
#include <QPalette>

#include <QScroller>

namespace {
int settingsSidebarWidth(QListWidget *listWidget)
{
    if (!listWidget)
        return 180;

    const QFontMetrics metrics(listWidget->font());
    int textWidth = 0;

    for (int row = 0; row < listWidget->count(); ++row) {
        if (QListWidgetItem *item = listWidget->item(row))
            textWidth = qMax(textWidth, metrics.horizontalAdvance(item->text()));
    }

    listWidget->doItemsLayout();
    const int delegateWidth = listWidget->sizeHintForColumn(0);
    const int iconWidth = listWidget->iconSize().isValid() ? listWidget->iconSize().width() : 0;
    return qBound(180, qMax(delegateWidth + 24, textWidth + iconWidth + 76), 360);
}

#ifdef Q_OS_MAC
QString macBundledStyleIcon(const QString &name)
{
    const QString bundlePath = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(
        QStringLiteral("../Resources/icons/appl/default/") + name);

    if (QFileInfo::exists(bundlePath))
        return QStringLiteral("url(\"%1\")").arg(bundlePath);

    return QStringLiteral("none");
}

QColor macReadableSelectionText(const QColor &background)
{
    return background.lightness() < 150 ? QColor(Qt::white) : QColor(18, 18, 18);
}

QColor macSettingsSelectionBackground(const QPalette &palette, const bool darkAppearance)
{
    QColor selection = palette.color(QPalette::Highlight);

    if (darkAppearance && (selection.lightness() > 190 || selection.saturation() < 35))
        selection = QColor(55, 112, 220);
    else if (darkAppearance && selection.lightness() < 80)
        selection = selection.lighter(145);
    else if (!darkAppearance && selection.lightness() > 230)
        selection = selection.darker(112);

    return selection;
}

void applyMacSettingsPanelStyle(QFrame *panel)
{
    if (!panel)
        return;

    const QPalette panelPalette = QApplication::palette(panel);
    const bool darkAppearance = (panelPalette.color(QPalette::Window).lightness() + panelPalette.color(QPalette::Base).lightness()) / 2 < 128;
    QColor fieldBorder = darkAppearance ? panelPalette.color(QPalette::Window).lighter(165)
                                        : panelPalette.color(QPalette::Window).darker(135);
    if (qAbs(fieldBorder.lightness() - panelPalette.color(QPalette::Window).lightness()) < 26) {
        const QColor textColor = panelPalette.color(QPalette::Text);
        fieldBorder = darkAppearance ? textColor.lighter(145) : textColor.darker(150);
    }
    const QColor panelBorder = darkAppearance ? fieldBorder.lighter(118) : fieldBorder.darker(112);
    QColor focusBorder = macSettingsSelectionBackground(panelPalette, darkAppearance);
    if (darkAppearance && focusBorder.lightness() < 150)
        focusBorder = focusBorder.lighter(140);
    else if (!darkAppearance && focusBorder.lightness() > 205)
        focusBorder = focusBorder.darker(118);
    const QColor selectedText = macReadableSelectionText(focusBorder);

    const QColor panelBackground = darkAppearance ? panelPalette.color(QPalette::Window).lighter(108)
                                                  : panelPalette.color(QPalette::Window);
    const QColor groupBackground = darkAppearance ? panelPalette.color(QPalette::Base).lighter(105)
                                                  : panelPalette.color(QPalette::AlternateBase);
    const QColor titleBackground = darkAppearance ? groupBackground.lighter(104)
                                                  : groupBackground;
    const QString comboArrow = macBundledStyleIcon(darkAppearance ? QStringLiteral("combo-arrow-down-light.svg")
                                                                  : QStringLiteral("combo-arrow-down-dark.svg"));
    const QColor disabledFieldBackground = darkAppearance ? QColor(46, 46, 46)
                                                          : QColor(207, 207, 207);
    const QColor disabledFieldBorder = darkAppearance ? QColor(78, 78, 78)
                                                      : QColor(166, 166, 166);
    const QColor disabledFieldText = darkAppearance ? QColor(138, 138, 138)
                                                    : QColor(78, 78, 78);
    const QColor disabledDropBackground = darkAppearance ? QColor(40, 40, 40)
                                                         : QColor(188, 188, 188);

    panel->setStyleSheet(QStringLiteral(
        "QFrame#settingsPagePanel {"
        " background-color: %1;"
        " border: 1px solid %3;"
        " border-radius: 12px;"
        "}"
        "QFrame#settingsPagePanel QLabel {"
        " font-size: 13px;"
        " color: palette(text);"
        "}"
        "QFrame#settingsPagePanel QCheckBox,"
        "QFrame#settingsPagePanel QRadioButton {"
        " font-size: 13px;"
        " color: palette(text);"
        " spacing: 8px;"
        " padding: 2px 0px;"
        " min-height: 22px;"
        "}"
        "QFrame#settingsPagePanel QGroupBox {"
        " border: 1px solid %3;"
        " border-radius: 10px;"
        " margin-top: 6px;"
        " padding: 24px 8px 8px 8px;"
        " background-color: %4;"
        "}"
        "QFrame#settingsPagePanel QGroupBox::title {"
        " subcontrol-origin: padding;"
        " subcontrol-position: top left;"
        " top: 4px;"
        " left: 10px;"
        " padding: 1px 7px;"
        " color: palette(text);"
        " font-size: 13px;"
        " font-weight: 600;"
        " background-color: %6;"
        " border: none;"
        " border-radius: 6px;"
        "}"
        "QFrame#settingsPagePanel QGroupBox::indicator {"
        " margin-right: 5px;"
        "}"
        "QFrame#settingsPagePanel QGroupBox[settingsSectionHeader=\"true\"]::title {"
        " padding: 2px 10px;"
        " font-weight: 700;"
        "}"
        "QFrame#settingsPagePanel QGroupBox[flat=\"true\"][settingsFlatSection=\"true\"] {"
        " border: none;"
        " border-radius: 0px;"
        " margin-top: 4px;"
        " padding: 2px 0px 0px 0px;"
        " background: transparent;"
        "}"
        "QFrame#settingsPagePanel QGroupBox[flat=\"true\"][settingsFlatSection=\"true\"]::title {"
        " left: 0px;"
        " padding: 0px 6px 0px 0px;"
        " margin: 0px;"
        " background: transparent;"
        " border: none;"
        " border-radius: 0px;"
        " font-size: 13px;"
        " font-weight: 700;"
        " color: palette(text);"
        "}"
        "QFrame#settingsPagePanel QLineEdit,"
        "QFrame#settingsPagePanel QTextEdit,"
        "QFrame#settingsPagePanel QPlainTextEdit,"
        "QFrame#settingsPagePanel QAbstractSpinBox {"
        " background-color: palette(base);"
        " border: 1px solid %2;"
        " border-radius: 7px;"
        " padding: 1px 7px;"
        " min-height: 24px;"
        " font-size: 13px;"
        "}"
        "QFrame#settingsPagePanel QLineEdit:focus,"
        "QFrame#settingsPagePanel QTextEdit:focus,"
        "QFrame#settingsPagePanel QPlainTextEdit:focus,"
        "QFrame#settingsPagePanel QAbstractSpinBox:focus {"
        " border: 1px solid %5;"
        "}"
        "QFrame#settingsPagePanel QLabel:disabled,"
        "QFrame#settingsPagePanel QCheckBox:disabled,"
        "QFrame#settingsPagePanel QRadioButton:disabled {"
        " color: %11;"
        "}"
        "QFrame#settingsPagePanel QLineEdit:disabled,"
        "QFrame#settingsPagePanel QTextEdit:disabled,"
        "QFrame#settingsPagePanel QPlainTextEdit:disabled,"
        "QFrame#settingsPagePanel QAbstractSpinBox:disabled {"
        " background-color: %9;"
        " border: 1px solid %10;"
        " color: %11;"
        "}"
        "QFrame#settingsPagePanel QComboBox {"
        " background-color: palette(base);"
        " border: 1px solid %2;"
        " border-radius: 7px;"
        " padding: 3px 30px 3px 9px;"
        " min-height: 26px;"
        " font-size: 13px;"
        " combobox-popup: 0;"
        "}"
        "QFrame#settingsPagePanel QComboBox:disabled {"
        " background-color: %9;"
        " border: 1px solid %10;"
        " color: %11;"
        "}"
        "QFrame#settingsPagePanel QComboBox::drop-down {"
        " subcontrol-origin: border;"
        " subcontrol-position: top right;"
        " width: 24px;"
        " border-left: 1px solid %2;"
        " border-top-right-radius: 7px;"
        " border-bottom-right-radius: 7px;"
        "}"
        "QFrame#settingsPagePanel QComboBox::drop-down:disabled {"
        " background-color: %12;"
        " border-left: 1px solid %10;"
        "}"
        "QFrame#settingsPagePanel QComboBox::down-arrow {"
        " image: %7;"
        " width: 9px;"
        " height: 9px;"
        " margin-right: 7px;"
        "}"
        "QFrame#settingsPagePanel QComboBox::down-arrow:disabled {"
        " image: %7;"
        "}"
        "QFrame#settingsPagePanel QComboBox QAbstractItemView {"
        " border: 1px solid %2;"
        " padding: 4px;"
        " selection-background-color: %5;"
        " selection-color: %8;"
        " outline: 0;"
        "}"
        "QFrame#settingsPagePanel QComboBox QAbstractItemView::item {"
        " padding: 5px 10px;"
        " min-height: 22px;"
        "}"
        "QFrame#settingsPagePanel QCheckBox::indicator,"
        "QFrame#settingsPagePanel QRadioButton::indicator {"
        " width: 16px;"
        " height: 16px;"
        "}"
        "QFrame#settingsPagePanel QCheckBox::indicator:disabled {"
        " background-color: %9;"
        " border: 1px solid %10;"
        " border-radius: 4px;"
        "}"
        "QFrame#settingsPagePanel QCheckBox::indicator:checked:disabled {"
        " background-color: %12;"
        " border: 1px solid %10;"
        " border-radius: 4px;"
        "}"
        "QFrame#settingsPagePanel QRadioButton::indicator:disabled {"
        " background-color: %9;"
        " border: 1px solid %10;"
        " border-radius: 8px;"
        "}"
        "QFrame#settingsPagePanel QRadioButton::indicator:checked:disabled {"
        " background-color: %12;"
        " border: 1px solid %10;"
        " border-radius: 8px;"
        "}"
    ).arg(panelBackground.name(), fieldBorder.name(), panelBorder.name(),
          groupBackground.name(), focusBorder.name(), titleBackground.name(),
          comboArrow, selectedText.name(), disabledFieldBackground.name(),
          disabledFieldBorder.name(), disabledFieldText.name(),
          disabledDropBackground.name()));
}

void applyMacSettingsTabWidgetStyle(QTabWidget *tabs)
{
    if (!tabs)
        return;

    tabs->setDocumentMode(false);
    tabs->setAutoFillBackground(false);
    if (auto *tabBar = tabs->tabBar()) {
        tabBar->setDocumentMode(false);
        tabBar->setAutoFillBackground(false);
    }

    const QPalette tabPalette = QApplication::palette(tabs);
    const bool darkAppearance = (tabPalette.color(QPalette::Window).lightness() + tabPalette.color(QPalette::Base).lightness()) / 2 < 128;
    QColor fieldBorder = darkAppearance ? tabPalette.color(QPalette::Window).lighter(165)
                                        : tabPalette.color(QPalette::Window).darker(135);
    if (qAbs(fieldBorder.lightness() - tabPalette.color(QPalette::Window).lightness()) < 26) {
        const QColor textColor = tabPalette.color(QPalette::Text);
        fieldBorder = darkAppearance ? textColor.lighter(145) : textColor.darker(150);
    }

    const QColor panelBorder = darkAppearance ? fieldBorder.lighter(118) : fieldBorder.darker(112);
    const QColor panelBackground = darkAppearance ? tabPalette.color(QPalette::Window).lighter(108)
                                                  : tabPalette.color(QPalette::Window);
    const QColor groupBackground = darkAppearance ? tabPalette.color(QPalette::Base).lighter(105)
                                                  : tabPalette.color(QPalette::AlternateBase);
    const QColor activeTabBackground = darkAppearance ? panelBackground.lighter(107)
                                                      : tabPalette.color(QPalette::Base);
    const QColor inactiveTabBackground = darkAppearance ? panelBackground.darker(116)
                                                        : panelBackground.darker(104);
    const QColor inactiveTabHoverBackground = darkAppearance ? inactiveTabBackground.lighter(116)
                                                             : groupBackground;

    tabs->setStyleSheet(QStringLiteral(
        "QTabWidget {"
        " background: transparent;"
        "}"
        "QTabWidget::pane {"
        " border: 1px solid %1;"
        " border-radius: 8px;"
        " top: -1px;"
        " background-color: %2;"
        " padding: 8px 8px 10px 8px;"
        " margin-top: 0px;"
        "}"
        "QTabWidget::tab-bar {"
        " alignment: left;"
        " left: 8px;"
        "}"
        "QTabBar {"
        " background: transparent;"
        "}"
        "QTabBar::base {"
        " background: transparent;"
        " border: 0px;"
        " height: 0px;"
        "}"
        "QTabBar::tab {"
        " background-color: %3;"
        " border: 1px solid %1;"
        " border-bottom-color: %1;"
        " border-top-left-radius: 7px;"
        " border-top-right-radius: 7px;"
        " border-bottom-left-radius: 0px;"
        " border-bottom-right-radius: 0px;"
        " min-width: 0px;"
        " padding: 3px 18px 4px 18px;"
        " margin-top: 2px;"
        " margin-right: 2px;"
        " margin-bottom: 0px;"
        " font-size: 12px;"
        " color: palette(text);"
        "}"
        "QTabBar::tab:selected {"
        " background-color: %2;"
        " color: palette(text);"
        " border-color: %1;"
        " border-bottom-color: %2;"
        " font-weight: 600;"
        " margin-top: 0px;"
        " margin-bottom: -1px;"
        " padding-top: 4px;"
        " padding-bottom: 5px;"
        "}"
        "QTabBar::tab:!selected {"
        " color: palette(text);"
        "}"
        "QTabBar::tab:hover:!selected {"
        " background-color: %4;"
        " border-color: %1;"
        "}"
    ).arg(panelBorder.name(), activeTabBackground.name(),
          inactiveTabBackground.name(), inactiveTabHoverBackground.name()));
}

void applyMacSettingsSidebarStyle(QListWidget *listWidget)
{
    if (!listWidget)
        return;

    const QPalette sidebarPalette = QApplication::palette(listWidget);
    const bool darkSidebar = (sidebarPalette.color(QPalette::Window).lightness() + sidebarPalette.color(QPalette::Base).lightness()) / 2 < 128;
    QColor sidebarBorder = darkSidebar ? sidebarPalette.color(QPalette::Window).lighter(170)
                                       : sidebarPalette.color(QPalette::Window).darker(140);
    if (qAbs(sidebarBorder.lightness() - sidebarPalette.color(QPalette::Window).lightness()) < 26) {
        const QColor textColor = sidebarPalette.color(QPalette::Text);
        sidebarBorder = darkSidebar ? textColor.lighter(145) : textColor.darker(150);
    }
    const QColor sidebarBg = darkSidebar ? sidebarPalette.color(QPalette::Window).lighter(108)
                                         : sidebarPalette.color(QPalette::Window);
    QColor sidebarHover = darkSidebar ? sidebarBg.lighter(124)
                                      : sidebarPalette.color(QPalette::AlternateBase);
    if (darkSidebar && sidebarHover.lightness() > 115)
        sidebarHover = sidebarBg.lighter(116);
    const QColor hoverText = macReadableSelectionText(sidebarHover);
    QColor selectedBg = macSettingsSelectionBackground(sidebarPalette, darkSidebar);
    if (darkSidebar && selectedBg.lightness() < 95)
        selectedBg = selectedBg.lighter(135);
    const QColor selectedText = macReadableSelectionText(selectedBg);
    QColor focusBg = darkSidebar ? selectedBg.darker(145) : selectedBg.lighter(180);
    focusBg.setAlpha(darkSidebar ? 170 : 95);
    QColor focusBorder = selectedBg;
    if (darkSidebar && focusBorder.lightness() < 130)
        focusBorder = focusBorder.lighter(150);
    else if (!darkSidebar && focusBorder.lightness() > 190)
        focusBorder = focusBorder.darker(125);
    const QString focusBackground = QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(focusBg.red())
        .arg(focusBg.green())
        .arg(focusBg.blue())
        .arg(focusBg.alpha());

    listWidget->setStyleSheet(QStringLiteral(
        "QListWidget {"
        " background-color: %1;"
        " border: 1px solid %2;"
        " border-radius: 10px;"
        " outline: none;"
        " padding: 4px;"
        " font-size: 13px;"
        "}"
        "QListWidget::item {"
        " border: 1px solid transparent;"
        " border-radius: 8px;"
        " padding: 3px 10px;"
        " margin: 1px 0px;"
        " color: palette(text);"
        "}"
        "QListWidget::item:selected,"
        "QListWidget::item:selected:active,"
        "QListWidget::item:selected:!active {"
        " background-color: %4;"
        " color: %5;"
        " border: 1px solid %4;"
        "}"
        "QListWidget::item:focus:!selected {"
        " background-color: %6;"
        " color: palette(text);"
        " border: 1px solid %7;"
        "}"
        "QListWidget::item:hover:!selected,"
        "QListWidget::item:hover:!selected:active,"
        "QListWidget::item:hover:!selected:!active {"
        " background-color: %3;"
        " color: %8;"
        " border: 1px solid %3;"
        "}"
    ).arg(sidebarBg.name(), sidebarBorder.name(), sidebarHover.name(),
          selectedBg.name(), selectedText.name(),
          focusBackground, focusBorder.name(), hoverText.name()));
}

QWidget *wrapSettingsPage(QWidget *owner, QWidget *page)
{
    if (page)
        page->show();

    QFrame *panel = new QFrame(owner);
    panel->setObjectName(QStringLiteral("settingsPagePanel"));
    panel->setFrameShape(QFrame::StyledPanel);
    panel->setFrameShadow(QFrame::Plain);
    applyMacSettingsPanelStyle(panel);

    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(8, 4, 8, 8);
    layout->setSpacing(0);
    layout->addWidget(page);

    return panel;
}

void polishMacScrollArea(QScrollArea *scrollArea)
{
    if (!scrollArea)
        return;

    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->viewport()->setAutoFillBackground(true);

    QPalette palette = QApplication::palette(scrollArea->viewport());
    palette.setColor(QPalette::Window, palette.color(QPalette::AlternateBase));
    palette.setColor(QPalette::Base, palette.color(QPalette::AlternateBase));
    scrollArea->viewport()->setPalette(palette);
}

void polishMacSettingsPage(QWidget *page)
{
    if (!page)
        return;

    if (auto *rootLayout = page->layout()) {
        auto margins = rootLayout->contentsMargins();
        if (margins.left() < 10 && margins.top() < 10 && margins.right() < 10 && margins.bottom() < 10)
            rootLayout->setContentsMargins(6, 6, 6, 6);
        if (rootLayout->spacing() >= 0 && rootLayout->spacing() < 4)
            rootLayout->setSpacing(4);
    }

    for (auto *layout : page->findChildren<QLayout*>()) {
        auto margins = layout->contentsMargins();
        if (margins.left() == 0 && margins.top() == 0 && margins.right() == 0 && margins.bottom() == 0)
            layout->setContentsMargins(2, 2, 2, 2);
        if (layout->spacing() >= 0 && layout->spacing() < 3)
            layout->setSpacing(3);
    }

    for (auto *groupBox : page->findChildren<QGroupBox*>()) {
        if (!groupBox->layout())
            continue;

        auto margins = groupBox->layout()->contentsMargins();
        groupBox->layout()->setContentsMargins(
            qMax(margins.left(), 6),
            qMax(margins.top(), 12),
            qMax(margins.right(), 6),
            qMax(margins.bottom(), 6)
        );
    }

    for (auto *form : page->findChildren<QFormLayout*>()) {
        if (form->horizontalSpacing() < 6)
            form->setHorizontalSpacing(6);
        if (form->verticalSpacing() < 4)
            form->setVerticalSpacing(4);
    }

    for (auto *tabs : page->findChildren<QTabWidget*>()) {
        tabs->setDocumentMode(false);
        tabs->setUsesScrollButtons(false);
        applyMacSettingsTabWidgetStyle(tabs);
        if (auto *tabBar = tabs->tabBar()) {
            tabBar->setExpanding(false);
            tabBar->setElideMode(Qt::ElideNone);
            tabBar->setUsesScrollButtons(false);
            tabBar->setDrawBase(false);
        }
    }
}
#endif
}

Settings::Settings(): is_dirty(false), appearance_style_in_progress(false)
{
    setupUi(this);

    
#ifdef Q_OS_MAC
    // Keep the native macOS group box painting. Overriding QGroupBox title
    // rendering with stylesheets causes black title bars with Qt 6 / qmacstyle.
    for (auto *groupBox : findChildren<QGroupBox*>()) {
        if (!groupBox->isFlat() && groupBox->layout()) {
            auto margins = groupBox->layout()->contentsMargins();
            groupBox->layout()->setContentsMargins(
                qMax(margins.left(), 8),
                qMax(margins.top(), 18),
                qMax(margins.right(), 8),
                qMax(margins.bottom(), 8)
            );
        }
    }

    for (auto *layout : findChildren<QLayout*>()) {
        if (layout->spacing() >= 0 && layout->spacing() < 3)
            layout->setSpacing(3);
    }

    for (auto *form : findChildren<QFormLayout*>()) {
        if (form->horizontalSpacing() < 6)
            form->setHorizontalSpacing(6);
        if (form->verticalSpacing() < 4)
            form->setVerticalSpacing(4);
    }

    for (auto *grid : findChildren<QGridLayout*>()) {
        if (grid->horizontalSpacing() >= 0 && grid->horizontalSpacing() < 6)
            grid->setHorizontalSpacing(6);
        if (grid->verticalSpacing() >= 0 && grid->verticalSpacing() < 4)
            grid->setVerticalSpacing(4);
    }

    for (auto *cb : findChildren<QCheckBox*>()) {
        cb->setMinimumHeight(22);
    }

    for (auto *rb : findChildren<QRadioButton*>()) {
        rb->setMinimumHeight(22);
    }
#endif
    init();

    setWindowTitle(tr("Preferences"));
}

Settings::~Settings(){
    if (is_dirty){
        qtCtx()->settings()->save();
        qtCtx()->dcCtx().getSettingsManager()->save();
    }
}

void Settings::changeEvent(QEvent *event)
{
    QDialog::changeEvent(event);

#ifdef Q_OS_MAC
    if (event->type() == QEvent::PaletteChange ||
        event->type() == QEvent::ApplicationPaletteChange ||
        event->type() == QEvent::StyleChange) {
        if (appearance_style_in_progress)
            return;

        appearance_style_in_progress = true;
        applyMacSettingsSidebarStyle(listWidget);

        for (auto *panel : findChildren<QFrame*>(QStringLiteral("settingsPagePanel")))
            applyMacSettingsPanelStyle(panel);

        for (auto *tabs : findChildren<QTabWidget*>())
            applyMacSettingsTabWidgetStyle(tabs);

        for (auto *scrollArea : findChildren<QScrollArea*>())
            polishMacScrollArea(scrollArea);

        appearance_style_in_progress = false;
    }
#endif
}

void Settings::init(){
    WulforUtil *WU = qtCtx()->wulforUtil();

    QListWidgetItem *item = new QListWidgetItem(WU->getPixmap(WulforUtil::eiSETTINGS_MAIN), tr("Main"), listWidget);
    SettingsPersonal *personal = new SettingsPersonal(this);
    connect(this, &Settings::timeToDie, personal, &SettingsPersonal::ok);
    widgets.insert(item, (int)Page::Personal);

    item = new QListWidgetItem(WU->getPixmap(WulforUtil::eiSETTINGS_CONNECTION), tr("Connection"), listWidget);
    SettingsConnection *connection = new SettingsConnection(this);
    connect(this, &Settings::timeToDie, connection, &SettingsConnection::ok);
    widgets.insert(item, (int)Page::Connection);

    item = new QListWidgetItem(WU->getPixmap(WulforUtil::eiSETTINGS_DOWNLOADS), tr("Downloads"), listWidget);
    SettingsDownloads *downloads = new SettingsDownloads(this);
    connect(this, &Settings::timeToDie, downloads, &SettingsDownloads::ok);
    widgets.insert(item, (int)Page::Downloads);

    item = new QListWidgetItem(WU->getPixmap(WulforUtil::eiFOLDER_BLUE), tr("Sharing"), listWidget);
    SettingsSharing *sharing = new SettingsSharing(this);
    connect(this, &Settings::timeToDie, sharing, &SettingsSharing::ok);
    widgets.insert(item, (int)Page::Sharing);

    item = new QListWidgetItem(WU->getPixmap(WulforUtil::eiSETTINGS_GUI), tr("GUI"), listWidget);
    SettingsGUI *gui = new SettingsGUI(this);
    connect(this, &Settings::timeToDie, gui, &SettingsGUI::ok);
    widgets.insert(item, (int)Page::GUI);

    item = new QListWidgetItem(WU->getPixmap(WulforUtil::eiMESSAGE), tr("Notifications"), listWidget);
    SettingsNotification *notify = new SettingsNotification(this);
    connect(this, &Settings::timeToDie, notify, &SettingsNotification::ok);
    widgets.insert(item, (int)Page::Notifications);

    item = new QListWidgetItem(WU->getPixmap(WulforUtil::eiOPEN_LOG_FILE), tr("Logs"), listWidget);
    SettingsLog *logs = new SettingsLog(this);
    connect(this, &Settings::timeToDie, logs, &SettingsLog::ok);
    widgets.insert(item, (int)Page::Logs);

    item = new QListWidgetItem(WU->getPixmap(WulforUtil::eiUSERS), tr("User Commands"), listWidget);
    SettingsUC *ucs = new SettingsUC(this);
    connect(this, &Settings::timeToDie, ucs, &SettingsUC::ok);
    widgets.insert(item, (int)Page::UserCommands);

    item = new QListWidgetItem(WU->getPixmap(WulforUtil::eiSETTINGS_SHORTCUTS), tr("Shortcuts"), listWidget);
    SettingsShortcuts *sshs = new SettingsShortcuts(this);
    connect(this, &Settings::timeToDie, sshs, &SettingsShortcuts::ok);
    widgets.insert(item, (int)Page::Shortcuts);
    
    item = new QListWidgetItem(WU->getPixmap(WulforUtil::eiHISTORY), tr("History"), listWidget);
    SettingsHistory *shist = new SettingsHistory(this);
    connect(this, &Settings::timeToDie, shist, &SettingsHistory::ok);
    widgets.insert(item, (int)Page::History);

    item = new QListWidgetItem(WU->getPixmap(WulforUtil::eiCONSOLE), tr("Advanced"), listWidget);
    SettingsAdvanced *sadv = new SettingsAdvanced(this);
    connect(this, &Settings::timeToDie, sadv, &SettingsAdvanced::ok);
    widgets.insert(item, (int)Page::Advanced);

#ifdef Q_OS_MAC
    for (QWidget *page : {static_cast<QWidget*>(personal), static_cast<QWidget*>(connection), static_cast<QWidget*>(downloads),
                          static_cast<QWidget*>(sharing), static_cast<QWidget*>(gui), static_cast<QWidget*>(notify),
                          static_cast<QWidget*>(logs), static_cast<QWidget*>(ucs), static_cast<QWidget*>(sshs),
                          static_cast<QWidget*>(shist), static_cast<QWidget*>(sadv)}) {
        polishMacSettingsPage(page);
    }
#endif

    listWidget->setIconSize(QSize(18, 18));
    listWidget->setSpacing(2);
    const int sidebarWidth = settingsSidebarWidth(listWidget);
    listWidget->setMinimumWidth(sidebarWidth);
    listWidget->setMaximumWidth(sidebarWidth);
#ifdef Q_OS_MAC
    applyMacSettingsSidebarStyle(listWidget);
#endif

    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(0);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
#ifdef Q_OS_MAC
    splitter->setStyleSheet(QStringLiteral("QSplitter::handle { background: transparent; width: 0px; }"));
#endif

    stackedWidget->insertWidget((int)Page::Personal, prepareWidget(personal));
    stackedWidget->insertWidget((int)Page::Connection, prepareWidget(connection));
    stackedWidget->insertWidget((int)Page::Downloads, prepareWidget(downloads));
    stackedWidget->insertWidget((int)Page::Sharing, prepareWidget(sharing));
    stackedWidget->insertWidget((int)Page::GUI, prepareWidget(gui));
    stackedWidget->insertWidget((int)Page::Notifications, prepareWidget(notify));
    stackedWidget->insertWidget((int)Page::Logs, prepareWidget(logs));
    stackedWidget->insertWidget((int)Page::UserCommands, prepareWidget(ucs));
    stackedWidget->insertWidget((int)Page::Shortcuts, prepareWidget(sshs));
    stackedWidget->insertWidget((int)Page::History, prepareWidget(shist));
    stackedWidget->insertWidget((int)Page::Advanced, prepareWidget(sadv));

    stackedWidget->setCurrentIndex(0);

    setMinimumSize(900, 640);
    if (qtCtx()->settings()->getVar("settings/dialog-size").isValid())
        resize(qtCtx()->settings()->getVar("settings/dialog-size").toSize().expandedTo(minimumSize()));
    else
        resize(minimumSize());

    // Convenient scrolling of widgets with the mouse, as well as from the touchpad and from the touchscreen:
    for (auto &asa : stackedWidget->findChildren<QAbstractScrollArea*>()) {
        setMouseScroller(asa->viewport());
    }
    // Smooth scrolling. See: http://stackoverflow.com/questions/19298486/qscroller-kinetic-scrolling-is-not-smooth)
    for (auto &aiv : stackedWidget->findChildren<QAbstractItemView*>()) {
        aiv->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    }

    connect(listWidget, &QListWidget::itemClicked, this, &Settings::slotItemActivated);
    connect(listWidget, &QListWidget::itemActivated, this, &Settings::slotItemActivated);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &Settings::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &Settings::reject);
    connect(this, &QDialog::accepted, this, &Settings::timeToDie);
    connect(this, &QDialog::accepted, this, &Settings::dirty);

#ifdef Q_OS_MAC
    if (auto *okButton = buttonBox->button(QDialogButtonBox::Ok)) {
        okButton->setMinimumSize(74, 28);
        okButton->setAutoDefault(false);
    }
    if (auto *cancelButton = buttonBox->button(QDialogButtonBox::Cancel)) {
        cancelButton->setMinimumSize(74, 28);
        cancelButton->setAutoDefault(false);
    }
    buttonBox->setCenterButtons(false);
#endif
}

void Settings::setMouseScroller(QWidget *w){
    QScroller::grabGesture(w, QScroller::LeftMouseButtonGesture);
    QScroller *scroller = QScroller::scroller(w);
    QScrollerProperties properties = scroller->scrollerProperties();
    QVariant overshootPolicy = QVariant::fromValue<QScrollerProperties::OvershootPolicy>(QScrollerProperties::OvershootAlwaysOff);
    properties.setScrollMetric(QScrollerProperties::VerticalOvershootPolicy, overshootPolicy);
    properties.setScrollMetric(QScrollerProperties::HorizontalOvershootPolicy, overshootPolicy);
    scroller->setScrollerProperties(properties);
}

QWidget *Settings::prepareWidget(QWidget *w)
{
    const bool containsTabs = !w->findChildren<QTabWidget*>().isEmpty();
    if (containsTabs) {
        for (auto *tw : w->findChildren<QTabWidget*>()) {
            tw->setDocumentMode(false);
            QList<QWidget*> pages;
            QStringList titles;

            for (int k = 0; k < tw->count(); ++k) {
                pages << tw->widget(k);
                titles << tw->tabText(k);
            }

            while (tw->count() > 0)
                tw->removeTab(0);

            // Content of each page should be placed into an independent QScrollArea
            for (int k = 0; k < pages.size(); ++k) {
                QScrollArea *scrollArea = new QScrollArea(this);
                pages.at(k)->show();
#ifdef Q_OS_MAC
                scrollArea->setWidget(wrapSettingsPage(this, pages.at(k)));
                polishMacScrollArea(scrollArea);
#else
                scrollArea->setWidget(pages.at(k));
                scrollArea->setWidgetResizable(true);
                scrollArea->setFrameShape(QFrame::NoFrame);
#endif
                tw->addTab(scrollArea, titles.at(k));
            }
            tw->setCurrentIndex(0);
#ifdef Q_OS_MAC
            applyMacSettingsTabWidgetStyle(tw);
#endif
        }
    }
    else { // Single widget may be placed directly to QScrollArea
        QScrollArea *scrollArea = new QScrollArea(this);
#ifdef Q_OS_MAC
        scrollArea->setWidget(wrapSettingsPage(this, w));
        polishMacScrollArea(scrollArea);
#else
        scrollArea->setWidget(w);
        scrollArea->setWidgetResizable(true);
        scrollArea->setFrameShape(QFrame::NoFrame);
#endif
#ifndef Q_OS_MAC
        w->layout()->setContentsMargins(0, 0, 0, 0);
#endif
        return scrollArea;
    }

#ifndef Q_OS_MAC
    w->layout()->setContentsMargins(0, 0, 0, 0);
#endif
    return w;
}

void Settings::slotItemActivated(QListWidgetItem *item){
    if (widgets.contains(item)){
        stackedWidget->setCurrentIndex(widgets[item]);
    }
}

void Settings::dirty(){
    is_dirty = true;

    qtCtx()->settings()->setVar("settings/dialog-size", size());
}

void Settings::navigate(enum Settings::Page pg, int tab){
    listWidget->setCurrentRow((int)pg);
    stackedWidget->setCurrentIndex((int)pg);

    if (tab < 0)
        return;

    QWidget *wgt = stackedWidget->currentWidget();
    QTabWidget *tabwgt = wgt->findChild<QTabWidget*>("tabWidget");

    if (tabwgt)
        tabwgt->setCurrentIndex(tab);
}
