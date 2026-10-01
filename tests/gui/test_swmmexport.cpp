/*!
 * \file   test_swmmexport.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  "Export ▸ SWMM objects…" against real engine-loaded models
 *         (workplans/FEATURE_LAYER_ROLES_AND_FIELDS_PLAN_2026-09-30.md §8, R9).
 *
 *   - all 11 spatial types become 11 layers with the right counts;
 *   - conduit vertices and subcatchment polygons are intact;
 *   - the statistics columns appear only with a run;
 *   - ROUND TRIP: junctions and conduits written to a GeoPackage re-import
 *     through Import Feature Layer's Auto-match with every attribute the
 *     table carries mapped (plan Q9) — so an import-key change that the export
 *     does not follow fails here.
 * Outputs land in tests/output/feature_layer_roles_2026-09-30/swmmexport/.
 */

#include "io/swmmexport.h"
#include "io/vectorexport.h"
#include "layers/swmmmodellayer.h"
#include "project/openswmmvisworkspace.h"
#include "swmmvisprojectwindow.h"
#include "ui/dialogs/import/importmappingmodel.h"
#include "ui/dialogs/import/importtargetregistry.h"

#include <gdal_priv.h>
#include <ogrsf_frmts.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSet>
#include <QTest>

#ifndef SWMMEXPORT_OUT_DIR
#define SWMMEXPORT_OUT_DIR "swmmexport_output"
#endif

using namespace openswmmvis::io;
using openswmmvis::import::ImportMappingModel;
using openswmmvis::import::ImportTargetRegistry;
using openswmmvis::import::TargetKind;

namespace {

QString fixture(const char *name)
{
    return QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral(".")))
        .filePath(QLatin1String(name));
}

QString outPath(const char *name)
{
    QDir().mkpath(QStringLiteral(SWMMEXPORT_OUT_DIR));
    const QString p = QDir(QStringLiteral(SWMMEXPORT_OUT_DIR)).absoluteFilePath(QLatin1String(name));
    QFile::remove(p);
    return p;
}

const ExportTable *table(const QVector<ExportTable> &tables, const char *name)
{
    for (const ExportTable &t : tables)
        if (t.name == QLatin1String(name)) return &t;
    return nullptr;
}

QStringList fieldNames(const ExportTable &t)
{
    QStringList out;
    for (const ExportField &f : t.fields) out << f.name;
    return out;
}

