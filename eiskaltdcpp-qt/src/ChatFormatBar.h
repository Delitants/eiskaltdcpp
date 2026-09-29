#pragma once

#include <QList>
#include <QPointer>
#include <QWidget>
#include <functional>

class QHBoxLayout;
class QMenu;
class QTextEdit;
class QToolButton;

class ChatFormatBar : public QWidget
{
    Q_OBJECT

public:
    // Adopt the existing buttons, keeping their signal connections and menus.
    explicit ChatFormatBar(QHBoxLayout *row, QTextEdit *editor);
    void bindEditorAction(QToolButton *button, const std::function<void()> &handler);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    bool event(QEvent *event) override;
    bool eventFilter(QObject *object, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    struct Item {
        QToolButton *button;
        int group;
        bool automaticAccessibleName;
    };

    void arrangeButtons();
    void showOverflow();

    QList<Item> items;
    QPointer<QTextEdit> editor;
    QToolButton *overflowButton;
    QMenu *overflowMenu;
    bool arranging = false;
};
