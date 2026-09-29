#include "ChatFormatBar.h"

#include <catch2/catch_test_macros.hpp>
#include <QAction>
#include <QApplication>
#include <QHBoxLayout>
#include <QImage>
#include <QMenu>
#include <QSignalSpy>
#include <QTest>
#include <QTextEdit>
#include <QToolButton>
#include <QVBoxLayout>

#ifdef CHAT_FORMAT_REAL_EDITOR
#include "ChatEdit.h"
#include <QInputDialog>
#include <QTimer>
#endif

namespace {
void settleFormatBar()
{
    QApplication::sendPostedEvents();
    QApplication::processEvents();
    QApplication::sendPostedEvents();
    QApplication::processEvents();
}

struct FormatFixture {
    QWidget window;
    QTextEdit *editor;
    ChatFormatBar *bar;
    QList<QToolButton *> buttons;

    explicit FormatFixture(bool extra = false, QTextEdit *input = nullptr,
                           const QString &accessibleLabel = QString())
    {
        auto *layout = new QVBoxLayout(&window);
        layout->setContentsMargins(0, 0, 0, 0);
        auto *row = new QHBoxLayout;
        layout->addLayout(row);
        editor = input ? input : new QTextEdit(&window);
        editor->setMinimumSize(0, 0);
        layout->addWidget(editor);
        const QStringList names = {"BOLD", "ITALIC", "UNDERLINE", "STRIKE",
                                   "COLOR", "LINK", "CODE", "IMAGE", "SMILE"};
        const QStringList labels = {"B", "I", "U", "S", "Color", "Link", "Code",
                                    QString(), QString::fromUtf8("\xf0\x9f\x98\x8a")};
        const QStringList tips = {"Bold", "Italic", "Underline", "Strikethrough",
                                  "Text color", "Insert link", "Code", "Image", "Emoji"};
        for (int i = 0; i < names.size(); ++i) {
            auto *button = new QToolButton(&window);
            button->setObjectName("toolButton_" + names[i]);
            button->setText(labels[i]);
            button->setToolTip(tips[i]);
            button->setMaximumSize(40, 28);
            button->setAutoRaise(true);
            row->addWidget(button);
            buttons << button;
        }
        QPixmap icon(18, 18);
        icon.fill(Qt::darkGreen);
        buttons[7]->setIcon(QIcon(icon));
        // A retained icon must not render beside the emoji text.
        buttons[8]->setIcon(QIcon(icon));
        buttons[8]->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        buttons[8]->setAccessibleName(accessibleLabel);
        if (extra) {
            auto *button = new QToolButton(&window);
            button->setObjectName("toolButton_EXISTING_MORE");
            button->setText("...");
            button->setToolTip("Additional actions");
            row->addWidget(button);
            buttons << button;
        }
        row->addStretch();
        bar = new ChatFormatBar(row, editor);
    }

    void show(int width)
    {
        window.resize(width, 220);
        window.show();
        window.activateWindow();
        editor->setFocus();
        settleFormatBar();
    }

    void selectText()
    {
        editor->setPlainText("before selected after");
        auto cursor = editor->textCursor();
        cursor.setPosition(7);
        cursor.setPosition(15, QTextCursor::KeepAnchor);
        editor->setTextCursor(cursor);
        editor->setFocus();
    }

    bool editorHasFocus() const
    {
        // Offscreen has no window manager to reactivate a window after a popup.
        // Native fixture runs still require actual keyboard focus.
        return QApplication::platformName() == "offscreen"
            ? window.focusWidget() == editor : editor->hasFocus();
    }

    QToolButton *overflow() const
    {
        return bar->findChild<QToolButton *>("chatFormatOverflowButton");
    }

    QMenu *openOverflow()
    {
        auto *button = overflow();
        if (!button)
            return nullptr;
        QTest::mouseClick(button, Qt::LeftButton);
        settleFormatBar();
        return bar->findChild<QMenu *>("chatFormatOverflowMenu");
    }
};

void checkVisibleGeometry(const FormatFixture &fixture)
{
    QList<QRect> occupied;
    auto buttons = fixture.buttons;
    if (fixture.overflow())
        buttons << fixture.overflow();
    for (auto *button : buttons) {
        if (!button->isVisible())
            continue;
        INFO(button->objectName().toStdString());
        CHECK(button->size() == QSize(32, 32));
        const QRect rect(button->mapTo(fixture.bar, QPoint()), button->size());
        CHECK(fixture.bar->rect().contains(rect));
        for (const auto &other : occupied)
            CHECK_FALSE(rect.intersects(other));
        occupied << rect;
    }
}

QAction *actionFor(QMenu *menu, const QToolButton *button)
{
    if (menu) {
        for (auto *action : menu->actions()) {
            if (action->objectName() == button->objectName())
                return action;
        }
    }
    return nullptr;
}
}

