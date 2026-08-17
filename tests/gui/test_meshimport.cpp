/*!
 * \file   test_meshimport.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Coverage for SWMMVisProjectWindow::importMeshFileAsync() — the
 *         browse-and-load path for an existing OpenSWMM 2D mesh (.2dm).
 *
 *         Before this, a .2dm could only reach a model by already sitting
 *         next to its .inp (Simulation Options → Mesh only lists siblings of
 *         the model file), so the invariants worth pinning are:
 *           - a mesh browsed from ANOTHER folder is copied next to the .inp
 *             (relative [2D_MESH_FILE] ⇒ portable project) and joins the
 *             canvas as the ACTIVE EXTERNAL mesh — the layer the save path
 *             retargets [2D_MESH_FILE] at;
 *           - the engine's in-memory MESH_FILE mirrors the import, or the
 *             next save re-serialises the .inp without the linkage and the
 *             model silently reverts to 1D;
 *           - a file that is not an OpenSWMM mesh is rejected AND the copy it
 *             produced is cleaned up (no junk .2dm left in the project);
 *           - a name collision never silently clobbers the existing mesh —
 *             "Keep Both" writes a uniquified sibling;
 *           - closing the project clears the (leaked) layer's scene item
 *             instead of leaving it dangling — see ~MapCanvas.
 *
 *         Everything the test writes lands under tests/output/mesh_import/
 *         for review (CLAUDE.md §4.1), never in a temp dir.
 *
 *         QTEST_MAIN (not APPLESS): a real SWMMVisProjectWindow is a QWidget.
 */
#include "layers/swmm2dmeshlayer.h"
#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "map/mapextent.h"
#include "mesh/inpmeshreader.h"
#include "project/openswmmvisworkspace.h"
#include "swmmvisprojectwindow.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_model.h>

#include <QAbstractButton>
#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsScene>
#include <QMessageBox>
#include <QObject>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

namespace {

QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
}

//! The project the mesh is imported INTO: a complete 1D model the engine can
//! open, carrying an explicit [MAP] Units line so the CRS resolves without the
//! modal picker (which would hang this unattended test). It has no 2D data —
//! the import is the only source of a mesh.
QString projectFixturePath()
{
    return QDir(dataDir()).filePath(QStringLiteral("typed_selection_fixture.inp"));
}

//! Source of the mesh geometry lifted into the standalone .2dm.
QString meshFixturePath()
{
    return QDir(dataDir()).filePath(QStringLiteral("mesh_async_fixture.inp"));
}

//! Reviewable output root: <repo>/tests/output/mesh_import (never a temp dir).
QString outputDir()
{
    QDir d(dataDir());              // tests/gui/data
    d.cdUp();                       // tests/gui
    d.cdUp();                       // tests
    const QString out = d.filePath(QStringLiteral("output/mesh_import"));
    QDir().mkpath(out);
    return out;
}

bool writeText(const QString &path, const QString &text)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return false;
    return f.write(text.toUtf8()) > 0;
}

