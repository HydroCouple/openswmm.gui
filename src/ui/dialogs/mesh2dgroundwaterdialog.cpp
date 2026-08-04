/*!
 * \file   mesh2dgroundwaterdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/mesh2dgroundwaterdialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QLabel>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>

namespace openswmmvis::ui {

namespace {

/*! Disable every input on \p page so the preview cannot be mistaken for a
 *  working editor, while leaving labels and tooltips readable. */
void disableInputs(QWidget *page)
{
    const auto widgets = page->findChildren<QWidget *>();
    for (QWidget *w : widgets) {
        if (qobject_cast<QAbstractSpinBox *>(w) || qobject_cast<QComboBox *>(w))
            w->setEnabled(false);
    }
}

} // namespace

QStringList Mesh2DGroundwaterDialog::soilModelTokens()
{
    // [2D_AQUIFER] soil_char column, in the draft plan's documented order.
    return {QStringLiteral("GARDNER"), QStringLiteral("RUSSO"),
            QStringLiteral("BROOKS_COREY"), QStringLiteral("VAN_GENUCHTEN")};
}

QStringList Mesh2DGroundwaterDialog::closureTokens()
{
    // [2D_AQUIFER] closure column.
    return {QStringLiteral("CLOSED_FORM"), QStringLiteral("KINEMATIC"),
            QStringLiteral("ENSLAVED"), QStringLiteral("AUTO")};
}

Mesh2DGroundwaterDialog::Mesh2DGroundwaterDialog(QWidget *parent,
                                                 Page initialPage)
    : QDialog(parent)
{
    setWindowTitle(tr("2D Groundwater (Preview)"));
    buildUi(initialPage);
}

