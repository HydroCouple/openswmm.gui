/**
 * @file      selectionrenderingprefs.h
 * @brief     Q_PROPERTY-wrapped view onto PreferencesManager's per-class selection pens and
 *            brushes (link / subcatchment / node / gage).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Q_PROPERTY-wrapped view onto PreferencesManager's per-class selection
 * pens and brushes (link / subcatchment / node / gage). Drives the
 * Preferences dialog's Selection page via QPropertyModel — getters
 * call PreferencesManager::selectionPen / selectionBrush, setters call
 * setSelectionPen / setSelectionBrush, so any edit routes back through
 * the singleton and fires its preferenceChanged signal exactly like a
 * hand-coded picker would.
 *
 * Note: links are stroked only, so no `linkBrush` property is exposed.
 * The link pen's width is interpreted ADDITIVELY over the base link
 * pen — see PreferencesManager::selectionPen() documentation.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef SELECTIONRENDERINGPREFS_H
#define SELECTIONRENDERINGPREFS_H

#include <QBrush>
#include <QObject>
#include <QPen>

class SelectionRenderingPrefs : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QPen linkPen READ linkPen WRITE setLinkPen NOTIFY linkPenChanged)
    Q_PROPERTY(QPen subcatchmentPen READ subcatchmentPen WRITE setSubcatchmentPen NOTIFY
                   subcatchmentPenChanged)
    Q_PROPERTY(QBrush subcatchmentFill READ subcatchmentFill WRITE setSubcatchmentFill NOTIFY
                   subcatchmentFillChanged)
    Q_PROPERTY(QPen nodePen READ nodePen WRITE setNodePen NOTIFY nodePenChanged)
    Q_PROPERTY(QBrush nodeFill READ nodeFill WRITE setNodeFill NOTIFY nodeFillChanged)
    Q_PROPERTY(QPen gagePen READ gagePen WRITE setGagePen NOTIFY gagePenChanged)
    Q_PROPERTY(QBrush gageFill READ gageFill WRITE setGageFill NOTIFY gageFillChanged)

public:
    /**
     * @brief Create a property-editor facade over application selection style preferences.
     * @param[in] parent Optional Qt owner; use this QObject on the GUI thread.
     */
    explicit SelectionRenderingPrefs(QObject *parent = nullptr);

    /**
     * @brief Read the linkPen outline pen from PreferencesManager.
     * @return Value copy of the saved canonical style or its compiled fallback.
     */
    [[nodiscard]] QPen linkPen() const;
    /**
     * @brief Read the subcatchmentPen outline pen from PreferencesManager.
     * @return Value copy of the saved canonical style or its compiled fallback.
     */
    [[nodiscard]] QPen subcatchmentPen() const;
    /**
     * @brief Read the subcatchmentFill fill brush from PreferencesManager.
     * @return Value copy of the saved canonical style or its compiled fallback.
     */
    [[nodiscard]] QBrush subcatchmentFill() const;
    /**
     * @brief Read the nodePen outline pen from PreferencesManager.
     * @return Value copy of the saved canonical style or its compiled fallback.
     */
    [[nodiscard]] QPen nodePen() const;
    /**
     * @brief Read the nodeFill fill brush from PreferencesManager.
     * @return Value copy of the saved canonical style or its compiled fallback.
     */
    [[nodiscard]] QBrush nodeFill() const;
    /**
     * @brief Read the gagePen outline pen from PreferencesManager.
     * @return Value copy of the saved canonical style or its compiled fallback.
     */
    [[nodiscard]] QPen gagePen() const;
    /**
     * @brief Read the gageFill fill brush from PreferencesManager.
     * @return Value copy of the saved canonical style or its compiled fallback.
     */
    [[nodiscard]] QBrush gageFill() const;

    /**
     * @brief Forward the property edit to the shared preference store.
     * @param[in] pen Complete pen; invalid colors are refused by PreferencesManager.
     * @details Skips equal values (fuzzy-equal for sizes), delegates persistence, then emits the matching property signal with the requested value. A store-refused value can still be signaled by this facade.
     */
    void setLinkPen(const QPen &pen);
    /**
     * @brief Forward the property edit to the shared preference store.
     * @param[in] pen Complete pen; invalid colors are refused by PreferencesManager.
     * @details Skips equal values (fuzzy-equal for sizes), delegates persistence, then emits the matching property signal with the requested value. A store-refused value can still be signaled by this facade.
     */
    void setSubcatchmentPen(const QPen &pen);
    /**
     * @brief Forward the property edit to the shared preference store.
     * @param[in] brush Complete brush; invalid colors are refused by PreferencesManager.
     * @details Skips equal values (fuzzy-equal for sizes), delegates persistence, then emits the matching property signal with the requested value. A store-refused value can still be signaled by this facade.
     */
    void setSubcatchmentFill(const QBrush &brush);
    /**
     * @brief Forward the property edit to the shared preference store.
     * @param[in] pen Complete pen; invalid colors are refused by PreferencesManager.
     * @details Skips equal values (fuzzy-equal for sizes), delegates persistence, then emits the matching property signal with the requested value. A store-refused value can still be signaled by this facade.
     */
    void setNodePen(const QPen &pen);
    /**
     * @brief Forward the property edit to the shared preference store.
     * @param[in] brush Complete brush; invalid colors are refused by PreferencesManager.
     * @details Skips equal values (fuzzy-equal for sizes), delegates persistence, then emits the matching property signal with the requested value. A store-refused value can still be signaled by this facade.
     */
    void setNodeFill(const QBrush &brush);
    /**
     * @brief Forward the property edit to the shared preference store.
     * @param[in] pen Complete pen; invalid colors are refused by PreferencesManager.
     * @details Skips equal values (fuzzy-equal for sizes), delegates persistence, then emits the matching property signal with the requested value. A store-refused value can still be signaled by this facade.
     */
    void setGagePen(const QPen &pen);
    /**
     * @brief Forward the property edit to the shared preference store.
     * @param[in] brush Complete brush; invalid colors are refused by PreferencesManager.
     * @details Skips equal values (fuzzy-equal for sizes), delegates persistence, then emits the matching property signal with the requested value. A store-refused value can still be signaled by this facade.
     */
    void setGageFill(const QBrush &brush);