//! Lift the fixture's inline [2D_VERTICES] / [2D_TRIANGLES] into a standalone
//! .2dm — exactly the shape InpMeshWriter::writeExternal emits.
bool buildStandaloneMesh(const QString &inpPath, const QString &meshPath)
{
    QFile in(inpPath);
    if (!in.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    const QStringList lines =
        QString::fromUtf8(in.readAll()).split(QChar('\n'), Qt::KeepEmptyParts);

    QString out = QStringLiteral(";; UNITS: SI\n"
                                 ";; Standalone mesh for test_meshimport\n\n");
    bool keep = false;
    for (const QString &raw : lines) {
        const QString t = raw.trimmed();
        if (t.startsWith(QChar('[')) && t.endsWith(QChar(']')))
            keep = (t.compare(QStringLiteral("[2D_VERTICES]"),  Qt::CaseInsensitive) == 0
                 || t.compare(QStringLiteral("[2D_TRIANGLES]"), Qt::CaseInsensitive) == 0);
        if (keep) out += raw + QChar('\n');
    }
    return writeText(meshPath, out);
}

QString engineOption(SWMM_Engine engine, const char *key)
{
    char buf[512] = {0};
    if (swmm_options_get_ext(engine, key, buf, int(sizeof(buf))) != 0)
        return QString();
    return QString::fromUtf8(buf);
}

SWMM2DMeshLayer *activeMeshLayer(MapCanvas *canvas)
{
    if (!canvas) return nullptr;
    for (OpenSWMMVisLayer *l : canvas->layers())
        if (auto *m = qobject_cast<SWMM2DMeshLayer *>(l))
            if (m->isActiveMesh()) return m;
    return nullptr;
}

int meshLayerCount(MapCanvas *canvas)
{
    int n = 0;
    if (canvas)
        for (OpenSWMMVisLayer *l : canvas->layers())
            if (qobject_cast<SWMM2DMeshLayer *>(l)) ++n;
    return n;
}

} // namespace

class TestMeshImport : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void importsMeshFromOutsideTheProjectFolder();
    void rejectsNonMeshFileAndRemovesTheCopy();
    void nameCollisionKeepBothLeavesTheExistingMeshIntact();
    void closingTheProjectClearsTheMeshLayersSceneItem();

private:
    /*! Fresh project folder + loaded window for one test. Each test gets its
     *  own subfolder so the artefacts stay individually reviewable. */
    SWMMVisProjectWindow *openProject(const QString &subdir);

    OpenSWMMVisWorkspace *m_workspace = nullptr;
    QString               m_outRoot;
    QString               m_externalDir;   ///< where the "browsed for" .2dm lives
    QString               m_externalMesh;
    int                   m_meshVerts = 0;
    int                   m_meshTris  = 0;
};

void TestMeshImport::initTestCase()
{
    m_workspace = OpenSWMMVisWorkspace::newInstance(QString(), nullptr);
    QVERIFY(m_workspace != nullptr);

    m_outRoot     = outputDir();
    m_externalDir = QDir(m_outRoot).filePath(QStringLiteral("elsewhere"));
    QVERIFY(QDir().mkpath(m_externalDir));

    // The mesh the user browses to — deliberately NOT next to any .inp.
    m_externalMesh = QDir(m_externalDir).filePath(QStringLiteral("imported.2dm"));
    QVERIFY(buildStandaloneMesh(meshFixturePath(), m_externalMesh));

    const mesh::InpMeshReadResult r = mesh::InpMeshReader::read(m_externalMesh);
    QVERIFY2(r.hasMesh, qPrintable(r.errorMsg));
    m_meshVerts = int(r.mesh.vertices.size());
    m_meshTris  = int(r.mesh.triangles.size());
    QVERIFY(m_meshVerts > 0 && m_meshTris > 0);
}

void TestMeshImport::cleanupTestCase()
{
    delete m_workspace;
    m_workspace = nullptr;
}

SWMMVisProjectWindow *TestMeshImport::openProject(const QString &subdir)
{
    const QString dir = QDir(m_outRoot).filePath(subdir);
    QDir(dir).removeRecursively();
    if (!QDir().mkpath(dir)) return nullptr;

    const QString inpPath = QDir(dir).filePath(QStringLiteral("model.inp"));
    if (!QFile::copy(projectFixturePath(), inpPath)) return nullptr;

    auto *window = new SWMMVisProjectWindow(m_workspace, inpPath, nullptr);
    QList<QString> warnings, errors;
    if (!window->loadModel(warnings, errors)) {
        qWarning().noquote() << "loadModel failed:" << errors.join(QStringLiteral("; "))
                             << "warnings:" << warnings.join(QStringLiteral("; "));
        delete window;
        return nullptr;
    }
    return window;
}

