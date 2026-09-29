#include "ChatFormatBar.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QStandardPaths>
#include <QStyle>
#include <QStyleHints>
#include <QTest>
#include <QTextEdit>
#include <QTextStream>
#include <QToolButton>
#include <QUiLoader>
#include <QVBoxLayout>
#include <memory>

// The preview loads the real forms without the application's core or profiles.
class FormLoader : public QUiLoader
{
protected:
    QWidget *createWidget(const QString &type, QWidget *parent, const QString &name) override
    {
        QWidget *widget = nullptr;
        if (type == "ChatEdit")
            widget = new QTextEdit(parent);
        else if (type == "LineEdit")
            widget = new QLineEdit(parent);
        else
            return QUiLoader::createWidget(type, parent, name);
        widget->setObjectName(name);
        return widget;
    }
};

struct PreviewRow {
    ChatFormatBar *bar;
    QToolButton *color;
    QToolButton *link;
    QString name;
    QList<QToolButton *> buttons;
};

static PreviewRow addRow(QVBoxLayout *layout, const QString &repo,
                         const QString &form, const QString &title)
{
    QFile file(repo + "/eiskaltdcpp-qt/ui/" + form);
    if (!file.open(QIODevice::ReadOnly))
        qFatal("Cannot open the preview form");
    FormLoader loader;
    std::unique_ptr<QWidget> source(loader.load(&file));
    if (!source)
        qFatal("Cannot load the preview form");
    auto *oldRow = source->findChild<QHBoxLayout *>("horizontalLayout_BBCODE");
    auto *editor = source->findChild<QTextEdit *>("plainTextEdit_INPUT");
    if (!oldRow || !editor)
        qFatal("The preview form has no chat input row");

    layout->addWidget(new QLabel(title));
    auto *frame = new QFrame;
    frame->setFrameShape(QFrame::StyledPanel);
    auto *input = new QVBoxLayout(frame);
    input->setContentsMargins(6, 6, 6, 6);
    input->setSpacing(6);
    auto *row = new QHBoxLayout;
    input->addLayout(row);
    while (auto *item = oldRow->takeAt(0))
        row->addItem(item);
    editor->setParent(frame);
    editor->setMinimumWidth(0);
    editor->setFixedHeight(76);
    editor->setPlainText("Keep the selected words while formatting.");
    auto cursor = editor->textCursor();
    cursor.setPosition(9);
    cursor.setPosition(23, QTextCursor::KeepAnchor);
    editor->setTextCursor(cursor);
    input->addWidget(editor);

    auto *smile = source->findChild<QToolButton *>("toolButton_SMILE");
    auto *image = new QToolButton;
    image->setObjectName("toolButton_IMAGE");
    image->setToolTip("Image");
    row->insertWidget(row->indexOf(smile), image);
    smile->setText(QString::fromUtf8("\xf0\x9f\x98\x8a"));
    auto *color = source->findChild<QToolButton *>("toolButton_COLOR");
    auto *link = source->findChild<QToolButton *>("toolButton_LINK");
    QList<QToolButton *> buttons;
    QStringList texts;
    QStringList tips;
    for (int i = 0; i < row->count(); ++i) {
        if (auto *button = qobject_cast<QToolButton *>(row->itemAt(i)->widget())) {
            buttons << button;
            texts << button->text();
            tips << button->toolTip();
        }
    }
    auto *bar = new ChatFormatBar(row, editor);
    if (buttons.size() != 9)
        qFatal("The preview must exercise all nine hub/PM controls");
    for (int i = 0; i < buttons.size(); ++i) {
        const auto *button = buttons[i];
        if (button->icon().isNull() || button->iconSize() != QSize(22, 22) ||
            button->toolButtonStyle() != Qt::ToolButtonIconOnly ||
            button->text() != texts[i] || button->toolTip() != tips[i] ||
            button->accessibleName() != tips[i])
            qFatal("Icon adoption changed a form's semantics or omitted an icon");
    }
    layout->addWidget(frame);
    return {bar, color, link, form == "HubFrame.ui" ? "hub" : "pm", buttons};
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTextStream(stdout) << "Platform=" << QApplication::platformName()
                        << ", style=" << app.style()->objectName() << '\n';
    QStandardPaths::setTestModeEnabled(true);
    if (argc != 3) {
        QTextStream(stderr) << "Usage: chatformat-preview REPO OUTPUT_DIRECTORY\n";
        return 2;
    }
    const QString repo = QString::fromLocal8Bit(argv[1]);
    QDir output(QString::fromLocal8Bit(argv[2]));
    if (!output.mkpath("."))
        return 2;
    QFile evidence(output.filePath("geometry.txt"));
    if (!evidence.open(QIODevice::WriteOnly | QIODevice::Text))
        return 2;
    QTextStream geometry(&evidence);

    QWidget window;
    window.setWindowTitle("BBCode toolbar fixture");
    auto *layout = new QVBoxLayout(&window);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);
    const QList<PreviewRow> rows = {
        addRow(layout, repo, "HubFrame.ui", "Hub chat"),
        addRow(layout, repo, "PrivateMessage.ui", "Private message")
    };
    window.show();
    window.activateWindow();
    for (const bool dark : {false, true}) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
        app.styleHints()->setColorScheme(dark ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
#endif
        QPalette palette(QColor("#f4f6f8"));
        palette.setColor(QPalette::WindowText, QColor("#253344"));
        palette.setColor(QPalette::Text, QColor("#253344"));
        palette.setColor(QPalette::Base, Qt::white);
        palette.setColor(QPalette::ButtonText, QColor("#253344"));
        palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#637387"));
        if (dark) {
            palette = QPalette(QColor("#292c30"));
            palette.setColor(QPalette::Window, QColor("#292c30"));
            palette.setColor(QPalette::WindowText, QColor("#f2f2f2"));
            palette.setColor(QPalette::Base, QColor("#202327"));
            palette.setColor(QPalette::Text, QColor("#f2f2f2"));
            palette.setColor(QPalette::Button, QColor("#292c30"));
            palette.setColor(QPalette::ButtonText, QColor("#f2f2f2"));
            palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#8d9bab"));
            palette.setColor(QPalette::Highlight, QColor("#345f9c"));
            palette.setColor(QPalette::HighlightedText, Qt::white);
        }
        app.setPalette(palette);
        for (const QString &mode : {QString("wide"), QString("narrow"), QString("long")}) {
            for (const auto &row : rows) {
                row.color->setText(mode == "long" ? "Nachrichten-Textfarbe" : "Color");
                row.color->setToolTip(mode == "long" ? "Nachrichten-Textfarbe" : "Text color");
                row.link->setText(mode == "long" ? "Verknuepfung einfuegen" : "Link");
                row.link->setToolTip(mode == "long" ? "Verknuepfung einfuegen" : "Insert link");
            }
            window.resize(mode == "wide" ? 760 : mode == "narrow" ? 320 : 200,
                          window.sizeHint().height());
            QTest::qWait(150);
            const QString stem = mode + (dark ? "-dark" : "-light");
            if (!window.grab().save(output.filePath(stem + ".png")))
                return 3;
            QTextStream(stdout) << stem << ": hub=" << rows[0].bar->width()
                                << "px, pm=" << rows[1].bar->width() << "px\n";
            for (const auto &row : rows) {
                geometry << stem << ' ' << row.name << " bar=" << row.bar->width()
                         << "x" << row.bar->height() << " DPR=" << row.bar->devicePixelRatioF() << '\n';
                QList<QRect> occupied;
                auto buttons = row.buttons;
                auto *overflow = row.bar->findChild<QToolButton *>("chatFormatOverflowButton");
                buttons << overflow;
                for (const auto *button : buttons) {
                    if (button->size() != QSize(32, 32))
                        qFatal("The native form has a non-32px target");
                    if (button->isVisible()) {
                        if (!row.bar->rect().contains(button->geometry()))
                            qFatal("A native form target is outside the bar");
                        for (const auto &other : occupied) {
                            if (button->geometry().intersects(other))
                                qFatal("Native form targets overlap");
                        }
                        occupied << button->geometry();
                    }
                    geometry << "  " << button->objectName() << " visible=" << button->isVisible()
                             << " rect=" << button->x() << ',' << button->y() << ','
                             << button->width() << ',' << button->height()
                             << " tooltip=" << button->toolTip()
                             << " accessible=" << button->accessibleName() << '\n';
                }
                if (overflow->isVisible()) {
                    overflow->click();
                    QTest::qWait(100);
                    auto *menu = row.bar->findChild<QMenu *>("chatFormatOverflowMenu");
                    if (!menu || !menu->grab().save(output.filePath(stem + "-" + row.name + "-menu.png")))
                        return 3;
                    for (const auto *button : row.buttons) {
                        if (button->isVisible())
                            continue;
                        bool found = false;
                        for (const auto *action : menu->actions()) {
                            if (action->objectName() != button->objectName())
                                continue;
                            found = action->text() == button->toolTip() && !action->icon().isNull();
                            geometry << "  overflow: " << action->text() << '\n';
                        }
                        if (!found)
                            qFatal("A hidden native form control lost its labeled overflow action");
                    }
                    menu->close();
                }
            }
        }
    }
    return 0;
}
