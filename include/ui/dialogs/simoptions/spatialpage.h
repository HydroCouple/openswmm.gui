/**
 * @file      spatialpage.h
 * @brief     Simulation Options → Spatial & CRS.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Simulation Options → Spatial & CRS.
 *
 * Ported verbatim from `SimulationOptionsDialog::buildSpatialTab()` and its
 * two CRS slots. The page writes no `[OPTIONS]` key during a write pass: the
 * CRS is written the moment the user picks one, so `write()` returns 0 and
 * reports the change through crsChanged().
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef SPATIALPAGE_H
#define SPATIALPAGE_H

#include "ui/dialogs/simoptions/simoptionspage.h"

class QLabel;
class QToolButton;

namespace openswmmvis::ui
{

class SpatialPage : public SimOptionsPage
{
    Q_OBJECT

public:
    /**
     * @brief Build CRS summary and immediate-action controls.
     * @param ctx Borrowed context that must outlive the page; backing layer must stay
     * valid while accessed, including across its modal CRS picker.
     * @param parent Optional widget owner; null leaves lifetime to the caller.
     * Controls start disabled when no layer is bound. Does not hydrate summaries
     * until read() or refreshSummary() and does not transform coordinates.
     */
    explicit SpatialPage(SimOptionsContext &ctx, QWidget *parent = nullptr);

    /**
     * @brief Return the localized sidebar caption.
     * @return Owned Spatial & CRS string.
     */
    [[nodiscard]] QString title() const override;
    /**
     * @brief Refresh the layer CRS/extent labels without changing the engine or stored
     * coordinates.
     */
    void read() override;
    /**
     * @brief Contribute no deferred Apply write.
     * @return Always zero; CRS picking performs its metadata/option edit immediately
     * and separately emits crsChanged. Does not record an option consideration here.
     */
    int write() override;

signals:
    /**
     * @brief Notify the host after an accepted non-null CRS is assigned to the layer.
     * Emitted after the option-write attempt even if its result is unchanged/refused;
     * the signal does not itself prove a successful engine write.
     */
    void crsChanged();

private slots:
    /**
     * @brief Run the modal CRS picker, adopt its selected SRS and attempt immediate CRS-option
     * persistence.
     * Returns on absent layer, rejection or no selected SRS. Uses authority text or
     * WKT, emits crsChanged regardless of setter result, then refreshes summaries.
     * Changes layer metadata without reprojecting stored coordinates. The captured
     * raw layer must outlive the nested picker event loop; context invalidation alone
     * does not guard that saved pointer. No undo command or file save is created here.
     */
    void onPickCRS();
    /**
     * @brief Display a non-destructive CRS hint from the current layer extent.
     * Returns on absent layer/invalid extent. Bounds within longitude +/-180 and
     * latitude +/-90 suggest EPSG:4326; other bounds suggest choosing a projected CRS.
     * Does not infer a unique CRS, set the layer/engine CRS or emit crsChanged.
     */
    void onDetectCRS();

private:
    /**
     * @brief Create owned CRS/extent labels and connect Change/Detect controls once.
     * Initially disables both controls without a model layer; labels begin as unknown
     * until read. Does not update engine options or configure a coordinate transform.
     */
    void buildUi();
    /**
     * @brief Present the current borrowed layer CRS authority and native-coordinate extent.
     * Uses local/none/no-layer/invalid-extent labels as appropriate and formats valid
     * bounds to eight significant digits. Returns if controls are absent; does not
     * transform coordinates, write options or change button capability gates.
     */
    void refreshSummary();

    QLabel *m_crsLabel = nullptr;
    QLabel *m_extentLabel = nullptr;
    QToolButton *m_crsChangeButton = nullptr;
    QToolButton *m_crsDetectButton = nullptr;
};

} // namespace openswmmvis::ui

#endif // SPATIALPAGE_H
