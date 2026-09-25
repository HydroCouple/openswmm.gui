/*!
 * \file   test_savewarnings.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Save-time engine warnings reach the GUI (embedded-section loss).
 *
 * Engine 7d43a1ff routes the writer's "embedded [REACTION_*] sections are
 * lost from this save" notice into ctx.warnings — but until this round the
 * GUI never read the list after a save, so the loss stayed invisible exactly
 * one repo short of the user. These gates drive SWMMVisProjectWindow::saveAs
 * — the funnel every GUI save path goes through — and assert the delta is
 * captured and the signal fires. They deliberately do NOT call any engine
 * write function themselves: the engine round's lesson is that a gate which
 * bypasses the production caller certifies a behaviour users never get.
 *
 * Output lands in ./test_savewarnings_output/ (CLAUDE.md §4.1).
 */
#include "project/openswmmvisworkspace.h"
#include "project/projectserializer.h"
#include "layers/swmmmodellayer.h"
#include "layers/swmm2dmeshlayer.h"
#include "map/mapcanvas.h"
#include "mesh/inpmeshreader.h"
#include "mesh/inpmeshwriter.h"
#include "mesh/meshcellgeom.h"
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_2d.h>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#ifdef Q_OS_UNIX
#include <sys/resource.h>
#include <csignal>
#endif
#include "swmmvisprojectwindow.h"
#include "swmmvis.h"
#include <QMdiArea>
#include <QMessageBox>
#include <QTimer>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTest>
#include <QMap>
#include <filesystem>

namespace {

QString outDir()
{
    const QString path = qEnvironmentVariable("SWMMVIS_SAVE_TEST_OUTPUT",
                                               QStringLiteral("test_savewarnings_output"));
    QDir().mkpath(path);
    return path;
}

/*! Minimal valid deck with an EMBEDDED reaction system (no external .rxn):
 *  the configuration the engine writer drops on save. A reactions config is
 *  rejected without at least one species, so the block carries one — species
 *  `A`, chosen not to collide with the TSS pollutant (its own open error). */
bool writeDeck(const QString &path, bool withEmbedded)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return false;
    QTextStream ts(&f);
    ts << "[TITLE]\nsave-warning gate deck\n\n"
       << "[OPTIONS]\n"
       << "FLOW_UNITS           CFS\nFLOW_ROUTING         DYNWAVE\n"
       << "START_DATE           01/01/2026\nSTART_TIME           00:00:00\n"
       << "END_DATE             01/01/2026\nEND_TIME             00:30:00\n"
       << "ROUTING_STEP         5\nREPORT_STEP          00:05:00\n\n"
       << "[JUNCTIONS]\nJ0     10.0 10 0.5 0 0\n\n"
       << "[OUTFALLS]\nOUT 7.0 FREE  NO\n\n"
       << "[CONDUITS]\nC1 J0 OUT 400 0.013 0 0 0\n\n"
       << "[XSECTIONS]\nC1 CIRCULAR 1.5 0 0 0\n\n"
       << "[POLLUTANTS]\nTSS MG/L 0.0 0.0 0.0 0.0 NO * 0.0 0.0 0.0\n\n";
    if (withEmbedded)
        ts << "[REACTION_OPTIONS]\nSOLVER RK5\n\n"
           << "[REACTION_SPECIES]\nBULK A MG\n\n";
    ts << "[REPORT]\nINPUT NO\n";
    return true;
}

/*! Window on \p deckPath, model loaded; nullptr + \p why on failure. */
SWMMVisProjectWindow *openWindow(const QString &deckPath, QString *why)
{
    auto *workspace = OpenSWMMVisWorkspace::newInstance(QString(), nullptr);
    if (!workspace) { *why = QStringLiteral("workspace null"); return nullptr; }
    auto *window = new SWMMVisProjectWindow(workspace, deckPath, nullptr);
    QList<QString> warnings, errors;
    if (!window->loadModel(warnings, errors)) {
        *why = QStringLiteral("loadModel failed: %1")
                   .arg(errors.join(QLatin1String("; ")));
        return nullptr;
    }
    return window;
}

} // namespace

class TestSaveWarnings : public QObject
{
    Q_OBJECT
private slots:
    void savePathCollision_preservesFiles_data()
    {
        QTest::addColumn<int>("collision");
        const char *names[] = {"model-is-mesh", "settings-is-mesh", "model-symlink-mesh",
            "model-hardlink-mesh", "settings-symlink-mesh", "settings-hardlink-mesh",
            "settings-symlink-model", "settings-hardlink-model", "model-is-inactive-mesh",
            "model-symlink-inactive-mesh", "model-hardlink-inactive-mesh", "settings-is-inactive-mesh",
            "mesh-hardlink-source-model", "settings-symlink-source-model", "model-symlink-source-settings",
            "mesh-hardlink-source-settings", "model-through-directory-symlink", "model-is-own-settings"};
        for (int i = 0; i < int(std::size(names)); ++i) QTest::newRow(names[i]) << i;
    }

    void savePathCollision_preservesFiles()
    {
        QFETCH(int, collision);
        const QString dir = outDir() + QStringLiteral("/collision_%1").arg(QTest::currentDataTag());
        QVERIFY(QDir().mkpath(dir));
        const QString source = dir + "/source.inp";
        QString target = dir + "/saved.inp";
        QString meshPath = dir + "/active.2dm";
        QString inactivePath;
        const QString sourceSettings = ProjectSerializer::sidecarPathFor(source);
        QString targetSettings = ProjectSerializer::sidecarPathFor(target);
        // Re-running keeps evidence directories but recreates this test's
        // owned links before writing fixtures, never following stale aliases.
        for (const auto &name : {"source.inp", "source.oswp", "saved.inp", "saved.oswp",
                                "active.2dm", "inactive.2dm", "alias"})
            QFile::remove(dir + "/" + name);
        QVERIFY(writeDeck(source, false));
        mesh::MeshResult mesh;
        mesh.ok = true;
        mesh.vertices = {{QPointF(0, 0), 10}, {QPointF(20, 0), 10}, {QPointF(0, 20), 10}};
        mesh.triangles = {{0, 1, 2}};
        QString err;
        QVERIFY(mesh::InpMeshWriter::writeInline(source, mesh, {}, 0.035, &err));
        const auto write = [](const QString &path, const QByteArray &bytes) {
            QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
        };
        const auto read = [](const QString &path) {
            QFile file(path); if (!file.open(QIODevice::ReadOnly)) return QByteArray();
            return file.readAll();
        };
        const auto fsPath = [](const QString &p) {
            return std::filesystem::u8path(p.toUtf8().constData());
        };
        const auto link = [&](const QString &from, const QString &to, bool hard = false) {
            QFile::remove(to);
            std::error_code ec;
            if (hard) std::filesystem::create_hard_link(fsPath(from), fsPath(to), ec);
            else std::filesystem::create_symlink(fsPath(from), fsPath(to), ec);
            return !ec;
        };
        const QByteArray meshBytes = mesh::InpMeshWriter::buildSectionText(mesh, {}).toUtf8();
        QVERIFY(write(meshPath, meshBytes));
        QVERIFY(write(sourceSettings, "{\"notesHtml\":\"original settings\"}\n"));
        QVERIFY(write(target, ";; original target\n"));
        QVERIFY(write(targetSettings, "{\"notesHtml\":\"previous target settings\"}\n"));
        if (collision == 0) { meshPath = target; QVERIFY(write(meshPath, meshBytes)); }
        if (collision == 1) { meshPath = targetSettings; QVERIFY(write(meshPath, meshBytes)); }
        if (collision == 2 || collision == 3) QVERIFY(link(meshPath, target, collision == 3));
        if (collision == 4 || collision == 5) QVERIFY(link(meshPath, targetSettings, collision == 5));
        if (collision == 6 || collision == 7) QVERIFY(link(target, targetSettings, collision == 7));
        if (collision >= 8 && collision <= 11) {
            inactivePath = dir + "/inactive.2dm";
            QVERIFY(write(inactivePath, meshBytes));
            if (collision == 8) { inactivePath = target; QVERIFY(write(inactivePath, meshBytes)); }
            if (collision == 9) QVERIFY(link(inactivePath, target));
            if (collision == 10) QVERIFY(link(inactivePath, target, true));
            if (collision == 11) { inactivePath = targetSettings; QVERIFY(write(inactivePath, meshBytes)); }
        }
        if (collision == 12) QVERIFY(link(source, meshPath, true));
        if (collision == 13) QVERIFY(link(source, targetSettings));
        if (collision == 14) QVERIFY(link(sourceSettings, target));
        if (collision == 15) QVERIFY(link(sourceSettings, meshPath, true));
        if (collision == 16) {
            std::error_code ec;
            std::filesystem::create_directory_symlink(fsPath(dir), fsPath(dir + "/alias"), ec);
            QVERIFY2(!ec, ec.message().c_str());
            QVERIFY(QFile::remove(meshPath));
            meshPath = target; QVERIFY(write(meshPath, meshBytes));
            target = dir + "/alias/saved.inp";
            targetSettings = ProjectSerializer::sidecarPathFor(target);
        }
        if (collision == 17) { target = targetSettings; }
        auto *w = openWindow(source, &err);
        QVERIFY2(w, qPrintable(err));
        auto *layer = new SWMM2DMeshLayer(mesh, meshPath);
        layer->setExternalMesh(true); layer->setActiveMesh(true);
        w->canvas()->addLayer(layer, false); w->attachMeshLayer(layer);
        QVERIFY(layer->applyMeshVertexZ(0, 25.0));
        if (!inactivePath.isEmpty()) {
            auto *inactive = new SWMM2DMeshLayer(mesh, inactivePath);
            inactive->setExternalMesh(true);
            w->canvas()->addLayer(inactive, false); w->attachMeshLayer(inactive, true);
        }
        w->setHasChanges(true);
        QMap<QString, QByteArray> before;
        for (const auto &path : {source, target, meshPath, sourceSettings, targetSettings, inactivePath})
            if (!path.isEmpty()) before.insert(path, read(path));
        double x, y, zBefore;
        QCOMPARE(swmm_2d_vertex_get_xyz(w->modelLayer()->engine(), 0, &x, &y, &zBefore), 0);
        QSignalSpy completed(w, &SWMMVisProjectWindow::saveCompletedWithEngineWarnings);
        QVERIFY2(!w->saveAs(target, &err), "Conflicting file ownership must fail Save");
        QVERIFY2(err.contains("same file"), qPrintable(err));
        for (auto it = before.cbegin(); it != before.cend(); ++it)
            QCOMPARE(read(it.key()), it.value());
        double zAfter;
        QCOMPARE(swmm_2d_vertex_get_xyz(w->modelLayer()->engine(), 0, &x, &y, &zAfter), 0);
        QCOMPARE(zAfter, zBefore);
        QVERIFY(w->hasChanges()); QVERIFY(layer->hasUnsavedMeshEdits());
        QCOMPARE(w->modelLayer()->modelFilePath(), source);
        QCOMPARE(completed.count(), 0);
        // Keep working edits; repair ownership with an independent mesh and
        // Save As name. No corrupt result from the failed attempt is needed.
        const QString repairedMesh = dir + "/repaired.2dm";
        QVERIFY(write(repairedMesh, meshBytes));
        layer->setSourcePath(repairedMesh);
        const QString repairedTarget = dir + "/repaired.inp";
        QVERIFY2(w->saveAs(repairedTarget, &err), qPrintable(err));
        QVERIFY(!w->hasChanges()); QVERIFY(!layer->hasUnsavedMeshEdits());
        QCOMPARE(mesh::InpMeshReader::read(repairedMesh).mesh.vertices[0].z, 25.0);
        w->deleteLater();
    }

