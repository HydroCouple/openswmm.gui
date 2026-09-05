/*!
 * \file   test_aquifer_editor_dialog.cpp
 * \brief  Phase G2 — the rebuilt Aquifer editor: add / rename / delete through
 *         the registry, spin-box ↔ provider sync, evaporation-pattern round
 *         trip to the engine, soft-validation warnings and the live diagram.
 *
 * Fixture models are written to a persistent, reviewable directory under
 * the GUI test data dir (SWMMVIS_GUI_TEST_DATA/aquifer_editor/), never to
 * temp dirs. Mirrors test_landuse_unified_editor.cpp.
 */

#include <QtTest/QtTest>

#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QTextStream>

#include "aquifer/aquiferprovider.h"
#include "aquifer/aquiferregistry.h"
#include "ui/dialogs/aquifereditordialog.h"
#include "ui/models/aquiferlistmodel.h"
#include "ui/sectionview/aquiferdiagram.h"
#include "ui/sectionview/sectionpreviewwidget.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_subcatchments.h>

using openswmmvis::aquifer::AquiferProvider;
using openswmmvis::aquifer::AquiferRegistry;
using openswmmvis::ui::AquiferEditorDialog;

namespace {

QString fixtureDir()
{
    const QString base = QString::fromUtf8(qgetenv("SWMMVIS_GUI_TEST_DATA"));
    return base + QStringLiteral("/aquifer_editor");
}

QString writeFixture(const QString &tag)
{
    QDir().mkpath(fixtureDir());
    const QString path = fixtureDir() + QStringLiteral("/%1.inp").arg(tag);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    QTextStream out(&f);
    out << "[OPTIONS]\n"
           "FLOW_UNITS           CFS\n"
           "FLOW_ROUTING         KINWAVE\n"
           "INFILTRATION         HORTON\n"
           "START_DATE           01/01/2026\n"
           "START_TIME           00:00:00\n"
           "END_DATE             01/01/2026\n"
           "END_TIME             01:00:00\n"
           "REPORT_STEP          00:05:00\n"
           "ROUTING_STEP         0:00:30\n"
           "\n[RAINGAGES]\n"
           "RG1  INTENSITY 0:05 1.0 TIMESERIES TS1\n"
           "\n[SUBCATCHMENTS]\n"
           "S1  RG1  J1  10.0  50  500  0.5  0\n"
           "\n[SUBAREAS]\n"
           "S1  0.01  0.1  0.05  0.05  25  OUTLET\n"
           "\n[INFILTRATION]\n"
           "S1  3.0  0.5  4.0  7  0\n"
           "\n[TIMESERIES]\n"
           "TS1  01/01/2026 00:00 1.0\n"
           "\n[JUNCTIONS]\n"
           "J1  100.0  10.0  0.0  0.0  0.0\n"
           "\n[OUTFALLS]\n"
           "O1  95.0  FREE\n"
           "\n[CONDUITS]\n"
           "C1  J1  O1  400.0  0.013  0  0\n"
           "\n[XSECTIONS]\n"
           "C1  CIRCULAR  1.5  0  0  0  1\n"
           "\n[PATTERNS]\n"
           ";; HR1 is HOURLY (24 factors over two lines) so the picker's MONTHLY filter has something to drop.\n"
           "EVAP1  MONTHLY  1.0 1.0 1.0 1.0 1.0 1.0 1.0 1.0 1.0 1.0 1.0 1.0\n"
           "HR1    HOURLY   1.0 1.0 1.0 1.0 1.0 1.0 1.0 1.0 1.0 1.0 1.0 1.0\n"
           "HR1    HOURLY   1.0 1.0 1.0 1.0 1.0 1.0 1.0 1.0 1.0 1.0 1.0 1.0\n"
           "\n[AQUIFERS]\n"
           ";;Name  Por  WP   FC   Ksat  Kslope  Tslope  ETu   ETs  Seep   Ebot  Egw   Umc   ETupat\n"
           "AQ1     0.5  0.15 0.30 5.0   10.0    15.0    0.35  2.0  0.002  90.0  100.0 0.35  EVAP1\n"
           "AQ2     0.4  0.10 0.20 2.0   5.0     10.0    0.20  1.0  0.001  80.0  85.0  0.20\n"
           "\n[GROUNDWATER]\n"
           // Eleven columns: the engine skips a [GROUNDWATER] row with fewer
           // (handle_groundwater), which would leave S1 without its aquifer.
           ";;Subcatch  Aquifer  Node  Esurf  A1     B1  A2  B2  A3  Tw  Hstar\n"
           "S1          AQ1      J1    110.0  0.001  1   0   0   0   0   0\n";
    return path;
}

struct EngineFixture {
    SWMM_Engine engine = nullptr;

