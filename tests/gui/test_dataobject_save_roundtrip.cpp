/**
 * @file      test_dataobject_save_roundtrip.cpp
 * @brief     A data object edit that reached the model while its editor is
 *            STILL OPEN must be in the file written by Save
 *            (workplans/DATA_OBJECT_APPLY_REVERT_PLAN_2026-11-11.md).
 *
 * Phase 1 had Save write the Pollutant, Aquifer, Street, Inlet and Transect
 * registries to the engine, because their editors kept edits in the providers
 * until the dialog closed. Since Phase 2/4 those editors write the model at
 * Apply, and the registries hold what the model holds — except when another
 * writer changes the engine around them (the Properties panel's data-object
 * adapters write the engine directly). Each case opens the editor (which loads
 * the registry), changes one value in the engine the way those adapters do,
 * saves through the real SWMMVisProjectWindow::saveAs with the editor open,
 * reopens the saved file and compares: Save must not write the registry's
 * older copy over it. Saved files are kept in
 * workplans/data-object-apply/phase1/review.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "aquifer/aquiferprovider.h"
#include "curve/curveprovider.h"
#include "curve/curveregistry.h"
#include "aquifer/aquiferregistry.h"
#include "inlet/inletprovider.h"
#include "inlet/inletregistry.h"
#include "pattern/patternprovider.h"
#include "pattern/patternregistry.h"
#include "layers/swmmmodellayer.h"
#include "pollutant/pollutantprovider.h"
#include "pollutant/pollutantregistry.h"
#include "project/openswmmvisworkspace.h"
#include "street/streetprovider.h"
#include "street/streetregistry.h"
#include "swmmvisprojectwindow.h"
#include "transect/transectprovider.h"
#include "transect/transectregistry.h"
#include "ui/editors/comprehensiveeditorregistry.h"

#include <QDialog>
#include <QDir>
#include <QFile>
#include <QTest>
#include <QUndoStack>

#include <openswmm/engine/openswmm_infrastructure.h>
#include <openswmm/engine/openswmm_pollutants.h>
#include <openswmm/engine/openswmm_subcatchments.h>
#include <openswmm/engine/openswmm_tables.h>

#include <functional>

namespace
{

QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
}

/** @brief Reviewable output folder: &lt;repo&gt;/workplans/data-object-apply/phase1/review. */
QString outDir()
{
    QDir d(dataDir()); // tests/gui/data
    d.cdUp();
    d.cdUp();
    d.cdUp();
    const QString out = d.filePath(QStringLiteral("workplans/data-object-apply/phase1/review"));
    QDir().mkpath(out);
    return out;
}

SWMMVisProjectWindow *openWindow(const QString &path, QString *why)
{
    auto *workspace = OpenSWMMVisWorkspace::newInstance(QString(), nullptr);
    auto *window = new SWMMVisProjectWindow(workspace, path, nullptr);
    QList<QString> warnings, errors;
    if (!window->loadModel(warnings, errors)) {
        *why = errors.join(QLatin1String("; "));
        delete window;
        return nullptr;
    }
    return window;
}

/** @brief One family: its category, the object edited, and value access on a layer. */
struct Family {
    int category;
    QString name;
    double value;
    std::function<bool(SWMMModelLayer *, double)> set; ///< engine write; false when it failed
    std::function<double(SWMMModelLayer *)> get;       ///< NaN when the object is missing
};

template <class Reg> Reg *reg(QObject *o)
{
    return qobject_cast<Reg *>(o);
}

