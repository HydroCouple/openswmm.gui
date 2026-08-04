#include "ui/dialogs/objectdefaultspage.h"

#include "core/unitsystem.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

namespace {

QDoubleSpinBox *makeSpin(double max, int decimals)
{
    auto *s = new QDoubleSpinBox;
    s->setRange(0.0, max);
    s->setDecimals(decimals);
    return s;
}

//! Combo whose currentData() carries the keyword the struct stores.
QComboBox *makeCombo(const QList<QPair<QString, QString>> &labelKeyword)
{
    auto *c = new QComboBox;
    for (const auto &lk : labelKeyword)
        c->addItem(lk.first, lk.second);
    return c;
}

void selectData(QComboBox *c, const QString &keyword)
{
    const int i = c->findData(keyword);
    c->setCurrentIndex(i >= 0 ? i : 0);
}

} // anonymous

ObjectDefaultsPage::ObjectDefaultsPage(QWidget *parent)
    : QWidget(parent),
      m_us(OD::usSeed()),
      m_si(OD::siSeed())
{
    buildUi();
    populateWidgets(m_us);
}

void ObjectDefaultsPage::buildUi()
{
    auto *outer = new QVBoxLayout(this);

    auto *intro = new QLabel(
        tr("Property defaults applied to newly created objects (draw tools "
           "and GIS import). Existing objects are not modified. Values are "
           "in the display units of the selected unit system; separate "
           "defaults are kept for US customary and SI projects."));
    intro->setWordWrap(true);
    outer->addWidget(intro);

    auto *unitForm = new QFormLayout;
    m_unitSystemCombo = new QComboBox;
    m_unitSystemCombo->addItem(tr("US customary (CFS, ft, ac)"));
    m_unitSystemCombo->addItem(tr("SI metric (CMS, m, ha)"));
    unitForm->addRow(tr("Unit system:"), m_unitSystemCombo);
    outer->addLayout(unitForm);
    connect(m_unitSystemCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &ObjectDefaultsPage::onUnitSystemSwitched);

    auto *tabs = new QTabWidget;
    outer->addWidget(tabs, 1);

    // ---- Nodes tab --------------------------------------------------------
    {
        auto *page = new QWidget;
        auto *v = new QVBoxLayout(page);

        auto *junction = new QGroupBox(tr("Junctions"));
        auto *jf = new QFormLayout(junction);
        jf->addRow(tr("Max depth (0 = highest crown):"),
                   m_junctionMaxDepth = makeSpin(1e6, 3));
        jf->addRow(tr("Initial depth:"),  m_junctionInitDepth = makeSpin(1e6, 3));
        jf->addRow(tr("Surcharge depth:"), m_junctionSurDepth = makeSpin(1e6, 3));
        jf->addRow(tr("Ponded area:"),    m_junctionPondedArea = makeSpin(1e9, 2));
        v->addWidget(junction);

        auto *outfall = new QGroupBox(tr("Outfalls"));
        auto *of = new QFormLayout(outfall);
        of->addRow(tr("Type:"), m_outfallType = makeCombo({
            {tr("Free"),   QStringLiteral("FREE")},
            {tr("Normal"), QStringLiteral("NORMAL")},
            {tr("Fixed"),  QStringLiteral("FIXED")}}));
        of->addRow(tr("Flap gate:"), m_outfallFlapGate = new QCheckBox);
        v->addWidget(outfall);

        auto *storage = new QGroupBox(tr("Storage units"));
        auto *sf = new QFormLayout(storage);
        sf->addRow(tr("Max depth:"), m_storageMaxDepth = makeSpin(1e6, 3));
        sf->addRow(tr("Area coefficient (a):"), m_storageFuncCoeff = makeSpin(1e9, 4));
        sf->addRow(tr("Area exponent (b):"),    m_storageFuncExponent = makeSpin(10.0, 4));
        sf->addRow(tr("Area constant (c):"),    m_storageFuncConstant = makeSpin(1e9, 2));
        sf->addRow(tr("Seepage rate:"),         m_storageSeepRate = makeSpin(1e6, 4));
        v->addWidget(storage);

        auto *divider = new QGroupBox(tr("Dividers"));
        auto *df = new QFormLayout(divider);
        df->addRow(tr("Type:"), m_dividerType = makeCombo({
            {tr("Overflow"), QStringLiteral("OVERFLOW")},
            {tr("Cutoff"),   QStringLiteral("CUTOFF")},
            {tr("Tabular"),  QStringLiteral("TABULAR")},
            {tr("Weir"),     QStringLiteral("WEIR")}}));
        v->addWidget(divider);

        v->addStretch(1);
        tabs->addTab(page, tr("Nodes"));
    }

    // ---- Links tab --------------------------------------------------------
    {
        auto *page = new QWidget;
        auto *v = new QVBoxLayout(page);

        auto *conduit = new QGroupBox(tr("Conduits"));
        auto *cf = new QFormLayout(conduit);
        cf->addRow(tr("Cross-section shape:"), m_conduitShape = makeCombo({
            {tr("Circular"),           QStringLiteral("CIRCULAR")},
            {tr("Rectangular closed"), QStringLiteral("RECT_CLOSED")},
            {tr("Rectangular open"),   QStringLiteral("RECT_OPEN")},
            {tr("Trapezoidal"),        QStringLiteral("TRAPEZOIDAL")},
            {tr("Triangular"),         QStringLiteral("TRIANGULAR")}}));
        cf->addRow(tr("Geom1 (diameter / height):"), m_conduitGeom1 = makeSpin(1e4, 3));
        cf->addRow(tr("Geom2:"), m_conduitGeom2 = makeSpin(1e4, 3));
        cf->addRow(tr("Geom3:"), m_conduitGeom3 = makeSpin(1e4, 3));
        cf->addRow(tr("Geom4:"), m_conduitGeom4 = makeSpin(1e4, 3));
        cf->addRow(tr("Roughness (Manning n):"), m_conduitRoughness = makeSpin(1.0, 4));
        cf->addRow(tr("Length (when auto-length is off):"), m_conduitLength = makeSpin(1e7, 2));
        m_conduitBarrels = new QSpinBox;
        m_conduitBarrels->setRange(1, 100);
        cf->addRow(tr("Barrels:"), m_conduitBarrels);
        cf->addRow(tr("Entry loss coefficient:"), m_conduitLossInlet = makeSpin(100.0, 3));
        cf->addRow(tr("Exit loss coefficient:"),  m_conduitLossOutlet = makeSpin(100.0, 3));
        cf->addRow(tr("Flap gate:"), m_conduitFlapGate = new QCheckBox);
        v->addWidget(conduit);

        auto *pump = new QGroupBox(tr("Pumps (ideal — no curve)"));
        auto *pf = new QFormLayout(pump);
        pf->addRow(tr("Initially on:"),   m_pumpInitStateOn = new QCheckBox);
        pf->addRow(tr("Startup depth:"),  m_pumpStartupDepth = makeSpin(1e6, 3));
        pf->addRow(tr("Shutoff depth:"),  m_pumpShutoffDepth = makeSpin(1e6, 3));
        v->addWidget(pump);

        auto *orifice = new QGroupBox(tr("Orifices"));
        auto *orf = new QFormLayout(orifice);
        orf->addRow(tr("Type:"), m_orificeType = makeCombo({
            {tr("Side"),   QStringLiteral("SIDE")},
            {tr("Bottom"), QStringLiteral("BOTTOM")}}));
        orf->addRow(tr("Diameter:"), m_orificeGeom1 = makeSpin(1e4, 3));
        orf->addRow(tr("Discharge coefficient:"), m_orificeCd = makeSpin(10.0, 3));
        orf->addRow(tr("Flap gate:"), m_orificeFlapGate = new QCheckBox);
        orf->addRow(tr("Open/close rate (hr):"), m_orificeOpenCloseRate = makeSpin(1e4, 3));
        v->addWidget(orifice);

        auto *weir = new QGroupBox(tr("Weirs"));
        auto *wf = new QFormLayout(weir);
        wf->addRow(tr("Type:"), m_weirType = makeCombo({
            {tr("Transverse"),  QStringLiteral("TRANSVERSE")},
            {tr("Side flow"),   QStringLiteral("SIDEFLOW")},
            {tr("V-notch"),     QStringLiteral("VNOTCH")},
            {tr("Trapezoidal"), QStringLiteral("TRAPEZOIDAL")},
            {tr("Roadway"),     QStringLiteral("ROADWAY")}}));
        wf->addRow(tr("Height (geom1):"), m_weirGeom1 = makeSpin(1e4, 3));
        wf->addRow(tr("Length (geom2):"), m_weirGeom2 = makeSpin(1e4, 3));
        wf->addRow(tr("Discharge coefficient:"), m_weirCd = makeSpin(10.0, 3));
        m_weirEndContractions = new QSpinBox;
        m_weirEndContractions->setRange(0, 2);
        wf->addRow(tr("End contractions:"), m_weirEndContractions);
        wf->addRow(tr("Flap gate:"), m_weirFlapGate = new QCheckBox);
        v->addWidget(weir);

        auto *outlet = new QGroupBox(tr("Outlets (rating Q = C·hⁿ)"));
        auto *ouf = new QFormLayout(outlet);
        ouf->addRow(tr("Rating basis:"), m_outletRatingType = makeCombo({
            {tr("Functional, depth"), QStringLiteral("FUNCTIONAL_DEPTH")},
            {tr("Functional, head"),  QStringLiteral("FUNCTIONAL_HEAD")}}));
        ouf->addRow(tr("Coefficient (C):"), m_outletCoeff = makeSpin(1e6, 3));
        ouf->addRow(tr("Exponent (n):"),    m_outletExponent = makeSpin(10.0, 3));
        ouf->addRow(tr("Flap gate:"), m_outletFlapGate = new QCheckBox);
        v->addWidget(outlet);

        v->addStretch(1);
        tabs->addTab(page, tr("Links"));
    }

    // ---- Subcatchments tab ------------------------------------------------
    {
        auto *page = new QWidget;
        auto *v = new QVBoxLayout(page);

        auto *surface = new QGroupBox(tr("Surface"));
        auto *suf = new QFormLayout(surface);
        suf->addRow(tr("Area (when auto-area is off):"), m_subcatchArea = makeSpin(1e7, 3));
        suf->addRow(tr("Width:"), m_subcatchWidth = makeSpin(1e7, 2));
        suf->addRow(tr("Slope (%):"), m_subcatchSlopePct = makeSpin(1000.0, 3));
        suf->addRow(tr("Imperviousness (%):"), m_subcatchImpervPct = makeSpin(100.0, 1));
        suf->addRow(tr("N imperv:"), m_subcatchNImperv = makeSpin(1.0, 4));
        suf->addRow(tr("N perv:"),   m_subcatchNPerv = makeSpin(1.0, 4));
        suf->addRow(tr("Depression storage, imperv:"), m_subcatchDsImperv = makeSpin(100.0, 3));
        suf->addRow(tr("Depression storage, perv:"),   m_subcatchDsPerv = makeSpin(100.0, 3));
        suf->addRow(tr("Zero-storage imperv (%):"), m_subcatchPctZeroImperv = makeSpin(100.0, 1));
        v->addWidget(surface);

        auto *infil = new QGroupBox(
            tr("Infiltration (the family matching the project's model is applied)"));
        auto *inf = new QFormLayout(infil);
        inf->addRow(tr("Horton max rate:"), m_hortonMaxRate = makeSpin(1e4, 3));
        inf->addRow(tr("Horton min rate:"), m_hortonMinRate = makeSpin(1e4, 3));
        inf->addRow(tr("Horton decay (1/hr):"), m_hortonDecay = makeSpin(100.0, 3));
        inf->addRow(tr("Horton drying time (days):"), m_hortonDryTime = makeSpin(100.0, 2));
        inf->addRow(tr("Green-Ampt suction head:"), m_gaSuction = makeSpin(1e4, 3));
        inf->addRow(tr("Green-Ampt conductivity:"), m_gaKsat = makeSpin(1e4, 3));
        inf->addRow(tr("Green-Ampt initial deficit:"), m_gaImd = makeSpin(1.0, 3));
        inf->addRow(tr("Curve number:"), m_cnCurveNumber = makeSpin(100.0, 1));
        inf->addRow(tr("Curve number drying time (days):"), m_cnDryTime = makeSpin(100.0, 2));
        v->addWidget(infil);

        v->addStretch(1);
        tabs->addTab(page, tr("Subcatchments"));
    }

    // ---- Rain gages tab ---------------------------------------------------
    {
        auto *page = new QWidget;
        auto *v = new QVBoxLayout(page);

        auto *gage = new QGroupBox(tr("Rain gages"));
        auto *gf = new QFormLayout(gage);
        gf->addRow(tr("Rain format:"), m_gageRainFormat = makeCombo({
            {tr("Intensity"),  QStringLiteral("INTENSITY")},
            {tr("Volume"),     QStringLiteral("VOLUME")},
            {tr("Cumulative"), QStringLiteral("CUMULATIVE")}}));
        m_gageIntervalMin = new QSpinBox;
        m_gageIntervalMin->setRange(1, 1440);
        gf->addRow(tr("Recording interval (min):"), m_gageIntervalMin);
        gf->addRow(tr("Snow catch factor:"), m_gageSnowCatch = makeSpin(10.0, 2));
        v->addWidget(gage);

        v->addStretch(1);
        tabs->addTab(page, tr("Rain Gages"));
    }
}

