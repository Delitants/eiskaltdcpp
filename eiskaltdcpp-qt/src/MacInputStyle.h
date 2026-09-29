#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QPalette>
#include <QString>

// Shared by the application and UI regression fixtures.
namespace mac_input_style {
inline QString macBundledStyleIcon(const QString &name)
{
    const QString bundlePath = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(
        QStringLiteral("../Resources/icons/appl/default/") + name);

    if (QFileInfo::exists(bundlePath))
        return QStringLiteral("url(\"%1\")").arg(bundlePath);

    return QStringLiteral("none");
}

inline bool isDarkMacPalette(const QPalette &pal)
{
    return (pal.color(QPalette::Window).lightness() + pal.color(QPalette::Base).lightness()) / 2 < 128;
}

inline QColor macBorderColor(const QPalette &pal, const bool strong)
{
    const bool dark = isDarkMacPalette(pal);
    const QColor window = pal.color(QPalette::Window);
    QColor border = dark
            ? window.lighter(strong ? 185 : 165)
            : window.darker(strong ? 155 : 135);

    if (qAbs(border.lightness() - window.lightness()) < (strong ? 40 : 26)) {
        const QColor text = pal.color(QPalette::Text);
        border = dark
                ? text.lighter(strong ? 170 : 145)
                : text.darker(strong ? 170 : 145);
    }

    return border;
}

inline QColor macFocusColor(const QPalette &pal)
{
    QColor focus = pal.color(QPalette::Highlight);
    const bool dark = isDarkMacPalette(pal);

    if (dark && focus.lightness() < 150)
        focus = focus.lighter(145);
    else if (!dark && focus.lightness() > 205)
        focus = focus.darker(118);

    return focus;
}

inline QColor macReadableTextColor(const QColor &base, const bool dark)
{
    QColor text = dark ? QColor(242, 242, 242) : QColor(18, 18, 18);

    if (qAbs(text.lightness() - base.lightness()) < 96)
        text = dark ? QColor(Qt::white) : QColor(Qt::black);

    return text;
}

inline QColor macReadableForegroundOn(const QColor &background)
{
    return background.lightness() < 150 ? QColor(Qt::white) : QColor(18, 18, 18);
}

inline QColor macSelectionBackground(const QPalette &pal)
{
    QColor selection = pal.color(QPalette::Highlight);
    const bool dark = isDarkMacPalette(pal);

    if (dark && (selection.lightness() > 190 || selection.saturation() < 35))
        selection = QColor(55, 112, 220);
    else if (dark && selection.lightness() < 80)
        selection = selection.lighter(145);
    else if (!dark && selection.lightness() > 230)
        selection = selection.darker(112);

    return selection;
}

inline QColor macSoftAlternateBase(const QColor &base, const bool dark)
{
    QColor alternate = dark ? base.lighter(112) : base.darker(104);

    if (qAbs(alternate.lightness() - base.lightness()) > 18)
        alternate = dark ? base.lighter(106) : base.darker(102);

    if (qAbs(alternate.lightness() - base.lightness()) < 4)
        alternate = dark ? base.lighter(118) : base.darker(108);

    return alternate;
}

inline QString macInputContrastStyle(const QPalette &pal)
{
    const bool dark = isDarkMacPalette(pal);
    const QColor inputBorder = macBorderColor(pal, false);
    QColor panelBorder = pal.color(QPalette::Mid);
    if (qAbs(panelBorder.lightness() - pal.color(QPalette::Base).lightness()) < 22)
        panelBorder = macBorderColor(pal, true);
    const QColor focusBorder = macFocusColor(pal);
    const QColor base = pal.color(QPalette::Base);
    const QColor alternate = macSoftAlternateBase(base, dark);
    const QColor text = macReadableTextColor(base, dark);
    const QColor highlight = macSelectionBackground(pal);
    const QColor highlightedText = macReadableForegroundOn(highlight);
    const QColor header = dark ? pal.color(QPalette::Window).lighter(115)
                               : pal.color(QPalette::Window).darker(104);
    const QColor disabledText = pal.color(QPalette::Disabled, QPalette::Text);
    const QString comboArrow = macBundledStyleIcon(dark ? QStringLiteral("combo-arrow-down-light.svg")
                                                       : QStringLiteral("combo-arrow-down-dark.svg"));
    const QString menuArrow = macBundledStyleIcon(dark ? QStringLiteral("menu-arrow-right-light.svg")
                                                      : QStringLiteral("menu-arrow-right-dark.svg"));
    const QString menuArrowSelected = macBundledStyleIcon(QStringLiteral("menu-arrow-right-light.svg"));

    return QStringLiteral(
        "QLineEdit, QTextEdit, QPlainTextEdit, QComboBox, QAbstractSpinBox {"
        " border: 1px solid %1;"
        " border-radius: 6px;"
        " padding: 2px 6px;"
        " background-color: %4;"
        " color: %6;"
        " selection-background-color: %7;"
        " selection-color: %8;"
        "}"
        "QComboBox {"
        " padding: 3px 30px 3px 9px;"
        " min-height: 24px;"
        " combobox-popup: 0;"
        "}"
        "QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus, QComboBox:focus, QAbstractSpinBox:focus {"
        " border: 1px solid %2;"
        "}"
        "QComboBox::drop-down {"
        " subcontrol-origin: border;"
        " subcontrol-position: top right;"
        " width: 24px;"
        " border-left: 1px solid %1;"
        " border-top-right-radius: 6px;"
        " border-bottom-right-radius: 6px;"
        "}"
        "QComboBox::down-arrow {"
        " image: %11;"
        " width: 9px;"
        " height: 9px;"
        " margin-right: 7px;"
        "}"
        "QComboBox::down-arrow:disabled {"
        " image: %11;"
        "}"
        "QAbstractSpinBox::up-button, QAbstractSpinBox::down-button {"
        " border-left: 1px solid %1;"
        "}"
        "QAbstractItemView, QListView, QTreeView, QTableView {"
        " border: 1px solid %3;"
        " background-color: %4;"
        " alternate-background-color: %5;"
        " color: %6;"
        " selection-background-color: %7;"
        " selection-color: %8;"
        " outline: 0;"
        "}"
        "QListView::item, QTreeView::item, QTableView::item {"
        " color: %6;"
        "}"
        "QListView::item:alternate, QTreeView::item:alternate, QTableView::item:alternate {"
        " background-color: %5;"
        "}"
        "QListView::item:selected, QTreeView::item:selected, QTableView::item:selected {"
        " background-color: %7;"
        " color: %8;"
        "}"
        "QComboBox QAbstractItemView {"
        " border: 1px solid %3;"
        " border-radius: 8px;"
        " padding: 4px;"
        " background-color: %4;"
        " color: %6;"
        " selection-background-color: %7;"
        " selection-color: %8;"
        "}"
        "QComboBox QAbstractItemView::item {"
        " padding: 3px 10px;"
        " min-height: 20px;"
        "}"
        "QAbstractItemView:disabled, QListView:disabled, QTreeView:disabled, QTableView:disabled {"
        " color: %10;"
        "}"
        "QMenu {"
        " background-color: %4;"
        " color: %6;"
        " border: 1px solid %3;"
        " border-radius: 8px;"
        " padding: 4px;"
        "}"
        "QMenu::item {"
        " padding: 4px 30px 4px 12px;"
        " border-radius: 5px;"
        " min-width: 140px;"
        "}"
        "QMenu::item:selected {"
        " background-color: %7;"
        " color: %8;"
        "}"
        "QMenu::item:disabled {"
        " color: %10;"
        "}"
        "QMenu::separator {"
        " height: 1px;"
        " background: %3;"
        " margin: 4px 7px;"
        "}"
        "QMenu::indicator {"
        " width: 14px;"
        " height: 14px;"
        " left: 5px;"
        "}"
        "QMenu::right-arrow {"
        " image: %12;"
        " width: 9px;"
        " height: 9px;"
        " margin-right: 8px;"
        "}"
        "QMenu::right-arrow:selected {"
        " image: %13;"
        "}"
        "QHeaderView::section {"
        " background-color: %9;"
        " color: %6;"
        " border: 0px;"
        " border-right: 1px solid %3;"
        " border-bottom: 1px solid %3;"
        " padding: 2px 5px;"
        "}"
        "QTableCornerButton::section {"
        " background-color: %9;"
        " border: 0px;"
        " border-right: 1px solid %3;"
        " border-bottom: 1px solid %3;"
        "}"
        "QFrame#frame_INPUT {"
        " border: 1px solid %3;"
        " border-radius: 8px;"
        " background-color: %4;"
        "}"
        "QFrame#settingsPagePanel {"
        " border: 1px solid %3;"
        " background-color: %4;"
        "}"
    ).arg(inputBorder.name(), focusBorder.name(), panelBorder.name(),
          base.name(), alternate.name(), text.name(), highlight.name(),
          highlightedText.name(), header.name(), disabledText.name(),
          comboArrow, menuArrow, menuArrowSelected);
}

} // namespace mac_input_style