TEST_CASE("Chat format icons use 22px artwork in evenly spaced 32px targets",
          "[qt][chatformatbar][chatformat-icons]")
{
    FormatFixture fixture;
    fixture.show(900);
    checkVisibleGeometry(fixture);
    for (auto *button : fixture.buttons) {
        REQUIRE(button->isVisible());
        CHECK_FALSE(button->toolTip().isEmpty());
        CHECK(button->accessibleName() == button->toolTip());
        CHECK(button->toolButtonStyle() == Qt::ToolButtonIconOnly);
        CHECK(button->iconSize() == QSize(22, 22));
        CHECK_FALSE(button->icon().isNull());
    }
    const auto gap = [&fixture](int left, int right) {
        return fixture.buttons[right]->x() - fixture.buttons[left]->geometry().right() - 1;
    };
    for (int i = 1; i < fixture.buttons.size(); ++i)
        CHECK(gap(i - 1, i) == 4);
    CHECK(fixture.bar->sizeHint() == QSize(320, 32));
    REQUIRE(fixture.overflow());
    CHECK(fixture.overflow()->toolButtonStyle() == Qt::ToolButtonIconOnly);
    CHECK(fixture.overflow()->iconSize() == QSize(22, 22));
    CHECK_FALSE(fixture.overflow()->icon().isNull());
    CHECK_FALSE(fixture.overflow()->isVisible());
}

TEST_CASE("Chat format bar fits 320 pixels and exposes every hidden action", "[qt][chatformatbar]")
{
    FormatFixture fixture(true);
    fixture.show(320);
    CHECK(fixture.window.width() == 320);
    CHECK(fixture.bar->width() <= 320);
    checkVisibleGeometry(fixture);
    REQUIRE(fixture.overflow());
    REQUIRE(fixture.overflow()->isVisible());
    auto *menu = fixture.openOverflow();
    REQUIRE(menu);
    REQUIRE(menu->isVisible());
    int hidden = 0;
    for (auto *button : fixture.buttons) {
        const auto *action = actionFor(menu, button);
        if (button->isVisible()) {
            CHECK(action == nullptr);
        } else {
            ++hidden;
            REQUIRE(action);
            CHECK_FALSE(action->text().isEmpty());
        }
    }
    CHECK(hidden > 0);
    CHECK(actionFor(menu, fixture.buttons.last()) != nullptr);
    menu->close();
    fixture.window.resize(900, 220);
    settleFormatBar();
    for (auto *button : fixture.buttons)
        CHECK(button->isVisible());
    CHECK_FALSE(fixture.overflow()->isVisible());
}

TEST_CASE("Long translated labels stay in tooltips and overflow without widening icons",
          "[qt][chatformatbar][chatformat-icons]")
{
    FormatFixture fixture;
    const QString color = QString::fromUtf8("\u041a\u043e\u043b\u0456\u0440 \u0442\u0435\u043a\u0441\u0442\u0443 \u043f\u043e\u0432\u0456\u0434\u043e\u043c\u043b\u0435\u043d\u043d\u044f");
    const QString link = "Eine ausfuehrlich beschriftete Verknuepfung einfuegen";
    fixture.buttons[4]->setText(color);
    fixture.buttons[4]->setToolTip(color);
    fixture.buttons[5]->setText(link);
    fixture.buttons[5]->setToolTip(link);
    fixture.show(320);
    checkVisibleGeometry(fixture);
    for (auto *button : fixture.buttons)
        REQUIRE(button->isVisible());
    CHECK(fixture.buttons[4]->text() == color);
    CHECK(fixture.buttons[5]->text() == link);
    CHECK(fixture.buttons[4]->accessibleName() == color);
    CHECK(fixture.buttons[5]->accessibleName() == link);
    fixture.window.resize(180, 220);
    settleFormatBar();
    auto *menu = fixture.openOverflow();
    REQUIRE(menu);
    REQUIRE(actionFor(menu, fixture.buttons[4]));
    REQUIRE(actionFor(menu, fixture.buttons[5]));
    CHECK(actionFor(menu, fixture.buttons[4])->text() == color);
    CHECK(actionFor(menu, fixture.buttons[5])->text() == link);
    menu->close();
    fixture.window.resize(1600, 220);
    settleFormatBar();
    REQUIRE(fixture.buttons[4]->isVisible());
    REQUIRE(fixture.buttons[5]->isVisible());
    CHECK(fixture.buttons[4]->size() == QSize(32, 32));
    CHECK(fixture.buttons[5]->size() == QSize(32, 32));
}

