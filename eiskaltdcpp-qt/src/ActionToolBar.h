#pragma once

#include <QToolBar>
#include <QMainWindow>

class QStyle;
class QMenu;
class QToolButton;

class ActionToolBar : public QToolBar
{
    Q_OBJECT
public:
    explicit ActionToolBar(QWidget *parent = nullptr);
    ~ActionToolBar() override;

protected:
    void actionEvent(QActionEvent *event) override;
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    void refreshButtons();
    void showOverflow(QToolButton *button);
    void prepareSplitMenu();
    QStyle *buttonStyle = nullptr;
    QMenu *overflowMenu = nullptr;
};

namespace action_toolbar {
void separateRows(QMainWindow &window, QToolBar *actions, QToolBar *tabs, QToolBar *search);
}
