/**
 * @file      noderenderingprefs.h
 * @brief     Q_PROPERTY-wrapped view onto PreferencesManager's per-node-type outline pen, fill
 *            brush, and marker size (junction / outfall / storage / divider).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Q_PROPERTY-wrapped view onto PreferencesManager's per-node-type
 * outline pen, fill brush, and marker size (junction / outfall /
 * storage / divider). Drives the Preferences dialog's Rendering page
 * via QPropertyModel — getters call PreferencesManager::nodePen /
 * nodeBrush / nodeSize, setters call setNodePen / setNodeBrush /
 * setNodeSize, so any edit routes back through the singleton and
 * fires its preferenceChanged signal exactly like the link-pen
 * bridge does.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef NODERENDERINGPREFS_H
#define NODERENDERINGPREFS_H

#include <QBrush>
#include <QObject>
#include <QPen>

class NodeRenderingPrefs : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QPen junctionPen READ junctionPen WRITE setJunctionPen NOTIFY junctionPenChanged)
    Q_PROPERTY(
        QBrush junctionFill READ junctionFill WRITE setJunctionFill NOTIFY junctionFillChanged)
    Q_PROPERTY(
        double junctionSize READ junctionSize WRITE setJunctionSize NOTIFY junctionSizeChanged)

    Q_PROPERTY(QPen outfallPen READ outfallPen WRITE setOutfallPen NOTIFY outfallPenChanged)
    Q_PROPERTY(QBrush outfallFill READ outfallFill WRITE setOutfallFill NOTIFY outfallFillChanged)
    Q_PROPERTY(double outfallSize READ outfallSize WRITE setOutfallSize NOTIFY outfallSizeChanged)

    Q_PROPERTY(QPen storagePen READ storagePen WRITE setStoragePen NOTIFY storagePenChanged)
    Q_PROPERTY(QBrush storageFill READ storageFill WRITE setStorageFill NOTIFY storageFillChanged)
    Q_PROPERTY(double storageSize READ storageSize WRITE setStorageSize NOTIFY storageSizeChanged)

    Q_PROPERTY(QPen dividerPen READ dividerPen WRITE setDividerPen NOTIFY dividerPenChanged)
    Q_PROPERTY(QBrush dividerFill READ dividerFill WRITE setDividerFill NOTIFY dividerFillChanged)
    Q_PROPERTY(double dividerSize READ dividerSize WRITE setDividerSize NOTIFY dividerSizeChanged)

    Q_PROPERTY(QPen virtualJunctionPen READ virtualJunctionPen WRITE setVirtualJunctionPen NOTIFY
                   virtualJunctionPenChanged)
    Q_PROPERTY(QBrush virtualJunctionFill READ virtualJunctionFill WRITE setVirtualJunctionFill
                   NOTIFY virtualJunctionFillChanged)
    Q_PROPERTY(double virtualJunctionSize READ virtualJunctionSize WRITE setVirtualJunctionSize
                   NOTIFY virtualJunctionSizeChanged)

    Q_PROPERTY(QPen inletJunctionPen READ inletJunctionPen WRITE setInletJunctionPen NOTIFY
                   inletJunctionPenChanged)
    Q_PROPERTY(QBrush inletJunctionFill READ inletJunctionFill WRITE setInletJunctionFill NOTIFY
                   inletJunctionFillChanged)
    Q_PROPERTY(double inletJunctionSize READ inletJunctionSize WRITE setInletJunctionSize NOTIFY
                   inletJunctionSizeChanged)

    // Feedback C14 — rain gage marker + subcatchment polygon defaults.
    Q_PROPERTY(QPen rainGagePen READ rainGagePen WRITE setRainGagePen NOTIFY rainGagePenChanged)
    Q_PROPERTY(
        QBrush rainGageFill READ rainGageFill WRITE setRainGageFill NOTIFY rainGageFillChanged)
    Q_PROPERTY(
        double rainGageSize READ rainGageSize WRITE setRainGageSize NOTIFY rainGageSizeChanged)

    Q_PROPERTY(QPen subcatchmentPen READ subcatchmentPen WRITE setSubcatchmentPen NOTIFY
                   subcatchmentPenChanged)
    Q_PROPERTY(QBrush subcatchmentFill READ subcatchmentFill WRITE setSubcatchmentFill NOTIFY
                   subcatchmentFillChanged)

public:
    /**
     * @brief Create a property-editor facade over application node/marker style preferences.
     * @param[in] parent Optional Qt owner; use this QObject on the GUI thread.
     */
    explicit NodeRenderingPrefs(QObject *parent = nullptr);

    /**
     * @brief Read the junctionPen outline pen from PreferencesManager.
     * @return Value copy of the saved canonical style or its compiled fallback.
     */
    [[nodiscard]] QPen junctionPen() const;
    /**
     * @brief Read the junctionFill fill brush from PreferencesManager.
     * @return Value copy of the saved canonical style or its compiled fallback.
     */
    [[nodiscard]] QBrush junctionFill() const;
    /**
     * @brief Read the junctionSize marker diameter in screen pixels from PreferencesManager.
     * @return Value copy of the saved canonical style or its compiled fallback.
     */
    [[nodiscard]] double junctionSize() const;

    /**
     * @brief Read the outfallPen outline pen from PreferencesManager.
     * @return Value copy of the saved canonical style or its compiled fallback.
     */
    [[nodiscard]] QPen outfallPen() const;
    /**
     * @brief Read the outfallFill fill brush from PreferencesManager.
     * @return Value copy of the saved canonical style or its compiled fallback.
     */
    [[nodiscard]] QBrush outfallFill() const;
    /**
     * @brief Read the outfallSize marker diameter in screen pixels from PreferencesManager.
     * @return Value copy of the saved canonical style or its compiled fallback.
     */
    [[nodiscard]] double outfallSize() const;

    /**
     * @brief Read the storagePen outline pen from PreferencesManager.
     * @return Value copy of the saved canonical style or its compiled fallback.
     */
    [[nodiscard]] QPen storagePen() const;
    /**
     * @brief Read the storageFill fill brush from PreferencesManager.
     * @return Value copy of the saved canonical style or its compiled fallback.
     */
    [[nodiscard]] QBrush storageFill() const;
    /**
     * @brief Read the storageSize marker diameter in screen pixels from PreferencesManager.
     * @return Value copy of the saved canonical style or its compiled fallback.
     */
    [[nodiscard]] double storageSize() const;

    /**
     * @brief Read the dividerPen outline pen from PreferencesManager.
     * @return Value copy of the saved canonical style or its compiled fallback.
     */
    [[nodiscard]] QPen dividerPen() const;
    /**
     * @brief Read the dividerFill fill brush from PreferencesManager.
     * @return Value copy of the saved canonical style or its compiled fallback.
     */
    [[nodiscard]] QBrush dividerFill() const;
    /**
     * @brief Read the dividerSize marker diameter in screen pixels from PreferencesManager.
     * @return Value copy of the saved canonical style or its compiled fallback.
     */
    [[nodiscard]] double dividerSize() const;

    /**
     * @brief Read the virtualJunctionPen outline pen from PreferencesManager.
     * @return Value copy of the saved canonical style or its compiled fallback.
     */
    [[nodiscard]] QPen virtualJunctionPen() const;
    /**
     * @brief Read the virtualJunctionFill fill brush from PreferencesManager.
     * @return Value copy of the saved canonical style or its compiled fallback.
     */
    [[nodiscard]] QBrush virtualJunctionFill() const;
    /**
     * @brief Read the virtualJunctionSize marker diameter in screen pixels from PreferencesManager.
     * @return Value copy of the saved canonical style or its compiled fallback.
     */
    [[nodiscard]] double virtualJunctionSize() const;

    /**
     * @brief Read the inletJunctionPen outline pen from PreferencesManager.
     * @return Value copy of the saved canonical style or its compiled fallback.
     */
    [[nodiscard]] QPen inletJunctionPen() const;
    /**
     * @brief Read the inletJunctionFill fill brush from PreferencesManager.
     * @return Value copy of the saved canonical style or its compiled fallback.
     */
    [[nodiscard]] QBrush inletJunctionFill() const;
    /**
     * @brief Read the inletJunctionSize marker diameter in screen pixels from PreferencesManager.
     * @return Value copy of the saved canonical style or its compiled fallback.
     */
    [[nodiscard]] double inletJunctionSize() const;

    /**
     * @brief Read the rainGagePen outline pen from PreferencesManager.
     * @return Value copy of the saved canonical style or its compiled fallback.
     */
    [[nodiscard]] QPen rainGagePen() const;
    /**
     * @brief Read the rainGageFill fill brush from PreferencesManager.
     * @return Value copy of the saved canonical style or its compiled fallback.
     */
    [[nodiscard]] QBrush rainGageFill() const;
    /**
     * @brief Read the rainGageSize marker diameter in screen pixels from PreferencesManager.
     * @return Value copy of the saved canonical style or its compiled fallback.
     */
    [[nodiscard]] double rainGageSize() const;

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
     * @brief Forward the property edit to the shared preference store.
     * @param[in] pen Complete pen; invalid colors are refused by PreferencesManager.
     * @details Skips equal values (fuzzy-equal for sizes), delegates persistence, then emits the matching property signal with the requested value. A store-refused value can still be signaled by this facade.
     */
    void setJunctionPen(const QPen &pen);
    /**
     * @brief Forward the property edit to the shared preference store.
     * @param[in] brush Complete brush; invalid colors are refused by PreferencesManager.
     * @details Skips equal values (fuzzy-equal for sizes), delegates persistence, then emits the matching property signal with the requested value. A store-refused value can still be signaled by this facade.
     */
    void setJunctionFill(const QBrush &brush);
    /**
     * @brief Forward the property edit to the shared preference store.
     * @param[in] sizePx Marker diameter in [1,64] screen pixels, enforced by PreferencesManager.
     * @details Skips equal values (fuzzy-equal for sizes), delegates persistence, then emits the matching property signal with the requested value. A store-refused value can still be signaled by this facade.
     */
    void setJunctionSize(double sizePx);

    /**
     * @brief Forward the property edit to the shared preference store.
     * @param[in] pen Complete pen; invalid colors are refused by PreferencesManager.
     * @details Skips equal values (fuzzy-equal for sizes), delegates persistence, then emits the matching property signal with the requested value. A store-refused value can still be signaled by this facade.
     */
    void setOutfallPen(const QPen &pen);
    /**
     * @brief Forward the property edit to the shared preference store.
     * @param[in] brush Complete brush; invalid colors are refused by PreferencesManager.
     * @details Skips equal values (fuzzy-equal for sizes), delegates persistence, then emits the matching property signal with the requested value. A store-refused value can still be signaled by this facade.
     */
    void setOutfallFill(const QBrush &brush);
    /**
     * @brief Forward the property edit to the shared preference store.
     * @param[in] sizePx Marker diameter in [1,64] screen pixels, enforced by PreferencesManager.
     * @details Skips equal values (fuzzy-equal for sizes), delegates persistence, then emits the matching property signal with the requested value. A store-refused value can still be signaled by this facade.
     */
    void setOutfallSize(double sizePx);

    /**
     * @brief Forward the property edit to the shared preference store.
     * @param[in] pen Complete pen; invalid colors are refused by PreferencesManager.
     * @details Skips equal values (fuzzy-equal for sizes), delegates persistence, then emits the matching property signal with the requested value. A store-refused value can still be signaled by this facade.
     */
    void setStoragePen(const QPen &pen);
    /**
     * @brief Forward the property edit to the shared preference store.
     * @param[in] brush Complete brush; invalid colors are refused by PreferencesManager.
     * @details Skips equal values (fuzzy-equal for sizes), delegates persistence, then emits the matching property signal with the requested value. A store-refused value can still be signaled by this facade.
     */
    void setStorageFill(const QBrush &brush);
    /**
     * @brief Forward the property edit to the shared preference store.
     * @param[in] sizePx Marker diameter in [1,64] screen pixels, enforced by PreferencesManager.
     * @details Skips equal values (fuzzy-equal for sizes), delegates persistence, then emits the matching property signal with the requested value. A store-refused value can still be signaled by this facade.
     */
    void setStorageSize(double sizePx);

    /**
     * @brief Forward the property edit to the shared preference store.
     * @param[in] pen Complete pen; invalid colors are refused by PreferencesManager.
     * @details Skips equal values (fuzzy-equal for sizes), delegates persistence, then emits the matching property signal with the requested value. A store-refused value can still be signaled by this facade.
     */
    void setDividerPen(const QPen &pen);
    /**
     * @brief Forward the property edit to the shared preference store.
     * @param[in] brush Complete brush; invalid colors are refused by PreferencesManager.
     * @details Skips equal values (fuzzy-equal for sizes), delegates persistence, then emits the matching property signal with the requested value. A store-refused value can still be signaled by this facade.
     */
    void setDividerFill(const QBrush &brush);
    /**
     * @brief Forward the property edit to the shared preference store.
     * @param[in] sizePx Marker diameter in [1,64] screen pixels, enforced by PreferencesManager.
     * @details Skips equal values (fuzzy-equal for sizes), delegates persistence, then emits the matching property signal with the requested value. A store-refused value can still be signaled by this facade.
     */
    void setDividerSize(double sizePx);

    /**
     * @brief Forward the property edit to the shared preference store.
     * @param[in] pen Complete pen; invalid colors are refused by PreferencesManager.
     * @details Skips equal values (fuzzy-equal for sizes), delegates persistence, then emits the matching property signal with the requested value. A store-refused value can still be signaled by this facade.
     */
    void setVirtualJunctionPen(const QPen &pen);
    /**
     * @brief Forward the property edit to the shared preference store.
     * @param[in] brush Complete brush; invalid colors are refused by PreferencesManager.
     * @details Skips equal values (fuzzy-equal for sizes), delegates persistence, then emits the matching property signal with the requested value. A store-refused value can still be signaled by this facade.
     */
    void setVirtualJunctionFill(const QBrush &brush);
    /**
     * @brief Forward the property edit to the shared preference store.
     * @param[in] sizePx Marker diameter in [1,64] screen pixels, enforced by PreferencesManager.
     * @details Skips equal values (fuzzy-equal for sizes), delegates persistence, then emits the matching property signal with the requested value. A store-refused value can still be signaled by this facade.
     */
    void setVirtualJunctionSize(double sizePx);

    /**
     * @brief Forward the property edit to the shared preference store.
     * @param[in] pen Complete pen; invalid colors are refused by PreferencesManager.
     * @details Skips equal values (fuzzy-equal for sizes), delegates persistence, then emits the matching property signal with the requested value. A store-refused value can still be signaled by this facade.
     */
    void setInletJunctionPen(const QPen &pen);
    /**
     * @brief Forward the property edit to the shared preference store.
     * @param[in] brush Complete brush; invalid colors are refused by PreferencesManager.
     * @details Skips equal values (fuzzy-equal for sizes), delegates persistence, then emits the matching property signal with the requested value. A store-refused value can still be signaled by this facade.
     */
    void setInletJunctionFill(const QBrush &brush);
    /**
     * @brief Forward the property edit to the shared preference store.
     * @param[in] sizePx Marker diameter in [1,64] screen pixels, enforced by PreferencesManager.
     * @details Skips equal values (fuzzy-equal for sizes), delegates persistence, then emits the matching property signal with the requested value. A store-refused value can still be signaled by this facade.
     */
    void setInletJunctionSize(double sizePx);

    /**
     * @brief Forward the property edit to the shared preference store.
     * @param[in] pen Complete pen; invalid colors are refused by PreferencesManager.
     * @details Skips equal values (fuzzy-equal for sizes), delegates persistence, then emits the matching property signal with the requested value. A store-refused value can still be signaled by this facade.
     */
    void setRainGagePen(const QPen &pen);
    /**
     * @brief Forward the property edit to the shared preference store.
     * @param[in] brush Complete brush; invalid colors are refused by PreferencesManager.
     * @details Skips equal values (fuzzy-equal for sizes), delegates persistence, then emits the matching property signal with the requested value. A store-refused value can still be signaled by this facade.
     */
    void setRainGageFill(const QBrush &brush);
    /**
     * @brief Forward the property edit to the shared preference store.
     * @param[in] sizePx Marker diameter in [1,64] screen pixels, enforced by PreferencesManager.
     * @details Skips equal values (fuzzy-equal for sizes), delegates persistence, then emits the matching property signal with the requested value. A store-refused value can still be signaled by this facade.
     */
    void setRainGageSize(double sizePx);

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