TEST_CASE("Bold and Link keep editor selection and focus at the handler boundary", "[qt][chatformatbar]")
{
    for (const int index : {0, 5}) {
        FormatFixture fixture;
        fixture.show(900);
        fixture.selectText();
        QString selected;
        bool editorFocused = false;
        int calls = 0;
        fixture.bar->bindEditorAction(fixture.buttons[index], [&]() {
            selected = fixture.editor->textCursor().selectedText();
            editorFocused = fixture.editorHasFocus();
            ++calls;
        });
        QTest::mouseClick(fixture.buttons[index], Qt::LeftButton);
        CHECK(selected == "selected");
        CHECK(editorFocused);
        CHECK(calls == 1);
        fixture.buttons[index]->setFocus(Qt::TabFocusReason);
        QTest::keyClick(fixture.buttons[index], Qt::Key_Space);
        CHECK(selected == "selected");
        CHECK(editorFocused);
        CHECK(calls == 2);
    }
}

TEST_CASE("Every action including existing more dispatches once from overflow", "[qt][chatformatbar]")
{
    FormatFixture fixture(true);
    fixture.show(320);
    fixture.bar->setFixedWidth(32);
    settleFormatBar();
    for (auto *button : fixture.buttons) {
        INFO(button->objectName().toStdString());
        fixture.selectText();
        QSignalSpy spy(button, &QToolButton::clicked);
        QString selection;
        bool focused = false;
        fixture.bar->bindEditorAction(button, [&]() {
            selection = fixture.editor->textCursor().selectedText();
            focused = fixture.editorHasFocus();
        });
        auto *menu = fixture.openOverflow();
        REQUIRE(menu);
        auto *action = actionFor(menu, button);
        REQUIRE(action);
        QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, menu->actionGeometry(action).center());
        settleFormatBar();
        CHECK(spy.count() == 1);
        CHECK(selection == "selected");
        CHECK(focused);
        CHECK_FALSE(menu->isVisible());
        QObject::disconnect(button, &QToolButton::clicked, fixture.bar, nullptr);
    }
}

TEST_CASE("Overflow follows enabled state and cancelling preserves the editor", "[qt][chatformatbar]")
{
    FormatFixture fixture;
    fixture.show(320);
    fixture.bar->setFixedWidth(32);
    fixture.selectText();
    fixture.buttons[5]->setEnabled(false);
    auto *menu = fixture.openOverflow();
    REQUIRE(menu);
    REQUIRE(actionFor(menu, fixture.buttons[5]));
    CHECK_FALSE(actionFor(menu, fixture.buttons[5])->isEnabled());
    QTest::keyClick(menu, Qt::Key_Escape);
    settleFormatBar();
    CHECK(fixture.editor->textCursor().selectedText() == "selected");
    CHECK(fixture.editorHasFocus());
}

TEST_CASE("Native palette and font changes keep one smile icon and fixed targets",
          "[qt][chatformatbar][chatformat-icons]")
{
    FormatFixture fixture;
    fixture.show(900);
    auto *emoji = fixture.buttons[8];
    CHECK_FALSE(emoji->icon().isNull());
    CHECK(emoji->toolButtonStyle() == Qt::ToolButtonIconOnly);
    CHECK(fixture.bar->findChildren<QToolButton *>("toolButton_SMILE").size() == 1);
    for (const QColor background : {QColor("#f4f4f4"), QColor("#25282c")}) {
        auto palette = fixture.window.palette();
        palette.setColor(QPalette::Window, background);
        palette.setColor(QPalette::Button, background);
        palette.setColor(QPalette::ButtonText, background.lightness() < 128 ? Qt::white : Qt::black);
        fixture.window.setPalette(palette);
        auto font = fixture.window.font();
        font.setPointSize(32);
        fixture.window.setFont(font);
        fixture.window.resize(320, 220);
        settleFormatBar();
        checkVisibleGeometry(fixture);
        for (auto *button : fixture.buttons) {
            CHECK(button->styleSheet().isEmpty());
            CHECK(button->palette().color(QPalette::ButtonText) == palette.color(QPalette::ButtonText));
            CHECK(button->iconSize() == QSize(22, 22));
            CHECK(button->size() == QSize(32, 32));
        }
    }
}