    void savePathOwnership_allowsIndependentAndOwnFiles_data()
    {
        QTest::addColumn<int>("alias");
        QTest::newRow("same-basename-different-directory") << 0;
        QTest::newRow("symlink-to-own-model") << 1;
        QTest::newRow("hardlink-to-own-model") << 2;
        QTest::newRow("directory-symlink-to-own-model") << 3;
    }

    void savePathOwnership_allowsIndependentAndOwnFiles()
    {
        QFETCH(int, alias);
        const QString dir = outDir() + QStringLiteral("/valid_paths_%1").arg(QTest::currentDataTag());
        QVERIFY(QDir().mkpath(dir + "/comparison"));
        QVERIFY(QDir().mkpath(dir + "/destination"));
        const QString source = dir + "/saved.inp";
        QString target = dir + "/destination/saved.inp";
        const QString comparison = dir + "/comparison/saved.inp";
        QFile::remove(target); QFile::remove(dir + "/alias");
        QVERIFY(writeDeck(source, false));
        mesh::MeshResult mesh;
        mesh.ok = true;
        mesh.vertices = {{QPointF(0, 0), 10}, {QPointF(20, 0), 10}, {QPointF(0, 20), 10}};
        mesh.triangles = {{0, 1, 2}};
        QString err;
        QVERIFY(mesh::InpMeshWriter::writeInline(source, mesh, {}, 0.035, &err));
        const QByteArray comparisonBytes = mesh::InpMeshWriter::buildSectionText(mesh, {}).toUtf8();
        QFile comparisonFile(comparison); QVERIFY(comparisonFile.open(QIODevice::WriteOnly));
        QCOMPARE(comparisonFile.write(comparisonBytes), comparisonBytes.size()); comparisonFile.close();
        std::error_code ec;
        const auto fsPath = [](const QString &p) { return std::filesystem::u8path(p.toUtf8().constData()); };
        if (alias == 1) std::filesystem::create_symlink(fsPath(source), fsPath(target), ec);
        if (alias == 2) std::filesystem::create_hard_link(fsPath(source), fsPath(target), ec);
        if (alias == 3) {
            std::filesystem::create_directory_symlink(fsPath(dir), fsPath(dir + "/alias"), ec);
            target = dir + "/alias/saved.inp";
        }
        QVERIFY2(!ec, ec.message().c_str());
        auto *w = openWindow(source, &err); QVERIFY2(w, qPrintable(err));
        auto *active = new SWMM2DMeshLayer(mesh, source);
        active->setActiveMesh(true);
        w->canvas()->addLayer(active, false); w->attachMeshLayer(active);
        QVERIFY(active->applyMeshVertexZ(0, 25));
        auto *inactive = new SWMM2DMeshLayer(mesh, comparison);
        inactive->setExternalMesh(true);
        w->canvas()->addLayer(inactive, false); w->attachMeshLayer(inactive, true);
        w->setHasChanges(true);
        for (int repeat = 0; repeat < 2; ++repeat) {
            QVERIFY2(w->saveAs(target, &err), qPrintable(err));
            QVERIFY(!w->hasChanges()); QVERIFY(!active->hasUnsavedMeshEdits());
            QCOMPARE(mesh::InpMeshReader::read(target).mesh.vertices[0].z, 25.0);
            QVERIFY(comparisonFile.open(QIODevice::ReadOnly));
            QCOMPARE(comparisonFile.readAll(), comparisonBytes); comparisonFile.close();
        }
        w->deleteLater();
    }

    void invalidMesh_failsBeforeMutation_data()
    {
        QTest::addColumn<int>("failure");
        QTest::addColumn<QString>("detail");
        QTest::addColumn<bool>("external");
        const QStringList fields = {"elevation", "coordinates", "roughness", "roughness",
            "initial depth", "initial depth", "geometry", "vertex", "vertex",
            "boundary head", "boundary flow", "boundary slope", "boundary type",
            "time series", "rating curve", "coupling coefficient", "conveyance"};
        const char *names[] = {"nan-z", "infinite-x", "negative-n", "infinite-n",
            "negative-depth", "infinite-depth", "collapsed-cell", "duplicate-vertex", "invalid-index",
            "nan-head", "infinite-flow", "nan-slope", "unknown-bc", "empty-series",
            "empty-curve", "nan-coupling", "nan-conveyance"};
        for (int i = 0; i < fields.size(); ++i) {
            QTest::newRow(names[i]) << i << fields[i] << false;
            QTest::newRow(qPrintable(QString::fromLatin1(names[i]) + "-external")) << i << fields[i] << true;
        }
    }