// 1. The headline case: a .2dm browsed from another folder is copied next to
//    the .inp, parsed, and adopted as the active external mesh.
void TestMeshImport::importsMeshFromOutsideTheProjectFolder()
{
    SWMMVisProjectWindow *window = openProject(QStringLiteral("import_ok"));
    QVERIFY(window != nullptr);
    QVERIFY(!window->hasChanges());

    QSignalSpy spy(window, &SWMMVisProjectWindow::meshImportFinished);
    QVERIFY(spy.isValid());

    window->importMeshFileAsync(m_externalMesh);
    if (spy.isEmpty())
        QVERIFY2(spy.wait(30000), "meshImportFinished did not fire within 30s");
    QCOMPARE(spy.count(), 1);

    const QList<QVariant> args = spy.takeFirst();
    QVERIFY2(args.at(0).toBool(), qPrintable(args.at(1).toString()));

    // Copied into the project folder (⇒ relative [2D_MESH_FILE] on save).
    const QString projectDir =
        QFileInfo(window->modelLayer()->modelFilePath()).absolutePath();
    const QString expected =
        QDir(projectDir).filePath(QStringLiteral("imported.2dm"));
    QCOMPARE(QFileInfo(args.at(2).toString()).absoluteFilePath(),
             QFileInfo(expected).absoluteFilePath());
    QVERIFY(QFileInfo::exists(expected));
    QVERIFY(QFileInfo::exists(m_externalMesh));   // source untouched

    // Adopted as THE active external mesh layer, geometry intact.
    QCOMPARE(meshLayerCount(window->canvas()), 1);
    SWMM2DMeshLayer *layer = activeMeshLayer(window->canvas());
    QVERIFY(layer != nullptr);
    QVERIFY(layer->isExternalMesh());
    QCOMPARE(layer->vertexCount(), m_meshVerts);
    QCOMPARE(layer->triangleCount(), m_meshTris);
    QCOMPARE(QFileInfo(layer->sourcePath()).absoluteFilePath(),
             QFileInfo(expected).absoluteFilePath());

    // Engine mirror + dirty flag: without either, the linkage never reaches
    // the saved .inp and the model silently runs 1D-only.
    QCOMPARE(engineOption(window->modelLayer()->engine(), "MESH_FILE"),
             QStringLiteral("imported.2dm"));
    QVERIFY(window->hasChanges());

    delete window;
}

// 2. A file that carries no [2D_VERTICES]/[2D_TRIANGLES] is rejected, and the
//    copy the import made on the way in is removed again — a failed import
//    must not leave a junk .2dm the Mesh tab would then offer as a choice.
void TestMeshImport::rejectsNonMeshFileAndRemovesTheCopy()
{
    SWMMVisProjectWindow *window = openProject(QStringLiteral("import_reject"));
    QVERIFY(window != nullptr);

    const QString bogus =
        QDir(m_externalDir).filePath(QStringLiteral("not_a_mesh.2dm"));
    QVERIFY(writeText(bogus, QStringLiteral("hello, this is not a mesh\n")));

    QSignalSpy spy(window, &SWMMVisProjectWindow::meshImportFinished);
    window->importMeshFileAsync(bogus);
    if (spy.isEmpty())
        QVERIFY2(spy.wait(30000), "meshImportFinished did not fire within 30s");
    QCOMPARE(spy.count(), 1);

    const QList<QVariant> args = spy.takeFirst();
    QVERIFY(!args.at(0).toBool());
    QVERIFY(!args.at(1).toString().isEmpty());     // names the reason
    QVERIFY(args.at(2).toString().isEmpty());

    const QString projectDir =
        QFileInfo(window->modelLayer()->modelFilePath()).absolutePath();
    QVERIFY(!QFileInfo::exists(
        QDir(projectDir).filePath(QStringLiteral("not_a_mesh.2dm"))));
    QCOMPARE(meshLayerCount(window->canvas()), 0);

    delete window;
}