TEST_CASE("Vector icons render distinct palette-colored artwork at native and high DPI",
          "[qt][chatformatbar][chatformat-icons]")
{
    FormatFixture fixture;
    fixture.show(900);
    auto buttons = fixture.buttons;
    buttons << fixture.overflow();
    for (const bool dark : {false, true}) {
        QPalette palette = fixture.window.palette();
        const QColor foreground(dark ? "#edf2f7" : "#253344");
        const QColor disabled(dark ? "#8d9bab" : "#637387");
        palette.setColor(QPalette::Window, QColor(dark ? "#20252b" : "#f4f6f8"));
        palette.setColor(QPalette::ButtonText, foreground);
        palette.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
        palette.setColor(QPalette::HighlightedText, QColor("#fff0cc"));
        fixture.window.setPalette(palette);
        settleFormatBar();
        for (const qreal scale : {1.0, 2.0, 3.0}) {
            QList<QImage> rendered;
            for (auto *button : buttons) {
                INFO(button->objectName().toStdString() << ", dark=" << dark << ", DPR=" << scale);
                for (const auto mode : {QIcon::Normal, QIcon::Disabled, QIcon::Selected}) {
                    const auto pixmap = button->icon().pixmap(QSize(22, 22), scale, mode);
                    REQUIRE_FALSE(pixmap.isNull());
                    CHECK(pixmap.size() == QSize(qRound(22 * scale), qRound(22 * scale)));
                    CHECK(pixmap.devicePixelRatio() == scale);
                    const QImage raster = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
                    const auto expected = mode == QIcon::Disabled ? disabled
                        : mode == QIcon::Selected ? QColor("#fff0cc") : foreground;
                    int solid = 0;
                    int wrongColor = 0;
                    for (int y = 0; y < raster.height(); ++y) {
                        for (int x = 0; x < raster.width(); ++x) {
                            const auto color = raster.pixelColor(x, y);
                            if (color.alpha() == 255) {
                                ++solid;
                                if (color.rgb() != expected.rgb())
                                    ++wrongColor;
                            }
                        }
                    }
                    CHECK(solid > 10);
                    CHECK(wrongColor == 0);
                    CHECK(raster.pixelColor(0, 0).alpha() == 0);
                    if (mode == QIcon::Normal) {
                        for (const auto &other : rendered)
                            CHECK(raster != other);
                        rendered << raster;
                    }
                }
            }
        }
    }
}

TEST_CASE("Existing accessibility labels survive icon adoption and tooltip translation",
          "[qt][chatformatbar][chatformat-icons]")
{
    const QString label = "Choose a message emoticon";
    FormatFixture fixture(false, nullptr, label);
    fixture.show(900);
    CHECK(fixture.buttons[8]->accessibleName() == label);
    CHECK(fixture.buttons[8]->toolTip() == "Emoji");
    fixture.buttons[8]->setToolTip("Emoticons einfuegen");
    settleFormatBar();
    CHECK(fixture.buttons[8]->accessibleName() == label);
    CHECK(fixture.buttons[8]->toolTip() == "Emoticons einfuegen");
}

TEST_CASE("Open overflow icons follow palette changes without losing action labels",
          "[qt][chatformatbar][chatformat-icons][chatformat-open-palette]")
{
    FormatFixture fixture;
    fixture.show(320);
    fixture.bar->setFixedWidth(32);
    settleFormatBar();
    auto *menu = fixture.openOverflow();
    REQUIRE(menu);
    REQUIRE(menu->isVisible());
    auto *action = actionFor(menu, fixture.buttons[0]);
    REQUIRE(action);
    const auto before = action->icon().pixmap(QSize(22, 22)).toImage();
    auto palette = fixture.window.palette();
    palette.setColor(QPalette::ButtonText, QColor("#f2bd71"));
    fixture.window.setPalette(palette);
    settleFormatBar();
    const auto after = action->icon().pixmap(QSize(22, 22)).toImage();
    CHECK(after != before);
    CHECK(after == fixture.buttons[0]->icon().pixmap(QSize(22, 22)).toImage());
    CHECK(action->text() == "Bold");
    CHECK(menu->isVisible());
    menu->close();
}

TEST_CASE("Hidden media controls keep an on-bar anchor for their existing dialogs", "[qt][chatformatbar]")
{
    FormatFixture fixture;
    fixture.show(900);
    fixture.window.resize(288, 220);
    settleFormatBar();
    REQUIRE(fixture.overflow());
    for (const int index : {7, 8}) {
        REQUIRE(fixture.buttons[index]->isHidden());
        CHECK(fixture.buttons[index]->geometry() == fixture.overflow()->geometry());
    }
}

