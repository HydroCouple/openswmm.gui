/*!
 * \file initialqualitydialog.h
 * \brief Editor for `[INITIAL_QUALITY]` — per-node/per-link initial
 *        concentrations (GUI plan §3.4 pattern / program E-A, round G-A1).
 *
 * \details One add/remove row table over the engine's
 *          `swmm_init_quality_*` surface: Scope (NODE/LINK), Element,
 *          Constituent (pollutants plus the reserved `__WATER_AGE__` /
 *          `__TEMPERATURE__` species, offered only when their [OPTIONS]
 *          toggle is on), Value. Pollutant values are concentrations in the
 *          pollutant's own units; water age is HOURS (signed — engine
 *          D-NS1); temperature is degC.
 *
 *          The engine handle IS the model (GUI plan §2): edits are read
 *          from and written to `openswmm_initial_quality.h` directly, with
 *          no intermediate registry. Deliberately **dependency-light** — Qt
 *          Widgets plus that one engine ABI — so a test can link and drive
 *          it (the `WaterAgeSourcesDialog` / `ClimatologyDialog` precedent).
 *
 * \author  Caleb Buahin <caleb.buahin@gmail.com>
 * \copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * \license Apache-2.0
 */

#ifndef OPENSWMM_UI_DIALOGS_INITIALQUALITYDIALOG_H
#define OPENSWMM_UI_DIALOGS_INITIALQUALITYDIALOG_H

#include <openswmm/engine/openswmm_engine.h>

#include <QDialog>

class QTableWidget;
class QLabel;

namespace OpenSWMMVis
{

/*!
 * \brief Non-modal editor for the per-element initial-quality table.
 */
class InitialQualityDialog : public QDialog
{
    Q_OBJECT

public:
    explicit InitialQualityDialog(SWMM_Engine engine,
                                  QWidget *parent = nullptr);
    ~InitialQualityDialog() override = default;

    /*! \brief Restrict the dialog to one node (\p isLink 0) or link
     *         (\p isLink 1). The Scope / Element columns collapse, rows
     *         for other elements are neither shown nor touched on OK,
     *         and Add creates rows pinned to the element. No-op when
     *         the element is unknown to the engine. Used by the
     *         Property Browser's per-element "Initial Quality" cell. */
    void setElementScope(int isLink, const QString &elementName);

    /*! \brief True once OK has written at least one change to the engine. */
    [[nodiscard]] bool wroteAnyChanges() const { return m_wroteAnyChanges; }

    /*! \brief Number of engine writes the last OK performed (0 when the
     *         dialog was accepted with no edits). Test-facing. */
    [[nodiscard]] int lastWriteCount() const { return m_lastWriteCount; }

private slots:
    void onAddRow();
    void onRemoveRow();
    void onAccept();

private:
    void buildUi();
    void readFromEngine();
    int  writeToEngine();          ///< returns the number of writes made
    void populateElementCombo(int row);

    SWMM_Engine   m_engine          = nullptr;
    QTableWidget *m_table           = nullptr;
    QLabel       *m_hintLabel       = nullptr;
    bool          m_wroteAnyChanges = false;
    int           m_lastWriteCount  = 0;
    int           m_scopeIsLink     = -1;   ///< -1 = whole-model mode
    int           m_scopeElemIdx    = -1;   ///< engine index of the scoped element
};

} // namespace OpenSWMMVis

#endif // OPENSWMM_UI_DIALOGS_INITIALQUALITYDIALOG_H