/*! Field names of \p layer in the GeoPackage at \p path. */
QStringList gpkgFields(const QString &path, const char *layer)
{
    QStringList out;
    GDALDataset *ds = static_cast<GDALDataset *>(GDALOpenEx(
        path.toUtf8().constData(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    if (!ds) return out;
    if (OGRLayer *l = ds->GetLayerByName(layer)) {
        OGRFeatureDefn *d = l->GetLayerDefn();
        for (int i = 0; i < d->GetFieldCount(); ++i)
            out << QString::fromUtf8(d->GetFieldDefn(i)->GetNameRef());
    }
    GDALClose(ds);
    return out;
}

}   // namespace

class TestSwmmExport : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        GDALAllRegister();
        m_workspace = OpenSWMMVisWorkspace::newInstance(QString(), nullptr);
        QVERIFY(m_workspace);
        for (const char *f : {"selection_trace_alltypes_fixture.inp", "typed_selection_fixture.inp"}) {
            auto *pw = new SWMMVisProjectWindow(m_workspace, fixture(f), nullptr);
            QList<QString> warnings, errors;
            QVERIFY2(pw->loadModel(warnings, errors), qPrintable(errors.join('\n')));
            m_windows << pw;
        }
        QTest::qWait(50);
    }

    void cleanupTestCase()
    {
        qDeleteAll(m_windows);
        delete m_workspace;
    }

    void elevenLayersWithTheRightCounts()
    {
        SWMMModelLayer *model = m_windows.at(0)->modelLayer();
        QVERIFY(model);
        const QList<int> cats = swmmSpatialCategories();
        QCOMPARE(cats.size(), 11);
        const QVector<ExportTable> tables = swmmObjectTables(model, cats, nullptr, false);
        QCOMPARE(tables.size(), 11);
        for (int i = 0; i < cats.size(); ++i) {
            const auto c = static_cast<SWMMModelLayer::Category>(cats.at(i));
            QCOMPARE(tables.at(i).name, swmmTableName(cats.at(i)));
            QVERIFY2(tables.at(i).skipped.isEmpty(),
                     qPrintable(tables.at(i).skipped.join('\n')));
            QCOMPARE(tables.at(i).rows.size(), model->categoryCount(c));
        }
        QCOMPARE(table(tables, "junctions")->geometry, ExportGeometry::Point);
        QCOMPARE(table(tables, "pumps")->geometry, ExportGeometry::LineString);
        QCOMPARE(table(tables, "subcatchments")->geometry, ExportGeometry::Polygon);
        QCOMPARE(table(tables, "subcatchments")->rows.first().points.size(), 4);

        // Inputs only: no statistics column without a run.
        for (const ExportTable &t : tables)
            for (const ExportField &f : t.fields)
                QVERIFY2(!f.label.contains(QStringLiteral("(Sim.)")), qPrintable(f.label));

        // Choice columns hold INP tokens.
        const ExportTable *outfalls = table(tables, "outfalls");
        const int type = fieldNames(*outfalls).indexOf(QStringLiteral("outfallType"));
        QVERIFY(type >= 0);
        QCOMPARE(outfalls->rows.first().values.at(type).toString(), QStringLiteral("FREE"));
        QVERIFY(!outfalls->fields.at(type).choices.isEmpty());

        // The whole lot writes.
        ExportOptions o;
        o.driver = QStringLiteral("GPKG");
        o.destination = outPath("alltypes.gpkg");
        ExportReport rep;
        QVERIFY2(exportVectorTables(tables, o, {}, &rep), qPrintable(rep.error));
        QCOMPARE(rep.layers.size(), 11);
    }

    void conduitVerticesAndPolygonsAreIntact()
    {
        SWMMModelLayer *model = m_windows.at(1)->modelLayer();
        QVERIFY(model);
        const QVector<ExportTable> tables = swmmObjectTables(
            model, {SWMMModelLayer::CatConduits, SWMMModelLayer::CatSubcatchments}, nullptr, false);
        const ExportTable *conduits = table(tables, "conduits");
        QVERIFY(conduits);
        const int name = fieldNames(*conduits).indexOf(QStringLiteral("name"));
        bool sawC1 = false;
        for (const ExportRow &r : conduits->rows) {
            if (r.values.at(name).toString() != QLatin1String("C1")) continue;
            sawC1 = true;
            QCOMPARE(r.points.size(), 3);   // J1, the [VERTICES] point, X1
            QCOMPARE(r.points.at(1), QPointF(500.0, 50.0));
        }
        QVERIFY(sawC1);
        const ExportTable *subs = table(tables, "subcatchments");
        QVERIFY(subs);
        QCOMPARE(subs->rows.size(), 1);
        QCOMPARE(subs->rows.first().points.size(), 4);
    }

    void exportedLayersReimportWithAutoMatch()
    {
        SWMMModelLayer *model = m_windows.at(0)->modelLayer();
        const QVector<ExportTable> tables = swmmObjectTables(
            model, {SWMMModelLayer::CatJunctions, SWMMModelLayer::CatConduits}, nullptr, false);
        ExportOptions o;
        o.driver = QStringLiteral("GPKG");
        o.destination = outPath("roundtrip.gpkg");
        ExportReport rep;
        QVERIFY2(exportVectorTables(tables, o, {}, &rep), qPrintable(rep.error));

        const struct { TargetKind kind; const char *layer; QStringList mustMap; } cases[] = {
            {TargetKind::Junction, "junctions",
             {QStringLiteral("name"), QStringLiteral("invertElev"), QStringLiteral("maxDepth"),
              QStringLiteral("initialDepth"), QStringLiteral("surchargeDepth"),
              QStringLiteral("pondedArea"), QStringLiteral("tag")}},
            {TargetKind::Conduit, "conduits",
             {QStringLiteral("name"), QStringLiteral("fromNode"), QStringLiteral("toNode"),
              QStringLiteral("length"), QStringLiteral("roughness"), QStringLiteral("offsetUp"),
              QStringLiteral("offsetDn"), QStringLiteral("initialFlow"), QStringLiteral("maxFlow"),
              QStringLiteral("lossInlet"), QStringLiteral("lossOutlet"), QStringLiteral("lossAvg"),
              QStringLiteral("seepRate"), QStringLiteral("barrels"), QStringLiteral("flapGate"),
              QStringLiteral("geom1"), QStringLiteral("geom2"), QStringLiteral("geom3"),
              QStringLiteral("geom4"), QStringLiteral("tag")}},
        };
        for (const auto &c : cases) {
            const QStringList fields = gpkgFields(o.destination, c.layer);
            QVERIFY2(!fields.isEmpty(), c.layer);
            ImportMappingModel mapping;
            mapping.reset(c.kind, fields);
            mapping.autoMatch();
            QVERIFY2(mapping.validationError().isEmpty(), qPrintable(mapping.validationError()));
            QSet<QString> bound;
            for (const auto &b : mapping.mapping().bindings)
                if (!b.sourceField.isEmpty()) bound.insert(b.targetKey);
            for (const QString &key : c.mustMap)
                QVERIFY2(bound.contains(key),
                         qPrintable(QStringLiteral("%1.%2 did not auto-match").arg(c.layer, key)));
        }
    }

private:
    OpenSWMMVisWorkspace *m_workspace = nullptr;
    QList<SWMMVisProjectWindow *> m_windows;
};

QTEST_MAIN(TestSwmmExport)
#include "test_swmmexport.moc"