signals:
    /**
     * @brief Notify the property editor after its facade setter delegates a changed request.
     * @param[in] pen Requested style value; query the getter for the effective stored value after validation.
     */
    void junctionPenChanged(const QPen &pen);
    /**
     * @brief Notify the property editor after its facade setter delegates a changed request.
     * @param[in] brush Requested style value; query the getter for the effective stored value after validation.
     */
    void junctionFillChanged(const QBrush &brush);
    /**
     * @brief Notify the property editor after its facade setter delegates a changed request.
     * @param[in] sizePx Requested style value; query the getter for the effective stored value after validation.
     */
    void junctionSizeChanged(double sizePx);

    /**
     * @brief Notify the property editor after its facade setter delegates a changed request.
     * @param[in] pen Requested style value; query the getter for the effective stored value after validation.
     */
    void outfallPenChanged(const QPen &pen);
    /**
     * @brief Notify the property editor after its facade setter delegates a changed request.
     * @param[in] brush Requested style value; query the getter for the effective stored value after validation.
     */
    void outfallFillChanged(const QBrush &brush);
    /**
     * @brief Notify the property editor after its facade setter delegates a changed request.
     * @param[in] sizePx Requested style value; query the getter for the effective stored value after validation.
     */
    void outfallSizeChanged(double sizePx);

    /**
     * @brief Notify the property editor after its facade setter delegates a changed request.
     * @param[in] pen Requested style value; query the getter for the effective stored value after validation.
     */
    void storagePenChanged(const QPen &pen);
    /**
     * @brief Notify the property editor after its facade setter delegates a changed request.
     * @param[in] brush Requested style value; query the getter for the effective stored value after validation.
     */
    void storageFillChanged(const QBrush &brush);
    /**
     * @brief Notify the property editor after its facade setter delegates a changed request.
     * @param[in] sizePx Requested style value; query the getter for the effective stored value after validation.
     */
    void storageSizeChanged(double sizePx);

    /**
     * @brief Notify the property editor after its facade setter delegates a changed request.
     * @param[in] pen Requested style value; query the getter for the effective stored value after validation.
     */
    void dividerPenChanged(const QPen &pen);
    /**
     * @brief Notify the property editor after its facade setter delegates a changed request.
     * @param[in] brush Requested style value; query the getter for the effective stored value after validation.
     */
    void dividerFillChanged(const QBrush &brush);
    /**
     * @brief Notify the property editor after its facade setter delegates a changed request.
     * @param[in] sizePx Requested style value; query the getter for the effective stored value after validation.
     */
    void dividerSizeChanged(double sizePx);

    /**
     * @brief Notify the property editor after its facade setter delegates a changed request.
     * @param[in] pen Requested style value; query the getter for the effective stored value after validation.
     */
    void virtualJunctionPenChanged(const QPen &pen);
    /**
     * @brief Notify the property editor after its facade setter delegates a changed request.
     * @param[in] brush Requested style value; query the getter for the effective stored value after validation.
     */
    void virtualJunctionFillChanged(const QBrush &brush);
    /**
     * @brief Notify the property editor after its facade setter delegates a changed request.
     * @param[in] sizePx Requested style value; query the getter for the effective stored value after validation.
     */
    void virtualJunctionSizeChanged(double sizePx);

    /**
     * @brief Notify the property editor after its facade setter delegates a changed request.
     * @param[in] pen Requested style value; query the getter for the effective stored value after validation.
     */
    void inletJunctionPenChanged(const QPen &pen);
    /**
     * @brief Notify the property editor after its facade setter delegates a changed request.
     * @param[in] brush Requested style value; query the getter for the effective stored value after validation.
     */
    void inletJunctionFillChanged(const QBrush &brush);
    /**
     * @brief Notify the property editor after its facade setter delegates a changed request.
     * @param[in] sizePx Requested style value; query the getter for the effective stored value after validation.
     */
    void inletJunctionSizeChanged(double sizePx);

    /**
     * @brief Notify the property editor after its facade setter delegates a changed request.
     * @param[in] pen Requested style value; query the getter for the effective stored value after validation.
     */
    void rainGagePenChanged(const QPen &pen);
    /**
     * @brief Notify the property editor after its facade setter delegates a changed request.
     * @param[in] brush Requested style value; query the getter for the effective stored value after validation.
     */
    void rainGageFillChanged(const QBrush &brush);
    /**
     * @brief Notify the property editor after its facade setter delegates a changed request.
     * @param[in] sizePx Requested style value; query the getter for the effective stored value after validation.
     */
    void rainGageSizeChanged(double sizePx);

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
};

#endif // NODERENDERINGPREFS_H