signals:
    /**
     * @brief Notify the property editor after its facade setter delegates a changed request.
     * @param[in] pen Requested style value; query the getter for the effective stored value after validation.
     */
    void linkPenChanged(const QPen &pen);
    /**
     * @brief Notify the property editor after its facade setter delegates a changed request.
     * @param[in] pen Requested style value; query the getter for the effective stored value after validation.
     */
    void subcatchmentPenChanged(const QPen &pen);
    /**
     * @brief Notify the property editor after its facade setter delegates a changed request.
     * @param[in] brush Requested style value; query the getter for the effective stored value after validation.
     */
    void subcatchmentFillChanged(const QBrush &brush);
    /**
     * @brief Notify the property editor after its facade setter delegates a changed request.
     * @param[in] pen Requested style value; query the getter for the effective stored value after validation.
     */
    void nodePenChanged(const QPen &pen);
    /**
     * @brief Notify the property editor after its facade setter delegates a changed request.
     * @param[in] brush Requested style value; query the getter for the effective stored value after validation.
     */
    void nodeFillChanged(const QBrush &brush);
    /**
     * @brief Notify the property editor after its facade setter delegates a changed request.
     * @param[in] pen Requested style value; query the getter for the effective stored value after validation.
     */
    void gagePenChanged(const QPen &pen);
    /**
     * @brief Notify the property editor after its facade setter delegates a changed request.
     * @param[in] brush Requested style value; query the getter for the effective stored value after validation.
     */
    void gageFillChanged(const QBrush &brush);
};

#endif // SELECTIONRENDERINGPREFS_H