void ObjectDefaultsPage::populateWidgets(const OD &d)
{
    m_junctionMaxDepth->setValue(d.junctionMaxDepth);
    m_junctionInitDepth->setValue(d.junctionInitDepth);
    m_junctionSurDepth->setValue(d.junctionSurDepth);
    m_junctionPondedArea->setValue(d.junctionPondedArea);
    selectData(m_outfallType, d.outfallType);
    m_outfallFlapGate->setChecked(d.outfallFlapGate);
    m_storageMaxDepth->setValue(d.storageMaxDepth);
    m_storageFuncCoeff->setValue(d.storageFuncCoeff);
    m_storageFuncExponent->setValue(d.storageFuncExponent);
    m_storageFuncConstant->setValue(d.storageFuncConstant);
    m_storageSeepRate->setValue(d.storageSeepRate);
    selectData(m_dividerType, d.dividerType);

    selectData(m_conduitShape, d.conduitShape);
    m_conduitGeom1->setValue(d.conduitGeom1);
    m_conduitGeom2->setValue(d.conduitGeom2);
    m_conduitGeom3->setValue(d.conduitGeom3);
    m_conduitGeom4->setValue(d.conduitGeom4);
    m_conduitRoughness->setValue(d.conduitRoughness);
    m_conduitLength->setValue(d.conduitLength);
    m_conduitBarrels->setValue(d.conduitBarrels);
    m_conduitLossInlet->setValue(d.conduitLossInlet);
    m_conduitLossOutlet->setValue(d.conduitLossOutlet);
    m_conduitFlapGate->setChecked(d.conduitFlapGate);
    m_pumpInitStateOn->setChecked(d.pumpInitStateOn);
    m_pumpStartupDepth->setValue(d.pumpStartupDepth);
    m_pumpShutoffDepth->setValue(d.pumpShutoffDepth);
    selectData(m_orificeType, d.orificeType);
    m_orificeGeom1->setValue(d.orificeGeom1);
    m_orificeCd->setValue(d.orificeCd);
    m_orificeFlapGate->setChecked(d.orificeFlapGate);
    m_orificeOpenCloseRate->setValue(d.orificeOpenCloseRate);
    selectData(m_weirType, d.weirType);
    m_weirGeom1->setValue(d.weirGeom1);
    m_weirGeom2->setValue(d.weirGeom2);
    m_weirCd->setValue(d.weirCd);
    m_weirEndContractions->setValue(d.weirEndContractions);
    m_weirFlapGate->setChecked(d.weirFlapGate);
    selectData(m_outletRatingType, d.outletRatingType);
    m_outletCoeff->setValue(d.outletCoeff);
    m_outletExponent->setValue(d.outletExponent);
    m_outletFlapGate->setChecked(d.outletFlapGate);

    m_subcatchArea->setValue(d.subcatchArea);
    m_subcatchWidth->setValue(d.subcatchWidth);
    m_subcatchSlopePct->setValue(d.subcatchSlopePct);
    m_subcatchImpervPct->setValue(d.subcatchImpervPct);
    m_subcatchNImperv->setValue(d.subcatchNImperv);
    m_subcatchNPerv->setValue(d.subcatchNPerv);
    m_subcatchDsImperv->setValue(d.subcatchDsImperv);
    m_subcatchDsPerv->setValue(d.subcatchDsPerv);
    m_subcatchPctZeroImperv->setValue(d.subcatchPctZeroImperv);
    m_hortonMaxRate->setValue(d.hortonMaxRate);
    m_hortonMinRate->setValue(d.hortonMinRate);
    m_hortonDecay->setValue(d.hortonDecay);
    m_hortonDryTime->setValue(d.hortonDryTime);
    m_gaSuction->setValue(d.gaSuction);
    m_gaKsat->setValue(d.gaKsat);
    m_gaImd->setValue(d.gaImd);
    m_cnCurveNumber->setValue(d.cnCurveNumber);
    m_cnDryTime->setValue(d.cnDryTime);

    selectData(m_gageRainFormat, d.gageRainFormat);
    m_gageIntervalMin->setValue(d.gageIntervalMin);
    m_gageSnowCatch->setValue(d.gageSnowCatch);
}