    explicit EngineFixture(const QString &tag)
    {
        const QString inp = writeFixture(tag);
        if (inp.isEmpty()) return;
        engine = swmm_engine_create();
        const QString rpt = fixtureDir() + QStringLiteral("/%1.rpt").arg(tag);
        const QString out = fixtureDir() + QStringLiteral("/%1.out").arg(tag);
        if (swmm_engine_open(engine, inp.toUtf8().constData(),
                             rpt.toUtf8().constData(),
                             out.toUtf8().constData(), nullptr) != SWMM_OK) {
            swmm_engine_destroy(engine);
            engine = nullptr;
        }
    }
    ~EngineFixture()
    {
        if (engine) swmm_engine_destroy(engine);
    }
};

QString engineEvapPattern(SWMM_Engine e, const char *aquifer)
{
    const int idx = swmm_aquifer_index(e, aquifer);
    if (idx < 0) return QStringLiteral("<missing>");
    char buf[256] = {};
    if (swmm_aquifer_get_evap_pattern(e, idx, buf, sizeof buf) != SWMM_OK)
        return QStringLiteral("<error>");
    return QString::fromUtf8(buf);
}

int countLeadersContaining(const openswmmvis::sectionview::SectionDiagramModel &m,
                           const QString &needle)
{
    int n = 0;
    for (const auto &l : m.leaders)
        if (l.text.contains(needle)) ++n;
    return n;
}

} // namespace

class TestAquiferEditorDialog : public QObject
{
    Q_OBJECT

private slots:
    void bindsFirstAquiferIntoForm();
    void addAndRenameThroughDialog();
    void deleteReportsImpactAndReachesEngine();
    void spinEditsReachProvider();
    void evapPatternRoundTripsToEngine();
    void softValidationWarnings();
    void diagramTracksForm();
};

void TestAquiferEditorDialog::bindsFirstAquiferIntoForm()
{
    EngineFixture fx(QStringLiteral("bind"));
    QVERIFY(fx.engine);
    AquiferRegistry reg;
    QCOMPARE(reg.loadFromEngine(fx.engine), 2);

    AquiferEditorDialog dlg(&reg, nullptr);
    QVERIFY(dlg.currentProvider());
    QCOMPARE(dlg.currentProvider()->name(), QStringLiteral("AQ1"));
    QCOMPARE(dlg.nameEdit()->text(), QStringLiteral("AQ1"));

    for (int k = 0; k < AquiferProvider::ParamCount; ++k) {
        QVERIFY2(dlg.spinBox(k), qPrintable(QStringLiteral("no spin for param %1").arg(k)));
        QCOMPARE(dlg.spinBox(k)->value(), dlg.currentProvider()->param(k));
    }
    QCOMPARE(dlg.spinBox(AquiferProvider::Porosity)->value(), 0.5);
    QCOMPARE(dlg.spinBox(AquiferProvider::BottomElev)->value(), 90.0);
    QCOMPARE(dlg.spinBox(AquiferProvider::WaterTableElev)->value(), 100.0);
    QVERIFY(dlg.spinBox(-1) == nullptr);

    // Engine bounds are the spin ranges: fractions in [0,1], elevations may
    // be negative, the rest non-negative.
    for (int k : { 0, 1, 2, 6, 11 }) {
        QCOMPARE(dlg.spinBox(k)->minimum(), 0.0);
        QCOMPARE(dlg.spinBox(k)->maximum(), 1.0);
    }
    for (int k : { 9, 10 }) QVERIFY(dlg.spinBox(k)->minimum() < 0.0);
    for (int k : { 3, 4, 5, 7, 8 }) {
        QCOMPARE(dlg.spinBox(k)->minimum(), 0.0);
        QVERIFY(dlg.spinBox(k)->maximum() > 1.0);
    }
    // Unit suffixes come from UnitSystem (CFS fixture → US units).
    QVERIFY(dlg.spinBox(AquiferProvider::BottomElev)->suffix().contains(QStringLiteral("ft")));
    QVERIFY(dlg.spinBox(AquiferProvider::Conductivity)->suffix().contains(QStringLiteral("in/hr")));
    QVERIFY(dlg.spinBox(AquiferProvider::Porosity)->suffix().isEmpty());

    // The pattern combo shows the bound aquifer's ETupat.
    QCOMPARE(dlg.evapPatternCombo()->currentText(), QStringLiteral("EVAP1"));
    QVERIFY(!dlg.warningLabel()->isVisibleTo(&dlg));
}

