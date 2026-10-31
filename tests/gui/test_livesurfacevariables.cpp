/**
 * @file      test_livesurfacevariables.cpp
 * @brief     R3 (Phase 4A, 2026-10-30): live surface species, buildup, saturated
 *            thickness and groundwater fluxes during a run.
 *
 * Opens a small two-cell deck (its own, independent of the pre-existing
 * test_livegroundwatervariables decks), steps the real engine and checks the
 * worker capture against the engine's own getters and, after the run, against
 * the identities, labels and units of the saved .2d.h5. Every file is written
 * under tests/output/gw_phase4a_2026-10-30/live_surface for review.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "io/mesh2dderivedvariables.h"
#include "io/mesh2dh5reader.h"
#include "io/mesh2dlivevariables.h"
#include <openswmm/engine/openswmm_2d.h>
#include <openswmm/engine/openswmm_gw2d.h>
#include <openswmm/engine/openswmm_sq2d.h>
#include <QDir>
#include <QFile>
#include <QtTest>
#include <cmath>

using namespace openswmmvis::io;

namespace
{
const QString kRoot = QStringLiteral(SWMMVIS_PHASE4A_OUTPUT "/live_surface");

// Two 10 m triangles at elevation 10 m; the second starts dry. Surface
// transport of SurfPoll with land-use buildup, and a two-zone aquifer
// (zs = 1 m, so its base is 9 m) with one cell-scoped source.
const char *kDeck = R"INP([TITLE]
Phase 4A R3 live surface quality and groundwater fluxes

[OPTIONS]
FLOW_UNITS CMS
FLOW_ROUTING DYNWAVE
CRS EPSG:32618
START_DATE 01/01/2026
START_TIME 00:00:00
END_DATE 01/01/2026
END_TIME 00:10:00
REPORT_STEP 00:05:00
WET_STEP 00:01:00
DRY_STEP 00:01:00
ROUTING_STEP 5
ALLOW_PONDING NO

[POLLUTANTS]
SurfPoll MG/L 0.0 0.0 0.0 0.0 NO * 0.0 0.0

[LANDUSES]
SpillPad 0 0 0

[BUILDUP]
SpillPad SurfPoll NONE 0 0 0 AREA

[WASHOFF]
SpillPad SurfPoll EXP 0.1 1.0 0 0

[JUNCTIONS]
J1 8.0 4.0 0 0 0

[OUTFALLS]
O1 7.5 FREE NO

[CONDUITS]
C1 J1 O1 30.0 0.013 0 0 0

[XSECTIONS]
C1 CIRCULAR 0.5 0 0 0 1

[RAINGAGES]
RG1 INTENSITY 0:05 1.0 TIMESERIES RAIN

[TIMESERIES]
RAIN 0:00 20.0
RAIN 0:05 20.0
RAIN 0:10 0.0

[2D_OPTIONS]
INTEGRATOR EXPLICIT
LTS_TIERS 1
MAX_TIMESTEP 5
DRY_DEPTH 0.001
COUPLING_CD 0.7
REPORT_2D YES
OUTPUT_FILE live-surface.2d.h5
OUTPUT_PRECISION FLOAT64
TRANSPORT_POLLUTANTS YES
RAINFALL_MODE SYSTEM

[2D_VERTICES]
500000.0 4500000.0 10.0
500010.0 4500000.0 10.0
500010.0 4500010.0 10.0
500000.0 4500010.0 10.0

[2D_TRIANGLES]
0 1 2 0.03 0.2
0 2 3 0.03 0.0

[2D_COVERAGES]
* SpillPad 100

[2D_LOADINGS]
* SurfPoll 50

[2D_INFILTRATION_DEFAULTS]
* CONSTANT 40.0 - - - -

[2D_AQUIFER_OPTIONS]
CLOSURE CLOSED_FORM

[2D_AQUIFER]
* 36.0 1.0 0.45 0.10 2.0 HG0 0.5

[GW_TRANSPORT_OPTIONS]
TRANSPORT_POLLUTANTS YES

[GW_SOURCES]
W CELL 1 FLOW 0.0001 SurfPoll CONC 2

[COORDINATES]
J1 500005.0 4500005.0
O1 500040.0 4500040.0

[REPORT]
INPUT NO
)INP";

struct Engine {
    SWMM_Engine handle = swmm_engine_create();
    bool started = false;
    ~Engine() { close(); }
    /**
     * @brief End a started run and release the handle; safe to repeat.
     */
    void close()
    {
        if (!handle)
            return;
        if (started)
            swmm_engine_end(handle);
        swmm_engine_close(handle);
        swmm_engine_destroy(handle);
        handle = nullptr;
    }
    /**
     * @return The engine's last diagnostic.
     */
    QString error() const { return QString::fromUtf8(swmm_get_last_error_msg(handle)); }
};

