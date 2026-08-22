/*!
 * \file   test_meshsavedirty.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Save must not re-push an unedited 2D mesh — and must still push an
 *         edited one.
 *
 *         mesh::pushMeshEditsToEngine is O(nVertices x nTriangles) inside the
 *         engine (each swmm_2d_set_vertex_z rescans every triangle), which is
 *         minutes on a million-cell mesh. SWMMVisProjectWindow::saveAs used to
 *         run it on every save regardless of whether anything had changed.
 *
 *         Skipping it is only safe while the layer and the engine still hold
 *         the same mesh, so these slots pin both directions:
 *           - a layer attached as pristine saves WITHOUT a re-push, and the
 *             mesh in the written .inp is still intact;
 *           - any edit arms the flag again, the edit reaches the file, and the
 *             flag clears only after the write succeeded.
 *
 *         Output .inp files land next to the fixture (the CTest working
 *         directory) per CLAUDE.md §4.1, so they can be reviewed.
 */
#include "layers/swmm2dmeshlayer.h"
#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "mesh/inpmeshreader.h"
#include "project/openswmmvisworkspace.h"
#include "swmmvisprojectwindow.h"

#include <QDir>
#include <QFile>
#include <QObject>
#include <QString>
#include <QTest>

namespace {

QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
}

QString fixturePath()
{
    return QDir(dataDir()).filePath(QStringLiteral("mesh_async_fixture.inp"));
}

QString readAll(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(f.readAll());
}

} // namespace

class TestMeshSaveDirty : public QObject
{
    Q_OBJECT
private slots:
    void cleanSaveKeepsMeshAndSkipsPush();
    void editArmsPushAndReachesFile();

private:
    // Builds a window on the fixture with its inline mesh attached as pristine
    // — the state the file-open path leaves behind. Returns nullptr and fills
    // \p why on failure, so a setup break is diagnosable rather than opaque.
    SWMM2DMeshLayer *setUp(SWMMVisProjectWindow **outWindow, QString *why);
};

SWMM2DMeshLayer *TestMeshSaveDirty::setUp(SWMMVisProjectWindow **outWindow,
                                          QString *why)
{
    if (!QFile::exists(fixturePath())) {
        *why = QStringLiteral("fixture missing: %1").arg(fixturePath());
        return nullptr;
    }
    auto *workspace = OpenSWMMVisWorkspace::newInstance(QString(), nullptr);
    if (!workspace) {
        *why = QStringLiteral("OpenSWMMVisWorkspace::newInstance returned null");
        return nullptr;
    }
    auto *window = new SWMMVisProjectWindow(workspace, fixturePath(), nullptr);
    QList<QString> warnings, errors;
    if (!window->loadModel(warnings, errors)) {
        *why = QStringLiteral("loadModel failed: %1").arg(errors.join(QLatin1String("; ")));
        return nullptr;
    }

    const mesh::InpMeshReadResult rr = mesh::InpMeshReader::read(fixturePath());
    if (!rr.hasMesh) {
        *why = QStringLiteral("InpMeshReader found no mesh: %1").arg(rr.errorMsg);
        return nullptr;
    }
    auto *layer = new SWMM2DMeshLayer(rr.mesh, rr.sourcePath);
    if (!rr.edgeBCs.isEmpty()) layer->edgeBCsMutable() = rr.edgeBCs;
    if (!window->canvas()) {
        *why = QStringLiteral("window has no canvas");
        return nullptr;
    }
    window->canvas()->addLayer(layer, /*pushUndo=*/false);
    // Mirrors SWMMVis::attachMesh2DLayersAsync: the layer came from the same
    // file the engine parsed, so the two meshes agree.
    window->attachMeshLayer(layer, /*pristine=*/true);

    *outWindow = window;
    return layer;
}

void TestMeshSaveDirty::cleanSaveKeepsMeshAndSkipsPush()
{
    SWMMVisProjectWindow *window = nullptr;
    QString               why;
    SWMM2DMeshLayer      *layer = setUp(&window, &why);
    QVERIFY2(layer, qPrintable(why));

    QVERIFY2(!layer->hasUnsavedMeshEdits(),
             "a layer attached as pristine must not be flagged for re-push");

    const QString outPath =
        QDir(dataDir()).filePath(QStringLiteral("mesh_savedirty_clean.inp"));
    QString err;
    QVERIFY2(window->saveAs(outPath, &err), qPrintable(err));

    // The whole point of skipping the push is that it changes nothing on disk:
    // the engine still holds the mesh it parsed, so it writes it out intact.
    const QString written = readAll(outPath);
    QVERIFY2(written.contains(QStringLiteral("[2D_VERTICES]")),
             "skipping the mesh push must not drop [2D_VERTICES]");
    QVERIFY2(written.contains(QStringLiteral("[2D_TRIANGLES]")),
             "skipping the mesh push must not drop [2D_TRIANGLES]");

    // Vertex count survives the round trip.
    const mesh::InpMeshReadResult back = mesh::InpMeshReader::read(outPath);
    QVERIFY(back.hasMesh);
    QCOMPARE(back.mesh.vertices.size(), layer->mesh().vertices.size());
    QCOMPARE(back.mesh.triangles.size(), layer->mesh().triangles.size());

    // Still clean — nothing edited it.
    QVERIFY(!layer->hasUnsavedMeshEdits());

    window->deleteLater();
}

void TestMeshSaveDirty::editArmsPushAndReachesFile()
{
    SWMMVisProjectWindow *window = nullptr;
    QString               why;
    SWMM2DMeshLayer      *layer = setUp(&window, &why);
    QVERIFY2(layer, qPrintable(why));
    QVERIFY(!layer->hasUnsavedMeshEdits());

    // Any apply* routes through attributeChanged, which must arm the re-push.
    const double kEditedZ = 4242.25;
    QVERIFY(layer->applyMeshVertexZ(0, kEditedZ));
    QVERIFY2(layer->hasUnsavedMeshEdits(),
             "a vertex-Z edit must arm the mesh re-push");

    const QString outPath =
        QDir(dataDir()).filePath(QStringLiteral("mesh_savedirty_edited.inp"));
    QString err;
    QVERIFY2(window->saveAs(outPath, &err), qPrintable(err));

    // The edit must be in the file...
    const mesh::InpMeshReadResult back = mesh::InpMeshReader::read(outPath);
    QVERIFY(back.hasMesh);
    QVERIFY(back.mesh.vertices.size() > 0);
    QCOMPARE(back.mesh.vertices[0].z, kEditedZ);

    // ...and the flag must clear only after that successful write.
    QVERIFY2(!layer->hasUnsavedMeshEdits(),
             "a successful save must clear the mesh re-push flag");

    window->deleteLater();
}

QTEST_MAIN(TestMeshSaveDirty)
#include "test_meshsavedirty.moc"
