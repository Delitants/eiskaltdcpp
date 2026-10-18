#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QPalette>
#include <QString>
#include <QListWidget>

// Shared by the application and UI regression fixtures.
namespace mac_input_style {
inline int settingsSidebarWidth(QListWidget *listWidget)
{
    if (!listWidget)
        return 160;

    const QFontMetrics metrics(listWidget->font());
    int textWidth = 0;
    for (int row = 0; row < listWidget->count(); ++row) {
        if (const auto *item = listWidget->item(row))
            textWidth = qMax(textWidth, metrics.horizontalAdvance(item->text()));
    }
    listWidget->doItemsLayout();
    const int iconWidth = listWidget->iconSize().isValid() ? listWidget->iconSize().width() : 0;
    // Include item padding, frame, icon spacing and a possible vertical scrollbar.
    return qMax(160, qMax(listWidget->sizeHintForColumn(0) + 24,
                         textWidth + iconWidth + 48));
}

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
    const QColor text = pal.color(QPalette::Text);
    const QColor highlight = macSelectionBackground(pal);
    const QColor highlightedText = macReadableForegroundOn(highlight);
    const QColor header = dark ? pal.color(QPalette::Window).lighter(115)
                               : pal.color(QPalette::Window).darker(104);
    const QColor disabledText = pal.color(QPalette::Disabled, QPalette::Text);

    return QStringLiteral(
        "QLineEdit, QTextEdit, QPlainTextEdit, QComboBox, QAbstractSpinBox {"
        " border: 1px solid %1;"
        " border-radius: 3px;"
        " padding: 2px 6px;"
        " background-color: %4;"
        " color: %6;"
        " selection-background-color: %7;"
        " selection-color: %8;"
        "}"
        "QLineEdit, QAbstractSpinBox { min-height: 18px; }"
        "QAbstractSpinBox { min-height: 16px; }"
        "QCheckBox, QRadioButton { min-height: 24px; spacing: 6px; }"
        "QLineEdit:disabled, QTextEdit:disabled, QPlainTextEdit:disabled, QComboBox:disabled, QAbstractSpinBox:disabled {"
        " color: %10; background-color: palette(disabled, base);"
        "}"
        "QComboBox {"
        " padding: 2px 24px 2px 6px;"
        " min-height: 18px;"
        "}"
        "QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus, QComboBox:focus, QAbstractSpinBox:focus {"
        " border: 1px solid %2;"
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
        " border-radius: 3px;"
        " padding: 2px;"
        " background-color: %4;"
        " color: %6;"
        " selection-background-color: %7;"
        " selection-color: %8;"
        "}"
        "QComboBox QAbstractItemView::item {"
        " padding: 2px 6px;"
        " min-height: 20px;"
        "}"
        "QAbstractItemView:disabled, QListView:disabled, QTreeView:disabled, QTableView:disabled {"
        " color: %10;"
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
        " border-radius: 4px;"
        " background-color: %4;"
        "}"
        "QFrame#settingsPagePanel {"
        " border: 1px solid %3;"
        " background-color: %4;"
        "}"
    ).arg(inputBorder.name(), focusBorder.name(), panelBorder.name(),
          base.name(), alternate.name(), text.name(), highlight.name(),
          highlightedText.name(), header.name(), disabledText.name());
}

} // namespace mac_input_style
