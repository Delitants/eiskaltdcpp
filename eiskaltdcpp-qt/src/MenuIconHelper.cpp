#include "MenuIconHelper.h"

#include <QAction>
#include <QMenu>

namespace MenuIconHelper {

void enableFor(QAction* action)
{
    if(action && !action->icon().isNull()) {
        action->setIconVisibleInMenu(true);
    }
}

void enableFor(QMenu* menu)
{
    if(menu) {
        enableFor(menu->menuAction());
    }
}

}
