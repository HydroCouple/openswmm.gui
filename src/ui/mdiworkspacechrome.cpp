#include "ui/mdiworkspacechrome.h"
#include "ui/theme/thememanager.h"
#include "ui/theme/themetokens.h"

#include <QMdiArea>
#include <QObject>
#include <QWidget>

namespace openswmmvis::ui {

void installMdiWorkspaceChrome(QMdiArea *area, QWidget *welcome)
{
    // DO NOT REMOVE. This looks like dead styling — it is not, and it has
    // been reverted once already. The welcome tab must paint its own
    // background: what it lets show through is not just the backdrop but
    // every sub-window Qt left restored in the viewport below it (TabbedView
    // never hides the outgoing one, qmdiarea.cpp:685). Drop this and a model
    // tab reappears as a detached 200x150 framed window over the welcome
    // screen after welcome -> model -> welcome. QPalette::Window is already
    // surfaceWindow, so this changes what is *covered*, not how the welcome
    // looks — which is exactly why deleting it looks safe and is not.
    //
    // Guarded by test_mdi_tab_maximize's restoredModelCannotShowThroughTheWelcome.
    // Note visibleStrays() there CANNOT catch this: it measures z-order only,
    // and the model is correctly z-ordered underneath a cover that does not
    // paint. See mdiworkspacechrome.h for the full mechanism.
    if (welcome)
        welcome->setAutoFillBackground(true);

    if (!area)
        return;

    // Backdrop follows the theme. setBackground() is what QMdiArea's
    // paintEvent fills the viewport with; re-running it on themeChanged
    // covers the Preferences → Appearance switch as well as the initial
    // apply() (which lands after the SWMMVis ctor has already run).
    //
    // Read the token, NOT area->palette(): QApplication::setPalette only
    // updates qApp's palette synchronously and *posts* the
    // ApplicationPaletteChange to widgets (qapplication.cpp:1750-1759), so
    // inside this handler the widget palette still holds the outgoing
    // scheme. Same approach as profileplotwidget.cpp.
    const auto syncBackdrop = [area] {
        area->setBackground(ThemeManager::instance()->colors().surfaceWindow);
    };
    syncBackdrop();
    QObject::connect(ThemeManager::instance(), &ThemeManager::themeChanged,
                     area, syncBackdrop);
}

}   // namespace openswmmvis::ui