void TestAquiferEditorDialog::addAndRenameThroughDialog()
{
    EngineFixture fx(QStringLiteral("addrename"));
    QVERIFY(fx.engine);
    AquiferRegistry reg;
    reg.loadFromEngine(fx.engine);
    AquiferEditorDialog dlg(&reg, nullptr);

    dlg.invokeNew();
    QCOMPARE(reg.providerCount(), 3);
    QVERIFY(dlg.currentProvider());
    const QString suggested = dlg.currentProvider()->name();
    QVERIFY(suggested.startsWith(QStringLiteral("Aquifer")));
    QCOMPARE(dlg.nameEdit()->text(), suggested);
    // New aquifer → nothing in the engine until saveToEngine.
    QCOMPARE(swmm_aquifer_index(fx.engine, suggested.toUtf8().constData()), -1);
    reg.saveToEngine(fx.engine);
    QVERIFY(swmm_aquifer_index(fx.engine, suggested.toUtf8().constData()) >= 0);

    // Rename via the name field; the registry renames engine-side too.
    dlg.nameEdit()->setText(QStringLiteral("AQ_NEW"));
    emit dlg.nameEdit()->editingFinished();
    QCOMPARE(dlg.currentProvider()->name(), QStringLiteral("AQ_NEW"));
    QVERIFY(reg.findByName(QStringLiteral("AQ_NEW")));
    QVERIFY(swmm_aquifer_index(fx.engine, "AQ_NEW") >= 0);
    QCOMPARE(swmm_aquifer_index(fx.engine, suggested.toUtf8().constData()), -1);
    QCOMPARE(swmm_aquifer_count(fx.engine), 3);

    // Duplicate names are rejected by the registry.
    QVERIFY(!reg.rename(dlg.currentProvider(), QStringLiteral("AQ1")));
    QCOMPARE(dlg.currentProvider()->name(), QStringLiteral("AQ_NEW"));
}

void TestAquiferEditorDialog::deleteReportsImpactAndReachesEngine()
{
    EngineFixture fx(QStringLiteral("delete"));
    QVERIFY(fx.engine);
    AquiferRegistry reg;
    reg.loadFromEngine(fx.engine);

    // S1's [GROUNDWATER] row references AQ1 — the delete prompt says so.
    auto *aq1 = reg.findByName(QStringLiteral("AQ1"));
    QVERIFY(aq1);
    const QString impact = reg.impactSummary(aq1);
    QVERIFY2(!impact.isEmpty(), "expected a non-empty impact summary for a referenced aquifer");
    QVERIFY(impact.contains(QStringLiteral("subcatchment")));
    // AQ2 is unreferenced.
    QVERIFY(reg.impactSummary(reg.findByName(QStringLiteral("AQ2"))).isEmpty());

    // The dialog's delete goes through the same registry path (the modal
    // confirmation is not driven here); the engine object must go with it.
    reg.remove(aq1);
    QCOMPARE(reg.providerCount(), 1);
    QCOMPARE(swmm_aquifer_index(fx.engine, "AQ1"), -1);
    QCOMPARE(swmm_aquifer_count(fx.engine), 1);
    int aqIdx = 99;
    const int s1 = swmm_subcatch_index(fx.engine, "S1");
    QVERIFY(s1 >= 0);
    QCOMPARE(swmm_subcatch_get_aquifer(fx.engine, s1, &aqIdx), SWMM_OK);
    QCOMPARE(aqIdx, -1);

    // A dialog opened afterwards binds the survivor.
    AquiferEditorDialog dlg(&reg, nullptr);
    QVERIFY(dlg.currentProvider());
    QCOMPARE(dlg.currentProvider()->name(), QStringLiteral("AQ2"));
}