void Mesh2DGroundwaterDialog::buildUi(Page initialPage)
{
    auto *outer = new QVBoxLayout(this);

    auto *banner = new QLabel(
        tr("Preview — the 2D two-zone groundwater kernel is not in the engine "
           "yet, so these inputs are display-only. They follow the draft "
           "[2D_AQUIFER] design under review; nothing is written to the model."),
        this);
    banner->setWordWrap(true);
    banner->setFrameShape(QFrame::StyledPanel);
    banner->setContentsMargins(8, 6, 8, 6);
    outer->addWidget(banner);

    // Scope selector — present so the eventual per-cell/selection semantics
    // are visible, disabled with the rest.
    auto *scopeRow = new QFormLayout;
    m_scopeCombo = new QComboBox(this);
    m_scopeCombo->addItem(tr("All cells"));
    m_scopeCombo->addItem(tr("Selected cells"));
    m_scopeCombo->addItem(tr("Cells with tag…"));
    m_scopeCombo->setEnabled(false);
    scopeRow->addRow(tr("Apply to:"), m_scopeCombo);
    outer->addLayout(scopeRow);

    m_tabs = new QTabWidget(this);
    m_tabs->addTab(buildAquiferPage(), tr("Aquifer Properties"));
    m_tabs->addTab(buildInitialConditionsPage(), tr("Initial Conditions"));
    m_tabs->setCurrentIndex(initialPage == Page::InitialConditions ? 1 : 0);
    outer->addWidget(m_tabs, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    outer->addWidget(buttons);

    resize(460, 380);
}

QWidget *Mesh2DGroundwaterDialog::buildAquiferPage()
{
    auto *page = new QWidget(this);
    auto *form = new QFormLayout(page);

    m_ksSpin = new QDoubleSpinBox(page);
    m_ksSpin->setRange(1e-9, 1.0);
    m_ksSpin->setDecimals(8);
    m_ksSpin->setValue(1e-5);
    m_ksSpin->setSuffix(QStringLiteral(" m/s"));
    m_ksSpin->setToolTip(tr("Saturated hydraulic conductivity (Ks)."));
    form->addRow(tr("Saturated conductivity (Ks):"), m_ksSpin);

    m_zsSpin = new QDoubleSpinBox(page);
    m_zsSpin->setRange(0.0, 1000.0);
    m_zsSpin->setDecimals(3);
    m_zsSpin->setValue(2.0);
    m_zsSpin->setSuffix(QStringLiteral(" m"));
    m_zsSpin->setToolTip(tr("Thickness of the aquifer below the cell bed (zs)."));
    form->addRow(tr("Aquifer thickness (zs):"), m_zsSpin);

    m_thetaSpin = new QDoubleSpinBox(page);
    m_thetaSpin->setRange(0.01, 1.0);
    m_thetaSpin->setDecimals(3);
    m_thetaSpin->setValue(0.4);
    m_thetaSpin->setToolTip(tr("Saturated water content / porosity (theta_s)."));
    form->addRow(tr("Porosity (theta_s):"), m_thetaSpin);

    m_soilCombo = new QComboBox(page);
    m_soilCombo->addItems(soilModelTokens());
    m_soilCombo->setCurrentText(QStringLiteral("RUSSO"));   // the draft default
    m_soilCombo->setToolTip(tr("Soil characteristic model (soil_char)."));
    form->addRow(tr("Soil model:"), m_soilCombo);

    // Extra parameters differ per soil model; the stack keeps each set laid
    // out so wiring later is only a matter of enabling and reading them.
    m_soilExtraStack = new QStackedWidget(page);
    m_soilExtraStack->setObjectName(QStringLiteral("soilExtraStack"));
    {
        auto *gardner = new QWidget(m_soilExtraStack);   // GARDNER + RUSSO
        auto *gf = new QFormLayout(gardner);
        auto *alpha = new QDoubleSpinBox(gardner);
        alpha->setRange(0.001, 100.0);
        alpha->setDecimals(4);
        alpha->setValue(1.0);
        gf->addRow(tr("alpha:"), alpha);
        m_soilExtraStack->addWidget(gardner);            // index 0 → GARDNER
        auto *russo = new QWidget(m_soilExtraStack);
        auto *rf = new QFormLayout(russo);
        auto *ralpha = new QDoubleSpinBox(russo);
        ralpha->setRange(0.001, 100.0);
        ralpha->setDecimals(4);
        ralpha->setValue(1.0);
        rf->addRow(tr("alpha:"), ralpha);
        m_soilExtraStack->addWidget(russo);              // index 1 → RUSSO

        auto *bc = new QWidget(m_soilExtraStack);        // BROOKS_COREY
        auto *bf = new QFormLayout(bc);
        auto *psib = new QDoubleSpinBox(bc);
        psib->setRange(0.0, 100.0);
        psib->setDecimals(4);
        psib->setValue(0.1);
        psib->setSuffix(QStringLiteral(" m"));
        bf->addRow(tr("psi_b:"), psib);
        auto *lambda = new QDoubleSpinBox(bc);
        lambda->setRange(0.01, 10.0);
        lambda->setDecimals(4);
        lambda->setValue(0.5);
        bf->addRow(tr("lambda:"), lambda);
        m_soilExtraStack->addWidget(bc);                 // index 2

        auto *vg = new QWidget(m_soilExtraStack);        // VAN_GENUCHTEN
        auto *vf = new QFormLayout(vg);
        auto *vgAlpha = new QDoubleSpinBox(vg);
        vgAlpha->setRange(0.001, 100.0);
        vgAlpha->setDecimals(4);
        vgAlpha->setValue(1.0);
        vf->addRow(tr("alpha:"), vgAlpha);
        auto *vgN = new QDoubleSpinBox(vg);
        vgN->setRange(1.01, 10.0);
        vgN->setDecimals(4);
        vgN->setValue(1.5);
        vf->addRow(tr("n:"), vgN);
        auto *thetaR = new QDoubleSpinBox(vg);
        thetaR->setRange(0.0, 1.0);
        thetaR->setDecimals(3);
        thetaR->setValue(0.05);
        vf->addRow(tr("theta_r:"), thetaR);
        m_soilExtraStack->addWidget(vg);                 // index 3
    }
    m_soilExtraStack->setCurrentIndex(1);   // matches the RUSSO default above
    connect(m_soilCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            m_soilExtraStack, &QStackedWidget::setCurrentIndex);
    form->addRow(tr("Soil parameters:"), m_soilExtraStack);

    m_closureCombo = new QComboBox(page);
    m_closureCombo->addItems(closureTokens());
    m_closureCombo->setCurrentText(QStringLiteral("AUTO"));
    m_closureCombo->setToolTip(
        tr("Unsaturated-zone closure used by the two-zone kernel."));
    form->addRow(tr("Closure:"), m_closureCombo);

    m_layersSpin = new QSpinBox(page);
    m_layersSpin->setRange(1, 100);
    m_layersSpin->setValue(5);
    m_layersSpin->setToolTip(
        tr("Layer count override, used by the KINEMATIC closure only."));
    form->addRow(tr("Kinematic layers:"), m_layersSpin);

    disableInputs(page);
    return page;
}

QWidget *Mesh2DGroundwaterDialog::buildInitialConditionsPage()
{
    auto *page = new QWidget(this);
    auto *form = new QFormLayout(page);

    m_huSpin = new QDoubleSpinBox(page);
    m_huSpin->setRange(0.0, 1000.0);
    m_huSpin->setDecimals(4);
    m_huSpin->setSuffix(QStringLiteral(" m"));
    m_huSpin->setToolTip(
        tr("Equivalent water depth held in the unsaturated zone at t = 0."));
    form->addRow(tr("Initial unsaturated depth (hu):"), m_huSpin);

    m_hgSpin = new QDoubleSpinBox(page);
    m_hgSpin->setRange(0.0, 1000.0);
    m_hgSpin->setDecimals(4);
    m_hgSpin->setSuffix(QStringLiteral(" m"));
    m_hgSpin->setToolTip(
        tr("Saturated (groundwater) depth above the aquifer base at t = 0."));
    form->addRow(tr("Initial saturated depth (hg):"), m_hgSpin);

    auto *note = new QLabel(
        tr("Both depths are per cell. When the kernel lands they will be "
           "assignable from the Mesh 2D cell editor and from rasters or "
           "shapefile fields, like Manning's n and initial surface depth."),
        page);
    note->setWordWrap(true);
    note->setEnabled(false);
    form->addRow(note);

    disableInputs(page);
    return page;
}

} // namespace openswmmvis::ui