void ObjectDefaultsPage::commitWidgets(OD &d) const
{
    d.junctionMaxDepth      = m_junctionMaxDepth->value();
    d.junctionInitDepth     = m_junctionInitDepth->value();
    d.junctionSurDepth      = m_junctionSurDepth->value();
    d.junctionPondedArea    = m_junctionPondedArea->value();
    d.outfallType           = m_outfallType->currentData().toString();
    d.outfallFlapGate       = m_outfallFlapGate->isChecked();
    d.storageMaxDepth       = m_storageMaxDepth->value();
    d.storageFuncCoeff      = m_storageFuncCoeff->value();
    d.storageFuncExponent   = m_storageFuncExponent->value();
    d.storageFuncConstant   = m_storageFuncConstant->value();
    d.storageSeepRate       = m_storageSeepRate->value();
    d.dividerType           = m_dividerType->currentData().toString();

    d.conduitShape          = m_conduitShape->currentData().toString();
    d.conduitGeom1          = m_conduitGeom1->value();
    d.conduitGeom2          = m_conduitGeom2->value();
    d.conduitGeom3          = m_conduitGeom3->value();
    d.conduitGeom4          = m_conduitGeom4->value();
    d.conduitRoughness      = m_conduitRoughness->value();
    d.conduitLength         = m_conduitLength->value();
    d.conduitBarrels        = m_conduitBarrels->value();
    d.conduitLossInlet      = m_conduitLossInlet->value();
    d.conduitLossOutlet     = m_conduitLossOutlet->value();
    d.conduitFlapGate       = m_conduitFlapGate->isChecked();
    d.pumpInitStateOn       = m_pumpInitStateOn->isChecked();
    d.pumpStartupDepth      = m_pumpStartupDepth->value();
    d.pumpShutoffDepth      = m_pumpShutoffDepth->value();
    d.orificeType           = m_orificeType->currentData().toString();
    d.orificeGeom1          = m_orificeGeom1->value();
    d.orificeCd             = m_orificeCd->value();
    d.orificeFlapGate       = m_orificeFlapGate->isChecked();
    d.orificeOpenCloseRate  = m_orificeOpenCloseRate->value();
    d.weirType              = m_weirType->currentData().toString();
    d.weirGeom1             = m_weirGeom1->value();
    d.weirGeom2             = m_weirGeom2->value();
    d.weirCd                = m_weirCd->value();
    d.weirEndContractions   = m_weirEndContractions->value();
    d.weirFlapGate          = m_weirFlapGate->isChecked();
    d.outletRatingType      = m_outletRatingType->currentData().toString();
    d.outletCoeff           = m_outletCoeff->value();
    d.outletExponent        = m_outletExponent->value();
    d.outletFlapGate        = m_outletFlapGate->isChecked();

    d.subcatchArea          = m_subcatchArea->value();
    d.subcatchWidth         = m_subcatchWidth->value();
    d.subcatchSlopePct      = m_subcatchSlopePct->value();
    d.subcatchImpervPct     = m_subcatchImpervPct->value();
    d.subcatchNImperv       = m_subcatchNImperv->value();
    d.subcatchNPerv         = m_subcatchNPerv->value();
    d.subcatchDsImperv      = m_subcatchDsImperv->value();
    d.subcatchDsPerv        = m_subcatchDsPerv->value();
    d.subcatchPctZeroImperv = m_subcatchPctZeroImperv->value();
    d.hortonMaxRate         = m_hortonMaxRate->value();
    d.hortonMinRate         = m_hortonMinRate->value();
    d.hortonDecay           = m_hortonDecay->value();
    d.hortonDryTime         = m_hortonDryTime->value();
    d.gaSuction             = m_gaSuction->value();
    d.gaKsat                = m_gaKsat->value();
    d.gaImd                 = m_gaImd->value();
    d.cnCurveNumber         = m_cnCurveNumber->value();
    d.cnDryTime             = m_cnDryTime->value();

    d.gageRainFormat        = m_gageRainFormat->currentData().toString();
    d.gageIntervalMin       = m_gageIntervalMin->value();
    d.gageSnowCatch         = m_gageSnowCatch->value();
}