TEST_CASE("Runtime translated captions update semantics without changing icon geometry",
          "[qt][chatformatbar][chatformat-icons]")
{
    FormatFixture fixture;
    fixture.show(900);
    const QString expanded = QString("Translated message text color ").repeated(8);
    fixture.buttons[4]->setText(expanded);
    fixture.buttons[4]->setToolTip(expanded);
    settleFormatBar();
    REQUIRE(fixture.buttons[4]->isVisible());
    CHECK(fixture.buttons[4]->size() == QSize(32, 32));
    CHECK(fixture.buttons[4]->accessibleName() == expanded);
    CHECK_FALSE(fixture.overflow()->isVisible());
    fixture.bar->setFixedWidth(32);
    settleFormatBar();
    REQUIRE(fixture.buttons[4]->isHidden());
    auto *menu = fixture.openOverflow();
    REQUIRE(menu);
    REQUIRE(actionFor(menu, fixture.buttons[4]));
    CHECK(actionFor(menu, fixture.buttons[4])->text() == expanded);
    menu->close();
    fixture.buttons[4]->setText("Color");
    fixture.buttons[4]->setToolTip(QString());
    fixture.bar->setFixedWidth(900);
    settleFormatBar();
    CHECK(fixture.buttons[4]->accessibleName() == "Color");
    CHECK(fixture.buttons[4]->isVisible());
    CHECK_FALSE(fixture.overflow()->isVisible());
}

TEST_CASE("Right-to-left bars mirror geometry without losing overflow actions", "[qt][chatformatbar]")
{
    FormatFixture fixture;
    fixture.window.setLayoutDirection(Qt::RightToLeft);
    fixture.show(288);
    checkVisibleGeometry(fixture);
    REQUIRE(fixture.overflow());
    CHECK(fixture.overflow()->x() == 0);
    CHECK(fixture.buttons[0]->x() > fixture.buttons[1]->x());
    auto *menu = fixture.openOverflow();
    REQUIRE(menu);
    REQUIRE(actionFor(menu, fixture.buttons[8]));
    menu->close();
}

#ifdef CHAT_FORMAT_REAL_EDITOR
TEST_CASE("Production editor Bold and Link use the hub and PM focus-before-dispatch binding",
          "[qt][chatformatbar][chatformat-integration]")
{
    for (const bool overflow : {false, true}) {
        for (const bool link : {false, true}) {
            INFO("overflow=" << overflow << ", link=" << link);
            auto *editor = new ChatEdit;
            FormatFixture fixture(false, editor);
            fixture.show(900);
            fixture.selectText();
            auto *button = fixture.buttons[link ? 5 : 0];
            bool focusedAtDispatch = false;
            bool dialogSawSelection = false;
            const auto handler = [&]() {
                focusedAtDispatch = fixture.editorHasFocus();
                if (link) {
                    QTimer::singleShot(0, &fixture.window, [&]() {
                        auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
                        if (!dialog)
                            qFatal("The production Link handler did not open its input dialog");
                        dialogSawSelection = dialog->textValue() == "selected";
                        dialog->setTextValue("https://example.invalid/fixture");
                        dialog->accept();
                    });
                    editor->insertUrlTag();
                } else {
                    editor->wrapWithTag("b");
                }
            };
            if (qEnvironmentVariableIsSet("CHAT_FORMAT_LEGACY_FOCUS")) {
                // RED mutation for the original late-focus wiring hazard.
                QObject::connect(button, &QToolButton::clicked, &fixture.window, [&]() {
                    handler();
                    editor->setFocus();
                });
            } else {
                fixture.bar->bindEditorAction(button, handler);
            }
            if (overflow) {
                fixture.bar->setFixedWidth(32);
                settleFormatBar();
                auto *menu = fixture.openOverflow();
                REQUIRE(menu);
                auto *action = actionFor(menu, button);
                REQUIRE(action);
                QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier,
                                  menu->actionGeometry(action).center());
            } else {
                button->setFocus(Qt::TabFocusReason);
                QTest::keyClick(button, Qt::Key_Space);
            }
            settleFormatBar();
            CHECK(focusedAtDispatch);
            CHECK(fixture.editorHasFocus());
            if (link) {
                CHECK(dialogSawSelection);
                CHECK(editor->toPlainText() ==
                      "before [url=https://example.invalid/fixture]selected[/url] after");
            } else {
                CHECK(editor->toPlainText() == "before [b]selected[/b] after");
            }
        }
    }
}
#endif
