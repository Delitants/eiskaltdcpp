#include "dcpp/stdinc.h"
#include "dcpp/ScopedFunctor.h"
#include "ApplicationQuitTestFixture.h"
#include "DownloadToSettings.h"
#include "MacInputStyle.h"
#include "Settings.h"
#include "SettingsDownloads.h"
#include "PreferencesCheckIndicators.h"
#include "TorrentCheckDelegate.h"
#include "updates/SettingsUpdates.h"
#include "updates/UpdateController.h"

#include <QAbstractItemDelegate>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QGroupBox>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalSpy>
#include <QStyledItemDelegate>
#include <QStyleOptionButton>
#include <QStyleOptionViewItem>
#include <QTabWidget>
#include <QTreeWidget>
#include <QTimer>
#include <QtMath>
#include <QVBoxLayout>
#include <typeinfo>

namespace {
class CheckboxProbe : public QCheckBox {
public:
    using QCheckBox::QCheckBox;
    QStyleOptionButton option() const
    {
        QStyleOptionButton result;
        initStyleOption(&result);
        return result;
    }
};

QImage paintQueueRow(QListWidget &list, int row, const QStyleOptionViewItem &option)
{
    QImage result(500, 144, QImage::Format_ARGB32_Premultiplied);
    result.fill(option.palette.base().color());
    QPainter painter(&result);
    list.itemDelegate()->paint(&painter, option, list.model()->index(row, 0));
    painter.end();
    return result;
}

QRect paintedInk(const QImage &image, QRect band, QColor background)
{
    QRect bounds;
    band = band.intersected(image.rect());
    for (int y = band.top(); y <= band.bottom(); ++y) {
        for (int x = band.left(); x <= band.right(); ++x) {
            const QColor pixel = image.pixelColor(x, y);
            const int contrast = qMax(qAbs(pixel.red() - background.red()),
                qMax(qAbs(pixel.green() - background.green()), qAbs(pixel.blue() - background.blue())));
            if (contrast > 20) bounds = bounds.united(QRect(x, y, 1, 1));
        }
    }
    return bounds;
}

TEST_CASE("Shared checkbox styling vertically centers both indicator and label",
          "[qt][settings-downloads][settings-check-indicators]")
{
    for (bool dark : {false, true}) {
        QWidget host;
        QPalette palette = host.palette();
        palette.setColor(QPalette::Window, dark ? QColor(35, 35, 35) : QColor(240, 240, 240));
        palette.setColor(QPalette::Base, dark ? QColor(28, 28, 28) : QColor(Qt::white));
        palette.setColor(QPalette::Text, dark ? QColor(235, 235, 235) : QColor(24, 24, 24));
        palette.setColor(QPalette::WindowText, palette.color(QPalette::Text));
        host.setPalette(palette);
        host.setStyleSheet(mac_input_style::macInputContrastStyle(palette));
        CheckboxProbe checkbox("Keep completed downloads", &host);
        checkbox.ensurePolished();
        checkbox.resize(checkbox.sizeHint());
        for (Qt::LayoutDirection direction : {Qt::LeftToRight, Qt::RightToLeft}) {
            checkbox.setLayoutDirection(direction);
            for (bool enabled : {false, true}) {
                checkbox.setEnabled(enabled);
                for (bool checked : {false, true}) {
                    CAPTURE(dark, direction, enabled, checked);
                    checkbox.setChecked(checked);
                    const auto option = checkbox.option();
                    const QRect indicator = checkbox.style()->subElementRect(
                        QStyle::SE_CheckBoxIndicator, &option, &checkbox);
                    const QRect label = checkbox.style()->subElementRect(
                        QStyle::SE_CheckBoxContents, &option, &checkbox);
                    CHECK(checkbox.rect().contains(indicator));
                    CHECK(qAbs(indicator.center().y() - checkbox.rect().center().y()) <= 2);
                    CHECK(qAbs(indicator.center().y() - label.center().y()) <= 2);
                }
            }
        }
    }
}
}

