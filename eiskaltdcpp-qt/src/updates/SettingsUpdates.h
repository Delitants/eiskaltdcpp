#pragma once
#include "UpdateController.h"
#include <QPointer>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QTextBrowser;

namespace updates {
class SettingsUpdates final : public QWidget {
    Q_OBJECT
public:
    explicit SettingsUpdates(UpdateController&, QWidget* parent = nullptr);
    void apply();
private:
    void refresh();
    void refreshAutoInstall();
    QPointer<UpdateController> controller;
    QCheckBox *checks, *notifications, *autoInstall;
    QComboBox* channel;
    QLabel *lastSuccess, *version, *result, *installSupport;
    QTextBrowser* notes;
    QProgressBar* progress;
    QPushButton *checkNow, *download, *installOnQuit, *later, *skip, *cancel;
};
}
