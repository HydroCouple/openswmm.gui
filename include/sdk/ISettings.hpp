/**
 * @file ISettings.hpp
 * @brief Extension-scoped persistent user preferences.
 * @details The host owns the settings facade and prefixes keys with the extension identity. All methods require the GUI thread; extensions must never delete this facade.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once
#include <QStringList>
#include <QVariant>

namespace OpenSWMM::Sdk
{
/** Host-owned settings, automatically scoped to the calling extension. */
class ISettings
{
public:
    /** @brief Release an implementation; extensions borrow and never delete the host facade. */
    virtual ~ISettings() = default;
    /** @brief Read a key beneath extensions/&lt;pluginId&gt;/.
     * @param[in] key Relative key; absolute paths and traversal components are refused.
     * @param[in] fallback Value returned for an absent or refused key.
     * @return Owning QVariant; no other extension namespace is visible. */
    virtual QVariant value(const QString &key, const QVariant &fallback = {}) const = 0;
    /** @brief Persist a nonempty relative key and announce settingsChanged.
     * @param[in] key Relative setting name; empty and traversal keys are ignored.
     * @param[in] value Owning value copied into QSettings.
     * @note Values use normal QSettings storage; this is not a credential vault. */
    virtual void setValue(const QString &key, const QVariant &value) = 0;
    /** @brief Remove a relative key or subgroup within this extension's settings.
     * @param[in] key Nonempty relative key; invalid and empty values are ignored. */
    virtual void remove(const QString &key) = 0;
    /** @brief List this extension's stored settings without the host prefix.
     * @return Owning relative keys, including nested groups. */
    virtual QStringList keys() const = 0;
};
}