TEST_CASE("Actual Preferences checkbox and radio ink is vertically aligned, not merely its style rect",
          "[qt][settings-downloads][settings-check-indicators][cocoa-pixel-probe]")
{
    IsolatedMainWindow fixture;
    Settings preferences;
    preferences.resize(1100, 800);
    preferences.show();
    QCoreApplication::processEvents();
    const auto inspect = [&](QAbstractButton *button, bool radio) {
        REQUIRE(button);
        button->ensurePolished();
        button->resize(button->sizeHint().expandedTo(QSize(240, 28)));
        button->clearFocus();
        const auto image = button->grab().toImage().scaled(button->size(),
            Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        QStyleOptionButton option;
        option.initFrom(button);
        option.text = button->text();
        const QRect indicator = button->style()->subElementRect(radio
            ? QStyle::SE_RadioButtonIndicator : QStyle::SE_CheckBoxIndicator, &option, button);
        const QRect label = button->style()->subElementRect(radio
            ? QStyle::SE_RadioButtonContents : QStyle::SE_CheckBoxContents, &option, button);
        const QColor background = image.pixelColor(image.width() - 1, image.height() - 1);
        // Only the horizontal bands come from style geometry. Scan the full
        // rendered height so a native primitive painted above its rect is seen.
        const QRect indicatorInk = paintedInk(image,
            QRect(indicator.left() - 2, 0, indicator.width() + 4, image.height()), background);
        const QRect labelInk = paintedInk(image,
            QRect(label.left() + 1, 0, label.width() - 2, image.height()), background);
        CAPTURE(button->objectName(), indicatorInk, labelInk);
        REQUIRE_FALSE(indicatorInk.isNull());
        REQUIRE_FALSE(labelInk.isNull());
        CHECK(qAbs(indicatorInk.center().y() - labelInk.center().y()) <= 2);
    };
    preferences.navigate(Settings::Page::Connection, 0);
    QCoreApplication::processEvents();
    inspect(preferences.findChild<QCheckBox*>("checkBox_AUTO_DETECT_CONNECTION"), false);
    preferences.navigate(Settings::Page::Downloads, 0);
    QCoreApplication::processEvents();
    inspect(preferences.findChild<QCheckBox*>("checkBox_NO_USE_TEMP_DIR"), false);
    preferences.navigate(Settings::Page::GUI, 0);
    QCoreApplication::processEvents();
    inspect(preferences.findChild<QRadioButton*>("radioButton_REMEMBER"), true);
    inspect(preferences.findChild<QRadioButton*>("radioButton_SHOW"), true);
    preferences.reject();
}

TEST_CASE("Downloads shows saved destination shortcuts beside default and incomplete folders",
          "[qt][settings-downloads]")
{
    IsolatedMainWindow fixture;
    auto *settings = fixture.core->getSettingsManager();
    const QString destination = fixture.home.filePath("complete/");
    const QString incomplete = fixture.home.filePath("incomplete/");
    settings->set(dcpp::SettingsManager::DOWNLOAD_DIRECTORY, destination.toStdString());
    settings->set(dcpp::SettingsManager::TEMP_DOWNLOAD_DIRECTORY, incomplete.toStdString());
    const QList<DownloadToEntry> entries{{fixture.home.filePath("music"), "Music"},
                                       {fixture.home.filePath("videos"), "Videos"}};
    const auto encoded = encodeDownloadTo(entries);
    fixture.gui->settings()->setStr(WS_DOWNLOADTO_PATHS, encoded.first);
    fixture.gui->settings()->setStr(WS_DOWNLOADTO_ALIASES, encoded.second);

    SettingsDownloads page;
    page.resize(680, 760);
    page.show();
    QCoreApplication::processEvents();
    auto *tabs = page.findChild<QTabWidget*>("tabWidget");
    auto *shortcuts = page.findChild<QTreeWidget*>("treeWidget");
    auto *defaultPath = page.findChild<QLineEdit*>("lineEdit_DLDIR");
    auto *incompletePath = page.findChild<QLineEdit*>("lineEdit_UNF_DL_DIR");
    REQUIRE(tabs);
    REQUIRE(shortcuts);
    REQUIRE(defaultPath);
    REQUIRE(incompletePath);
    CHECK(tabs->count() == 3);
    tabs->setCurrentIndex(0);
    CHECK(shortcuts->isVisibleTo(tabs->widget(0)));
    CHECK(defaultPath->isVisibleTo(tabs->widget(0)));
    CHECK(incompletePath->isVisibleTo(tabs->widget(0)));
    REQUIRE(shortcuts->topLevelItemCount() == 2);
    CHECK(shortcuts->topLevelItem(0)->text(0) == entries[0].path);
    CHECK(shortcuts->topLevelItem(0)->text(1) == "Music");
    CHECK(shortcuts->topLevelItem(1)->text(0) == entries[1].path);
    CHECK(shortcuts->topLevelItem(1)->text(1) == "Videos");
    auto *group = qobject_cast<QGroupBox*>(shortcuts->parentWidget());
    REQUIRE(group);
    CHECK(group->title().contains("shortcut", Qt::CaseInsensitive));
    bool explainsSeparation = false;
    for (auto *label : group->findChildren<QLabel*>()) {
        explainsSeparation |= label->text().contains("default", Qt::CaseInsensitive)
            && label->text().contains("incomplete", Qt::CaseInsensitive);
    }
    CHECK(explainsSeparation);

    page.ok();
    CHECK(settings->get(dcpp::SettingsManager::DOWNLOAD_DIRECTORY) == destination.toStdString());
    CHECK(settings->get(dcpp::SettingsManager::TEMP_DOWNLOAD_DIRECTORY) == incomplete.toStdString());
    CHECK(fixture.gui->settings()->getStr(WS_DOWNLOADTO_PATHS) == encoded.first);
    CHECK(fixture.gui->settings()->getStr(WS_DOWNLOADTO_ALIASES) == encoded.second);
    SettingsDownloads reopened;
    auto *restored = reopened.findChild<QTreeWidget*>("treeWidget");
    REQUIRE(restored);
    REQUIRE(restored->topLevelItemCount() == 2);
    CHECK(restored->topLevelItem(1)->text(1) == "Videos");
}

TEST_CASE("Downloads no longer contains the obsolete hidden public-list proxy controls",
          "[qt][settings-downloads]")
{
    IsolatedMainWindow fixture;
    SettingsDownloads page;
    CHECK_FALSE(page.findChild<QWidget*>("lineEdit_PROXY"));
    CHECK_FALSE(page.findChild<QWidget*>("pushButton_CFGLISTS"));
}

TEST_CASE("Every Queue checkbox paints inside its own vertically centered row",
          "[qt][settings-downloads][settings-check-indicators]")
{
    IsolatedMainWindow fixture;
    SettingsDownloads page;
    page.setStyleSheet(mac_input_style::macInputContrastStyle(page.palette()));
    auto *list = page.findChild<QListWidget*>("listWidget");
    REQUIRE(list);
    REQUIRE(list->count() == 14);
    list->ensurePolished();
    for (Qt::LayoutDirection direction : {Qt::LeftToRight, Qt::RightToLeft}) {
        list->setLayoutDirection(direction);
        for (int row = 0; row < list->count(); ++row) {
            CAPTURE(row, direction);
            auto *item = list->item(row);
            REQUIRE(item->flags().testFlag(Qt::ItemIsUserCheckable));
            QStyleOptionViewItem option;
            option.initFrom(list->viewport());
            option.widget = list->viewport();
            option.rect = QRect(8, 56, 460, 32);
            option.direction = direction;
            option.state = QStyle::State_Enabled;
            item->setCheckState(Qt::Unchecked);
            const QImage unchecked = paintQueueRow(*list, row, option);
            item->setCheckState(Qt::Checked);
            const QImage checked = paintQueueRow(*list, row, option);
            const QRect indicatorBand(direction == Qt::LeftToRight ? option.rect.left()
                : option.rect.right() - 27, option.rect.top(), 28, option.rect.height());
            QRect changed;
            int changedPixels = 0, outsideRow = 0;
            for (int y = 0; y < checked.height(); ++y) {
                for (int x = 0; x < checked.width(); ++x) {
                    if (checked.pixel(x, y) == unchecked.pixel(x, y)) continue;
                    if (!option.rect.contains(x, y)) ++outsideRow;
                    if (!indicatorBand.contains(x, y)) continue;
                    changed = changed.united(QRect(x, y, 1, 1));
                    ++changedPixels;
                }
            }
            CHECK(outsideRow == 0);
            CHECK(changedPixels >= 5);
            if (!changed.isNull())
                CHECK(qAbs(changed.center().y() - option.rect.center().y()) <= 4);
        }
    }
}

TEST_CASE("Queue checkbox interaction remains row local after scrolling and persists each option",
          "[qt][settings-downloads][settings-check-indicators]")
{
    IsolatedMainWindow fixture;
    SettingsDownloads page;
    page.resize(680, 600);
    auto *tabs = page.findChild<QTabWidget*>("tabWidget");
    auto *list = page.findChild<QListWidget*>("listWidget");
    REQUIRE(tabs);
    REQUIRE(list);
    for (int tab = 0; tab < tabs->count(); ++tab)
        if (tabs->widget(tab)->isAncestorOf(list)) tabs->setCurrentIndex(tab);
    page.show();
    QCoreApplication::processEvents();
    REQUIRE(list->count() == 14);
    for (int row = 0; row < list->count(); ++row) list->item(row)->setCheckState(Qt::Unchecked);
    for (int row : {0, 4, 13}) {
        CAPTURE(row);
        auto *item = list->item(row);
        list->scrollToItem(item, QAbstractItemView::PositionAtCenter);
        QCoreApplication::processEvents();
        const QRect rect = list->visualItemRect(item);
        REQUIRE(list->viewport()->rect().contains(rect.center()));
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                          QPoint(rect.left() + 10, rect.center().y()));
        CHECK(item->checkState() == Qt::Checked);
        list->setCurrentItem(item);
        QTest::keyClick(list, Qt::Key_Space);
        CHECK(item->checkState() == Qt::Unchecked);
        item->setCheckState(Qt::Checked);
    }
    page.ok();
    const auto *settings = fixture.core->getSettingsManager();
    CHECK(settings->getBool(dcpp::SettingsManager::PRIO_LOWEST));
    CHECK(settings->getBool(dcpp::SettingsManager::AUTO_SEARCH));
    CHECK(settings->getBool(dcpp::SettingsManager::SEGMENTED_DL));
    CHECK_FALSE(settings->getBool(dcpp::SettingsManager::AUTODROP_ALL));
}