void TestAquiferEditorDialog::spinEditsReachProvider()
{
    EngineFixture fx(QStringLiteral("spinsync"));
    QVERIFY(fx.engine);
    AquiferRegistry reg;
    reg.loadFromEngine(fx.engine);
    AquiferEditorDialog dlg(&reg, nullptr);
    AquiferProvider *aq1 = dlg.currentProvider();
    QVERIFY(aq1);

    dlg.spinBox(AquiferProvider::Porosity)->setValue(0.45);
    dlg.spinBox(AquiferProvider::Conductivity)->setValue(7.25);
    dlg.spinBox(AquiferProvider::BottomElev)->setValue(-12.5);
    QCOMPARE(aq1->param(AquiferProvider::Porosity), 0.45);
    QCOMPARE(aq1->param(AquiferProvider::Conductivity), 7.25);
    QCOMPARE(aq1->param(AquiferProvider::BottomElev), -12.5);

    // Out-of-range entry is clamped by the spin box, so the provider only
    // ever sees engine-legal values.
    dlg.spinBox(AquiferProvider::WiltingPoint)->setValue(1.7);
    QCOMPARE(aq1->param(AquiferProvider::WiltingPoint), 1.0);

    // Through to the engine on save.
    reg.saveToEngine(fx.engine);
    double v = 0.0;
    const int idx = swmm_aquifer_index(fx.engine, "AQ1");
    QVERIFY(idx >= 0);
    QCOMPARE(swmm_aquifer_get_param(fx.engine, idx, AquiferProvider::Conductivity, &v), SWMM_OK);
    QCOMPARE(v, 7.25);

    // Switching the bound aquifer re-reads the form from the other provider.
    auto *aq2 = reg.findByName(QStringLiteral("AQ2"));
    QVERIFY(aq2);
    dlg.listView()->setCurrentIndex(dlg.listModel()->index(1));
    QCOMPARE(dlg.currentProvider(), aq2);
    QCOMPARE(dlg.spinBox(AquiferProvider::Porosity)->value(), 0.4);
    QCOMPARE(dlg.evapPatternCombo()->currentIndex(), 0);   // "(none)"
    // …and did not disturb the one just left.
    QCOMPARE(aq1->param(AquiferProvider::Porosity), 0.45);
}

void TestAquiferEditorDialog::evapPatternRoundTripsToEngine()
{
    EngineFixture fx(QStringLiteral("evappat"));
    QVERIFY(fx.engine);
    AquiferRegistry reg;
    reg.loadFromEngine(fx.engine);
    AquiferEditorDialog dlg(&reg, nullptr);
    AquiferProvider *aq1 = dlg.currentProvider();
    QVERIFY(aq1);

    // "(none)" + every MONTHLY pattern; the HOURLY one is filtered out.
    QComboBox *combo = dlg.evapPatternCombo();
    QVERIFY(combo);
    QCOMPARE(combo->count(), 2);
    QVERIFY(combo->findText(QStringLiteral("EVAP1")) > 0);
    QCOMPARE(combo->findText(QStringLiteral("HR1")), -1);
    QCOMPARE(combo->currentText(), QStringLiteral("EVAP1"));
    QCOMPARE(aq1->evapPattern(), QStringLiteral("EVAP1"));

    // Clear it → provider empty → engine empty after save.
    combo->setCurrentIndex(0);
    QVERIFY(aq1->evapPattern().isEmpty());
    reg.saveToEngine(fx.engine);
    QVERIFY(engineEvapPattern(fx.engine, "AQ1").isEmpty());

    // Set it back → engine sees the name again.
    combo->setCurrentIndex(combo->findText(QStringLiteral("EVAP1")));
    QCOMPARE(aq1->evapPattern(), QStringLiteral("EVAP1"));
    reg.saveToEngine(fx.engine);
    QCOMPARE(engineEvapPattern(fx.engine, "AQ1"), QStringLiteral("EVAP1"));

    // The pattern name rides along on the diagram's ETu callout.
    QVERIFY(countLeadersContaining(dlg.diagram()->model(), QStringLiteral("EVAP1")) == 1);
}