    void invalidMesh_failsBeforeMutation()
    {
        QFETCH(int, failure);
        QFETCH(QString, detail);
        QFETCH(bool, external);
        const QString dir = outDir() + QStringLiteral("/validation_%1").arg(QTest::currentDataTag());
        QVERIFY(QDir().mkpath(dir));
        const QString source = dir + "/source.inp";
        const QString target = dir + "/saved.inp";
        QVERIFY(writeDeck(source, false));
        mesh::MeshResult initial;
        initial.ok = true;
        initial.vertices = {{QPointF(0, 0), 10}, {QPointF(20, 0), 10},
                            {QPointF(20, 20), 10}, {QPointF(0, 20), 10}};
        initial.triangles = {{0, 1, 2}, {0, 2, 3}};
        QString err;
        const QString meshPath = external ? dir + "/active.2dm" : source;
        if (external) QVERIFY(mesh::InpMeshWriter::writeExternal(source, meshPath, initial, {}, 0.035, &err));
        else QVERIFY(mesh::InpMeshWriter::writeInline(source, initial, {}, 0.035, &err));
        auto *w = openWindow(source, &err);
        QVERIFY2(w, qPrintable(err));
        auto *layer = new SWMM2DMeshLayer(initial, meshPath);
        layer->setExternalMesh(external);
        layer->setActiveMesh(true);
        w->canvas()->addLayer(layer, false);
        w->attachMeshLayer(layer);
        QVERIFY(layer->applyMeshVertexZ(0, 25.0));
        // Deliberately bypass editors: imports/worker payloads must receive
        // the same Save preflight. Construct valid caches before corruption.
        auto &state = const_cast<mesh::MeshResult &>(layer->mesh());
        const auto repaired = state;
        const auto repairedBCs = layer->edgeBCs();
        auto &bc = layer->edgeBCsMutable()[mesh::edgeSlot(0, 2)];
        using T = mesh::MeshBCTypes::Type;
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double inf = std::numeric_limits<double>::infinity();
        switch (failure) {
        case 0: state.vertices[1].z = nan; break;
        case 1: state.vertices[1].xy.setX(inf); break;
        case 2: state.triangles[0].mannings = -1; break;
        case 3: state.triangles[0].mannings = inf; break;
        case 4: state.triangles[0].initDepth = -1; break;
        case 5: state.triangles[0].initDepth = inf; break;
        case 6: state.vertices[2].xy = QPointF(10, 0); break;
        case 7: state.triangles[0].v2 = 1; break;
        case 8: state.triangles[0].v2 = 100; break;
        case 9: bc.type = T::SpecifiedStageConst; bc.head = nan; break;
        case 10: bc.type = T::SpecifiedFlowConst; bc.flow = inf; break;
        case 11: bc.type = T::NormalFlow; bc.slope = nan; break;
        case 12: bc.type = static_cast<T>(100); break;
        case 13: bc.type = T::SpecifiedFlowTS; bc.tseries = "  "; break;
        case 14: bc.type = T::RatingCurve; bc.curve = ""; break;
        case 15: state.vertices[1].coupledNode = "J0"; state.vertices[1].couplingCd = nan; break;
        case 16: bc.conveyance = nan; break;
        }
        w->setHasChanges(true);
        const auto read = [](const QString &path) {
            QFile file(path); if (!file.open(QIODevice::ReadOnly)) return QByteArray();
            return file.readAll();
        };
        const QByteArray sourceBytes = read(source);
        const QByteArray meshBytes = read(meshPath);
        const QByteArray sentinel = ";; previous saved data\n";
        const QString sidecar = ProjectSerializer::sidecarPathFor(target);
        for (const auto &path : {target, sidecar}) {
            QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(sentinel), sentinel.size());
        }
        double x, y, zBefore;
        QCOMPARE(swmm_2d_vertex_get_xyz(w->modelLayer()->engine(), 0, &x, &y, &zBefore), 0);
        QSignalSpy completed(w, &SWMMVisProjectWindow::saveCompletedWithEngineWarnings);
        QVERIFY2(!w->saveAs(target, &err), "Invalid mesh must fail before changing engine or files");
        QVERIFY2(err.contains(detail), qPrintable(err));
        QCOMPARE(read(source), sourceBytes);
        QCOMPARE(read(meshPath), meshBytes);
        QCOMPARE(read(target), sentinel);
        QCOMPARE(read(sidecar), sentinel);
        double zAfter;
        QCOMPARE(swmm_2d_vertex_get_xyz(w->modelLayer()->engine(), 0, &x, &y, &zAfter), 0);
        QCOMPARE(zAfter, zBefore);
        QCOMPARE(completed.count(), 0);
        QVERIFY(w->hasChanges());
        QVERIFY(layer->hasUnsavedMeshEdits());
        QCOMPARE(w->modelLayer()->modelFilePath(), source);
        state = repaired;
        layer->edgeBCsMutable() = repairedBCs;
        QVERIFY2(w->saveAs(target, &err), qPrintable(err));
        QVERIFY(!w->hasChanges());
        QVERIFY(!layer->hasUnsavedMeshEdits());
        QCOMPARE(mesh::InpMeshReader::read(external ? meshPath : target).mesh.vertices[0].z, 25.0);
        w->deleteLater();
    }

    void inlineMeshPayload_data()
    {
        QTest::addColumn<bool>("usProject");
        QTest::addColumn<bool>("siMesh");
        QTest::addColumn<bool>("changeGeometry");
        QTest::newRow("metric-geometry") << false << true << true;
        QTest::newRow("us-feet-geometry") << true << false << true;
        QTest::newRow("us-si-mesh-geometry") << true << true << true;
        QTest::newRow("boundary-group-only") << false << true << false;
    }

    void inlineMeshPayload()
    {
        QFETCH(bool, usProject);
        QFETCH(bool, siMesh);
        QFETCH(bool, changeGeometry);
        const QString dir = outDir() + QStringLiteral("/inline_payload_%1").arg(QTest::currentDataTag());
        QVERIFY(QDir().mkpath(dir));
        const QString source = dir + QStringLiteral("/source.inp");
        const QString target = dir + QStringLiteral("/saved.inp");
        QVERIFY(writeDeck(source, false));
        if (!usProject) {
            QFile file(source); QVERIFY(file.open(QIODevice::ReadOnly));
            QByteArray bytes = file.readAll(); file.close();
            bytes.replace("FLOW_UNITS           CFS", "FLOW_UNITS           CMS");
            QVERIFY(file.open(QIODevice::WriteOnly)); file.write(bytes);
        }
        mesh::MeshResult initial;
        initial.ok = true;
        initial.vertices = {{QPointF(0, 0), 10}, {QPointF(20, 0), 10},
                            {QPointF(20, 20), 10}, {QPointF(0, 20), 10}};
        initial.triangles = {{0, 1, 2}, {0, 2, 3}};
        mesh::InpMeshWriter::UnitInfo units;
        units.linearUnitName = siMesh ? QStringLiteral("SI (m)") : QStringLiteral("feet");
        QString err;
        QVERIFY(mesh::InpMeshWriter::writeInline(source, initial, {}, 0.035, &err, units));
        auto *w = openWindow(source, &err);
        QVERIFY2(w, qPrintable(err));
        auto edited = mesh::InpMeshReader::read(source).mesh;
        if (changeGeometry) {
            for (auto &vertex : edited.vertices) vertex.xy += QPointF(100, 200);
            edited.triangles[0] = {0, 1, 3};
            edited.triangles[1] = {1, 2, 3};
        }
        edited.vertices[0].z = 23.25;
        edited.triangles[0].mannings = 0.081;
        edited.triangles[0].initDepth = 0.33;
        auto *layer = new SWMM2DMeshLayer(edited, source);
        layer->setMeshUnitsSI(siMesh);
        layer->setActiveMesh(true);
        w->canvas()->addLayer(layer, false);
        w->attachMeshLayer(layer);
        auto &bc = layer->edgeBCsMutable()[mesh::edgeSlot(0, 2)];
        bc.type = mesh::MeshBCTypes::Type::SpecifiedFlowConst;
        bc.flow = 0.125;
        bc.group = QStringLiteral("corridor_outlet");
        bc.conveyance = 0.42;
        w->setHasChanges(true);
        for (int attempt = 0; attempt < 2; ++attempt) {
            QVERIFY2(w->saveAs(target, &err), qPrintable(err));
            QVERIFY(!w->hasChanges());
            QVERIFY(!layer->hasUnsavedMeshEdits());
            const auto back = mesh::InpMeshReader::read(target);
            QVERIFY(back.hasMesh);
            QCOMPARE(back.mesh.vertices.size(), edited.vertices.size());
            QCOMPARE(back.mesh.triangles.size(), edited.triangles.size());
            for (int v = 0; v < edited.vertices.size(); ++v) {
                QCOMPARE(back.mesh.vertices[v].xy, edited.vertices[v].xy);
                QCOMPARE(back.mesh.vertices[v].z, edited.vertices[v].z);
            }
            for (int c = 0; c < edited.triangles.size(); ++c)
                for (int v = 0; v < 3; ++v)
                    QCOMPARE(back.mesh.triangles[c].vertex(v), edited.triangles[c].vertex(v));
            QCOMPARE(back.mesh.triangles[0].mannings, 0.081);
            QCOMPARE(back.mesh.triangles[0].initDepth, 0.33);
            QCOMPARE(back.edgeBCs[mesh::edgeSlot(0, 2)].group, QStringLiteral("corridor_outlet"));
            QCOMPARE(back.edgeBCs[mesh::edgeSlot(0, 2)].flow, 0.125);
            QCOMPARE(back.edgeBCs[mesh::edgeSlot(0, 2)].conveyance, 0.42);
            QCOMPARE(mesh::unitsHeaderIsSI(back.unitsHeader), siMesh);
        }
        // Reopen in the real engine and initialize: metadata must preserve
        // physical coordinates, not merely make the text reader agree.
        SWMM_Engine check = swmm_engine_create();
        QVERIFY(check);
        QCOMPARE(swmm_engine_open(check, target.toUtf8().constData(),
            (dir + "/reopen.rpt").toUtf8().constData(),
            (dir + "/reopen.out").toUtf8().constData(), nullptr), 0);
        QCOMPARE(swmm_engine_initialize(check), 0);
        double x = 0, y = 0, z = 0;
        QCOMPARE(swmm_2d_vertex_get_xyz(check, 0, &x, &y, &z), 0);
        const double factor = usProject && !siMesh ? 0.3048 : 1.0;
        QVERIFY(qAbs(x - edited.vertices[0].xy.x() * factor) < 1e-8);
        QVERIFY(qAbs(y - edited.vertices[0].xy.y() * factor) < 1e-8);
        QVERIFY(qAbs(z - 23.25 * factor) < 1e-8);
        swmm_engine_destroy(check);
        w->deleteLater();
    }

    void incompleteBoundaryState_failsBeforeWriting()
    {
        const QString dir = outDir() + QStringLiteral("/incomplete_boundary");
        QVERIFY(QDir().mkpath(dir));
        const QString source = dir + QStringLiteral("/source.inp");
        const QString target = dir + QStringLiteral("/saved.inp");
        QFile fixture(QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", "."))
                          .filePath(QStringLiteral("mesh_async_fixture.inp")));
        QVERIFY(fixture.open(QIODevice::ReadOnly));
        QFile file(source); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(fixture.readAll()); file.close();
        const auto original = mesh::InpMeshReader::read(source);
        QVERIFY(original.hasMesh);
        QString err;
        auto *w = openWindow(source, &err);
        QVERIFY2(w, qPrintable(err));
        auto *layer = new SWMM2DMeshLayer(original.mesh, source);
        layer->setActiveMesh(true);
        w->canvas()->addLayer(layer, false);
        w->attachMeshLayer(layer);
        const auto complete = layer->edgeBCs();
        layer->edgeBCsMutable().removeLast();
        w->setHasChanges(true);
        QFile output(target); QVERIFY(output.open(QIODevice::WriteOnly));
        const QByteArray sentinel = ";; preserve previous target\n";
        output.write(sentinel); output.close();
        QVERIFY(!w->saveAs(target, &err));
        QVERIFY2(err.contains(QStringLiteral("boundary-condition data")), qPrintable(err));
        QVERIFY(output.open(QIODevice::ReadOnly));
        QCOMPARE(output.readAll(), sentinel); output.close();
        QVERIFY(w->hasChanges());
        QVERIFY(layer->hasUnsavedMeshEdits());
        QCOMPARE(w->modelLayer()->modelFilePath(), source);
        layer->edgeBCsMutable() = complete;
        QVERIFY2(w->saveAs(target, &err), qPrintable(err));
        QVERIFY(!w->hasChanges());
        QVERIFY(!layer->hasUnsavedMeshEdits());
        w->deleteLater();
    }

    void meshOwnership_data()
    {
        QTest::addColumn<int>("scenario");
        QTest::newRow("dirty-inactive-inline") << 0;
        QTest::newRow("dirty-inactive-external") << 1;
        QTest::newRow("no-active-among-multiple") << 2;
        QTest::newRow("multiple-active") << 3;
        QTest::newRow("clean-inactive-different-counts") << 4;
        QTest::newRow("switch-to-clean-mesh") << 5;
        QTest::newRow("inline-does-not-overwrite-old-external") << 6;
        QTest::newRow("save-as-does-not-overwrite-unrelated-sidecar") << 7;
    }

    void meshOwnership()
    {
        QFETCH(int, scenario);
        const QString dir = outDir() + QStringLiteral("/ownership_%1").arg(scenario);
        const QString destDir = dir + QStringLiteral("/destination");
        QVERIFY(QDir().mkpath(destDir));
        const QString deck = dir + QStringLiteral("/source.inp");
        const QString external = dir + QStringLiteral("/mesh.2dm");
        const QString target = destDir + QStringLiteral("/saved.inp");
        const QString decoy = destDir + QStringLiteral("/mesh.2dm");
        QFile fixture(QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", "."))
                          .filePath(QStringLiteral("mesh_async_fixture.inp")));
        QVERIFY(fixture.open(QIODevice::ReadOnly));
        QFile source(deck); QVERIFY(source.open(QIODevice::WriteOnly));
        const QByteArray inlineBytes = fixture.readAll();
        QCOMPARE(source.write(inlineBytes), inlineBytes.size()); source.close();
        const auto original = mesh::InpMeshReader::read(deck);
        QVERIFY(original.hasMesh);
        QString err;
        if (scenario >= 6) {
            QVERIFY(mesh::InpMeshWriter::writeExternal(deck, external, original.mesh,
                                                       {}, 0.035, &err));
        } else {
            QFile other(external); QVERIFY(other.open(QIODevice::WriteOnly));
            other.write(mesh::InpMeshWriter::buildSectionText(original.mesh, {}).toUtf8());
        }
        auto bytesAt = [](const QString &path) {
            QFile file(path);
            return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
        };
        const QByteArray sourceBefore = bytesAt(deck);
        const QByteArray externalBefore = bytesAt(external);
        const QByteArray sentinel = ";; unrelated existing destination\n";
        for (const auto &path : {target, decoy, ProjectSerializer::sidecarPathFor(target)}) {
            QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(sentinel), sentinel.size());
        }
        auto *w = openWindow(deck, &err);
        QVERIFY2(w, qPrintable(err));
        auto activeMesh = original.mesh;
        if (scenario == 5) activeMesh.vertices[0].z = 111.0;
        auto *active = new SWMM2DMeshLayer(activeMesh, scenario == 7 ? external : deck);
        active->setExternalMesh(scenario == 7);
        w->canvas()->addLayer(active, false);
        w->attachMeshLayer(active, true);
        active->setActiveMesh(scenario != 2);
        if (scenario != 5) QVERIFY(active->applyMeshVertexZ(0, 111.0));

        auto otherMesh = original.mesh;
        otherMesh.vertices[0].z = 222.0;
        if (scenario == 4) otherMesh.triangles.removeLast();
        auto *other = new SWMM2DMeshLayer(otherMesh, external);
        other->setExternalMesh(scenario == 1 || scenario >= 6);
        w->canvas()->addLayer(other, false);
        w->attachMeshLayer(other, scenario >= 2);
        other->setActiveMesh(scenario == 3);
        if (scenario == 5) {
            w->setHasChanges(false);
            active->setActiveMesh(false);
            QVERIFY(w->hasChanges());
            w->setHasChanges(false);
            active->setActiveMesh(true);
            QVERIFY(w->hasChanges());
            QVERIFY(!active->hasUnsavedMeshEdits());
        }
        w->setHasChanges(true);
        QSignalSpy pathChanged(w->modelLayer(), &SWMMModelLayer::modelFilePathChanged);
        QSignalSpy completed(w, &SWMMVisProjectWindow::saveCompletedWithEngineWarnings);
        if (scenario < 4) {
            QVERIFY2(!w->saveAs(target, &err), "Ambiguous ownership or inactive edits must fail before writing");
            QVERIFY2(err.contains(scenario < 2 ? QStringLiteral("inactive mesh")
                                              : QStringLiteral("active mesh")), qPrintable(err));
            QCOMPARE(bytesAt(deck), sourceBefore);
            QCOMPARE(bytesAt(external), externalBefore);
            QCOMPARE(bytesAt(target), sentinel);
            QCOMPARE(bytesAt(ProjectSerializer::sidecarPathFor(target)), sentinel);
            QCOMPARE(bytesAt(decoy), sentinel);
            QCOMPARE(w->modelLayer()->modelFilePath(), deck);
            QCOMPARE(pathChanged.count(), 0);
            QCOMPARE(completed.count(), 0);
            QVERIFY(w->hasChanges());
            QVERIFY(active->hasUnsavedMeshEdits());
            QCOMPARE(other->hasUnsavedMeshEdits(), scenario < 2);
            QCOMPARE(active->mesh().vertices[0].z, 111.0);
            QCOMPARE(other->mesh().vertices[0].z, 222.0);
            // Resolve ambiguous selection without changing the working edits.
            if (scenario >= 2) {
                other->setActiveMesh(false);
                active->setActiveMesh(true);
                QVERIFY2(w->saveAs(target, &err), qPrintable(err));
                QVERIFY(!w->hasChanges());
                const auto back = mesh::InpMeshReader::read(target);
                QVERIFY(back.hasMesh);
                QCOMPARE(back.mesh.vertices[0].z, 111.0);
            }
        } else {
            QVERIFY2(w->saveAs(target, &err), qPrintable(err));
            QVERIFY(!w->hasChanges());
            QVERIFY(!active->hasUnsavedMeshEdits());
            QVERIFY(!other->hasUnsavedMeshEdits());
            const auto back = mesh::InpMeshReader::read(target);
            QVERIFY(back.hasMesh);
            QCOMPARE(back.isExternal, scenario == 7);
            QCOMPARE(back.mesh.vertices[0].z, 111.0);
            QCOMPARE(back.mesh.triangles.size(), original.mesh.triangles.size());
            QCOMPARE(other->mesh().vertices[0].z, 222.0);
            if (scenario != 7) QCOMPARE(bytesAt(external), externalBefore);
            QCOMPARE(bytesAt(decoy), sentinel);
            char reference[4096] = {};
            QCOMPARE(swmm_options_get_ext(w->modelLayer()->engine(), "MESH_FILE",
                                           reference, sizeof reference), 0);
            QCOMPARE(QString::fromUtf8(reference), scenario == 7
                ? QFileInfo(external).absoluteFilePath() : QString());
            // Repeat Save after Save As to catch stale engine references.
            QVERIFY2(w->saveAs(target, &err), qPrintable(err));
            QCOMPARE(bytesAt(decoy), sentinel);
        }
        w->deleteLater();
    }

    void meshWriterFailure_restoresEngineReference()
    {
        const QString dir = outDir() + QStringLiteral("/writer_reference_failure");
        QVERIFY(QDir().mkpath(dir));
        const QString deck = dir + QStringLiteral("/source.inp");
        const QString external = dir + QStringLiteral("/active.2dm");
        const QString blocked = dir + QStringLiteral("/blocked.inp");
        QVERIFY(QDir().mkpath(blocked));
        QVERIFY(writeDeck(deck, false));
        const auto original = mesh::InpMeshReader::read(
            QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", "."))
                .filePath(QStringLiteral("mesh_async_fixture.inp")));
        QVERIFY(original.hasMesh);
        QString err;
        QVERIFY(mesh::InpMeshWriter::writeExternal(deck, external, original.mesh,
                                                   {}, 0.035, &err));
        QFile file(external); QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray previous = file.readAll(); file.close();
        auto *w = openWindow(deck, &err);
        QVERIFY2(w, qPrintable(err));
        auto *layer = new SWMM2DMeshLayer(original.mesh, external);
        layer->setExternalMesh(true);
        layer->setActiveMesh(true);
        w->canvas()->addLayer(layer, false);
        w->attachMeshLayer(layer, true);
        QVERIFY(layer->applyMeshVertexZ(0, 1234.5));
        char before[4096] = {}, after[4096] = {};
        QCOMPARE(swmm_options_get_ext(w->modelLayer()->engine(), "MESH_FILE",
                                       before, sizeof before), 0);
        QVERIFY(!w->saveAs(blocked, &err));
        QVERIFY2(err.contains(QStringLiteral("write the model")), qPrintable(err));
        QCOMPARE(swmm_options_get_ext(w->modelLayer()->engine(), "MESH_FILE",
                                       after, sizeof after), 0);
        QCOMPARE(QByteArray(after), QByteArray(before));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), previous); file.close();
        QVERIFY(layer->hasUnsavedMeshEdits());
        QVERIFY(w->hasChanges());
        QCOMPARE(w->modelLayer()->modelFilePath(), deck);
        const QString target = dir + QStringLiteral("/saved.inp");
        QVERIFY2(w->saveAs(target, &err), qPrintable(err));
        const auto back = mesh::InpMeshReader::read(target);
        QVERIFY(back.hasMesh);
        QCOMPARE(back.mesh.vertices[0].z, 1234.5);
        w->deleteLater();
    }

    void meshRejection_keepsPendingState_data()
    {
        QTest::addColumn<int>("failure");
        QTest::addColumn<QString>("detail");
        QTest::newRow("vertex-cd") << 0 << QStringLiteral("coupling coefficient");
        QTest::newRow("vertex-area") << 1 << QStringLiteral("coupling area");
        QTest::newRow("cell-area") << 2 << QStringLiteral("cell coupling");
        QTest::newRow("cell-index") << 3 << QStringLiteral("cell coupling");
        QTest::newRow("cell-node") << 4 << QStringLiteral("cell coupling");
        QTest::newRow("edge-conveyance") << 5 << QStringLiteral("conveyance");
    }

    void meshRejection_keepsPendingState()
    {
        QFETCH(int, failure);
        QFETCH(QString, detail);
        const QString dir = outDir() + QStringLiteral("/engine_rejection_%1").arg(failure);
        QVERIFY(QDir().mkpath(dir));
        const QString deck = dir + QStringLiteral("/source.inp");
        const QString target = dir + QStringLiteral("/saved.inp");
        QFile fixture(QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", "."))
                          .filePath(QStringLiteral("mesh_async_fixture.inp")));
        QVERIFY(fixture.open(QIODevice::ReadOnly));
        const QByteArray sourceBytes = fixture.readAll();
        QFile source(deck); QVERIFY(source.open(QIODevice::WriteOnly));
        QCOMPARE(source.write(sourceBytes), sourceBytes.size()); source.close();
        const auto original = mesh::InpMeshReader::read(deck);
        QVERIFY(original.hasMesh);
        QString err;
        auto *w = openWindow(deck, &err);
        QVERIFY2(w, qPrintable(err));
        // Model state can arrive from an import or another editor. Exercise
        // rejected engine values even if an individual widget validates them.
        auto edited = original.mesh;
        if (failure <= 1) {
            edited.vertices[0].coupledNode = QStringLiteral("J0");
            if (failure == 0) edited.vertices[0].couplingCd = -1.0;
            else edited.vertices[0].couplingArea = -1.0;
        } else if (failure <= 4) {
            mesh::CellCoupling row;
            row.tri = failure == 3 ? edited.triangles.size() : 0;
            row.nodeId = failure == 4 ? QString() : QStringLiteral("J0");
            row.cd = 0.65;
            row.area = failure == 2 ? -1.0 : 1.0;
            edited.cellCouplings.append(row);
        }
        auto *layer = new SWMM2DMeshLayer(edited, deck);
        layer->setActiveMesh(true);
        w->canvas()->addLayer(layer, false);
        w->attachMeshLayer(layer, true);
        QVERIFY(layer->applyMeshVertexZ(0, 1234.5));
        if (failure == 5) layer->edgeBCsMutable()[0].conveyance = 1.5;
        w->setHasChanges(true);
        const QByteArray sentinel = ";; previous target\n";
        QFile output(target); QVERIFY(output.open(QIODevice::WriteOnly));
        QCOMPARE(output.write(sentinel), sentinel.size()); output.close();
        const QString sidecar = ProjectSerializer::sidecarPathFor(target);
        QFile settings(sidecar); QVERIFY(settings.open(QIODevice::WriteOnly));
        QCOMPARE(settings.write(sentinel), sentinel.size()); settings.close();
        QSignalSpy pathChanged(w->modelLayer(), &SWMMModelLayer::modelFilePathChanged);
        QSignalSpy completed(w, &SWMMVisProjectWindow::saveCompletedWithEngineWarnings);
        QVERIFY2(!w->saveAs(target, &err), "Engine rejection must fail Save before file writes");
        QVERIFY2(err.contains(detail), qPrintable(err));
        QVERIFY(err.contains(deck));
        QVERIFY(err.contains(QStringLiteral("validate mesh data")));
        QVERIFY(w->hasChanges());
        QVERIFY(layer->hasUnsavedMeshEdits());
        QCOMPARE(layer->mesh().vertices[0].z, 1234.5);
        QCOMPARE(w->modelLayer()->modelFilePath(), deck);
        QCOMPARE(pathChanged.count(), 0);
        QCOMPARE(completed.count(), 0);
        QVERIFY(source.open(QIODevice::ReadOnly));
        QCOMPARE(source.readAll(), sourceBytes); source.close();
        QVERIFY(output.open(QIODevice::ReadOnly));
        QCOMPARE(output.readAll(), sentinel); output.close();
        QVERIFY(settings.open(QIODevice::ReadOnly));
        QCOMPARE(settings.readAll(), sentinel); settings.close();

        if (failure <= 1) QVERIFY(layer->applyMeshVertexCoupledNode(0, QString()));
        else if (failure <= 4) layer->applyCellCouplings({});
        else layer->edgeBCsMutable()[0].conveyance = 0.5;
        QVERIFY2(w->saveAs(target, &err), qPrintable(err));
        QVERIFY(err.isEmpty());
        QVERIFY(!w->hasChanges());
        QVERIFY(!layer->hasUnsavedMeshEdits());
        QCOMPARE(w->modelLayer()->modelFilePath(), target);
        const auto saved = mesh::InpMeshReader::read(target);
        QVERIFY(saved.hasMesh);
        QCOMPARE(saved.mesh.vertices[0].z, 1234.5);
        if (failure == 5) QCOMPARE(saved.edgeBCs[0].conveyance, 0.5);
        w->deleteLater();
    }

    void activeInlineMesh_isNotRetargetedToInactiveExternal()
    {
        const QString deck = outDir() + QStringLiteral("/active_inline.inp");
        const QString target = outDir() + QStringLiteral("/active_inline_saved.inp");
        const QString external = outDir() + QStringLiteral("/inactive_external.2dm");
        QFile fixture(QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", "."))
                          .filePath(QStringLiteral("mesh_async_fixture.inp")));
        QVERIFY(fixture.open(QIODevice::ReadOnly));
        const QByteArray bytes = fixture.readAll();
        QFile copy(deck); QVERIFY(copy.open(QIODevice::WriteOnly));
        QCOMPARE(copy.write(bytes), bytes.size()); copy.close();
        const auto original = mesh::InpMeshReader::read(deck);
        QVERIFY(original.hasMesh);
        const QByteArray externalBytes = mesh::InpMeshWriter::buildSectionText(original.mesh, {}).toUtf8();
        QFile other(external); QVERIFY(other.open(QIODevice::WriteOnly));
        QCOMPARE(other.write(externalBytes), externalBytes.size()); other.close();
        QString err;
        auto *w = openWindow(deck, &err);
        QVERIFY2(w, qPrintable(err));
        auto *inactive = new SWMM2DMeshLayer(original.mesh, external);
        inactive->setExternalMesh(true);
        w->canvas()->addLayer(inactive, false);
        w->attachMeshLayer(inactive, true);
        auto *active = new SWMM2DMeshLayer(original.mesh, deck);
        active->setActiveMesh(true);
        w->canvas()->addLayer(active, false);
        w->attachMeshLayer(active, true);
        w->setHasChanges(true);
        QVERIFY2(w->saveAs(target, &err), qPrintable(err));
        QFile saved(target); QVERIFY(saved.open(QIODevice::ReadOnly));
        QVERIFY(!saved.readAll().contains("[2D_MESH_FILE]"));
        QVERIFY(other.open(QIODevice::ReadOnly));
        QCOMPARE(other.readAll(), externalBytes);
        QVERIFY(!w->hasChanges());
        w->deleteLater();
    }

    void externalRestore_shortWriteFailsSave()
    {
#ifndef Q_OS_UNIX
        QSKIP("Short-write injection uses POSIX per-process file-size limits");
#else
        if (!qEnvironmentVariableIsSet("SWMMVIS_MESH_SHORT_WRITE_CHILD")) {
            QProcess child;
            auto env = QProcessEnvironment::systemEnvironment();
            env.insert(QStringLiteral("SWMMVIS_MESH_SHORT_WRITE_CHILD"), QStringLiteral("1"));
            child.setProcessEnvironment(env);
            child.setProcessChannelMode(QProcess::MergedChannels);
            child.start(QCoreApplication::applicationFilePath(),
                        {QStringLiteral("externalRestore_shortWriteFailsSave")});
            QVERIFY(child.waitForFinished(20000));
            const QByteArray report = child.readAll();
            QFile log(outDir() + QStringLiteral("/mesh_short_write_child.log"));
            QVERIFY(log.open(QIODevice::WriteOnly)); log.write(report);
            QVERIFY2(child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0,
                     report.constData());
            return;
        }
        const auto original = mesh::InpMeshReader::read(
            QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", "."))
                .filePath(QStringLiteral("mesh_async_fixture.inp")));
        QVERIFY(original.hasMesh);
        const QString deck = outDir() + QStringLiteral("/mesh_short_write.inp");
        const QString external = outDir() + QStringLiteral("/mesh_short_write.2dm");
        const QString target = outDir() + QStringLiteral("/mesh_short_write_saved.inp");
        QVERIFY(writeDeck(deck, false));
        QString err;
        QVERIFY(mesh::InpMeshWriter::writeExternal(deck, external, original.mesh,
                                                   {}, 0.035, &err));
        auto *w = openWindow(deck, &err);
        QVERIFY2(w, qPrintable(err));
        auto *layer = new SWMM2DMeshLayer(original.mesh, external);
        layer->setExternalMesh(true);
        layer->setActiveMesh(true);
        w->canvas()->addLayer(layer, false);
        w->attachMeshLayer(layer, true);
        QVERIFY(layer->applyMeshVertexZ(0, 1234.5));
        w->setHasChanges(true);
        // The engine's small serialization fits; restoring the original
        // sidecar plus its long comment cannot fit. This targets restoration,
        // not engine write failure, without a production fault-injection hook.
        QFile padded(external); QVERIFY(padded.open(QIODevice::Append));
        const QByteArray padding = "\n;; " + QByteArray(131072, 'x') + "\n";
        QCOMPARE(padded.write(padding), padding.size()); padded.close();
        struct rlimit oldLimit;
        QVERIFY(getrlimit(RLIMIT_FSIZE, &oldLimit) == 0);
        auto limited = oldLimit; limited.rlim_cur = 32768;
        const auto oldHandler = std::signal(SIGXFSZ, SIG_IGN);
        QVERIFY(setrlimit(RLIMIT_FSIZE, &limited) == 0);
        const bool saved = w->saveAs(target, &err);
        const int restored = setrlimit(RLIMIT_FSIZE, &oldLimit);
        std::signal(SIGXFSZ, oldHandler);
        QVERIFY(restored == 0);
        QVERIFY(!saved);
        QVERIFY2(err.contains(QStringLiteral("restore the external mesh")), qPrintable(err));
        QVERIFY(err.contains(external));
        QVERIFY(w->hasChanges());
        QVERIFY(layer->hasUnsavedMeshEdits());
        QCOMPARE(w->modelLayer()->modelFilePath(), deck);
        QCOMPARE(layer->mesh().vertices[0].z, 1234.5);
        // The engine may already have overwritten the mesh. This test does
        // not certify multi-file rollback; it certifies truthful failure.
        w->deleteLater();
#endif
    }

    void meshFailure_keepsPendingState_data()
    {
        QTest::addColumn<int>("failure");
        QTest::newRow("missing-active-external") << 0;
        QTest::newRow("external-is-directory") << 1;
        QTest::newRow("empty-external") << 2;
        QTest::newRow("external-topology-mismatch") << 3;
        QTest::newRow("inline-topology-mismatch") << 4;
    }

    void meshFailure_keepsPendingState()
    {
        QFETCH(int, failure);
        const QString dir = outDir() + QStringLiteral("/mesh_failure_%1").arg(failure);
        QVERIFY(QDir().mkpath(dir));
        const QString deck = dir + QStringLiteral("/source.inp");
        const QString target = dir + QStringLiteral("/save_as.inp");
        const QString external = dir + QStringLiteral("/active.2dm");
        QFile fixture(QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", "."))
                          .filePath(QStringLiteral("mesh_async_fixture.inp")));
        QVERIFY(fixture.open(QIODevice::ReadOnly));
        const QByteArray bytes = fixture.readAll();
        QFile copy(deck); QVERIFY(copy.open(QIODevice::WriteOnly));
        QCOMPARE(copy.write(bytes), bytes.size()); copy.close();
        const auto original = mesh::InpMeshReader::read(deck);
        QVERIFY(original.hasMesh);
        QVERIFY(original.mesh.triangles.size() > 1);
        QString err;
        if (QFileInfo(external).isDir()) QVERIFY(QDir().rmdir(external));
        if (failure != 4)
            QVERIFY2(mesh::InpMeshWriter::writeExternal(deck, external, original.mesh,
                                                       {}, 0.035, &err), qPrintable(err));
        auto *w = openWindow(deck, &err);
        QVERIFY2(w, qPrintable(err));
        auto edited = original.mesh;
        if (failure >= 3) edited.triangles.removeLast();
        auto *layer = new SWMM2DMeshLayer(edited, failure == 4 ? deck : external);
        layer->setExternalMesh(failure != 4);
        layer->setActiveMesh(true);
        w->canvas()->addLayer(layer, false);
        w->attachMeshLayer(layer, true);
        QVERIFY(layer->applyMeshVertexZ(0, 1234.5));
        w->setHasChanges(true);
        // A missing active mesh must never cause a switch to another valid
        // external layer. Its files are unrelated to this Save.
        if (failure == 0) {
            const QString fallback = dir + QStringLiteral("/other.2dm");
            QFile other(fallback); QVERIFY(other.open(QIODevice::WriteOnly));
            other.write(mesh::InpMeshWriter::buildSectionText(original.mesh, {}).toUtf8());
            other.close();
            auto *otherLayer = new SWMM2DMeshLayer(original.mesh, fallback);
            otherLayer->setExternalMesh(true);
            w->canvas()->addLayer(otherLayer, false);
            w->attachMeshLayer(otherLayer, true);
        }
        if (failure <= 1) {
            QVERIFY(QFile::remove(external));
            if (failure == 1) QVERIFY(QDir().mkpath(external));
        } else if (failure == 2) {
            QFile empty(external); QVERIFY(empty.open(QIODevice::WriteOnly)); empty.close();
        }
        const QByteArray targetSentinel = ";; previous Save As target must survive preflight\n";
        QFile before(target); QVERIFY(before.open(QIODevice::WriteOnly));
        QCOMPARE(before.write(targetSentinel), targetSentinel.size()); before.close();
        const QString originalPath = w->modelLayer()->modelFilePath();
        QSignalSpy pathChanged(w->modelLayer(), &SWMMModelLayer::modelFilePathChanged);
        QSignalSpy dirty(w, &SWMMVisProjectWindow::hasChangesChanged);
        QSignalSpy completed(w, &SWMMVisProjectWindow::saveCompletedWithEngineWarnings);
        QVERIFY2(!w->saveAs(target, &err), "A failed mesh stage must fail the whole Save");
        QVERIFY(err.contains(failure == 4 ? target : external));
        QVERIFY(w->hasChanges());
        QVERIFY(layer->hasUnsavedMeshEdits());
        QCOMPARE(layer->mesh().vertices[0].z, 1234.5);
        QCOMPARE(w->modelLayer()->modelFilePath(), originalPath);
        QCOMPARE(pathChanged.count(), 0);
        QCOMPARE(completed.count(), 0);
        for (const auto &change : dirty) QVERIFY(change[0].toBool());
        if (failure <= 2) {
            QFile after(target); QVERIFY(after.open(QIODevice::ReadOnly));
            QCOMPARE(after.readAll(), targetSentinel); after.close();
            if (failure == 1) QVERIFY(QDir().rmdir(external));
            QFile repaired(external); QVERIFY(repaired.open(QIODevice::WriteOnly));
            const QByteArray meshBytes = mesh::InpMeshWriter::buildSectionText(original.mesh, {}).toUtf8();
            QCOMPARE(repaired.write(meshBytes), meshBytes.size()); repaired.close();
            err.clear();
            QVERIFY2(w->saveAs(target, &err), qPrintable(err));
            QVERIFY(!w->hasChanges());
            QVERIFY(!layer->hasUnsavedMeshEdits());
            QCOMPARE(w->modelLayer()->modelFilePath(), target);
            const auto saved = mesh::InpMeshReader::read(target);
            QVERIFY(saved.hasMesh);
            QCOMPARE(saved.mesh.vertices[0].z, 1234.5);
        }
        w->deleteLater();
    }

    void sidecarFailure_keepsPendingState_data()
    {
        QTest::addColumn<int>("kind");
        QTest::newRow("save") << 0;
        QTest::newRow("save-as") << 1;
        QTest::newRow("untitled-save-as") << 2;
    }

    void sidecarFailure_keepsPendingState()
    {
        QFETCH(int, kind);
        const QString dir = outDir() + QStringLiteral("/failure_%1").arg(kind);
        QVERIFY(QDir().mkpath(dir));
        const QString deck = dir + QStringLiteral("/source.inp");
        QFile fixture(QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", "."))
                          .filePath(QStringLiteral("mesh_async_fixture.inp")));
        QVERIFY(fixture.open(QIODevice::ReadOnly));
        QFile copy(deck); QVERIFY(copy.open(QIODevice::WriteOnly));
        const QByteArray bytes = fixture.readAll();
        QCOMPARE(copy.write(bytes), bytes.size()); copy.close();
        QString why;
        auto *w = openWindow(deck, &why);
        QVERIFY2(w, qPrintable(why));
        const auto read = mesh::InpMeshReader::read(deck);
        QVERIFY(read.hasMesh);
        auto *layer = new SWMM2DMeshLayer(read.mesh, read.sourcePath);
        if (!read.edgeBCs.isEmpty()) layer->edgeBCsMutable() = read.edgeBCs;
        w->canvas()->addLayer(layer, false);
        w->attachMeshLayer(layer, true);
        QVERIFY(layer->applyMeshVertexZ(0, 1234.5));
        w->setNotesHtml(QStringLiteral("<p>Unsaved styling and project notes</p>"));
        w->setHasChanges(true);
        if (kind == 2) w->markUntitled();
        const QString originalPath = w->modelLayer()->modelFilePath();
        const QString originalName = w->modelLayer()->name();
        const QString target = kind == 0 ? deck : dir + QStringLiteral("/saved.inp");
        const QString sidecar = ProjectSerializer::sidecarPathFor(target);
        if (QFileInfo(sidecar).isFile()) QVERIFY(QFile::remove(sidecar));
        QVERIFY(QDir().mkpath(sidecar)); // deterministic write failure on all platforms
        QSignalSpy dirty(w, &SWMMVisProjectWindow::hasChangesChanged);
        QSignalSpy pathChanged(w->modelLayer(), &SWMMModelLayer::modelFilePathChanged);
        QString err;
        QVERIFY(!w->saveAs(target, &err));
        QVERIFY(err.contains(sidecar));
        QVERIFY(w->hasChanges());
        QVERIFY(layer->hasUnsavedMeshEdits());
        QCOMPARE(w->modelLayer()->modelFilePath(), originalPath);
        QCOMPARE(w->modelLayer()->name(), originalName);
        QCOMPARE(w->isUntitled(), kind == 2);
        QCOMPARE(pathChanged.count(), 0);
        for (const auto &change : dirty) QVERIFY(change[0].toBool());

        // Removing the obstruction allows a retry; only this successful save
        // adopts the target path and clears both project and mesh dirty flags.
        QVERIFY(QDir().rmdir(sidecar));
        err.clear();
        QVERIFY2(w->saveAs(target, &err), qPrintable(err));
        QVERIFY(!w->hasChanges());
        QVERIFY(!layer->hasUnsavedMeshEdits());
        QVERIFY(!w->isUntitled());
        QCOMPARE(w->modelLayer()->modelFilePath(), target);
        QCOMPARE(pathChanged.count(), kind == 0 ? 0 : 1);
        QFile saved(sidecar); QVERIFY(saved.open(QIODevice::ReadOnly));
        const auto root = QJsonDocument::fromJson(saved.readAll()).object();
        const auto session = root[QStringLiteral("sessions")].toArray().first().toObject();
        QCOMPARE(QDir(QFileInfo(sidecar).absolutePath()).absoluteFilePath(session[QStringLiteral("inpPath")].toString()),
                 QFileInfo(target).absoluteFilePath());
        QCOMPARE(session[QStringLiteral("notesHtml")].toString(), w->notesHtml());
        const auto meshBack = mesh::InpMeshReader::read(target);
        QVERIFY(meshBack.hasMesh);
        QCOMPARE(meshBack.mesh.vertices[0].z, 1234.5);
        w->deleteLater();
    }

    void saveCommand_showsFailure()
    {
        const QString deck = outDir() + QStringLiteral("/save_command.inp");
        QVERIFY(writeDeck(deck, false));
        QString why;
        auto *w = openWindow(deck, &why);
        QVERIFY2(w, qPrintable(why));
        const QString sidecar = ProjectSerializer::sidecarPathFor(deck);
        if (QFileInfo(sidecar).isFile()) QVERIFY(QFile::remove(sidecar));
        QVERIFY(QDir().mkpath(sidecar));
        SWMMVis host;
        auto *mdi = host.findChild<QMdiArea *>();
        QVERIFY(mdi);
        mdi->addSubWindow(w);
        host.show();
        w->show();
        mdi->setActiveSubWindow(w);
        w->setHasChanges(true);
        QString message;
        bool sawCriticalMessage = false;
        // Inspect and dismiss the real modal opened by the production Save
        // command; an unexpected Save As dialog is dismissed but fails below.
        QTimer dismissDialog;
        connect(&dismissDialog, &QTimer::timeout, &host, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            if (auto *box = qobject_cast<QMessageBox *>(dialog)) {
                message = box->text();
                sawCriticalMessage = box->icon() == QMessageBox::Critical;
            }
            dialog->reject();
        });
        dismissDialog.start(10);
        QVERIFY(QMetaObject::invokeMethod(&host, "onSaveProject", Qt::DirectConnection));
        dismissDialog.stop();
        // macOS ignores QMessageBox window titles; assert the actual error
        // content and severity rather than a platform-specific decoration.
        QVERIFY(sawCriticalMessage);
        QVERIFY(message.contains(sidecar));
        QVERIFY(w->hasChanges());
        QVERIFY(QDir().rmdir(sidecar));
        w->setHasChanges(false);
    }

    void atomicSidecar_shortWritePreservesOriginal()
    {
#ifndef Q_OS_UNIX
        QSKIP("Short-write injection uses POSIX per-process file-size limits");
#else
        if (!qEnvironmentVariableIsSet("SWMMVIS_SHORT_WRITE_CHILD"))
        {
            QProcess child;
            auto env = QProcessEnvironment::systemEnvironment();
            env.insert(QStringLiteral("SWMMVIS_SHORT_WRITE_CHILD"), QStringLiteral("1"));
            child.setProcessEnvironment(env);
            child.setProcessChannelMode(QProcess::MergedChannels);
            child.start(QCoreApplication::applicationFilePath(),
                        {QStringLiteral("atomicSidecar_shortWritePreservesOriginal")});
            QVERIFY(child.waitForFinished(20000));
            const QByteArray report = child.readAll();
            QFile log(outDir() + QStringLiteral("/short_write_child.log"));
            QVERIFY(log.open(QIODevice::WriteOnly)); log.write(report);
            QVERIFY2(child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0,
                     report.constData());
            return;
        }
        const QString deck = outDir() + QStringLiteral("/short_write.inp");
        QVERIFY(writeDeck(deck, false));
        QString why;
        auto *w = openWindow(deck, &why);
        QVERIFY2(w, qPrintable(why));
        w->setNotesHtml(QString(8192, QLatin1Char('x')));
        const QString sidecar = ProjectSerializer::sidecarPathFor(deck);
        const QByteArray original = "{\"previous\":\"project must survive\"}\n";
        QFile before(sidecar); QVERIFY(before.open(QIODevice::WriteOnly));
        QCOMPARE(before.write(original), original.size()); before.close();
        struct rlimit oldLimit;
        QVERIFY(getrlimit(RLIMIT_FSIZE, &oldLimit) == 0);
        auto limited = oldLimit; limited.rlim_cur = 512;
        const auto oldHandler = std::signal(SIGXFSZ, SIG_IGN);
        QVERIFY(setrlimit(RLIMIT_FSIZE, &limited) == 0);
        QString err;
        const bool saved = ProjectSerializer::saveToFile(sidecar, w, &err);
        const int restored = setrlimit(RLIMIT_FSIZE, &oldLimit);
        std::signal(SIGXFSZ, oldHandler);
        QVERIFY(restored == 0);
        QFile after(sidecar); QVERIFY(after.open(QIODevice::ReadOnly));
        const auto actual = after.readAll();
        QVERIFY2(!saved, "A short write must not report success");
        QVERIFY(!err.isEmpty());
        QCOMPARE(actual, original);
        w->deleteLater();
#endif
    }


    /*! The reported defect end-to-end: a save that drops the embedded
     *  reaction system must leave the loss visible on the window — in
     *  lastSaveWarnings() AND through the signal SWMMVis routes to the log
     *  panel and the modal. */
    void embeddedSectionLoss_surfacesOnSave()
    {
        const QString deck = outDir() + QStringLiteral("/sw_embed.inp");
        QVERIFY(writeDeck(deck, /*withEmbedded=*/true));
        QString why;
        SWMMVisProjectWindow *w = openWindow(deck, &why);
        QVERIFY2(w, qPrintable(why));

        QSignalSpy spy(w, &SWMMVisProjectWindow::saveCompletedWithEngineWarnings);
        QString err;
        const QString saved = outDir() + QStringLiteral("/sw_embed_saved.inp");
        QVERIFY2(w->saveAs(saved, &err), qPrintable(err));

        // The delta is captured on the window…
        const QStringList warns = w->lastSaveWarnings();
        QVERIFY2(!warns.isEmpty(),
                 "the save dropped the embedded reaction sections and the "
                 "window captured NO warning — the GUI is silent again");
        bool namesLoss = false, namesSection = false;
        for (const QString &wtext : warns) {
            if (wtext.contains(QStringLiteral("lost from this save")))
                namesLoss = true;
            if (wtext.contains(QStringLiteral("REACTION_OPTIONS")))
                namesSection = true;
        }
        QVERIFY2(namesLoss, "no captured warning says the data is lost");
        QVERIFY2(namesSection,
                 "the warning does not name the dropped section, so the user "
                 "cannot tell what to rescue");

        // …and the signal SWMMVis subscribes to fired once, with that list.
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toStringList(), warns);

        // The loss is real: the saved deck has no reaction section. If this
        // ever fails because the engine started round-tripping embedded
        // sections (IO3's saveData()), this whole gate is obsolete — replace
        // it with a round-trip assertion, don't weaken it.
        QFile f(saved);
        QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
        QVERIFY(!QString::fromUtf8(f.readAll())
                     .contains(QStringLiteral("[REACTION_OPTIONS]")));
    }

    /*! Lesson-148 guard: a clean save must warn about NOTHING — no signal,
     *  empty lastSaveWarnings(). A notice that fires on every save is one
     *  users learn to ignore, which re-creates the silence. */
    void cleanSave_staysSilent()
    {
        const QString deck = outDir() + QStringLiteral("/sw_clean.inp");
        QVERIFY(writeDeck(deck, /*withEmbedded=*/false));
        QString why;
        SWMMVisProjectWindow *w = openWindow(deck, &why);
        QVERIFY2(w, qPrintable(why));

        QSignalSpy spy(w, &SWMMVisProjectWindow::saveCompletedWithEngineWarnings);
        QString err;
        QVERIFY2(w->saveAs(outDir() + QStringLiteral("/sw_clean_saved.inp"), &err),
                 qPrintable(err));

        QVERIFY(w->lastSaveWarnings().isEmpty());
        QCOMPARE(spy.count(), 0);
    }

    /*! Two saves in a row: the second save's delta must not re-report the
     *  first save's warnings on top (the engine list is CUMULATIVE — the
     *  bracketing is what keeps the GUI honest about what THIS save did). */
    void secondSave_reportsOnlyItsOwnDelta()
    {
        const QString deck = outDir() + QStringLiteral("/sw_twice.inp");
        QVERIFY(writeDeck(deck, /*withEmbedded=*/true));
        QString why;
        SWMMVisProjectWindow *w = openWindow(deck, &why);
        QVERIFY2(w, qPrintable(why));

        QString err;
        QVERIFY2(w->saveAs(outDir() + QStringLiteral("/sw_twice_1.inp"), &err),
                 qPrintable(err));
        const int firstCount = w->lastSaveWarnings().size();
        QVERIFY(firstCount >= 1);

        QVERIFY2(w->saveAs(outDir() + QStringLiteral("/sw_twice_2.inp"), &err),
                 qPrintable(err));
        // The embedded sections are still in the engine's context, so the
        // second save legitimately warns again — but exactly as much, not
        // cumulatively more.
        QCOMPARE(w->lastSaveWarnings().size(), firstCount);
    }
};

QTEST_MAIN(TestSaveWarnings)
#include "test_savewarnings.moc"