QHash<QString, Family> families()
{
    using namespace openswmmvis;
    const double nan = qQNaN();
    QHash<QString, Family> f;
    f["pollutant"] = {SWMMModelLayer::DataPollutants, "TSS", 0.125,
                      [](SWMMModelLayer *l, double v) {
                          const int i = swmm_pollutant_index(l->engine(), "TSS");
                          return i >= 0 && swmm_pollutant_set_kdecay(l->engine(), i, v) == SWMM_OK;
                      },
                      [nan](SWMMModelLayer *l) {
                          auto *p = reg<pollutant::PollutantRegistry>(l->ensurePollutantRegistry())->findByName("TSS");
                          return p ? p->kDecay() : nan;
                      }};
    f["aquifer"] = {SWMMModelLayer::DataAquifers, "AQ1", 0.55,
                    [](SWMMModelLayer *l, double v) {
                        const int i = swmm_aquifer_index(l->engine(), "AQ1");
                        return i >= 0 && swmm_aquifer_set_param(l->engine(), i, 0, v) == SWMM_OK; // porosity
                    },
                    [nan](SWMMModelLayer *l) {
                        auto *p = reg<aquifer::AquiferRegistry>(l->ensureAquiferRegistry())->findByName("AQ1");
                        return p ? p->param(0) : nan;
                    }};
    f["street"] = {SWMMModelLayer::DataStreets, "ST1", 0.025,
                   [](SWMMModelLayer *l, double v) {
                       SWMM_Engine e = l->engine();
                       const int i = swmm_street_index(e, "ST1");
                       double t, h, sx, n, gd, gw, bw, bs, bn;
                       int sides = 0;
                       return i >= 0 &&
                              swmm_street_get_params(e, i, &t, &h, &sx, &n, &gd, &gw, &sides, &bw,
                                                     &bs, &bn) == SWMM_OK &&
                              swmm_street_set_params(e, i, t, h, sx, v, gd, gw, sides, bw, bs, bn) ==
                                  SWMM_OK;
                   },
                   [nan](SWMMModelLayer *l) {
                       auto *p = reg<street::StreetRegistry>(l->ensureStreetRegistry())->findByName("ST1");
                       return p ? p->roadRoughness() : nan;
                   }};
    f["inlet"] = {SWMMModelLayer::DataInlets, "GRATE1", 3.5,
                  [](SWMMModelLayer *l, double v) {
                      SWMM_Engine e = l->engine();
                      const int i = swmm_inlet_index(e, "GRATE1");
                      SWMM_InletDesign d{};
                      if (i < 0 || swmm_inlet_get_design(e, i, &d) != SWMM_OK)
                          return false;
                      d.grate_length = v;
                      return swmm_inlet_set_design(e, i, &d) == SWMM_OK;
                  },
                  [nan](SWMMModelLayer *l) {
                      auto *p = reg<inlet::InletRegistry>(l->ensureInletRegistry())->findByName("GRATE1");
                      return p ? p->grateLength() : nan;
                  }};
    f["transect"] = {SWMMModelLayer::DataTransects, "Profile", 0.055,
                     [](SWMMModelLayer *l, double v) {
                         SWMM_Engine e = l->engine();
                         const int i = swmm_transect_index(e, "Profile");
                         double nl = 0, nr = 0, nc = 0;
                         return i >= 0 &&
                                swmm_transect_get_roughness(e, i, &nl, &nr, &nc) == SWMM_OK &&
                                swmm_transect_set_roughness(e, i, nl, nr, v) == SWMM_OK;
                     },
                     [nan](SWMMModelLayer *l) {
                         auto *p = reg<transect::TransectRegistry>(l->ensureTransectRegistry())->findByName("Profile");
                         return p ? p->nChannel() : nan;
                     }};
    f["curve"] = {SWMMModelLayer::DataCurves, "CurveA", 3.5,
                  [](SWMMModelLayer *l, double v) { // second point's Y, as the Properties panel writes it
                      SWMM_Engine e = l->engine();
                      const int i = swmm_table_index(e, "CurveA");
                      int n = 0;
                      if (i < 0 || swmm_table_get_point_count(e, i, &n) != SWMM_OK || n < 2)
                          return false;
                      QVector<double> xs(n), ys(n);
                      for (int j = 0; j < n; ++j)
                          swmm_table_get_point(e, i, j, &xs[j], &ys[j]);
                      ys[1] = v;
                      return swmm_table_replace_points(e, i, xs.constData(), ys.constData(), n, 0,
                                                       0.0, nullptr) == SWMM_OK;
                  },
                  [nan](SWMMModelLayer *l) {
                      auto *p = reg<curve::CurveRegistry>(l->ensureCurveRegistry())->findByName("CurveA");
                      return p && p->pointCount() > 1 ? p->pointAt(1).y : nan;
                  }};
    f["pattern"] = {SWMMModelLayer::DataPatterns, "FirstPattern", 1.25,
                    [](SWMMModelLayer *l, double v) { // January's factor
                        SWMM_Engine e = l->engine();
                        const int i = swmm_pattern_index(e, "FirstPattern");
                        int n = 0;
                        if (i < 0 || swmm_pattern_get_factor_count(e, i, &n) != SWMM_OK || n < 1)
                            return false;
                        QVector<double> factors(n);
                        for (int j = 0; j < n; ++j)
                            swmm_pattern_get_factor(e, i, j, &factors[j]);
                        factors[0] = v;
                        return swmm_pattern_set_factors(e, i, factors.constData(), n) == SWMM_OK;
                    },
                    [nan](SWMMModelLayer *l) {
                        // From the engine: the fixture's pattern has one factor where a
                        // monthly pattern has twelve, and the registry keeps its defaults
                        // for a count that does not match the type.
                        double v = nan;
                        const int i = swmm_pattern_index(l->engine(), "FirstPattern");
                        if (i >= 0)
                            swmm_pattern_get_factor(l->engine(), i, 0, &v);
                        return v;
                    }};
    return f;
}

} // namespace