// 3. Importing over an existing name prompts; "Keep Both" must uniquify the
//    copy and leave the file already in the project byte-for-byte intact
//    (it may well be the mesh the model currently runs on).
void TestMeshImport::nameCollisionKeepBothLeavesTheExistingMeshIntact()
{
    SWMMVisProjectWindow *window = openProject(QStringLiteral("import_collision"));
    QVERIFY(window != nullptr);

    // Pre-existing sibling with the same name but different content.
    const QString projectDir =
        QFileInfo(window->modelLayer()->modelFilePath()).absolutePath();
    const QString incumbent =
        QDir(projectDir).filePath(QStringLiteral("imported.2dm"));
    const QString incumbentText =
        QStringLiteral(";; the mesh already in the project — must survive\n");
    QVERIFY(writeText(incumbent, incumbentText));

    // The prompt is modal; drive it from a queued lambda.
    QTimer::singleShot(0, [] {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (!box) return;
        const auto buttons = box->buttons();
        for (QAbstractButton *b : buttons)
            if (b->text().contains(QLatin1String("Keep"))) { b->click(); return; }
    });

    QSignalSpy spy(window, &SWMMVisProjectWindow::meshImportFinished);
    window->importMeshFileAsync(m_externalMesh);
    if (spy.isEmpty())
        QVERIFY2(spy.wait(30000), "meshImportFinished did not fire within 30s");
    QCOMPARE(spy.count(), 1);

    const QList<QVariant> args = spy.takeFirst();
    QVERIFY2(args.at(0).toBool(), qPrintable(args.at(1).toString()));

    // Incumbent untouched; the import landed beside it under a new name.
    QFile f(incumbent);
    QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
    QCOMPARE(QString::fromUtf8(f.readAll()), incumbentText);

    const QString imported = args.at(2).toString();
    QCOMPARE(QFileInfo(imported).fileName(), QStringLiteral("imported_1.2dm"));
    QVERIFY(QFileInfo::exists(imported));
    QCOMPARE(engineOption(window->modelLayer()->engine(), "MESH_FILE"),
             QStringLiteral("imported_1.2dm"));

    delete window;
}


// 4. Closing a project must not leave the (leaked) mesh layer holding a freed
//    QGraphicsItem. Layers are owned by nobody — they outlive the canvas they
//    were added to — while their scene item dies with the canvas, so ~MapCanvas
//    hands each layer its item back. Without that, the deferred scene-geometry
//    build completing after the window closed crashed in
//    QGraphicsItem::prepareGeometryChange, and so did any later repaint or edit.
void TestMeshImport::closingTheProjectClearsTheMeshLayersSceneItem()
{
    SWMMVisProjectWindow *window = openProject(QStringLiteral("import_teardown"));
    QVERIFY(window != nullptr);

    QSignalSpy spy(window, &SWMMVisProjectWindow::meshImportFinished);
    window->importMeshFileAsync(m_externalMesh);
    if (spy.isEmpty())
        QVERIFY2(spy.wait(30000), "meshImportFinished did not fire within 30s");
    QVERIFY2(spy.takeFirst().at(0).toBool(), "import failed");

    SWMM2DMeshLayer *layer = activeMeshLayer(window->canvas());
    QVERIFY(layer != nullptr);

    // Deliberately do NOT wait for the deferred wireframe/spatial-index build:
    // closing mid-build is the exact sequence that used to crash.
    delete window;

    // Touching the item at all went through freed memory before the fix.
    layer->setQsgOwnsRendering(true);
    layer->setQsgOwnsRendering(false);
    QTest::qWait(500);      // let any still-running deferred build land

    // The observable proof that the pointer was cleared rather than left
    // dangling: the layer repopulates into a fresh scene. A stale non-null
    // item would take refreshScene's update() branch and add nothing (and
    // dereference freed memory on the way).
    QGraphicsScene fresh;
    layer->refreshScene(&fresh, MapExtent(), nullptr);
    QCOMPARE(fresh.items().size(), 1);

    delete layer;   // nothing else owns it
}

QTEST_MAIN(TestMeshImport)
#include "test_meshimport.moc"
