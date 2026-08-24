/*!
 * \file wateragesourcesdialog.h
 * \brief Editor for `[WATER_AGE_SOURCES]` — per-pathway initial water ages
 *        (GUI plan §3.4 / G3g, subplan Y3).
 *
 * \details Water age enters the model at seven source pathways (rainfall,
 *          DWF, groundwater, RDII, external inflow, routing-interface file,
 *          and the initial network state). Each carries a GLOBAL age in
 *          hours, and the DWF / external-inflow pathways additionally take
 *          per-NODE overrides — the scope rule the engine's parser enforces
 *          and `swmm_water_age_set_override` mirrors.
 *
 *          **Negative ages are legal** (engine D-NS1): a negative source
 *          age *extracts* age-volume, making water read younger, clamped so
 *          age never falls below zero. The editor therefore accepts
 *          negatives and says what they mean rather than validating them
 *          away.
 *
 *          The engine handle IS the model (GUI plan §2): edits are read
 *          from and written to `openswmm_water_age.h` directly, with no
 *          intermediate registry. The dialog is deliberately
 *          **dependency-light** — Qt Widgets plus that one engine ABI —
 *          which is what lets a test link and drive it (the
 *          `ClimatologyDialog` precedent; contrast
 *          `SimulationOptionsDialog`, whose link closure no test can
 *          satisfy, `tests/gui/CMakeLists.txt:1996`).
 *
 * \author  Caleb Buahin <caleb.buahin@gmail.com>
 * \copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * \license Apache-2.0
 */

#ifndef OPENSWMM_UI_DIALOGS_WATERAGESOURCESDIALOG_H
#define OPENSWMM_UI_DIALOGS_WATERAGESOURCESDIALOG_H

#include <openswmm/engine/openswmm_engine.h>

#include <QDialog>

class QTableWidget;
class QLabel;

namespace OpenSWMMVis
{

/*!
 * \brief Non-modal editor for the water-age source table.
 */
class WaterAgeSourcesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit WaterAgeSourcesDialog(SWMM_Engine engine,
                                   QWidget *parent = nullptr);
    ~WaterAgeSourcesDialog() override = default;

    /*! \brief True once OK has written at least one change to the engine.
     *  \details Lets the caller mark the project dirty without diffing —
     *           and lets a test assert that a no-op OK writes nothing. */
    [[nodiscard]] bool wroteAnyChanges() const { return m_wroteAnyChanges; }

    /*! \brief Number of engine writes the last OK performed (0 when the
     *         dialog was accepted with no edits). Test-facing. */
    [[nodiscard]] int lastWriteCount() const { return m_lastWriteCount; }

private slots:
    void onAddOverride();
    void onRemoveOverride();
    void onAccept();

private:
    void buildUi();
    void readFromEngine();
    int  writeToEngine();          ///< returns the number of writes made

    SWMM_Engine   m_engine          = nullptr;
    QTableWidget *m_globalTable     = nullptr;
    QTableWidget *m_overrideTable   = nullptr;
    QLabel       *m_hintLabel       = nullptr;
    bool          m_wroteAnyChanges = false;
    int           m_lastWriteCount  = 0;
};

} // namespace OpenSWMMVis

#endif // OPENSWMM_UI_DIALOGS_WATERAGESOURCESDIALOG_H
