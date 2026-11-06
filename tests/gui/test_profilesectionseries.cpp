/**
 * @file      test_profilesectionseries.cpp
 * @brief     Tests for profilesectionseries.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <QTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <cmath>
#include "plot/profilesection.h"
#include "plot/profilesectionseries.h"
#include "layers/swmm2dresultslayer.h"
using namespace ProfileSection;
using namespace openswmmvis::io;
namespace
{
class Source : public IMesh2DSource
{
public:
    QVector<QDateTime> times{QDateTime::fromSecsSinceEpoch(1000, Qt::UTC),
                             QDateTime::fromSecsSinceEpoch(1060, Qt::UTC)};
    int generation = 0;
    bool mutate = false, fail = false, missingFirst = false;
    QString path;
    /**
     *
     * @brief Report no mesh vertices for the scalar-sampling-only fixture.
     * @return Zero; geometry reads deliberately fail.
     *
     */
    int vertexCount() const override { return 0; }
    /**
     *
     * @brief Report two available scalar-cell slots.
     * @return Two, despite absent geometry; tests supply their own section station IDs.
     *
     */
    int triangleCount() const override { return 2; }
    /**
     *
     * @brief Report the mutable fixture clock length.
     * @return times.size() narrowed to int; requires a representable small test count.
     *
     */
    int timeCount() const override { return times.size(); }
    /**
     *
     * @brief Read the synthetic history generation.
     * @return generation verbatim; readFaceVariableAt can increment it to simulate
     * a concurrent source change during an otherwise synchronous fixture read.
     *
     */
    int historyGeneration() const override { return generation; }
    /**
     *
     * @brief Read the synthetic source identity path.
     * @return Owned QString copy; does not verify/create/open the named file.
     *
     */
    QString sourcePath() const override { return path; }
    /**
     *
     * @brief Read one synthetic frame instant.
     * @param t Zero-based frame index; out-of-range QVector::value returns an invalid date.
     * @return Copied UTC timestamp or invalid default, with no clock/generation mutation.
     *
     */
    QDateTime simTimeAt(int t) const override { return times.value(t); }
    /**
     *
     * @brief Refuse geometry access in the scalar-sampling fixture.
     * @param x Unused borrowed output x vector, unchanged.
     * @param y Unused borrowed output y vector, unchanged.
     * @param z Unused borrowed output elevation vector, unchanged.
     * @param triangles Unused borrowed output triangle vector, unchanged.
     * @return Always false without allocations, IO or retained references.
     *
     */
    bool readMeshGeometry(std::vector<double> &x, std::vector<double> &y, std::vector<double> &z,
                          std::vector<std::array<int, 3>> &triangles) override
    {
        return false;
    }
    /**
     *
     * @brief Refuse ordinary depth-frame reads in the scalar-sampling fixture.
     * @param time Unused unvalidated frame index.
     * @param depths Unused borrowed output, unchanged.
     * @return Always false; scalar values are provided only through readFaceVariableAt.
     *
     */
    bool readDepthsAt(int time, std::vector<float> &depths) override { return false; }
    /**
     *
     * @brief Describe four synthetic groundwater scalar datasets.
     * @param warnings Optional unused warning-list pointer, never written/dereferenced.
     * @return Owned water-table elevation in metres, unknown-unit saturated Nitrate,
     * held recharge in m/s and static bed elevation in metres. Most descriptors carry
     * two frames even if the mutable times list differs; no metadata validation/IO.
     *
     */
    QVector<Mesh2DResultVariable> faceVariables(QStringList *warnings = nullptr) const override
    {
        Mesh2DResultVariable a;
        a.dataset = "Mesh2_face_gw_table_elev";
        a.domain = Mesh2DResultVariable::Domain::Groundwater;
        a.label = "Water table";
        a.units = "m";
        a.unitsKnown = true;
        a.frameCount = 2;
        auto b = a;
        b.dataset = "Mesh2_face_gw_sat_conc";
        b.species = "Nitrate";
        b.zone = Mesh2DResultVariable::Zone::Saturated;
        b.units.clear();
        b.unitsKnown = false;
        auto c = a;
        c.dataset = "Mesh2_face_gw_recharge";
        c.units = "m s-1";
        c.temporal = Mesh2DResultVariable::Temporal::Held;
        auto base = a;
        base.dataset = "Mesh2_face_gw_bed_elev";
        base.frameCount = 0;
        base.temporal = Mesh2DResultVariable::Temporal::Static;
        return {a, b, c, base};
    }
    /**
     *
     * @brief Fill two synthetic native-cell values/statuses or simulate a failed source read.
     * @param v Borrowed descriptor: empty species selects frame-dependent 8/12 and zero,
     * nonempty species selects zero/three with second cell Waterless. Static overrides
     * values to two; arbitrary dataset identities are not validated.
     * @param t Valid zero-based time index, including static reads under this stub.
     * @param values Borrowed output replaced on success in descriptor units.
     * @param status Borrowed output two corresponding status codes; missingFirst
     * replaces first value/status with NaN/Missing.
     * @return False on fail flag or invalid time, leaving both outputs unchanged; true
     * after writes, then increments generation when mutate is set. Allocations may
     * throw after partial updates; no retained references or dataset IO.
     *
     */
    bool readFaceVariableAt(const Mesh2DResultVariable &v, int t, std::vector<float> &values,
                            std::vector<Mesh2DValueStatus> &status) override
    {
        if (fail || t < 0 || t >= times.size())
            return false;
        values = v.species.isEmpty() ? std::vector<float>{float(t ? 12 : 8), 0}
                                     : std::vector<float>{0, 3};
        status = {Mesh2DValueStatus::Valid,
                  v.species.isEmpty() ? Mesh2DValueStatus::Valid : Mesh2DValueStatus::Waterless};
        if (v.temporal == Mesh2DResultVariable::Temporal::Static)
            values = {2, 2};
        if (missingFirst) {
            values[0] = std::numeric_limits<float>::quiet_NaN();
            status[0] = Mesh2DValueStatus::Missing;
        }
        if (mutate)
            ++generation;
        return true;
    }
};
/**
 *
 * @brief Build the saved profile definition for run-A water-table sampling.
 * @param s Borrowed fixture supplying its path and first variable key; not retained.
 * @return Owned two-metre EPSG:26918 profile with metre horizontal/elevation units
 * and one Elevation series. Source ID/title/polyline are fixed; no file is opened.
 *
 */
