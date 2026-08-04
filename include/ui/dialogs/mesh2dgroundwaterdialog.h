/*!
 * \file   mesh2dgroundwaterdialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Preview of the per-cell 2D two-zone groundwater editor.
 *
 * The engine has no `[2D_AQUIFER]` implementation yet — only the draft design
 * in `openswmm.engine/plans/TWO_ZONE_GROUNDWATER_FV_INTEGRATION_PLAN.md`
 * (PIHM / Qu & Duffy 2007). This dialog lays the editor out against that
 * design with every input disabled and a banner saying so, which keeps the
 * planned parameter surface visible and makes the eventual wiring mechanical:
 * enable the inputs, add the fields to mesh::MeshTriangle, and flip the
 * `gw.*` entries in mesh::cellParamSpecs() to enabled.
 *
 * Parameters mirror §4.3 of that plan: per cell Ks, zs, theta_s, a soil
 * characteristic model with its model-specific extra parameters, a closure
 * mode, and the initial unsaturated / saturated zone depths.
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_MESH2DGROUNDWATERDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_MESH2DGROUNDWATERDIALOG_H

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QStackedWidget;
class QTabWidget;

namespace openswmmvis::ui {

class Mesh2DGroundwaterDialog : public QDialog
{
    Q_OBJECT

public:
    /*! \brief Which page opens first. */
    enum class Page { AquiferProperties, InitialConditions };

    explicit Mesh2DGroundwaterDialog(QWidget *parent = nullptr,
                                     Page initialPage = Page::AquiferProperties);

    /*! \brief Soil characteristic models accepted by the draft [2D_AQUIFER]
     *         section, in the order the section documents them. */
    [[nodiscard]] static QStringList soilModelTokens();

    /*! \brief Closure modes accepted by the draft [2D_AQUIFER] section. */
    [[nodiscard]] static QStringList closureTokens();

private:
    void buildUi(Page initialPage);
    QWidget *buildAquiferPage();
    QWidget *buildInitialConditionsPage();

    QTabWidget     *m_tabs           = nullptr;
    QComboBox      *m_scopeCombo     = nullptr;
    QDoubleSpinBox *m_ksSpin         = nullptr;
    QDoubleSpinBox *m_zsSpin         = nullptr;
    QDoubleSpinBox *m_thetaSpin      = nullptr;
    QComboBox      *m_soilCombo      = nullptr;
    QComboBox      *m_closureCombo   = nullptr;
    QStackedWidget *m_soilExtraStack = nullptr;
    QSpinBox       *m_layersSpin     = nullptr;
    QDoubleSpinBox *m_huSpin         = nullptr;
    QDoubleSpinBox *m_hgSpin         = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_MESH2DGROUNDWATERDIALOG_H