void ObjectDefaultsPage::onUnitSystemSwitched(int comboIndex)
{
    const bool showSi = (comboIndex == 1);
    if (showSi == m_showingSi)
        return;
    // Commit the outgoing set, then show the incoming one.
    commitWidgets(m_showingSi ? m_si : m_us);
    m_showingSi = showSi;
    populateWidgets(m_showingSi ? m_si : m_us);
}

void ObjectDefaultsPage::loadFrom(PreferencesManager *p)
{
    m_us = p->objectDefaults(/*si=*/false);
    m_si = p->objectDefaults(/*si=*/true);

    const bool projectSi =
        UnitSystem::instance() && UnitSystem::instance()->isSI();
    m_showingSi = projectSi;
    // Keep the combo in sync without re-triggering the switch handler's
    // commit of stale widget contents.
    const QSignalBlocker block(m_unitSystemCombo);
    m_unitSystemCombo->setCurrentIndex(projectSi ? 1 : 0);
    populateWidgets(m_showingSi ? m_si : m_us);
}

void ObjectDefaultsPage::applyTo(PreferencesManager *p)
{
    commitWidgets(m_showingSi ? m_si : m_us);
    p->setObjectDefaults(m_us, /*si=*/false);
    p->setObjectDefaults(m_si, /*si=*/true);
}

void ObjectDefaultsPage::resetToSeeds()
{
    m_us = OD::usSeed();
    m_si = OD::siSeed();
    populateWidgets(m_showingSi ? m_si : m_us);
}