TEST_CASE("Queue indicator delegate leaves non-checkable icon rows to standard painting",
          "[qt][settings-downloads][settings-check-indicators]")
{
    IsolatedMainWindow fixture;
    SettingsDownloads page;
    auto *list = page.findChild<QListWidget*>("listWidget");
    REQUIRE(list);
    QPixmap icon(16, 16);
    icon.fill(Qt::red);
    auto *item = new QListWidgetItem(QIcon(icon), "Not a checkbox", list);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    QStyleOptionViewItem option;
    option.initFrom(list->viewport());
    option.widget = list->viewport();
    option.rect = QRect(8, 56, 460, 32);
    QStyledItemDelegate standard;
    QImage expected(500, 144, QImage::Format_ARGB32_Premultiplied);
    expected.fill(option.palette.base().color());
    {
        QPainter painter(&expected);
        standard.paint(&painter, option, list->model()->index(list->count() - 1, 0));
    }
    CHECK(paintQueueRow(*list, list->count() - 1, option) == expected);
}

#ifdef Q_OS_MAC
TEST_CASE("Integrated Updates auto-install indicator stays visible in disabled and wrapped appearance states",
          "[qt][settings-downloads][updates-render-red][cocoa-pixel-probe]")
{
    const QString evidencePath = qEnvironmentVariable("EISKALT_QA_UI_EVIDENCE");
    const QDir evidence(evidencePath);
    if (!evidencePath.isEmpty()) REQUIRE(evidence.mkpath("."));
    const auto rectValues = [](QRect rect) {
        return QJsonArray{rect.x(), rect.y(), rect.width(), rect.height()};
    };
    const QPalette original = qApp->palette();
    auto restorePalette = dcpp::makeScopedFunctor([&] { qApp->setPalette(original); });
    for (bool dark : {false, true}) {
        QPalette palette = original;
        palette.setColor(QPalette::Window, dark ? QColor(35, 35, 35) : QColor(242, 242, 242));
        palette.setColor(QPalette::Base, dark ? QColor(28, 28, 28) : QColor(Qt::white));
        palette.setColor(QPalette::AlternateBase, dark ? QColor(38, 38, 38) : QColor(246, 246, 246));
        palette.setColor(QPalette::Text, dark ? QColor(235, 235, 235) : QColor(24, 24, 24));
        palette.setColor(QPalette::WindowText, palette.color(QPalette::Text));
        palette.setColor(QPalette::Disabled, QPalette::WindowText, dark ? QColor(138, 138, 138) : QColor(110, 110, 110));
        qApp->setPalette(palette);
        IsolatedMainWindow fixture;
        const QString platform = QApplication::platformName();
        const QString configuredTheme = fixture.gui->settings()->getStr(WS_APP_THEME).trimmed();
        const QString environmentStyle = qEnvironmentVariable("QT_STYLE_OVERRIDE").trimmed();
        const QString effectiveStyleName = qApp->style()->objectName();
        const auto isFusion = [](const QString &name) {
            return name.compare(QStringLiteral("fusion"), Qt::CaseInsensitive) == 0;
        };
        const bool nativeCocoaOptics = platform == QStringLiteral("cocoa")
            && !isFusion(configuredTheme) && !isFusion(environmentStyle) && !isFusion(effectiveStyleName);
        INFO("platform=" << platform.toStdString() << " configured_theme=" << configuredTheme.toStdString()
            << " QT_STYLE_OVERRIDE=" << environmentStyle.toStdString()
            << " effective_style=" << effectiveStyleName.toStdString()
            << " native_cocoa_optics=" << nativeCocoaOptics);
        QuitBackend backend(*fixture.core);
        updates::UpdateController updater(*fixture.gui->settings(), backend,
            {1, updates::Channel::Preview, "org.test.eiskalt", "arm64", QVersionNumber(26, 0)});
        Settings preferences(&updater);
        preferences.navigate(Settings::Page::Updates);
        preferences.show();
        auto *automaticChecks = preferences.findChild<QCheckBox*>("updateChecks");
        auto *automaticInstall = preferences.findChild<QCheckBox*>("updateAutoInstall");
        REQUIRE(automaticChecks);
        REQUIRE(automaticInstall);
        for (bool wrapped : {false, true}) {
            automaticInstall->setText(wrapped
                ? "Download and install verified updates after the next normal application quit "
                  "while keeping this complete translated caption visible on several lines"
                : "Download and install updates");
            automaticInstall->ensurePolished();
            const int width = wrapped ? 220 : 360;
            const int height = automaticInstall->heightForWidth(width);
            automaticInstall->setFixedSize(width, height);
            automaticInstall->setTristate(true);
            for (bool enabled : {false, true}) {
                // Exercise the real UI disabling rule, not a test-only backend
                // capability change or an installation action.
                automaticChecks->setChecked(enabled);
                REQUIRE(automaticInstall->isEnabled() == enabled);
                QList<QImage> stateCrops;
                for (Qt::CheckState state : {Qt::Unchecked, Qt::PartiallyChecked, Qt::Checked}) {
                    automaticInstall->setCheckState(state);
                    automaticInstall->clearFocus();
                    QCoreApplication::processEvents();
                    const auto image = automaticInstall->grab().toImage().scaled(automaticInstall->size(),
                        Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                    QStyleOptionButton option;
                    option.initFrom(automaticInstall);
                    option.text = automaticInstall->text();
                    const QRect indicator = automaticInstall->style()->subElementRect(
                        QStyle::SE_CheckBoxIndicator, &option, automaticInstall);
                    const QRect contents = automaticInstall->style()->subElementRect(
                        QStyle::SE_CheckBoxContents, &option, automaticInstall);
                    const QColor background = image.pixelColor(image.width() - 1, image.height() - 1);
                    const QRect markBand = QRect(indicator.left() - 2, 0,
                        indicator.width() + 4, image.height()).intersected(image.rect());
                    const QRect textBand = QRect(contents.left() + 1, 0,
                        contents.width() - 2, image.height()).intersected(image.rect());
                    const QRect markInk = paintedInk(image, markBand, background);
                    const QRect textInk = paintedInk(image, textBand, background);
                    stateCrops.append(image.copy(markBand));
                    int maximumContrast = 0, pixelsAbove2 = 0, pixelsAbove20 = 0;
                    for (int y = markBand.top(); y <= markBand.bottom(); ++y) {
                        for (int x = markBand.left(); x <= markBand.right(); ++x) {
                            const QColor pixel = image.pixelColor(x, y);
                            const int contrast = qMax(qAbs(pixel.red() - background.red()),
                                qMax(qAbs(pixel.green() - background.green()), qAbs(pixel.blue() - background.blue())));
                            maximumContrast = qMax(maximumContrast, contrast);
                            pixelsAbove2 += contrast > 2;
                            pixelsAbove20 += contrast > 20;
                        }
                    }
                    const QJsonObject statistics{{"dark", dark}, {"wrapped", wrapped},
                        {"enabled", enabled}, {"state", int(state)},
                        {"platform", platform}, {"configured_theme", configuredTheme},
                        {"environment_style", environmentStyle}, {"effective_style_name", effectiveStyleName},
                        {"native_cocoa_optics", nativeCocoaOptics},
                        {"widget_rect", rectValues(automaticInstall->rect())},
                        {"indicator_style_rect", rectValues(indicator)}, {"contents_style_rect", rectValues(contents)},
                        {"indicator_scan_band", rectValues(markBand)}, {"text_scan_band", rectValues(textBand)},
                        {"indicator_ink", rectValues(markInk)}, {"text_ink", rectValues(textInk)},
                        {"background_rgba", background.name(QColor::HexArgb)},
                        {"max_indicator_rgb_contrast", maximumContrast},
                        {"indicator_pixels_contrast_above_2", pixelsAbove2},
                        {"indicator_pixels_contrast_above_20", pixelsAbove20},
                        {"device_pixel_ratio", automaticInstall->devicePixelRatioF()}};
                    INFO(QJsonDocument(statistics).toJson(QJsonDocument::Compact).toStdString());
                    if (!evidencePath.isEmpty()) {
                        const QString name = QString("updates-indicator-dark%1-wrapped%2-enabled%3-state%4")
                            .arg(int(dark)).arg(int(wrapped)).arg(int(enabled)).arg(int(state));
                        CHECK(image.save(evidence.filePath(name + "-control.png")));
                        CHECK(stateCrops.last().save(evidence.filePath(name + "-indicator-crop.png")));
                        CHECK(preferences.grab().save(evidence.filePath(name + "-preferences.png")));
                        QFile statsFile(evidence.filePath(name + ".json"));
                        REQUIRE(statsFile.open(QIODevice::WriteOnly));
                        CHECK(statsFile.write(QJsonDocument(statistics).toJson(QJsonDocument::Indented)) > 0);
                        QJsonArray primitiveReports;
                        const qreal ratio = automaticInstall->devicePixelRatioF();
                        const QSize canvas = indicator.size() + QSize(16, 16);
                        for (int variant : {0, 1, 2}) {
                            for (bool withWidget : {true, false}) {
                                QStyleOptionButton rawOption;
                                rawOption.initFrom(automaticInstall);
                                rawOption.text = automaticInstall->text();
                                rawOption.state &= ~(QStyle::State_On | QStyle::State_Off
                                    | QStyle::State_NoChange | QStyle::State_HasFocus);
                                rawOption.state |= state == Qt::PartiallyChecked ? QStyle::State_NoChange
                                    : state == Qt::Checked ? QStyle::State_On : QStyle::State_Off;
                                if (variant >= 1) rawOption.state |= QStyle::State_Enabled;
                                if (variant == 2) rawOption.palette.setCurrentColorGroup(QPalette::Active);
                                rawOption.rect = QRect(QPoint(8, 8), indicator.size());
                                const QString variantName = variant == 0 ? "original"
                                    : variant == 1 ? "enabled" : "enabled-active-palette";
                                const QString artifact = name + "-primitive-" + variantName
                                    + (withWidget ? "-widget" : "-nullptr");
                                QImage primitive(QSize(qCeil(canvas.width() * ratio), qCeil(canvas.height() * ratio)),
                                    QImage::Format_ARGB32_Premultiplied);
                                primitive.setDevicePixelRatio(ratio);
                                primitive.fill(Qt::transparent);
                                {
                                    QPainter painter(&primitive);
                                    automaticInstall->style()->drawPrimitive(QStyle::PE_IndicatorCheckBox,
                                        &rawOption, &painter, withWidget ? automaticInstall : nullptr);
                                }
                                QRect alphaInk;
                                int alphaPixels = 0, maxAlpha = 0;
                                for (int y = 0; y < primitive.height(); ++y) {
                                    for (int x = 0; x < primitive.width(); ++x) {
                                        const int alpha = qAlpha(primitive.pixel(x, y));
                                        maxAlpha = qMax(maxAlpha, alpha);
                                        if (alpha > 16) {
                                            ++alphaPixels;
                                            alphaInk = alphaInk.united(QRect(x, y, 1, 1));
                                        }
                                    }
                                }
                                QImage composite(primitive.size(), QImage::Format_ARGB32_Premultiplied);
                                composite.setDevicePixelRatio(ratio);
                                composite.fill(background);
                                {
                                    QPainter painter(&composite);
                                    painter.drawImage(QPoint(0, 0), primitive);
                                }
                                int maximumDelta = 0, contrastingPixels = 0;
                                for (int y = 0; y < composite.height(); ++y) {
                                    for (int x = 0; x < composite.width(); ++x) {
                                        const QColor pixel = composite.pixelColor(x, y);
                                        const int delta = qMax(qAbs(pixel.red() - background.red()),
                                            qMax(qAbs(pixel.green() - background.green()), qAbs(pixel.blue() - background.blue())));
                                        maximumDelta = qMax(maximumDelta, delta);
                                        contrastingPixels += delta > 20;
                                    }
                                }
                                CHECK(primitive.save(evidence.filePath(artifact + "-raw.png")));
                                CHECK(composite.save(evidence.filePath(artifact + "-on-background.png")));
                                primitiveReports.append(QJsonObject{{"variant", variantName}, {"widget_argument", withWidget},
                                    {"option_state_bits", int(rawOption.state)},
                                    {"palette_group", int(rawOption.palette.currentColorGroup())},
                                    {"style_class", automaticInstall->style()->metaObject()->className()},
                                    {"option_rect_logical", rectValues(rawOption.rect)},
                                    {"alpha_bounds_physical", rectValues(alphaInk)}, {"pixels_alpha_above_16", alphaPixels},
                                    {"max_alpha", maxAlpha}, {"max_composited_rgb_contrast", maximumDelta},
                                    {"composited_pixels_contrast_above_20", contrastingPixels},
                                    {"device_pixel_ratio", ratio}, {"artifact_prefix", artifact}});
                            }
                        }
                        QFile rawStats(evidence.filePath(name + "-primitives.json"));
                        REQUIRE(rawStats.open(QIODevice::WriteOnly));
                        CHECK(rawStats.write(QJsonDocument(primitiveReports).toJson(QJsonDocument::Indented)) > 0);
                    }
                    CAPTURE(dark, wrapped, enabled, state, markInk, textInk);
                    CHECK_FALSE(markInk.isEmpty());
                    CHECK_FALSE(textInk.isEmpty());
                    // Fusion's stock contrast mask can isolate only the internal
                    // tick/bar. Its optical center is not the Cocoa artwork contract.
                    if (nativeCocoaOptics && !markInk.isEmpty() && !textInk.isEmpty()) {
                        const auto firstLineInk = paintedInk(image,
                            QRect(contents.left() + 1, textInk.top(), contents.width() - 2,
                                  automaticInstall->fontMetrics().height()), background);
                        CHECK(qAbs(markInk.center().y() - firstLineInk.center().y()) <= 2);
                    }
                    if (wrapped && !textInk.isEmpty())
                        CHECK(textInk.height() >= automaticInstall->fontMetrics().height() * 2);
                }
                REQUIRE(stateCrops.size() == 3);
                QJsonArray identityResults;
                for (int first = 0; first < 3; ++first) {
                    for (int second = first + 1; second < 3; ++second) {
                        REQUIRE(stateCrops[first].size() == stateCrops[second].size());
                        int differentPixels = 0;
                        // Compare only the fixed indicator band: captions,
                        // layout, theme and enabled state are identical.
                        for (int y = 0; y < stateCrops[first].height(); ++y) {
                            for (int x = 0; x < stateCrops[first].width(); ++x) {
                                const QColor a = stateCrops[first].pixelColor(x, y);
                                const QColor b = stateCrops[second].pixelColor(x, y);
                                const int difference = qMax(qAbs(a.red() - b.red()),
                                    qMax(qAbs(a.green() - b.green()), qAbs(a.blue() - b.blue())));
                                differentPixels += difference > 8;
                            }
                        }
                        CAPTURE(dark, wrapped, enabled, first, second, differentPixels);
                        CHECK(differentPixels >= 3);
                        identityResults.append(QJsonObject{{"first_state", first}, {"second_state", second},
                            {"pixels_rgb_difference_above_8", differentPixels}});
                    }
                }
                if (!evidencePath.isEmpty()) {
                    const QString name = QString("updates-indicator-dark%1-wrapped%2-enabled%3-state-identity.json")
                        .arg(int(dark)).arg(int(wrapped)).arg(int(enabled));
                    QFile comparisons(evidence.filePath(name));
                    REQUIRE(comparisons.open(QIODevice::WriteOnly));
                    CHECK(comparisons.write(QJsonDocument(identityResults).toJson(QJsonDocument::Indented)) > 0);
                }
            }
        }
        preferences.reject();
        CHECK(backend.checks == 0);
        CHECK(backend.installs == 0);
    }
}

TEST_CASE("Integrated Updates release-channel popup shows both complete choices and selects without applying consent",
          "[qt][settings-downloads][updates-render-red][updates-channel-popup]")
{
    IsolatedMainWindow fixture;
    QuitBackend backend(*fixture.core);
    updates::UpdateController updater(*fixture.gui->settings(), backend,
        {1, updates::Channel::Preview, "org.test.eiskalt", "arm64", QVersionNumber(26, 0)});
    const auto savedChannel = updater.preferences().channel;
    Settings preferences(&updater);
    preferences.resize(1100, 800);
    preferences.navigate(Settings::Page::Updates);
    preferences.show();
    auto *combo = preferences.findChild<QComboBox*>("updateChannel");
    auto *checks = preferences.findChild<QCheckBox*>("updateChecks");
    REQUIRE(combo);
    REQUIRE(checks);
    REQUIRE(combo->count() == 2);
    QLabel *label = nullptr;
    for (auto *candidate : preferences.findChildren<QLabel*>())
        if (candidate->buddy() == combo) label = candidate;
    REQUIRE(label);
    QCoreApplication::processEvents();
    CHECK(qAbs(label->mapTo(&preferences, QPoint()).x() - checks->mapTo(&preferences, QPoint()).x()) <= 8);
    CHECK(combo->width() <= combo->sizeHint().expandedTo(combo->minimumSizeHint()).width() + 24);
    for (int choice : {0, 1}) {
        QTimer watchdog;
        watchdog.setSingleShot(true);
        QObject::connect(&watchdog, &QTimer::timeout, combo, &QComboBox::hidePopup);
        watchdog.start(1500);
        combo->showPopup();
        QCoreApplication::processEvents();
        auto *view = combo->view();
        REQUIRE(view);
        CHECK(view->isVisible());
        if (view->isVisible()) {
            const auto first = view->model()->index(0, combo->modelColumn(), combo->rootModelIndex());
            const auto second = view->model()->index(1, combo->modelColumn(), combo->rootModelIndex());
            const QRect firstRect = view->visualRect(first), secondRect = view->visualRect(second);
            CHECK(view->viewport()->rect().contains(firstRect));
            CHECK(view->viewport()->rect().contains(secondRect));
            CHECK_FALSE(firstRect.intersects(secondRect));
            CHECK(firstRect.width() >= view->fontMetrics().horizontalAdvance(combo->itemText(0)) + 12);
            CHECK(secondRect.width() >= view->fontMetrics().horizontalAdvance(combo->itemText(1)) + 12);
            const auto popupImage = view->viewport()->grab().toImage().scaled(view->viewport()->size(),
                Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            for (int row : {0, 1}) {
                const QRect rect = row == 0 ? firstRect : secondRect;
                if (!popupImage.rect().contains(rect)) continue;
                const QColor rowBackground = popupImage.pixelColor(rect.right() - 3, rect.center().y());
                const QRect captionInk = paintedInk(popupImage, rect.adjusted(8, 2, -8, -2), rowBackground);
                CAPTURE(row, captionInk);
                CHECK(captionInk.width() >= view->fontMetrics().horizontalAdvance(combo->itemText(row)) - 4);
                CHECK(captionInk.height() >= view->fontMetrics().height() / 2);
            }
            const auto selected = choice == 0 ? first : second;
            QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, view->visualRect(selected).center());
            CHECK(combo->currentIndex() == choice);
            CHECK(combo->currentText() == combo->itemText(choice));
        }
        combo->hidePopup();
        watchdog.stop();
    }
    combo->setFocus();
    QTest::keyClick(combo, Qt::Key_Home);
    CHECK(combo->currentIndex() == 0);
    QTest::keyClick(combo, Qt::Key_End);
    CHECK(combo->currentIndex() == 1);
    preferences.reject();
    CHECK(updater.preferences().channel == savedChannel);
    CHECK(backend.checks == 0);
    CHECK(backend.installs == 0);
}

TEST_CASE("Integrated Updates actions keep natural sizes and leading alignment across available widths",
          "[qt][settings-downloads][updates-render-red][updates-action-layout]")
{
    IsolatedMainWindow fixture;
    QuitBackend backend(*fixture.core);
    updates::UpdateController updater(*fixture.gui->settings(), backend,
        {1, updates::Channel::Preview, "org.test.eiskalt", "arm64", QVersionNumber(26, 0)});
    Settings preferences(&updater);
    preferences.navigate(Settings::Page::Updates);
    auto *status = preferences.findChild<QLabel*>("updateLastSuccess");
    REQUIRE(status);
    const QStringList names{"updateCheckNow", "updateCancel", "updateDownload",
                            "updateInstallOnQuit", "updateLater", "updateSkip"};
    QList<QPushButton*> buttons;
    for (const auto &name : names) {
        auto *button = preferences.findChild<QPushButton*>(name);
        REQUIRE(button);
        buttons.append(button);
    }
    buttons[3]->setText("Install on the next normal application quit");
    for (int width : {900, 1360}) {
        preferences.resize(width, 850);
        preferences.show();
        QCoreApplication::processEvents();
        int leading = preferences.width();
        QList<QRect> paintedBounds;
        for (auto *button : buttons) {
            const QRect bounds(button->mapTo(&preferences, QPoint()), button->size());
            INFO("action=" << button->objectName().toStdString() << " rect=" << bounds.x() << ','
                << bounds.y() << ',' << bounds.width() << ',' << bounds.height());
            CAPTURE(width, button->objectName(), bounds, button->sizeHint());
            CHECK(button->isVisible());
            CHECK(button->width() >= button->sizeHint().width());
            CHECK(button->width() <= button->sizeHint().width() + 24);
            CHECK(button->height() <= button->sizeHint().height() + 8);
            CHECK(preferences.rect().contains(bounds));
            leading = qMin(leading, bounds.left());
            for (const auto &previous : paintedBounds) {
                INFO("previous_rect=" << previous.x() << ',' << previous.y() << ','
                    << previous.width() << ',' << previous.height());
                CHECK_FALSE(bounds.intersects(previous));
            }
            paintedBounds.append(bounds);
            bool isFirstInRow = true;
            for (auto *other : buttons) {
                if (other == button) continue;
                const QPoint otherPos = other->mapTo(&preferences, QPoint());
                if (qAbs(otherPos.y() - bounds.top()) <= 2 && otherPos.x() < bounds.left())
                    isFirstInRow = false;
            }
            if (isFirstInRow)
                CHECK(qAbs(bounds.left() - status->mapTo(&preferences, QPoint()).x()) <= 8);
        }
        CHECK(qAbs(leading - status->mapTo(&preferences, QPoint()).x()) <= 8);
    }
    preferences.reject();
    CHECK(backend.checks == 0);
    CHECK(backend.installs == 0);
}

TEST_CASE("Updates custom single-line and wrapped captions align native indicators with the first text line",
          "[qt][settings-downloads][application-check-indicators][cocoa-pixel-probe]")
{
    IsolatedMainWindow fixture;
    Settings preferences;
    preferences.navigate(Settings::Page::Updates);
    preferences.show();
    auto *checkbox = preferences.findChild<QCheckBox*>("updateChecks");
    REQUIRE(checkbox);
    REQUIRE(qApp->property("eiskaltCheckIndicatorFilterInstalled").toBool());
    for (bool wrapped : {false, true}) {
        checkbox->setText(wrapped
            ? "Check for updates automatically and keep showing complete translated captions "
              "even when the available settings column becomes narrow and needs several lines of text"
            : "Check for updates");
        checkbox->ensurePolished();
        constexpr int width = 220;
        const int captionHeight = checkbox->heightForWidth(width);
        if (wrapped) REQUIRE(captionHeight >= checkbox->fontMetrics().height() * 3);
        checkbox->setFixedSize(width, captionHeight);
        checkbox->clearFocus();
        QCoreApplication::processEvents();
        const QImage image = checkbox->grab().toImage().scaled(checkbox->size(),
            Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        QStyleOptionButton option;
        option.initFrom(checkbox);
        option.text = checkbox->text();
        const QRect contents = checkbox->style()->subElementRect(QStyle::SE_CheckBoxContents, &option, checkbox);
        const QRect indicator = checkbox->style()->subElementRect(QStyle::SE_CheckBoxIndicator, &option, checkbox);
        const QColor background = image.pixelColor(image.width() - 1, image.height() - 1);
        const QRect textInk = paintedInk(image,
            QRect(contents.left() + 1, 0, contents.width() - 2, image.height()), background);
        const QRect indicatorInk = paintedInk(image,
            QRect(indicator.left() - 2, 0, indicator.width() + 4, image.height()), background);
        CAPTURE(wrapped, captionHeight, textInk, indicatorInk);
        REQUIRE_FALSE(textInk.isEmpty());
        REQUIRE_FALSE(indicatorInk.isEmpty());
        const QRect firstLineInk = paintedInk(image, QRect(contents.left() + 1, textInk.top(),
            contents.width() - 2, checkbox->fontMetrics().height()), background);
        REQUIRE_FALSE(firstLineInk.isEmpty());
        CHECK(qAbs(indicatorInk.center().y() - firstLineInk.center().y()) <= 2);
        if (wrapped) {
            CHECK(textInk.height() >= checkbox->fontMetrics().height() * 2);
            CHECK(textInk.top() < image.height() / 3);
            CHECK(textInk.bottom() > image.height() * 2 / 3);
        }
    }
    checkbox->setChecked(false);
    checkbox->setFocus();
    QTest::keyClick(checkbox, Qt::Key_Space);
    CHECK(checkbox->isChecked());
    preferences.reject();
}

TEST_CASE("Ordinary Cocoa controls created after GUI startup align ink and retain native input automatically",
          "[qt][settings-downloads][application-check-indicators][cocoa-pixel-probe]")
{
    IsolatedMainWindow fixture;
    QWidget ordinary;
    ordinary.setStyleSheet(mac_input_style::macInputContrastStyle(ordinary.palette()));
    auto *layout = new QVBoxLayout(&ordinary);
    auto *check = new QCheckBox("Ordinary checkbox baseline", &ordinary);
    check->setTristate(true);
    check->setCheckState(Qt::PartiallyChecked);
    auto *radio = new QRadioButton("Ordinary radio baseline", &ordinary);
    layout->addWidget(check);
    layout->addWidget(radio);
    ordinary.resize(440, 120);
    ordinary.show();
    QCoreApplication::processEvents();
    REQUIRE(qApp->findChildren<QObject*>("eiskaltApplicationCheckIndicatorFilter",
        Qt::FindDirectChildrenOnly).size() == 1);
    for (auto *button : QList<QAbstractButton*>{check, radio}) {
        button->clearFocus();
        const QImage image = button->grab().toImage().scaled(button->size(),
            Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        QStyleOptionButton option;
        option.initFrom(button);
        option.text = button->text();
        const bool isRadio = button == radio;
        const QRect mark = button->style()->subElementRect(isRadio
            ? QStyle::SE_RadioButtonIndicator : QStyle::SE_CheckBoxIndicator, &option, button);
        const QRect text = button->style()->subElementRect(isRadio
            ? QStyle::SE_RadioButtonContents : QStyle::SE_CheckBoxContents, &option, button);
        const QColor background = image.pixelColor(image.width() - 1, image.height() - 1);
        const QRect markInk = paintedInk(image, QRect(mark.left() - 2, 0,
            mark.width() + 4, image.height()), background);
        const QRect textInk = paintedInk(image, QRect(text.left() + 1, 0,
            text.width() - 2, image.height()), background);
        CAPTURE(button->text(), markInk, textInk);
        REQUIRE_FALSE(markInk.isEmpty());
        REQUIRE_FALSE(textInk.isEmpty());
        CHECK(qAbs(markInk.center().y() - textInk.center().y()) <= 2);
    }
    check->setFocus();
    REQUIRE(check->isTristate());
    check->setCheckState(Qt::Unchecked);
    QTest::keyClick(check, Qt::Key_Space);
    CHECK(check->checkState() == Qt::PartiallyChecked);
    QTest::keyClick(check, Qt::Key_Space);
    CHECK(check->checkState() == Qt::Checked);
    QTest::keyClick(check, Qt::Key_Space);
    CHECK(check->checkState() == Qt::Unchecked);

    check->setCheckState(Qt::PartiallyChecked);
    REQUIRE(check->checkState() == Qt::PartiallyChecked);
    QStyleOptionButton clickOption;
    clickOption.initFrom(check);
    clickOption.text = check->text();
    const QPoint hit = check->style()->subElementRect(
        QStyle::SE_CheckBoxIndicator, &clickOption, check).center();
    QSignalSpy toggles(check, &QCheckBox::toggled);
    QTest::mouseClick(check, Qt::LeftButton, Qt::NoModifier, hit);
    CHECK(check->checkState() == Qt::Checked);
    // Partial and Checked both have boolean checked=true: no toggled signal.
    CHECK(toggles.count() == 0);
    check->setEnabled(false);
    QTest::mouseClick(check, Qt::LeftButton, Qt::NoModifier, hit);
    CHECK(check->checkState() == Qt::Checked);
    CHECK(toggles.count() == 0);
    check->setEnabled(true);
    check->setTristate(false);
    REQUIRE_FALSE(check->isTristate());
    check->setFocus();
    CHECK(check->hasFocus());
    CHECK_FALSE(check->grab().isNull());
    QTest::keyClick(check, Qt::Key_Space);
    CHECK(check->checkState() == Qt::Unchecked);
    CHECK(toggles.count() == 1);
    radio->setFocus();
    QTest::keyClick(radio, Qt::Key_Space);
    CHECK(radio->isChecked());
}

TEST_CASE("Application Cocoa default-delegate repair preserves mixed rows tristate disabled input and specialized delegates",
          "[qt][settings-downloads][application-check-indicators]")
{
    IsolatedMainWindow fixture;
    QListWidget list;
    list.setStyleSheet(mac_input_style::macInputContrastStyle(list.palette()));
    list.setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list.resize(420, 120);
    QPixmap icon(16, 16);
    icon.fill(Qt::red);
    auto *plain = new QListWidgetItem(QIcon(icon), "Non-checkable icon", &list);
    plain->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    for (int row = 1; row < 9; ++row) {
        auto *item = new QListWidgetItem(QString("Mixed row %1").arg(row), &list);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable
            | Qt::ItemIsUserTristate);
        item->setCheckState(Qt::PartiallyChecked);
        item->setSizeHint(QSize(360, 28));
    }
    list.item(2)->setFlags(list.item(2)->flags() & ~Qt::ItemIsEnabled);
    QListWidget specialized;
    auto *torrentDelegate = new TorrentCheckDelegate(&specialized);
    specialized.setItemDelegate(torrentDelegate);
    specialized.ensurePolished();
    list.show();
    QCoreApplication::processEvents();
    REQUIRE(dynamic_cast<preferences_check_indicators::RowDelegate*>(list.itemDelegate()));
    CHECK(specialized.itemDelegate() == torrentDelegate);
    CHECK_FALSE(plain->data(Qt::CheckStateRole).isValid());
    for (int row : {2, 7}) {
        auto *item = list.item(row);
        list.scrollToItem(item, QAbstractItemView::PositionAtCenter);
        QCoreApplication::processEvents();
        const QRect rect = list.visualItemRect(item);
        const QPoint hit(rect.left() + 10, rect.center().y());
        QTest::mouseClick(list.viewport(), Qt::LeftButton, Qt::NoModifier, hit);
        CHECK(item->checkState() == (row == 2 ? Qt::PartiallyChecked : Qt::Checked));
        if (row == 7) {
            list.setCurrentItem(item);
            QTest::keyClick(&list, Qt::Key_Space);
            CHECK(item->checkState() == Qt::Unchecked);
            QStyleOptionViewItem option;
            option.initFrom(list.viewport());
            option.widget = list.viewport();
            option.rect = QRect(8, 56, 360, 28);
            item->setCheckState(Qt::Unchecked);
            const auto off = paintQueueRow(list, row, option);
            item->setCheckState(Qt::Checked);
            const auto on = paintQueueRow(list, row, option);
            int pixels = 0;
            for (int y = 56; y < 84; ++y)
                for (int x = 8; x < 36; ++x)
                    pixels += off.pixel(x, y) != on.pixel(x, y);
            CHECK(pixels >= 5);
        }
    }
}
#endif
