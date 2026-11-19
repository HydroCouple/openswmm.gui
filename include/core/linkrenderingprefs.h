/**
 * @file      linkrenderingprefs.h
 * @brief     Q_PROPERTY-wrapped view onto PreferencesManager's per-link-type pens (conduit / pump
 *            / orifice / weir / outlet).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Q_PROPERTY-wrapped view onto PreferencesManager's per-link-type pens
 * (conduit / pump / orifice / weir / outlet). Drives the Preferences
 * dialog's Rendering page via QPropertyModel — getters call
 * PreferencesManager::linkPen(), setters call setLinkPen(), so any edit
 * routes back through the singleton and fires its preferenceChanged
 * signal exactly like a hand-coded color picker would.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef LINKRENDERINGPREFS_H
#define LINKRENDERINGPREFS_H

#include <QObject>
#include <QPen>

class LinkRenderingPrefs : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QPen conduit READ conduitPen WRITE setConduitPen NOTIFY conduitPenChanged)
    Q_PROPERTY(QPen pump READ pumpPen WRITE setPumpPen NOTIFY pumpPenChanged)
    Q_PROPERTY(QPen orifice READ orificePen WRITE setOrificePen NOTIFY orificePenChanged)
    Q_PROPERTY(QPen weir READ weirPen WRITE setWeirPen NOTIFY weirPenChanged)
    Q_PROPERTY(QPen outlet READ outletPen WRITE setOutletPen NOTIFY outletPenChanged)

public:
    /**
     * @brief Create a property-editor facade over application link style preferences.
     * @param[in] parent Optional Qt owner; use this QObject on the GUI thread.
     */
    explicit LinkRenderingPrefs(QObject *parent = nullptr);

    /**
     * @brief Read the conduitPen outline pen from PreferencesManager.
     * @return Value copy of the saved canonical style or its compiled fallback.
     */
    [[nodiscard]] QPen conduitPen() const;
    /**
     * @brief Read the pumpPen outline pen from PreferencesManager.
     * @return Value copy of the saved canonical style or its compiled fallback.
     */
    [[nodiscard]] QPen pumpPen() const;
    /**
     * @brief Read the orificePen outline pen from PreferencesManager.
     * @return Value copy of the saved canonical style or its compiled fallback.
     */
    [[nodiscard]] QPen orificePen() const;
    /**
     * @brief Read the weirPen outline pen from PreferencesManager.
     * @return Value copy of the saved canonical style or its compiled fallback.
     */
    [[nodiscard]] QPen weirPen() const;
    /**
     * @brief Read the outletPen outline pen from PreferencesManager.
     * @return Value copy of the saved canonical style or its compiled fallback.
     */
    [[nodiscard]] QPen outletPen() const;

    /**
     * @brief Forward the property edit to the shared preference store.
     * @param[in] pen Complete pen; invalid colors are refused by PreferencesManager.
     * @details Skips equal values (fuzzy-equal for sizes), delegates persistence, then emits the matching property signal with the requested value. A store-refused value can still be signaled by this facade.
     */
    void setConduitPen(const QPen &pen);
    /**
     * @brief Forward the property edit to the shared preference store.
     * @param[in] pen Complete pen; invalid colors are refused by PreferencesManager.
     * @details Skips equal values (fuzzy-equal for sizes), delegates persistence, then emits the matching property signal with the requested value. A store-refused value can still be signaled by this facade.
     */
    void setPumpPen(const QPen &pen);
    /**
     * @brief Forward the property edit to the shared preference store.
     * @param[in] pen Complete pen; invalid colors are refused by PreferencesManager.
     * @details Skips equal values (fuzzy-equal for sizes), delegates persistence, then emits the matching property signal with the requested value. A store-refused value can still be signaled by this facade.
     */
    void setOrificePen(const QPen &pen);
    /**
     * @brief Forward the property edit to the shared preference store.
     * @param[in] pen Complete pen; invalid colors are refused by PreferencesManager.
     * @details Skips equal values (fuzzy-equal for sizes), delegates persistence, then emits the matching property signal with the requested value. A store-refused value can still be signaled by this facade.
     */
    void setWeirPen(const QPen &pen);
    /**
     * @brief Forward the property edit to the shared preference store.
     * @param[in] pen Complete pen; invalid colors are refused by PreferencesManager.
     * @details Skips equal values (fuzzy-equal for sizes), delegates persistence, then emits the matching property signal with the requested value. A store-refused value can still be signaled by this facade.
     */
    void setOutletPen(const QPen &pen);

signals:
    /**
     * @brief Notify the property editor after its facade setter delegates a changed request.
     * @param[in] pen Requested style value; query the getter for the effective stored value after validation.
     */
    void conduitPenChanged(const QPen &pen);
    /**
     * @brief Notify the property editor after its facade setter delegates a changed request.
     * @param[in] pen Requested style value; query the getter for the effective stored value after validation.
     */
    void pumpPenChanged(const QPen &pen);
    /**
     * @brief Notify the property editor after its facade setter delegates a changed request.
     * @param[in] pen Requested style value; query the getter for the effective stored value after validation.
     */
    void orificePenChanged(const QPen &pen);
    /**
     * @brief Notify the property editor after its facade setter delegates a changed request.
     * @param[in] pen Requested style value; query the getter for the effective stored value after validation.
     */
    void weirPenChanged(const QPen &pen);
    /**
     * @brief Notify the property editor after its facade setter delegates a changed request.
     * @param[in] pen Requested style value; query the getter for the effective stored value after validation.
     */
    void outletPenChanged(const QPen &pen);
};

#endif // LINKRENDERINGPREFS_H