class TestDataObjectSaveRoundTrip : public QObject
{
    Q_OBJECT
private slots:
    void engineEditWithEditorOpenIsSaved_data()
    {
        QTest::addColumn<QString>("family");
        QTest::addColumn<QString>("ext");
        for (const char *fam : {"pollutant", "aquifer", "street", "inlet", "transect", "curve", "pattern"})
            for (const char *ext : {"inp", "gpkg"})
                QTest::newRow(qPrintable(QStringLiteral("%1-%2").arg(fam, ext))) << fam << ext;
    }

    void engineEditWithEditorOpenIsSaved()
    {
        QFETCH(QString, family);
        QFETCH(QString, ext);
        const Family fam = families().value(family);
        const QString src = outDir() + QStringLiteral("/%1_%2_src.inp").arg(family, ext);
        const QString saved = outDir() + QStringLiteral("/%1_saved.%2").arg(family, ext);
        for (const QString &f : {src, saved, saved + "-wal", saved + "-shm"})
            QFile::remove(f);
        QVERIFY(QFile::copy(QDir(dataDir()).filePath(QStringLiteral("data_editor_routing.inp")), src));

        QString why, err;
        {
            SWMMVisProjectWindow *w = openWindow(src, &why);
            QVERIFY2(w, qPrintable(why));
            QWidget host;
            QUndoStack undo;
            QVERIFY(ComprehensiveEditorRegistry::instance().openForObject(
                SWMMModelLayer::DataCategory(fam.category), w->modelLayer(), &undo, fam.name, &host));
            bool editorOpen = false;
            for (auto *d : host.findChildren<QDialog *>())
                editorOpen = editorOpen || d->isVisible();
            QVERIFY(editorOpen);
            QVERIFY(!qFuzzyCompare(fam.get(w->modelLayer()), fam.value));
            QVERIFY(fam.set(w->modelLayer(), fam.value)); // the registry still holds the old value

            QVERIFY2(w->saveAs(saved, &err), qPrintable(err)); // editor still open
            for (auto *d : host.findChildren<QDialog *>())
                d->close();
            delete w;
        }
        SWMMVisProjectWindow *r = openWindow(saved, &why);
        QVERIFY2(r, qPrintable(why));
        const double got = fam.get(r->modelLayer());
        delete r;
        QCOMPARE(got, fam.value);
    }
};

QTEST_MAIN(TestDataObjectSaveRoundTrip)
#include "test_dataobject_save_roundtrip.moc"
