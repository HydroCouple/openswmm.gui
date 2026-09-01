/*!
 * \file heatconfigdialog.h
 * \brief Editor for the heat-transport configuration — `[HEAT_SOURCES]`
 *        inlet temperatures, `[HEAT_FLUXES]` module toggles, and H6a's
 *        `[RADIATIVE_FLUXES]` / `[SOLAR_RADIATION]` / `[CLOUD_COVER]`
 *        (GUI plan G4g, against the corrected spec — there is no
 *        `[HEAT_METEOROLOGY]` section).
 *
 * \details Heat enters the model at seven water sources, each with a
 *          GLOBAL inlet temperature in °C and — for DWF and external
 *          inflow — per-NODE overrides (the engine parser's H1 scope
 *          rule, which `swmm_heat_set_node_override` refuses to break).
 *          A source the model never set reads the 20 °C default;
 *          `swmm_heat_get_source_configured` is what separates "set to
 *          20" from "never set", and the editor preserves that
 *          distinction with a per-source check box so an untouched OK
 *          cannot invent `[HEAT_SOURCES]` rows.
 *
 *          The engine handle IS the model (GUI plan §2): edits go through
 *          `openswmm_heat.h` directly, values the parser refuses are
 *          refused here too (ranges mirror `parse_celsius` and friends),
 *          and IO3b's renderer persists whatever this dialog writes.
 *
 *          Dependency-light — Qt Widgets plus that one engine ABI — so a
 *          test can construct it against a synthetic BUILDING engine and
 *          drive the widgets (the WaterAgeSourcesDialog precedent).
 *
 *          Known API gap, recorded in the G4g round notes: the engine
 *          exposes the shortwave/cloud timeseries MODE but not the bound
 *          series NAME, so those combos rebind rather than display — the
 *          "(keep current series)" placeholder is the no-op position.
 *
 * \author  Caleb Buahin <caleb.buahin@gmail.com>
 * \copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * \license Apache-2.0
 */

#ifndef OPENSWMM_UI_DIALOGS_HEATCONFIGDIALOG_H
#define OPENSWMM_UI_DIALOGS_HEATCONFIGDIALOG_H

#include <openswmm/engine/openswmm_engine.h>

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QRadioButton;
class QTableWidget;

namespace OpenSWMMVis
{

/*!
 * \brief Editor for `[HEAT_SOURCES]`, `[HEAT_FLUXES]` and the H6a
 *        radiative / solar / cloud configuration.
 */
class HeatConfigDialog : public QDialog
{
    Q_OBJECT

public:
    explicit HeatConfigDialog(SWMM_Engine engine, QWidget *parent = nullptr);
    ~HeatConfigDialog() override = default;

    /*! \brief True once OK has written at least one change to the engine. */
    bool wroteAnyChanges() const { return m_wroteAnyChanges; }

    /*! \brief Number of engine writes the last OK performed (test hook). */
    int lastWriteCount() const { return m_lastWriteCount; }

private slots:
    void onAccept();
    void onAddOverride();
    void onRemoveOverride();

private:
    void buildUi();
    void readFromEngine();
    int  writeToEngine();
    int  writeSolar();
    int  writeRadiative();
    int  writeSources();
    int  writeModules();
    int  writeCloud();

    SWMM_Engine m_engine = nullptr;
    bool m_wroteAnyChanges = false;
    int  m_lastWriteCount = 0;

    // Sources tab
    QTableWidget *m_sourceTable = nullptr;    ///< 7 rows: check + °C spin
    QTableWidget *m_overrideTable = nullptr;  ///< source / node / °C

    // Fluxes tab
    QCheckBox *m_modules[3] = { nullptr, nullptr, nullptr };

    // Radiative tab
    QRadioButton *m_swConstant = nullptr;
    QRadioButton *m_swTimeseries = nullptr;
    QRadioButton *m_swComputed = nullptr;
    QDoubleSpinBox *m_swConstSpin = nullptr;
    QComboBox *m_swTsCombo = nullptr;
    /// Bound series names as hydrated (swmm_heat_get_*_timeseries), so the
    /// OK path can tell a real rebind from reselecting what was shown.
    QString m_swTsInitial;
    QString m_cloudTsInitial;
    QDoubleSpinBox *m_radSpin[8] = {};        ///< index = param enum; [0] unused

    // Solar tab
    QDoubleSpinBox *m_solarSpin[9] = {};      ///< index = param enum

    // Cloud tab
    QCheckBox *m_cloudEnable = nullptr;
    QDoubleSpinBox *m_cloudSpin[4] = {};      ///< index = param enum
    QComboBox *m_cloudTsCombo = nullptr;
};

} // namespace OpenSWMMVis

#endif // OPENSWMM_UI_DIALOGS_HEATCONFIGDIALOG_H