Definition definition(const Source &s)
{
    Definition d;
    d.id = "section-1";
    d.title = "Section, one";
    d.scenePolyline = {{0, 0}, {2, 0}};
    d.sceneCRS = "EPSG:26918";
    d.horizontalUnits = "m";
    d.elevationUnits = "m";
    d.primarySourceId = "run-A";
    d.sources = {{"run-A", s.path, {}}};
    SeriesDefinition v;
    v.id = "table";
    v.sourceId = "run-A";
    v.variableKey = s.faceVariables()[0].key();
    v.role = SeriesRole::Elevation;
    d.series = {v};
    return d;
}
/**
 *
 * @brief Build two section sample stations with known ground and native cell indices.
 * @return Owned samples at chainages zero/two metres, scene points (0,0)/(2,0),
 * ground ten metres and cells zero/one. First sample starts a break; no sampling IO.
 *
 */
Section section()
{
    Section s;
    Sample a;
    a.chainage = 0;
    a.scenePt = {0, 0};
    a.ground = 10;
    a.triIdx = 0;
    a.breakBefore = true;
    Sample b = a;
    b.chainage = 2;
    b.scenePt = {2, 0};
    b.triIdx = 1;
    s.samples = {a, b};
    return s;
}
/**
 *
 * @brief Build a borrowed scalar source binding for profile tests.
 * @param s Mutable fixture whose lifetime must cover every later binding use.
 * @return By-value run-A/EPSG:26918 binding holding &s, with no ownership transfer
 * or source synchronization. Other binding policy fields retain defaults.
 *
 */