/**
 * @brief Find a captured variable by dataset and (optional) species.
 * @return Borrowed variable or null.
 */
const Mesh2DLiveVariable *find(const Mesh2DLiveVariablesPtr &frame, const QString &dataset,
                               const QString &species = {})
{
    if (frame)
        for (const auto &v : frame->variables)
            if (v.descriptor.dataset == dataset && v.descriptor.species == species)
                return &v;
    return nullptr;
}
} // namespace

class TestLiveSurfaceVariables : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() { QVERIFY(QDir().mkpath(kRoot)); }

    /**
     * @brief The live frame carries surface concentration, buildup, h_g, the eight held
     * groundwater fluxes and the ground elevation, equal to the engine's own getters, under
     * the saved file's identities and units; the derived depths compute from it.
     */
    void liveCaptureCarriesSurfaceQualityAndGroundwater()
    {
        const QString inp = kRoot + "/model.inp";
        QFile file(inp);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(file.write(kDeck) == qint64(qstrlen(kDeck)));
        file.close();
        QFile::remove(kRoot + "/live-surface.2d.h5");
        Engine engine;
        QVERIFY2(swmm_engine_open(engine.handle, inp.toUtf8().constData(),
                                  (kRoot + "/model.rpt").toUtf8().constData(),
                                  (kRoot + "/model.out").toUtf8().constData(),
                                  nullptr) == SWMM_OK,
                 qPrintable(engine.error()));
        QVERIFY2(swmm_engine_initialize(engine.handle) == SWMM_OK, qPrintable(engine.error()));
        QCOMPARE(swmm_engine_start(engine.handle, 1), SWMM_OK);
        engine.started = true;
        // Past the first 5-minute report, so the saved file holds a frame.
        double elapsed = 0;
        for (int i = 0; i < 1000 && elapsed * 1440.0 < 6.0; ++i)
            QCOMPARE(swmm_engine_step(engine.handle, &elapsed), SWMM_OK);
        QVERIFY(elapsed * 1440.0 >= 6.0);

        QString error;
        const auto frame = captureSurfaceProcessVariables(engine.handle, 2, &error);
        QVERIFY2(frame, qPrintable(error));
        QCOMPARE(frame->cellCount, 2);

        // Surface concentration: the engine's values, Waterless where the cell is dry.
        const auto *conc = find(frame, "Mesh2_face_species_conc", "SurfPoll");
        QVERIFY(conc);
        QCOMPARE(conc->descriptor.key(),
                 QString("surface:Mesh2_face_species_conc:species:SurfPoll"));
        QCOMPARE(conc->descriptor.units, QString("MG/L"));
        QVERIFY(conc->descriptor.unitsKnown);
        std::vector<double> raw(2), depth(2);
        int written = 0;
        QCOMPARE(swmm_2d_get_cell_conc(engine.handle, 0, raw.data(), 2, &written), SWMM_OK);
        QCOMPARE(swmm_2d_get_depths_bulk(engine.handle, depth.data()), SWMM_OK);
        for (int c = 0; c < 2; ++c) {
            QCOMPARE(conc->values[size_t(c)], float(raw[size_t(c)]));
            QCOMPARE(conc->status[size_t(c)], depth[size_t(c)] == 0 ? Mesh2DValueStatus::Waterless
                                                                     : Mesh2DValueStatus::Valid);
        }

        // Buildup: mass per area in the SI project unit.
        const auto *buildup = find(frame, "Mesh2_face_buildup", "SurfPoll");
        QVERIFY(buildup);
        QCOMPARE(buildup->descriptor.units, QString("kg ha-1"));
        QCOMPARE(swmm_2d_get_buildup_bulk(engine.handle, "SurfPoll", raw.data(), 2), SWMM_OK);
        QCOMPARE(buildup->values[0], float(raw[0]));
        QCOMPARE(buildup->values[1], float(raw[1]));
        QVERIFY(buildup->values[0] + buildup->values[1] > 0); // the 50 kg/ha loading

        // Saturated thickness and the held fluxes, as the engine reports them.
        struct Expect {
            const char *dataset;
            int selector;
            const char *units;
        };
        const Expect expected[] = {
            {"Mesh2_face_gw_hg", SWMM_GW2D_VAR_HG, "m"},
            {"Mesh2_face_gw_recharge", SWMM_GW2D_VAR_Q0, "m s-1"},
            {"Mesh2_face_gw_lateral", SWMM_GW2D_VAR_QLAT, "m3 s-1"},
            {"Mesh2_face_gw_node_exchange", SWMM_GW2D_VAR_QNODE, "m3 s-1"},
            {"Mesh2_face_gw_deep", SWMM_GW2D_VAR_QDEEP, "m s-1"},
            {"Mesh2_face_gw_et", SWMM_GW2D_VAR_QET, "m s-1"},
            {"Mesh2_face_gw_dunne", SWMM_GW2D_VAR_DUNNE, "m3 s-1"},
            {"Mesh2_face_gw_infil_in", SWMM_GW2D_VAR_QPLUS, "m s-1"},
            {"Mesh2_face_gw_link_seepage", SWMM_GW2D_VAR_QLINK, "m3 s-1"},
        };
        for (const auto &e : expected) {
            const auto *v = find(frame, e.dataset);
            QVERIFY2(v, e.dataset);
            QCOMPARE(v->descriptor.units, QString(e.units));
            QCOMPARE(v->descriptor.domain, Mesh2DResultVariable::Domain::Groundwater);
            QCOMPARE(v->descriptor.temporal, e.selector == SWMM_GW2D_VAR_HG
                                                 ? Mesh2DResultVariable::Temporal::Reported
                                                 : Mesh2DResultVariable::Temporal::Held);
            QCOMPARE(swmm_gw2d_get_cell_bulk(engine.handle, e.selector, raw.data(), 2, &written),
                     SWMM_OK);
            QCOMPARE(v->values[0], float(raw[0]));
            QCOMPARE(v->values[1], float(raw[1]));
        }
        const auto *infiltration = find(frame, "Mesh2_face_gw_infil_in");
        QVERIFY(infiltration->values[0] > 0); // the wet cell recharges the aquifer

        // Ground elevation (static) and the derived depths built on it.
        const auto *ground = find(frame, "Mesh2_face_z");
        QVERIFY(ground);
        QCOMPARE(ground->descriptor.key(), QString("surface:Mesh2_face_z"));
        QCOMPARE(ground->descriptor.temporal, Mesh2DResultVariable::Temporal::Static);
        QCOMPARE(ground->values[0], 10.f);
        const auto *table = find(frame, "Mesh2_face_gw_table_elev");
        QVERIFY(table);
        QVector<Mesh2DResultVariable> catalog;
        for (const auto &v : frame->variables)
            catalog.append(v.descriptor);
        const auto derived = derivedGroundwaterVariables(catalog);
        QCOMPARE(derived.size(), 2);
        std::vector<float> values;
        std::vector<Mesh2DValueStatus> status;
        QVERIFY(computeDerivedGroundwater(derived[0].dataset, ground->values, ground->status,
                                          table->values, table->status, values, status));
        QCOMPARE(values[0], 10.f - table->values[0]);

        // After the run, the saved file names the same variables the same way.
        engine.close();
        Mesh2DH5Reader reader;
        QVERIFY2(reader.open(kRoot + "/live-surface.2d.h5"), qPrintable(reader.lastError()));
        const auto saved = reader.faceVariables();
        QStringList checked;
        // Table/base elevation labels predate R3 and differ from the file's
        // long_name; the ground elevation has none in the file.
        const QStringList ownLabels{"Mesh2_face_gw_table_elev", "Mesh2_face_gw_bed_elev",
                                    "Mesh2_face_z"};
        for (const auto &live : frame->variables) {
            bool found = false;
            for (const auto &s : saved)
                if (s.key() == live.descriptor.key()) {
                    found = true;
                    QCOMPARE(s.units, live.descriptor.units);
                    QCOMPARE(s.temporal, live.descriptor.temporal);
                    if (!ownLabels.contains(live.descriptor.dataset))
                        QCOMPARE(s.label, live.descriptor.label);
                }
            QVERIFY2(found, qPrintable(live.descriptor.key()));
            checked << live.descriptor.key();
        }
        QFile list(kRoot + "/checked_keys.txt");
        QVERIFY(list.open(QIODevice::WriteOnly | QIODevice::Truncate));
        list.write(checked.join('\n').toUtf8());
    }
};

QTEST_MAIN(TestLiveSurfaceVariables)
#include "test_livesurfacevariables.moc"
