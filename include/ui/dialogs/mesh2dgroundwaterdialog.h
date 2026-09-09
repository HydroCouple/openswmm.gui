/*!
 * \file   mesh2dgroundwaterdialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * GG1-GG4, GG6 (2026-09-07) — the 2D two-zone groundwater editor.
 *
 * This was a display-only preview laid out against a draft design. The kernel
 * now exists (`openswmm.engine/src/engine/2d/subsurface/`), so the dialog is
 * live and talks to it through `openswmm_gw2d.h`:
 *
 *   - **Options** — `[2D_AQUIFER_OPTIONS]`: soil law, closure, σ layers,
 *     stability factors, Dunne and GW_ET.
 *   - **Aquifer** — the `[2D_AQUIFER]` rows, per scope, in a table.
 *   - **Node beds** — `[2D_AQUIFER_NODE]`: which node exchanges with which
 *     cell, through what conductance.
 *   - **State** — read-only, live during a run: table elevation, storage,
 *     the closure each cell resolved to, the LTS tier histogram and the
 *     continuity residual.
 *
 * MVC per CLAUDE.md §5.1: the tables are `Mesh2DAquiferModel` and
 * `Mesh2DAquiferNodeModel`, so the same rows can be edited from a future
 * cell-properties panel and stay in step. The dialog owns presentation and
 * the Apply/Cancel contract, nothing else.
 *
 * Editing is refused mid-run by the C API, on purpose: these rows seed the
 * kernel at initialize, so a mid-run edit would be silently dropped. The
 * dialog reads that state up front and disables the editors with a reason
 * rather than letting Apply fail.
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_MESH2DGROUNDWATERDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_MESH2DGROUNDWATERDIALOG_H

#include <QDialog>

#include <openswmm/engine/openswmm_engine.h>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableView;
class QTabWidget;

class UnitSystem;   // core/unitsystem.h — global scope, as every other dialog declares it

namespace openswmmvis::ui {

class Mesh2DAquiferModel;
class Mesh2DAquiferNodeModel;

class Mesh2DGroundwaterDialog : public QDialog
{
    Q_OBJECT

public:
    /*! \brief Which page opens first. */
    enum class Page { AquiferProperties, InitialConditions, Options, State };

    explicit Mesh2DGroundwaterDialog(SWMM_Engine engine,
                                     QWidget *parent = nullptr,
                                     Page initialPage = Page::AquiferProperties,
                                     const UnitSystem *units = nullptr);

    /*! \brief Soil characteristic laws, in the engine's enum order. */
    [[nodiscard]] static QStringList soilModelTokens();
    /*! \brief Unsaturated-zone closures, AUTO first. */
    [[nodiscard]] static QStringList closureTokens();

private slots:
    void onApply();
    void onAddAquiferRow();
    void onRemoveAquiferRow();
    void onAddNodeBed();
    void onRemoveNodeBed();
    void refreshState();

private:
    void buildUi(Page initialPage);
    QWidget *buildOptionsPage();
    QWidget *buildAquiferPage();
    QWidget *buildNodeBedPage();
    QWidget *buildStatePage();

    void loadFromEngine();
    QString applyOptions();
    void    setEditable(bool on, const QString &whyNot);

    SWMM_Engine                    m_engine = nullptr;
    const UnitSystem              *m_units  = nullptr;

    QTabWidget     *m_tabs           = nullptr;
    QLabel         *m_banner         = nullptr;

    // Options
    QComboBox      *m_soilCombo      = nullptr;
    QComboBox      *m_closureCombo   = nullptr;
    QSpinBox       *m_layersSpin     = nullptr;
    QCheckBox      *m_capillaryCheck = nullptr;
    QDoubleSpinBox *m_cgwSpin        = nullptr;
    QDoubleSpinBox *m_ccolSpin       = nullptr;
    QCheckBox      *m_forceCfCheck   = nullptr;
    QCheckBox      *m_dunneCheck     = nullptr;
    QComboBox      *m_modeCombo      = nullptr;
    QComboBox      *m_gwEtCombo      = nullptr;

    // Tables
    QTableView             *m_aquiferView = nullptr;
    Mesh2DAquiferModel     *m_aquifer     = nullptr;
    QTableView             *m_nodeView    = nullptr;
    Mesh2DAquiferNodeModel *m_nodes       = nullptr;
    QPushButton            *m_addRowBtn   = nullptr;
    QPushButton            *m_delRowBtn   = nullptr;
    QPushButton            *m_addBedBtn   = nullptr;
    QPushButton            *m_delBedBtn   = nullptr;

    // State
    QPlainTextEdit *m_stateText   = nullptr;
    QPushButton    *m_refreshBtn  = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_MESH2DGROUNDWATERDIALOG_H