SourceBinding binding(Source &s)
{
    SourceBinding b;
    b.sourceId = "run-A";
    b.sceneCRS = "EPSG:26918";
    b.source = &s;
    return b;
}
/**
 *
 * @brief Create the review-artifact parent and append a requested fixture filename.
 * @param name Borrowed relative leaf/path below the source-relative dated contract
 * artifact directory, not sanitized; nested leaf directories are not created.
 * @return Joined path. mkpath failure is ignored; this helper does not open/write
 * the leaf and callers must check subsequent file operations.
 *
 */
QString outputPath(const QString &name)
{
    const QString root =
        QFileInfo(QString::fromUtf8(__FILE__))
            .absoluteDir()
            .absoluteFilePath("../../workplans/artifacts/phase_35_profiles_forcing/contracts");
    QDir().mkpath(root);
    return QDir(root).filePath(name);
}
}
class TestProfileSectionSeries : public QObject
{
    Q_OBJECT
private slots:
    /**
     *
     * @brief Require Hold to select the preceding covered frame while Exact rejects an
     * intermediate instant; reject outside coverage and unsorted clocks.
     *
     */
    void exactHoldAndCoverage()
    {
        Source s;
        QString e;
        QCOMPARE(selectFrame(s.times, s.times[0].addSecs(30), TimePolicy::Hold, &e), 0);
        QCOMPARE(selectFrame(s.times, s.times[0].addSecs(30), TimePolicy::Exact, &e), -1);
        QCOMPARE(selectFrame(s.times, s.times[0].addSecs(-1), TimePolicy::Hold, &e), -1);
        QCOMPARE(selectFrame(s.times, s.times[1].addSecs(1), TimePolicy::Hold, &e), -1);
        QCOMPARE(selectFrame({s.times[1], s.times[0]}, s.times[0], TimePolicy::Hold, &e), -1);
    }
    /**
     *
     * @brief Require metre elevation values to convert to feet while valid zero stays valid and
     * original ground samples remain unchanged.
     *
     */
    void elevationsConvertAndKeepZeroAndEmergence()
    {
        Source src;
        auto d = definition(src);
        d.elevationUnits = "ft";
        auto s = section();
        QString e;
        QVERIFY2(sampleSeries(s, d, {binding(src)}, src.times[1], &e), qPrintable(e));
        QCOMPARE(s.series.size(), 1);
        QVERIFY(s.series[0].error.isEmpty());
        QVERIFY(qAbs(s.series[0].points[0].value - 12 / 0.3048) < 1e-6);
        QCOMPARE(s.series[0].points[1].value, 0.);
        QCOMPARE(s.series[0].points[1].status, Mesh2DValueStatus::Valid);
        QCOMPARE(s.samples[0].ground, 10.);
        QCOMPARE(s.series[0].units, QString("ft"));
    }
    /**
     *
     * @brief Require unknown units/Waterless status to survive scalar sampling and an unavailable
     * variable key to report an error with NaN values.
     *
     */
    void identityUnavailableAndUnknownUnitsRetained()
    {
        Source src;
        auto d = definition(src);
        d.series[0].role = SeriesRole::Scalar;
        d.series[0].variableKey = src.faceVariables()[1].key();
        auto s = section();
        QString e;
        QVERIFY(sampleSeries(s, d, {binding(src)}, src.times[0], &e));
        QVERIFY(!s.series[0].unitsKnown);
        QCOMPARE(s.series[0].points[0].value, 0.);
        QCOMPARE(s.series[0].points[1].status, Mesh2DValueStatus::Waterless);
        d.series[0].variableKey = "groundwater:missing:species:Absent";
        QVERIFY(sampleSeries(s, d, {binding(src)}, src.times[0], &e));
        QVERIFY(!s.series[0].error.isEmpty());
        QVERIFY(std::isnan(s.series[0].points[0].value));
    }
    /**
     *
     * @brief Require matching vertical datum and scene CRS before sampling another source station
     * grid, preserving station order and breaks.
     *
     */
    void separateMeshIntervalsAndDatumGate()
    {
        Source src;
        auto d = definition(src);
        d.sources.push_back({"run-B", {}, "datum-B"});
        d.series[0].sourceId = "run-B";
        auto b = binding(src);
        b.sourceId = "run-B";
        b.verticalDatum = "datum-B";
        b.stations = {{0, {0, 0}, 1, true},
                      {.5, {.5, 0}, 1, false},
                      {1.5, {1.5, 0}, 0, true},
                      {2, {2, 0}, 0, false}};
        auto s = section();
        QString e;
        QVERIFY(sampleSeries(s, d, {b}, src.times[1], &e));
        QVERIFY(!s.series[0].error.isEmpty());
        d.verticalDatum = "datum-B";
        QVERIFY(sampleSeries(s, d, {b}, src.times[1], &e));
        QVERIFY(s.series[0].error.isEmpty());
        QCOMPARE(s.series[0].points.size(), 4);
        QCOMPARE(s.series[0].points[0].value, 0.);
        QCOMPARE(s.series[0].points[2].value, 12.);
        QVERIFY(s.series[0].points[2].breakBefore);
        b.sceneCRS = "EPSG:4326";
        QVERIFY(sampleSeries(s, d, {b}, src.times[1], &e));
        QVERIFY(!s.series[0].error.isEmpty());
    }
    /**
     *
     * @brief Require generation mutation during read and uncovered report time to yield
     * per-series errors without stale numeric values.
     *
     */
    void sourceChangesAndNoCoverageNeverLeakStaleValues()
    {
        Source src;
        auto d = definition(src);
        auto s = section();
        QString e;
        src.mutate = true;
        QVERIFY(sampleSeries(s, d, {binding(src)}, src.times[0], &e));
        QVERIFY(!s.series[0].error.isEmpty());
        QVERIFY(std::isnan(s.series[0].points[0].value));
        src.mutate = false;
        QVERIFY(sampleSeries(s, d, {binding(src)}, src.times[0].addSecs(-1), &e));
        QVERIFY(!s.series[0].error.isEmpty());
    }
    /**
     *
     * @brief Require static bed sampling outside report coverage to have frame -1/no effective
     * time, preserve Missing gaps and reject recharge as elevation.
     *
     */
    void staticBaseHasNoReportTimeAndMissingGapsStayMissing()
    {
        Source src;
        auto d = definition(src);
        d.series[0].variableKey = src.faceVariables()[3].key();
        auto s = section();
        QString e;
        QVERIFY(sampleSeries(s, d, {binding(src)}, src.times[0].addSecs(-600), &e));
        QVERIFY(s.series[0].error.isEmpty());
        QCOMPARE(s.series[0].points[0].value, 2.);
        QCOMPARE(s.series[0].frame, -1);
        QVERIFY(!s.series[0].effectiveTime.isValid());
        src.missingFirst = true;
        QVERIFY(sampleSeries(s, d, {binding(src)}, src.times[0], &e));
        QCOMPARE(s.series[0].points[0].status, Mesh2DValueStatus::Missing);
        QVERIFY(std::isnan(s.series[0].points[0].value));
        d.series[0].variableKey = src.faceVariables()[2].key();
        QVERIFY(sampleSeries(s, d, {binding(src)}, src.times[0], &e));
        QVERIFY(!s.series[0]
                     .error.isEmpty()); // Recharge is never an elevation, even with numeric values.
    }
    /**
     *
     * @brief Require built-in ground/water rows to retain exact signed surface geometry,
     * Waterless status and actual/requested times.
     * Writes surface_only.csv through outputPath with a checked export result.
     *
     */
    void surfaceOnlyRowsRetainExactGeometryAndActualTime()
    {
        Source src;
        auto d = definition(src);
        d.series.clear();
        auto s = section();
        s.hasResults = true;
        s.exactWaterGeometry = true;
        s.samples[0].signedDepthNow = .25;
        s.samples[0].cellHasSurface = true;
        s.samples[1].signedDepthNow = -.5;
        auto b = binding(src);
        b.surfaceTime = src.times[0];
        QString e;
        const auto requested = src.times[0].addSecs(30);
        QVERIFY(sampleSeries(s, d, {b}, requested, &e));
        const auto rows = builtInSeries(s, d);
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows[1].points[0].value, 10.25);
        QCOMPARE(rows[1].effectiveTime, src.times[0]);
        QCOMPARE(rows[1].requestedTime, requested);
        QCOMPARE(rows[1].points[1].status, Mesh2DValueStatus::Waterless);
        QCOMPARE(rows[0].points[1].value, 10.);
        QVERIFY2(exportSectionCsv(s, d, outputPath("surface_only.csv"), &e), qPrintable(e));
    }
    /**
     *
     * @brief Require CSV export to refuse the source path without replacing its sentinel bytes.
     * Creates/overwrites protected-source.h5 as a text sentinel under outputPath; it is
     * a fixture identity file, not a real HDF5 result.
     *
     */
    void csvCannotReplaceItsSource()
    {
        Source src;
        src.path = outputPath("protected-source.h5");
        QFile f(src.path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QCOMPARE(f.write("source sentinel"), qint64(15));
        f.close();
        auto d = definition(src);
        auto s = section();
        QString e;
        QVERIFY(sampleSeries(s, d, {binding(src)}, src.times[1], &e));
        QVERIFY(!exportSectionCsv(s, d, src.path, &e));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("source sentinel"));
    }
    /**
     *
     * @brief Round-trip source path/key/pen and require bad version/series shape to reject
     * without replacing selected prior definition fields.
     * Uses in-memory JSON; outputPath creates the artifact parent but no JSON file is saved.
     *
     */
    void codecRoundTripAndAtomicMalformedRefusal()
    {
        Source src;
        src.path = outputPath("source/run.h5");
        auto d = definition(src);
        d.series[0].pen = QPen(Qt::red, 2, Qt::DashLine);
        d.series[0].opacity = .4;
        const QString base = QFileInfo(outputPath("section.json")).absolutePath();
        auto j = definitionToJson(d, base);
        Definition decoded;
        QString e;
        QVERIFY2(definitionFromJson(j, base, decoded, &e), qPrintable(e));
        QCOMPARE(decoded.sources[0].path, QDir::cleanPath(src.path));
        QCOMPARE(decoded.series[0].variableKey, d.series[0].variableKey);
        QCOMPARE(decoded.series[0].pen, d.series[0].pen);
        auto bad = j;
        bad["version"] = 99;
        QVERIFY(!definitionFromJson(bad, base, decoded, &e));
        QCOMPARE(decoded.id, d.id);
        bad = j;
        bad["series"] = QJsonObject{};
        QVERIFY(!definitionFromJson(bad, base, decoded, &e));
        QCOMPARE(decoded.series.size(), 1);
    }
    /**
     *
     * @brief Require known-unit CSV to include variable identity/valid status and unknown-unit
     * re-export to leave prior bytes unchanged.
     * Writes/overwrites section.csv under outputPath and reads it for comparison.
     *
     */
    void csvNativeUnitsValidityAndAtomicUnknownUnitGate()
    {
        Source src;
        auto d = definition(src);
        auto s = section();
        QString e;
        QVERIFY(sampleSeries(s, d, {binding(src)}, src.times[1], &e));
        const QString path = outputPath("section.csv");
        QVERIFY2(exportSectionCsv(s, d, path, &e), qPrintable(e));
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const auto before = f.readAll();
        f.close();
        QVERIFY(before.contains("water-table") || before.contains("gw_table_elev"));
        QVERIFY(before.contains("valid"));
        d.series[0].role = SeriesRole::Scalar;
        d.series[0].variableKey = src.faceVariables()[1].key();
        QVERIFY(sampleSeries(s, d, {binding(src)}, src.times[0], &e));
        QVERIFY(!exportSectionCsv(s, d, path, &e));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), before);
    }
};
QTEST_APPLESS_MAIN(TestProfileSectionSeries)
#include "test_profilesectionseries.moc"
