/**
 * @file extensionpreferences.h
 * @brief GUI-thread registry of dynamically registered extension preference pages.
 * @details Entries carry an owner guard so destroyed hosts cannot leave callable factories. The registry itself does not own factories or settings.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once
#include "sdk/IUiHost.hpp"
#include "sdk/ISettings.hpp"
#include <QPointer>

namespace openswmmvis
{
/** @brief Guarded registration for a dynamically supplied Preferences page. */
struct ExtensionPreferencesEntry {
    QString id;              ///< Fully qualified ext.&lt;pluginId&gt;.&lt;pageId&gt; identity.
    QPointer<QObject> owner; ///< Host lifetime; a null guard makes the entry unavailable.
    OpenSWMM::Sdk::IPreferencesPageFactory *factory; ///< Borrowed while owner remains live.
    OpenSWMM::Sdk::ISettings *settings; ///< Borrowed plugin-scoped settings passed to create().
};
/** @brief Access the GUI-thread preferences registry.
 * @return Borrowed process-lifetime list; entries borrow their guarded host factories.
 * @note A live dialog is closed before its factory is unregistered. */
inline QList<ExtensionPreferencesEntry> &extensionPreferences()
{
    static QList<ExtensionPreferencesEntry> pages;
    return pages;
}
}