void TestAquiferEditorDialog::softValidationWarnings()
{
    EngineFixture fx(QStringLiteral("warnings"));
    QVERIFY(fx.engine);
    AquiferRegistry reg;
    reg.loadFromEngine(fx.engine);
    AquiferEditorDialog dlg(&reg, nullptr);
    QVERIFY(dlg.currentProvider());

    // Fixture AQ1 is consistent: WP 0.15 ≤ FC 0.30 ≤ Por 0.5, Umc 0.35 ≤ Por,
    // Egw 100 ≥ Ebot 90.
    QVERIFY(dlg.validationWarnings().isEmpty());
    QVERIFY(dlg.warningLabel()->text().isEmpty());

    dlg.spinBox(AquiferProvider::WiltingPoint)->setValue(0.35);     // > FC 0.30
    QStringList w = dlg.validationWarnings();
    QCOMPARE(w.size(), 1);
    QVERIFY(w.first().contains(QStringLiteral("Wilting Point")));
    QVERIFY(dlg.warningLabel()->isVisibleTo(&dlg));
    QVERIFY(dlg.warningLabel()->text().contains(QStringLiteral("Wilting Point")));
    // …and forwarded to the diagram as a warning callout.
    QCOMPARE(countLeadersContaining(dlg.diagram()->model(),
                                    openswmmvis::sectionview::aquiferWarningCalloutPrefix()), 1);

    dlg.spinBox(AquiferProvider::FieldCapacity)->setValue(0.6);      // > Por 0.5, fixes WP
    dlg.spinBox(AquiferProvider::UpperMoisture)->setValue(0.55);     // > Por
    dlg.spinBox(AquiferProvider::WaterTableElev)->setValue(80.0);    // < Ebot 90
    w = dlg.validationWarnings();
    QCOMPARE(w.size(), 3);
    QVERIFY(w.join(QLatin1Char(' ')).contains(QStringLiteral("Field Capacity exceeds Porosity")));
    QVERIFY(w.join(QLatin1Char(' ')).contains(QStringLiteral("Moisture exceeds Porosity")));
    QVERIFY(w.join(QLatin1Char(' ')).contains(QStringLiteral("below Bottom Elevation")));
    QCOMPARE(dlg.warningLabel()->text().split(QLatin1Char('\n')).size(), 3);
    QCOMPARE(countLeadersContaining(dlg.diagram()->model(),
                                    openswmmvis::sectionview::aquiferWarningCalloutPrefix()), 3);

    // Back in range → clean.
    dlg.spinBox(AquiferProvider::WiltingPoint)->setValue(0.15);
    dlg.spinBox(AquiferProvider::FieldCapacity)->setValue(0.3);
    dlg.spinBox(AquiferProvider::UpperMoisture)->setValue(0.3);
    dlg.spinBox(AquiferProvider::WaterTableElev)->setValue(95.0);
    QVERIFY(dlg.validationWarnings().isEmpty());
    QVERIFY(!dlg.warningLabel()->isVisibleTo(&dlg));
}

void TestAquiferEditorDialog::diagramTracksForm()
{
    EngineFixture fx(QStringLiteral("diagram"));
    QVERIFY(fx.engine);
    AquiferRegistry reg;
    reg.loadFromEngine(fx.engine);
    AquiferEditorDialog dlg(&reg, nullptr);
    QVERIFY(dlg.diagram());

    const auto &m0 = dlg.diagram()->model();
    QCOMPARE(m0.title, QStringLiteral("AQ1"));
    QVERIFY(!m0.isEmpty());
    bool egwFound = false;
    for (const auto &d : m0.dims)
        if (d.text.contains(QStringLiteral("Egw"))) {
            egwFound = true;
            QVERIFY(d.text.contains(QStringLiteral("100.00 ft")));
        }
    QVERIFY(egwFound);

    // A value edit is reflected without rebinding.
    dlg.spinBox(AquiferProvider::WaterTableElev)->setValue(97.5);
    bool updated = false;
    for (const auto &d : dlg.diagram()->model().dims)
        if (d.text.contains(QStringLiteral("Egw")) && d.text.contains(QStringLiteral("97.50 ft")))
            updated = true;
    QVERIFY(updated);

    // Water table at / below the bottom → unknown hatch in the drawing.
    dlg.spinBox(AquiferProvider::WaterTableElev)->setValue(90.0);
    const auto &m1 = dlg.diagram()->model();
    QVERIFY(m1.polys.size() >= 2);
    QVERIFY(m1.polys[1].unknown);

    // Typing a new name retitles the drawing before the rename commits.
    dlg.nameEdit()->setText(QStringLiteral("Renamed"));
    emit dlg.nameEdit()->textEdited(QStringLiteral("Renamed"));
    QCOMPARE(dlg.diagram()->model().title, QStringLiteral("Renamed"));

    // Nothing to bind → empty drawing (placeholder), no warnings.
    AquiferRegistry empty;
    AquiferEditorDialog none(&empty, nullptr);
    QVERIFY(none.currentProvider() == nullptr);
    QVERIFY(none.diagram()->model().isEmpty());
    QVERIFY(none.validationWarnings().isEmpty());
    QVERIFY(!none.spinBox(AquiferProvider::Porosity)->isEnabled());
}

QTEST_MAIN(TestAquiferEditorDialog)
#include "test_aquifer_editor_dialog.moc"
