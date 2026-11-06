/**
 * @file      test_meshimport.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Coverage for SWMMVisProjectWindow::importMeshFileAsync() — the
 *         browse-and-load path for an existing SWMMVis 2D mesh (.2dm).
 *
 *         Imports are drafts: snapshot/parse/preview leave original inputs,
 *         saved destinations and engine mesh references unchanged. Save
 *         publishes the chosen mesh, and stale/closed/discarded jobs release
 *         their private snapshots without publishing any files.
 *
 *         Everything the test writes lands under tests/output/mesh_import/
 *         for review (CLAUDE.md §4.1), never in a temp dir.
 *
 *         QTEST_MAIN (not APPLESS): a real SWMMVisProjectWindow is a QWidget.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "layers/swmm2dmeshlayer.h"
#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "map/mapextent.h"
#include "mesh/inpmeshreader.h"
#include "project/openswmmvisworkspace.h"
#include "project/generatedmeshartifacts.h"
#include "swmmvisprojectwindow.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_2d.h>
#include <openswmm/engine/openswmm_model.h>

#include <QAbstractButton>
#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsScene>
#include <QMessageBox>
#include <QObject>
#include <QSignalSpy>
#include <QScopedPointer>
#include <QTest>
#include <QTimer>
#include <QThreadPool>
#include <QSemaphore>
#include <QScopeGuard>
#include <QtConcurrent/QtConcurrentRun>

namespace
{

/**
 * @brief Resolve the read-only GUI fixture directory.
 * @return Owned SWMMVIS_GUI_TEST_DATA value or dot, no existence check or IO.
 */
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

//! Reviewable output root: repository/tests/output/mesh_import (never a temp dir).
/**
 * @brief Create the persistent root for per-scenario mesh-import review files.
 * @return SWMMVIS_MESH_IMPORT_TEST_OUTPUT when nonempty, otherwise two parents above dataDir
 * plus output/mesh_import. Attempts mkpath without reporting failure; files persist for
 * inspection. Does not remove prior scenarios itself; openProject recreates only its named
 * subdirectory.
 */
QString outputDir()
{
    const QString configured = qEnvironmentVariable("SWMMVIS_MESH_IMPORT_TEST_OUTPUT");
    if (!configured.isEmpty()) {
        QDir().mkpath(configured);
        return configured;
    }
    QDir d(dataDir()); // tests/gui/data
    d.cdUp();          // tests/gui
    d.cdUp();          // tests
    const QString out = d.filePath(QStringLiteral("output/mesh_import"));
    QDir().mkpath(out);
    return out;
}

/**
 * @brief Replace a scenario-owned file with UTF8 text.
 * @param[in] path Writable destination whose parent already exists; existing file truncated.
 * @param[in] text Stable payload, preserved while encoded/written synchronously.
 * @return True if open succeeds and write returns >0, even for a partial write; false on open
 * failure, empty payload or zero/error write. Flush errors not checked, file may be partial on
 * failure. Closes handle locally, no caller reference retained.
 */
bool writeText(const QString &path, const QString &text)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return false;
    return f.write(text.toUtf8()) > 0;
}

//! Lift the fixture's inline [2D_VERTICES] / [2D_TRIANGLES] into a standalone
//! .2dm — exactly the shape InpMeshWriter::writeExternal emits.
/**
 * @brief Copy inline vertex/triangle sections into a standalone SI mesh fixture.
 * @param[in] inpPath Read-only UTF8 source deck, trusted fixture layout.
 * @param[in] meshPath Writable scenario-owned output, replaced through writeText.
 * @return False on input open or failed/nonpositive output write; true does not validate a mesh
 * exists or that all bytes were written. Adds SI header, preserves selected section lines,
 * ignores other sections. No input mutation, handle retention or native model allocation.
 */
bool buildStandaloneMesh(const QString &inpPath, const QString &meshPath)
{
    QFile in(inpPath);
    if (!in.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    const QStringList lines =
        QString::fromUtf8(in.readAll()).split(QChar('\n'), Qt::KeepEmptyParts);

    QString out = QStringLiteral(";; UNITS: SI (m)\n"
                                 ";; Standalone mesh for test_meshimport\n\n");
    bool keep = false;
    for (const QString &raw : lines) {
        const QString t = raw.trimmed();
        if (t.startsWith(QChar('[')) && t.endsWith(QChar(']')))
            keep = (t.compare(QStringLiteral("[2D_VERTICES]"), Qt::CaseInsensitive) == 0 ||
                    t.compare(QStringLiteral("[2D_TRIANGLES]"), Qt::CaseInsensitive) == 0);
        if (keep)
            out += raw + QChar('\n');
    }
    return writeText(meshPath, out);
}

/**
 * @brief Read all available raw fixture bytes for preservation assertions.
 * @param[in] path Read-only file path, borrowed synchronously.
 * @return Owning bytes, empty on open failure or empty file; partial read/error not
 * distinguished. File closes locally, no mutation.
 */
QByteArray readBytes(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

/**
 * @brief Construct a minimal native SMS text mesh for import conversion tests.
 * @return Owned MESH2D payload with one E 3 T and three ND rows, 10-unit right
 * triangle/elevations 1/2/3; no file creation or parser validation in this helper.
 */
QString smsMesh()
{
    return QStringLiteral("MESH2D\nE3T 1 1 2 3 1\n"
                          "ND 1 0 0 1\nND 2 10 0 2\nND 3 0 10 3\n");
}

/**
 * @brief Queue one GUI callback to choose the upcoming import collision dialog's Overwrite
 * action.
 * @note Requires a QMessageBox to be active when the zero-delay callback runs; otherwise no
 * action. Finds button by literal text fragment, no wait/retry or success result; GUI event loop
 * required. Authorizes only the synthetic modal prompt exercised by caller, no file write
 * itself.
 */
void chooseOverwrite()
{
    QTimer::singleShot(0, [] {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (!box)
            return;
        for (QAbstractButton *button : box->buttons())
            if (button->text().contains(QLatin1String("Overwrite"))) {
                button->click();
                return;
            }
    });
}

/**
 * @brief Read a named extended native-engine option into a bounded local buffer.
 * @param[in] engine Live initialized engine handle, borrowed for synchronous read.
 * @param[in] key Required nonnull option-key C string accepted by swmm_options_get_ext.
 * @return UTF8 option text on status zero, empty on API failure or an empty option; 512-byte
 * buffer. No ownership transfer or engine option changes, errors not propagated separately.
 */
QString engineOption(SWMM_Engine engine, const char *key)
{
    char buf[512] = {0};
    if (swmm_options_get_ext(engine, key, buf, int(sizeof(buf))) != 0)
        return QString();
    return QString::fromUtf8(buf);
}

/**
 * @brief Find the first active mesh layer on a borrowed canvas.
 * @param[in] canvas Optional live canvas, null returns null; GUI-thread inspection.
 * @return Borrowed SWMM2DMeshLayer pointer or null; lifetime remains with existing application
 * ownership, no layer adoption, allocation or ownership extension.
 */
SWMM2DMeshLayer *activeMeshLayer(MapCanvas *canvas)
{
    if (!canvas)
        return nullptr;
    for (OpenSWMMVisLayer *l : canvas->layers())
        if (auto *m = qobject_cast<SWMM2DMeshLayer *>(l))
            if (m->isActiveMesh())
                return m;
    return nullptr;
}

/**
 * @brief Count mesh layers currently attached to a canvas.
 * @param[in] canvas Optional live GUI-thread canvas; null gives zero.
 * @return Number of layers dynamically matching SWMM2DMeshLayer, including inactive meshes; no
 * mutation or ownership transfer.
 */
int meshLayerCount(MapCanvas *canvas)
{
    int n = 0;
    if (canvas)
        for (OpenSWMMVisLayer *l : canvas->layers())
            if (qobject_cast<SWMM2DMeshLayer *>(l))
                ++n;
    return n;
}

} // namespace

class TestMeshImport : public QObject
{
    Q_OBJECT

private slots:
    /**
     * @brief Create the workspace and standalone external mesh used by import scenarios.
     * @note Creates output/elsewhere, replaces imported.2dm from read-only mesh fixture, parses
     * expected positive vertex/cell counts. Owns workspace until cleanup, native/GIS
     * initialization may occur.
     */
    void initTestCase();
    /**
     * @brief Delete the shared import-test workspace and clear its stored pointer.
     * @note Scenario files persist for review; each slot owns/closes its project window
     * separately.
     */
    void cleanupTestCase();

    /**
     * @brief Verify an external import previews geometry immediately and publishes beside the
     * model only on Save.
     * @note Waits up to 30 seconds, one success signal, matching SI geometry and logical source
     * path; before save no destination/engine MESH_FILE change, dirty true. Save creates
     * mesh/reference and clears changes. Recreates import_ok scenario, leaves read-only external
     * mesh intact, deletes window.
     */
    void importsMeshFromOutsideTheProjectFolder();
    /**
     * @brief Verify invalid mesh text yields one failed completion and no adopted layer or
     * destination copy.
     * @note Writes scenario bogus input, waits up to 30 seconds, requires reason/empty returned
     * path, zero mesh layers. Does not publish or alter model engine mesh reference.
     */
    void rejectsNonMeshFileAndRemovesTheCopy();
    /**
     * @brief Verify Keep Both resolves destination collision to a unique name and preserves
     * incumbent bytes.
     * @note Queued GUI click selects Keep, preview chooses imported_1.2dm without publishing;
     * Save creates that mesh and native reference, original sibling content exact. Uses
     * recreated import_collision folder, modal/event environment required, no source mesh
     * changes.
     */
    void nameCollisionKeepBothLeavesTheExistingMeshIntact();
    /**
     * @brief Verify closing during deferred mesh geometry work clears scene ownership without
     * stale item access.
     * @note Imports then deletes window without waiting for heavy geometry; retained layer
     * toggles render ownership, processes 500 ms and repopulates exactly one item into a fresh
     * scene, then explicitly deleted. Guards asynchronous teardown/use-after-free; scenario
     * draft should not be saved.
     */
    void closingTheProjectClearsTheMeshLayersSceneItem();
    /**
     * @brief Register external native and same-folder SMS draft import cases.
     * @note Conversion of same-folder SMS requires a separate imported destination rather than
     * source overwrite.
     */
    void importRemainsDraftUntilSave_data();
    /**
     * @brief Verify import preview changes only draft state until Save, preserving
     * source/model/native mesh option.
     * @note Two source kinds, 30-second completion, exact pre-save file existence/bytes/options;
     * same-folder SMS chooses source_sms_imported.2dm. Two saves retain source bytes and
     * expected geometry, scenario outputs persist.
     */
    void importRemainsDraftUntilSave();
    /**
     * @brief Verify approving overwrite does not publish malformed source over a valid
     * incumbent.
     * @note Queues Overwrite, rejects invalid import with one signal within 30 seconds, exact
     * existing mesh, model/native option retained, no adopted mesh or dirty flag. Only
     * test-owned fixture setup writes files.
     */
    void invalidOverwriteKeepsExistingMesh();
    /**
     * @brief Verify closing an unsaved imported preview releases its last artifact owner and
     * removes staging only.
     * @note Captures weak ownership/staging path, resets window, requires owner
     * expired/directory absent; model/source exact and destination absent. No Save invoked,
     * review source/model copies retained.
     */
    void discardImportedMeshPreservesFiles();
    /**
     * @brief Verify replacing an inline mesh stays draft until Save then survives repeated save
     * and native reopen.
     * @note Original native vertex count remains during three-vertex SMS preview; saved external
     * mesh has three vertices/one cell, repeated bytes identical, no staging path embedded in
     * model, reopened engine counts three. Source SMS exact, staged review model/mesh
     * intentionally replaced.
     */
    void replacementSurvivesSaveRepeatAndReopen();
    /**
     * @brief Register backed and truly pathless untitled projects for import Save As rebasing.
     * @note Both retain a loaded fixture engine and resolved CRS; raw model path differs.
     */
    void untitledImportRebasesWithoutOverwriting_data();
    /**
     * @brief Verify failed Save As retains the import draft and a later successful rebase avoids
     * existing mesh collision.
     * @note Directory-blocked model target forces failure after destination selection; logical
     * paths/artifacts restored, original model/source/incumbent exact. Successful saved.inp
     * chooses unique target and repeated save preserves incumbent. Pathless stage parent must
     * equal configured review TMPDIR; all scenario IO persists.
     */
    void untitledImportRebasesWithoutOverwriting();
    /**
     * @brief Verify superseding a completed worker before queued GUI adoption rejects and
     * releases its snapshot.
     * @note Waits global pool up to 30 seconds, increments import serial, expects one failed
     * signal/reason, no mesh/dirty/native option/file changes and eventual empty generation
     * directories. Isolated pool/event loop required.
     */
    void staleQueuedImportCannotAdopt();
    /**
     * @brief Verify destroying the window before queued import adoption drops completion without
     * publishing.
     * @note Waits worker up to 30 seconds while callback remains queued, deletes owner/processes
     * events, no completion emitted, staging eventually gone, source/model exact and destination
     * absent.
     */
    void deletedOwnerDropsQueuedImport();
    /**
     * @brief Register native mesh, initial-quality, groundwater-quality and output references
     * unsafe to import verbatim.
     * @note Includes BOM/whitespace-wrapped mesh-reference header to cover lexical
     * normalization.
     */
    void rejectsReferencesThatCannotBeRebased_data();
    /**
     * @brief Verify unsupported relative companion references refuse import without changing
     * source, project or native state.
     * @note Five payload variants, one failure/reason within 30 seconds, no layers/dirty
     * flag/destination, and eventual staging cleanup. Scenario-owned candidate text persists for
     * review.
     */
    void rejectsReferencesThatCannotBeRebased();
    /**
     * @brief Verify destination edits after overwrite approval invalidate the queued import
     * instead of clobbering newer bytes.
     * @note One-worker semaphore gate controls dispatch, overwrites test destination while
     * import queued; failed signal/reason, newer bytes/model retained, no adopted layer or dirty
     * change. Scope guard releases blocker, waits and restores prior global thread count even
     * after assertion returns; requires isolated GUI test.
     */
    void queuedImportRejectsChangedDestination();
    /**
     * @brief Verify import and repeated saves preserve opaque vendor sections and comments in
     * native mesh text.
     * @note Adds VENDOR_EXTENSION payload to test source, imports/saves twice, destination still
     * contains exact foreign block and original source bytes unchanged. Model/mesh outputs
     * confined to recreated scenario.
     */
    void preservesForeignNativeSections();
    /**
     * @brief Register unchanged and externally edited same-folder native mesh sources.
     * @note Both previews use logical original source path while keeping private captured
     * artifacts.
     */
    void sameFolderNativeUsesSnapshot_data();
    /**
     * @brief Verify same-folder native imports use a draft snapshot and refuse Save after
     * external source changes.
     * @note Pre-save source/model bytes exact; unchanged case saves twice/clears dirty and
     * preserves geometry. Changed case keeps external edit/model original, returns error and
     * retains dirty draft/artifacts for retry. No source overwrite is permitted after its
     * captured destination identity changes.
     */
    void sameFolderNativeUsesSnapshot();

private:
    /*! Fresh project folder + loaded window for one test. Each test gets its
     *  own subfolder so the artefacts stay individually reviewable. */
    /**
     * @brief Recreate a scenario directory and load an independent project window from a source
     * fixture.
     * @param[in] subdir Safe single scenario-relative directory under m_outRoot; existing
     * directory recursively removed.
     * @param[in] fixture Optional read-only model path; empty selects projectFixturePath.
     * @return Newly allocated unparented window on successful load; caller owns and must delete
     * it. Returns null on directory/copy/load failure, may leave scenario files; failed window
     * is deleted, load diagnostics logged. Requires initialized shared workspace and GUI thread,
     * never rewrites source fixture.
     */
    SWMMVisProjectWindow *openProject(const QString &subdir, const QString &fixture = QString());

    OpenSWMMVisWorkspace *m_workspace = nullptr;
    QString m_outRoot;
    QString m_externalDir; ///< where the "browsed for" .2dm lives
    QString m_externalMesh;
    int m_meshVerts = 0;
    int m_meshTris = 0;
};

void TestMeshImport::initTestCase()
{
    m_workspace = OpenSWMMVisWorkspace::newInstance(QString(), nullptr);
    QVERIFY(m_workspace != nullptr);

    m_outRoot = outputDir();
    m_externalDir = QDir(m_outRoot).filePath(QStringLiteral("elsewhere"));
    QVERIFY(QDir().mkpath(m_externalDir));

    // The mesh the user browses to — deliberately NOT next to any .inp.
    m_externalMesh = QDir(m_externalDir).filePath(QStringLiteral("imported.2dm"));
    QVERIFY(buildStandaloneMesh(meshFixturePath(), m_externalMesh));

    const mesh::InpMeshReadResult r = mesh::InpMeshReader::read(m_externalMesh);
    QVERIFY2(r.hasMesh, qPrintable(r.errorMsg));
    m_meshVerts = int(r.mesh.vertices.size());
    m_meshTris = int(r.mesh.triangles.size());
    QVERIFY(m_meshVerts > 0 && m_meshTris > 0);
}

void TestMeshImport::cleanupTestCase()
{
    delete m_workspace;
    m_workspace = nullptr;
}

SWMMVisProjectWindow *TestMeshImport::openProject(const QString &subdir, const QString &fixture)
{
    const QString dir = QDir(m_outRoot).filePath(subdir);
    QDir(dir).removeRecursively();
    if (!QDir().mkpath(dir))
        return nullptr;

    const QString inpPath = QDir(dir).filePath(QStringLiteral("model.inp"));
    if (!QFile::copy(fixture.isEmpty() ? projectFixturePath() : fixture, inpPath))
        return nullptr;

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

// 1. The external mesh is previewed, then published beside the model on Save.
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

    // Logical destination is in the project folder; publication waits for Save.
    const QString projectDir = QFileInfo(window->modelLayer()->modelFilePath()).absolutePath();
    const QString expected = QDir(projectDir).filePath(QStringLiteral("imported.2dm"));
    QCOMPARE(QFileInfo(args.at(2).toString()).absoluteFilePath(),
             QFileInfo(expected).absoluteFilePath());
    QVERIFY(!QFileInfo::exists(expected));
    QVERIFY(QFileInfo::exists(m_externalMesh)); // source untouched

    // Adopted as THE active external mesh layer, geometry intact.
    QCOMPARE(meshLayerCount(window->canvas()), 1);
    SWMM2DMeshLayer *layer = activeMeshLayer(window->canvas());
    QVERIFY(layer != nullptr);
    QVERIFY(layer->isExternalMesh());
    QCOMPARE(layer->vertexCount(), m_meshVerts);
    QCOMPARE(layer->triangleCount(), m_meshTris);
    QVERIFY(layer->meshUnitsSI());
    QCOMPARE(QFileInfo(layer->sourcePath()).absoluteFilePath(),
             QFileInfo(expected).absoluteFilePath());

    QVERIFY(engineOption(window->modelLayer()->engine(), "MESH_FILE").isEmpty());
    QVERIFY(window->hasChanges());
    QString error;
    QVERIFY2(window->save(&error), qPrintable(error));
    QVERIFY(QFileInfo::exists(expected));
    QCOMPARE(engineOption(window->modelLayer()->engine(), "MESH_FILE"),
             QFileInfo(expected).absoluteFilePath());
    QVERIFY(readBytes(window->modelLayer()->modelFilePath()).contains("\nFILE  imported.2dm\n"));
    QVERIFY(!window->hasChanges());

    delete window;
}

// 2. A file that carries no [2D_VERTICES]/[2D_TRIANGLES] is rejected, and the
//    copy the import made on the way in is removed again — a failed import
//    must not leave a junk .2dm the Mesh tab would then offer as a choice.
void TestMeshImport::rejectsNonMeshFileAndRemovesTheCopy()
{
    SWMMVisProjectWindow *window = openProject(QStringLiteral("import_reject"));
    QVERIFY(window != nullptr);

    const QString bogus = QDir(m_externalDir).filePath(QStringLiteral("not_a_mesh.2dm"));
    QVERIFY(writeText(bogus, QStringLiteral("hello, this is not a mesh\n")));

    QSignalSpy spy(window, &SWMMVisProjectWindow::meshImportFinished);
    window->importMeshFileAsync(bogus);
    if (spy.isEmpty())
        QVERIFY2(spy.wait(30000), "meshImportFinished did not fire within 30s");
    QCOMPARE(spy.count(), 1);

    const QList<QVariant> args = spy.takeFirst();
    QVERIFY(!args.at(0).toBool());
    QVERIFY(!args.at(1).toString().isEmpty()); // names the reason
    QVERIFY(args.at(2).toString().isEmpty());

    const QString projectDir = QFileInfo(window->modelLayer()->modelFilePath()).absolutePath();
    QVERIFY(!QFileInfo::exists(QDir(projectDir).filePath(QStringLiteral("not_a_mesh.2dm"))));
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
    const QString projectDir = QFileInfo(window->modelLayer()->modelFilePath()).absolutePath();
    const QString incumbent = QDir(projectDir).filePath(QStringLiteral("imported.2dm"));
    const QString incumbentText =
        QStringLiteral(";; the mesh already in the project — must survive\n");
    QVERIFY(writeText(incumbent, incumbentText));

    // The prompt is modal; drive it from a queued lambda.
    QTimer::singleShot(0, [] {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (!box)
            return;
        const auto buttons = box->buttons();
        for (QAbstractButton *b : buttons)
            if (b->text().contains(QLatin1String("Keep"))) {
                b->click();
                return;
            }
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
    QVERIFY(!QFileInfo::exists(imported));
    QVERIFY(engineOption(window->modelLayer()->engine(), "MESH_FILE").isEmpty());
    QString error;
    QVERIFY2(window->save(&error), qPrintable(error));
    QVERIFY(QFileInfo::exists(imported));
    QCOMPARE(readBytes(incumbent), incumbentText.toUtf8());
    QCOMPARE(engineOption(window->modelLayer()->engine(), "MESH_FILE"),
             QFileInfo(imported).absoluteFilePath());
    QVERIFY(readBytes(window->modelLayer()->modelFilePath()).contains("\nFILE  imported_1.2dm\n"));

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
    QTest::qWait(500); // let any still-running deferred build land

    // The observable proof that the pointer was cleared rather than left
    // dangling: the layer repopulates into a fresh scene. A stale non-null
    // item would take refreshScene's update() branch and add nothing (and
    // dereference freed memory on the way).
    QGraphicsScene fresh;
    layer->refreshScene(&fresh, MapExtent(), nullptr);
    QCOMPARE(fresh.items().size(), 1);

    delete layer; // nothing else owns it
}

// Phase 24: adopting a preview is not a filesystem or engine commit.
void TestMeshImport::importRemainsDraftUntilSave_data()
{
    QTest::addColumn<bool>("sameFolderSms");
    QTest::newRow("external-swmm") << false;
    QTest::newRow("same-folder-sms") << true;
}

void TestMeshImport::importRemainsDraftUntilSave()
{
    QFETCH(bool, sameFolderSms);
    QScopedPointer<SWMMVisProjectWindow> window(openProject(
        sameFolderSms ? QStringLiteral("draft_sms") : QStringLiteral("draft_external")));
    QVERIFY(window);
    const QString model = window->modelLayer()->modelFilePath();
    const QDir project(QFileInfo(model).absolutePath());
    const QString source =
        sameFolderSms ? project.filePath(QStringLiteral("source_sms.2dm")) : m_externalMesh;
    if (sameFolderSms)
        QVERIFY(writeText(source, smsMesh()));
    const QByteArray sourceBefore = readBytes(source);
    const QByteArray modelBefore = readBytes(model);
    const QString optionBefore = engineOption(window->modelLayer()->engine(), "MESH_FILE");
    const QString intended = project.filePath(QFileInfo(source).fileName());
    const QByteArray intendedBefore = readBytes(intended);
    const bool intendedExisted = QFileInfo::exists(intended);
    QSignalSpy spy(window.data(), &SWMMVisProjectWindow::meshImportFinished);
    window->importMeshFileAsync(source);
    if (spy.isEmpty())
        QVERIFY(spy.wait(30000));
    QCOMPARE(spy.size(), 1);
    QVERIFY2(spy.first().at(0).toBool(), qPrintable(spy.first().at(1).toString()));
    QVERIFY(activeMeshLayer(window->canvas()));
    QVERIFY(window->hasChanges());
    QCOMPARE(readBytes(source), sourceBefore);
    QCOMPARE(readBytes(model), modelBefore);
    QCOMPARE(QFileInfo::exists(intended), intendedExisted);
    QCOMPARE(readBytes(intended), intendedBefore);
    QCOMPARE(engineOption(window->modelLayer()->engine(), "MESH_FILE"), optionBefore);
    const QString savedMesh = activeMeshLayer(window->canvas())->sourcePath();
    if (sameFolderSms) {
        QVERIFY(savedMesh != source);
        QCOMPARE(QFileInfo(savedMesh).fileName(), QStringLiteral("source_sms_imported.2dm"));
    }
    QString error;
    QVERIFY2(window->save(&error), qPrintable(error));
    const auto saved = mesh::InpMeshReader::read(savedMesh);
    QVERIFY2(saved.hasMesh, qPrintable(saved.errorMsg));
    QCOMPARE(int(saved.mesh.vertices.size()), sameFolderSms ? 3 : m_meshVerts);
    QCOMPARE(readBytes(source), sourceBefore);
    QVERIFY2(window->save(&error), qPrintable(error));
    QCOMPARE(readBytes(source), sourceBefore);
}

void TestMeshImport::invalidOverwriteKeepsExistingMesh()
{
    QScopedPointer<SWMMVisProjectWindow> window(openProject(QStringLiteral("invalid_overwrite")));
    QVERIFY(window);
    const QString model = window->modelLayer()->modelFilePath();
    const QString source = QDir(m_externalDir).filePath(QStringLiteral("invalid_collision.2dm"));
    const QString destination =
        QDir(QFileInfo(model).absolutePath()).filePath(QFileInfo(source).fileName());
    QVERIFY(writeText(source, QStringLiteral("not a mesh\n")));
    QVERIFY(buildStandaloneMesh(meshFixturePath(), destination));
    const QByteArray before = readBytes(destination);
    const QByteArray modelBefore = readBytes(model);
    const QString optionBefore = engineOption(window->modelLayer()->engine(), "MESH_FILE");
    chooseOverwrite();
    QSignalSpy spy(window.data(), &SWMMVisProjectWindow::meshImportFinished);
    window->importMeshFileAsync(source);
    if (spy.isEmpty())
        QVERIFY(spy.wait(30000));
    QCOMPARE(spy.size(), 1);
    QVERIFY(!spy.first().at(0).toBool());
    QCOMPARE(readBytes(destination), before);
    QCOMPARE(readBytes(model), modelBefore);
    QCOMPARE(engineOption(window->modelLayer()->engine(), "MESH_FILE"), optionBefore);
    QCOMPARE(meshLayerCount(window->canvas()), 0);
    QVERIFY(!window->hasChanges());
}

void TestMeshImport::discardImportedMeshPreservesFiles()
{
    QScopedPointer<SWMMVisProjectWindow> window(openProject(QStringLiteral("discard_import")));
    QVERIFY(window);
    const QString model = window->modelLayer()->modelFilePath();
    const QString destination =
        QDir(QFileInfo(model).absolutePath()).filePath(QFileInfo(m_externalMesh).fileName());
    const QByteArray modelBefore = readBytes(model);
    const QByteArray sourceBefore = readBytes(m_externalMesh);
    QSignalSpy spy(window.data(), &SWMMVisProjectWindow::meshImportFinished);
    window->importMeshFileAsync(m_externalMesh);
    if (spy.isEmpty())
        QVERIFY(spy.wait(30000));
    QVERIFY2(spy.first().at(0).toBool(), qPrintable(spy.first().at(1).toString()));
    auto *layer = activeMeshLayer(window->canvas());
    QVERIFY(layer && layer->generatedArtifacts());
    const QString stage = layer->generatedArtifacts()->directoryPath();
    std::weak_ptr<GeneratedMeshArtifacts> artifacts = layer->generatedArtifacts();
    QVERIFY(QFileInfo::exists(stage));
    window.reset(); // Discard the preview without invoking Save.
    QVERIFY(artifacts.expired());
    QVERIFY(!QFileInfo::exists(stage));
    QCOMPARE(readBytes(model), modelBefore);
    QCOMPARE(readBytes(m_externalMesh), sourceBefore);
    QVERIFY(!QFileInfo::exists(destination));
}

void TestMeshImport::replacementSurvivesSaveRepeatAndReopen()
{
    QScopedPointer<SWMMVisProjectWindow> window(
        openProject(QStringLiteral("replace_counts"), meshFixturePath()));
    QVERIFY(window);
    const auto original = mesh::InpMeshReader::read(window->modelLayer()->modelFilePath());
    QVERIFY2(original.hasMesh, qPrintable(original.errorMsg));
    auto *originalLayer = new SWMM2DMeshLayer(original.mesh, original.sourcePath);
    originalLayer->setActiveMesh(true);
    originalLayer->setMeshUnitsSI(true);
    window->canvas()->addLayer(originalLayer, false);
    window->attachMeshLayer(originalLayer, true);
    window->setHasChanges(false);
    int originalCount = 0;
    QCOMPARE(swmm_2d_vertex_count(window->modelLayer()->engine(), &originalCount), 0);
    QVERIFY(originalCount > 3);
    const QString model = window->modelLayer()->modelFilePath();
    const QString source = QDir(m_externalDir).filePath(QStringLiteral("replacement_sms.2dm"));
    QVERIFY(writeText(source, smsMesh()));
    const QByteArray sourceBefore = readBytes(source);
    QSignalSpy spy(window.data(), &SWMMVisProjectWindow::meshImportFinished);
    window->importMeshFileAsync(source);
    if (spy.isEmpty())
        QVERIFY(spy.wait(30000));
    QVERIFY2(spy.first().at(0).toBool(), qPrintable(spy.first().at(1).toString()));
    auto *layer = activeMeshLayer(window->canvas());
    QVERIFY(layer);
    QCOMPARE(layer->vertexCount(), 3);
    QCOMPARE(layer->triangleCount(), 1);
    int stillOriginal = 0;
    QCOMPARE(swmm_2d_vertex_count(window->modelLayer()->engine(), &stillOriginal), 0);
    QCOMPARE(stillOriginal, originalCount);
    const QString destination = layer->sourcePath();
    QVERIFY(!QFileInfo::exists(destination));
    QString error;
    QVERIFY2(window->save(&error), qPrintable(error));
    const auto first = mesh::InpMeshReader::read(destination);
    QVERIFY2(first.hasMesh, qPrintable(first.errorMsg));
    QCOMPARE(first.mesh.vertices.size(), 3);
    QCOMPARE(first.mesh.triangles.size(), 1);
    const QByteArray firstMesh = readBytes(destination);
    QVERIFY2(window->save(&error), qPrintable(error));
    QCOMPARE(readBytes(destination), firstMesh);
    QCOMPARE(readBytes(source), sourceBefore);
    QVERIFY(!readBytes(model).contains(".openswmm-generation-"));
    window.reset(new SWMMVisProjectWindow(m_workspace, model, nullptr));
    QList<QString> warnings, errors;
    QVERIFY2(window->loadModel(warnings, errors), qPrintable(errors.join(';')));
    const auto reopened = mesh::InpMeshReader::read(model);
    QVERIFY2(reopened.hasMesh, qPrintable(reopened.errorMsg));
    QCOMPARE(reopened.mesh.vertices.size(), 3);
    QCOMPARE(reopened.mesh.triangles.size(), 1);
    int reopenedCount = 0;
    QCOMPARE(swmm_2d_vertex_count(window->modelLayer()->engine(), &reopenedCount), 0);
    QCOMPARE(reopenedCount, 3);
}

void TestMeshImport::untitledImportRebasesWithoutOverwriting_data()
{
    QTest::addColumn<bool>("pathless");
    QTest::newRow("backed-untitled") << false;
    QTest::newRow("pathless-untitled") << true;
}

void TestMeshImport::untitledImportRebasesWithoutOverwriting()
{
    QFETCH(bool, pathless);
    QScopedPointer<SWMMVisProjectWindow> window(openProject(
        pathless ? QStringLiteral("untitled_pathless") : QStringLiteral("untitled_backed")));
    QVERIFY(window);
    const QString originalModel = window->modelLayer()->modelFilePath();
    const QByteArray originalModelBytes = readBytes(originalModel);
    // Keep the fixture engine and resolved CRS, but simulate a truly new
    // project's empty raw model path as initializeBlankModel supplies it.
    if (pathless)
        window->modelLayer()->setModelFilePath(QString());
    window->markUntitled();
    const QString draftModelPath = window->modelLayer()->modelFilePath();
    const QByteArray sourceBytes = readBytes(m_externalMesh);
    QSignalSpy spy(window.data(), &SWMMVisProjectWindow::meshImportFinished);
    window->importMeshFileAsync(m_externalMesh);
    if (spy.isEmpty())
        QVERIFY(spy.wait(30000));
    QVERIFY2(spy.first().at(0).toBool(), qPrintable(spy.first().at(1).toString()));
    auto *layer = activeMeshLayer(window->canvas());
    QVERIFY(layer);
    QVERIFY(layer->generatedArtifacts());
    const QString draftMeshPath = layer->sourcePath();
    const QString stagePath = layer->generatedArtifacts()->directoryPath();
    if (pathless) {
        // The runner sets TMPDIR to a reviewable artifact directory.
        QCOMPARE(QFileInfo(QFileInfo(stagePath).absolutePath()).canonicalFilePath(),
                 QFileInfo(QDir::tempPath()).canonicalFilePath());
    }
    const QDir target(QFileInfo(originalModel).absolutePath() + QStringLiteral("/saved"));
    QVERIFY(QDir().mkpath(target.path()));
    const QString incumbent = target.filePath(QFileInfo(m_externalMesh).fileName());
    const QByteArray sentinel("existing unrelated mesh must survive\n");
    QVERIFY(writeText(incumbent, QString::fromUtf8(sentinel)));
    // A directory in the model file's place forces Save As to fail after
    // selecting the new mesh destination. The draft path must be restored.
    const QString blockedModel = target.filePath(QStringLiteral("blocked.inp"));
    QVERIFY(QDir().mkpath(blockedModel));
    QString error;
    QVERIFY(!window->saveAs(blockedModel, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(layer->sourcePath(), draftMeshPath);
    QCOMPARE(window->modelLayer()->modelFilePath(), draftModelPath);
    QVERIFY(layer->importNeedsSaveAsRebase());
    QVERIFY(layer->generatedArtifacts());
    QCOMPARE(layer->generatedArtifacts()->directoryPath(), stagePath);
    QVERIFY(QFileInfo::exists(stagePath));
    QCOMPARE(readBytes(incumbent), sentinel);
    QCOMPARE(readBytes(m_externalMesh), sourceBytes);
    QCOMPARE(readBytes(originalModel), originalModelBytes);
    QCOMPARE(target.entryList({QStringLiteral("*.2dm")}, QDir::Files).size(), 1);

    const QString destinationModel = target.filePath(QStringLiteral("saved.inp"));
    QVERIFY2(window->saveAs(destinationModel, &error), qPrintable(error));
    QCOMPARE(QFileInfo(layer->sourcePath()).absolutePath(), target.absolutePath());
    QVERIFY(layer->sourcePath() != incumbent);
    const auto saved = mesh::InpMeshReader::read(layer->sourcePath());
    QVERIFY2(saved.hasMesh, qPrintable(saved.errorMsg));
    QCOMPARE(int(saved.mesh.vertices.size()), m_meshVerts);
    QCOMPARE(readBytes(incumbent), sentinel);
    QCOMPARE(readBytes(m_externalMesh), sourceBytes);
    QCOMPARE(readBytes(originalModel), originalModelBytes);
    QVERIFY2(window->save(&error), qPrintable(error));
    QCOMPARE(readBytes(incumbent), sentinel);
}

void TestMeshImport::staleQueuedImportCannotAdopt()
{
    QScopedPointer<SWMMVisProjectWindow> window(openProject(QStringLiteral("stale_import")));
    QVERIFY(window);
    const QString model = window->modelLayer()->modelFilePath();
    const QDir project(QFileInfo(model).absolutePath());
    const QByteArray before = readBytes(model);
    QSignalSpy spy(window.data(), &SWMMVisProjectWindow::meshImportFinished);
    window->importMeshFileAsync(m_externalMesh);
    // Finish computation without delivering the queued GUI adoption callback.
    QVERIFY(QThreadPool::globalInstance()->waitForDone(30000));
    QVERIFY(!project
                 .entryList({QStringLiteral(".openswmm-generation-*")},
                            QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot)
                 .isEmpty());
    ++window->mMeshImportSerial; // Existing friend seam: supersede this exact job.
    if (spy.isEmpty())
        QVERIFY(spy.wait(30000));
    QCOMPARE(spy.size(), 1);
    QVERIFY(!spy.first().at(0).toBool());
    QVERIFY(!spy.first().at(1).toString().isEmpty());
    QCOMPARE(meshLayerCount(window->canvas()), 0);
    QVERIFY(!window->hasChanges());
    QVERIFY(engineOption(window->modelLayer()->engine(), "MESH_FILE").isEmpty());
    QCOMPARE(readBytes(model), before);
    QVERIFY(!QFileInfo::exists(project.filePath(QFileInfo(m_externalMesh).fileName())));
    QTRY_VERIFY(project
                    .entryList({QStringLiteral(".openswmm-generation-*")},
                               QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot)
                    .isEmpty());
}

void TestMeshImport::deletedOwnerDropsQueuedImport()
{
    QScopedPointer<SWMMVisProjectWindow> window(openProject(QStringLiteral("deleted_owner")));
    QVERIFY(window);
    const QString model = window->modelLayer()->modelFilePath();
    const QDir project(QFileInfo(model).absolutePath());
    const QByteArray before = readBytes(model);
    const QByteArray sourceBefore = readBytes(m_externalMesh);
    QSignalSpy spy(window.data(), &SWMMVisProjectWindow::meshImportFinished);
    window->importMeshFileAsync(m_externalMesh);
    QVERIFY(QThreadPool::globalInstance()->waitForDone(30000));
    QVERIFY(spy.isEmpty()); // Completion is queued; nothing has been adopted.
    QVERIFY(!project
                 .entryList({QStringLiteral(".openswmm-generation-*")},
                            QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot)
                 .isEmpty());
    window.reset();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
    QCOMPARE(spy.size(), 0);
    QTRY_VERIFY(project
                    .entryList({QStringLiteral(".openswmm-generation-*")},
                               QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot)
                    .isEmpty());
    QCOMPARE(readBytes(model), before);
    QCOMPARE(readBytes(m_externalMesh), sourceBefore);
    QVERIFY(!QFileInfo::exists(project.filePath(QFileInfo(m_externalMesh).fileName())));
}

void TestMeshImport::rejectsReferencesThatCannotBeRebased_data()
{
    QTest::addColumn<QString>("payload");
    QTest::newRow("mesh-reference") << QStringLiteral("[2D_MESH_FILE]\nother.2dm\n");
    QTest::newRow("quality-file") << QStringLiteral("[INITIAL_QUALITY]\nFILE other.csv\n");
    QTest::newRow("groundwater-file") << QStringLiteral("[GW_INITIAL_QUALITY]\nFILE other.csv\n");
    QTest::newRow("output-file") << QStringLiteral("[2D_OPTIONS]\nOUTPUT_FILE other.out\n");
    QTest::newRow("bom-wrapper") << (QString(QChar(0xfeff)) +
                                     QStringLiteral("[ 2D_MESH_FILE ]\nother.2dm\n"));
}

void TestMeshImport::rejectsReferencesThatCannotBeRebased()
{
    QFETCH(QString, payload);
    QScopedPointer<SWMMVisProjectWindow> window(openProject(
        QStringLiteral("reject_reference_") + QString::fromLatin1(QTest::currentDataTag())));
    QVERIFY(window);
    const QString model = window->modelLayer()->modelFilePath();
    const QByteArray modelBefore = readBytes(model);
    const QString source = QDir(m_externalDir).filePath(QStringLiteral("reference_payload.2dm"));
    const QString meshText = QString::fromUtf8(readBytes(m_externalMesh));
    QVERIFY(writeText(source, payload.startsWith(QChar(0xfeff)) ? payload + '\n' + meshText
                                                                : meshText + '\n' + payload));
    const QByteArray sourceBefore = readBytes(source);
    QSignalSpy spy(window.data(), &SWMMVisProjectWindow::meshImportFinished);
    window->importMeshFileAsync(source);
    if (spy.isEmpty())
        QVERIFY(spy.wait(30000));
    QCOMPARE(spy.size(), 1);
    QVERIFY(!spy.first().at(0).toBool());
    QVERIFY(!spy.first().at(1).toString().isEmpty());
    QCOMPARE(readBytes(source), sourceBefore);
    QCOMPARE(readBytes(model), modelBefore);
    QVERIFY(!window->hasChanges());
    QCOMPARE(meshLayerCount(window->canvas()), 0);
    const QDir project(QFileInfo(model).absolutePath());
    QVERIFY(!QFileInfo::exists(project.filePath(QFileInfo(source).fileName())));
    QTRY_VERIFY(project
                    .entryList({QStringLiteral(".openswmm-generation-*")},
                               QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot)
                    .isEmpty());
}

void TestMeshImport::queuedImportRejectsChangedDestination()
{
    QScopedPointer<SWMMVisProjectWindow> window(openProject(QStringLiteral("changed_destination")));
    QVERIFY(window);
    const QString model = window->modelLayer()->modelFilePath();
    const QString destination =
        QDir(QFileInfo(model).absolutePath()).filePath(QFileInfo(m_externalMesh).fileName());
    QVERIFY(writeText(destination, QStringLiteral("approved old destination\n")));
    const QByteArray modelBefore = readBytes(model);
    // A bounded pool gate controls dispatch without relying on input size or
    // filesystem speed. The import worker starts only after the edit below.
    auto *pool = QThreadPool::globalInstance();
    QVERIFY(pool->waitForDone(30000));
    const int maxThreads = pool->maxThreadCount();
    pool->setMaxThreadCount(1);
    QSemaphore entered, release;
    auto blocker = QtConcurrent::run(pool, [&] {
        entered.release();
        release.acquire();
    });
    const auto restorePool = qScopeGuard([&] {
        release.release();
        blocker.waitForFinished();
        pool->waitForDone(30000);
        pool->setMaxThreadCount(maxThreads);
    });
    QVERIFY(entered.tryAcquire(1, 30000));
    chooseOverwrite();
    QSignalSpy spy(window.data(), &SWMMVisProjectWindow::meshImportFinished);
    window->importMeshFileAsync(m_externalMesh);
    const QByteArray newer("changed after import approval\n");
    QVERIFY(writeText(destination, QString::fromUtf8(newer)));
    release.release();
    if (spy.isEmpty())
        QVERIFY(spy.wait(30000));
    QCOMPARE(spy.size(), 1);
    QVERIFY(!spy.first().at(0).toBool());
    QVERIFY(!spy.first().at(1).toString().isEmpty());
    QCOMPARE(readBytes(destination), newer);
    QCOMPARE(readBytes(model), modelBefore);
    QCOMPARE(meshLayerCount(window->canvas()), 0);
    QVERIFY(!window->hasChanges());
}

void TestMeshImport::preservesForeignNativeSections()
{
    QScopedPointer<SWMMVisProjectWindow> window(openProject(QStringLiteral("foreign_sections")));
    QVERIFY(window);
    const QString source = QDir(m_externalDir).filePath(QStringLiteral("foreign_sections.2dm"));
    const QByteArray foreign(
        "[VENDOR_EXTENSION]\n;; retain this comment\nopaque_token 42 sentinel\n");
    QVERIFY(writeText(source, QString::fromUtf8(readBytes(m_externalMesh) + '\n' + foreign)));
    const QByteArray before = readBytes(source);
    QSignalSpy spy(window.data(), &SWMMVisProjectWindow::meshImportFinished);
    window->importMeshFileAsync(source);
    if (spy.isEmpty())
        QVERIFY(spy.wait(30000));
    QVERIFY2(spy.first().at(0).toBool(), qPrintable(spy.first().at(1).toString()));
    auto *layer = activeMeshLayer(window->canvas());
    QVERIFY(layer);
    const QString destination = layer->sourcePath();
    QString error;
    QVERIFY2(window->save(&error), qPrintable(error));
    QVERIFY(readBytes(destination).contains(foreign));
    QVERIFY2(window->save(&error), qPrintable(error));
    QVERIFY(readBytes(destination).contains(foreign));
    QCOMPARE(readBytes(source), before);
}

void TestMeshImport::sameFolderNativeUsesSnapshot_data()
{
    QTest::addColumn<bool>("changeSource");
    QTest::newRow("unchanged-save") << false;
    QTest::newRow("external-edit-rejected") << true;
}

void TestMeshImport::sameFolderNativeUsesSnapshot()
{
    QFETCH(bool, changeSource);
    QScopedPointer<SWMMVisProjectWindow> window(openProject(
        changeSource ? QStringLiteral("native_changed") : QStringLiteral("native_unchanged")));
    QVERIFY(window);
    const QString model = window->modelLayer()->modelFilePath();
    const QString source =
        QDir(QFileInfo(model).absolutePath()).filePath(QStringLiteral("native.2dm"));
    QVERIFY(buildStandaloneMesh(meshFixturePath(), source));
    const QByteArray sourceBefore = readBytes(source);
    const QByteArray modelBefore = readBytes(model);
    QSignalSpy spy(window.data(), &SWMMVisProjectWindow::meshImportFinished);
    window->importMeshFileAsync(source);
    if (spy.isEmpty())
        QVERIFY(spy.wait(30000));
    QVERIFY2(spy.first().at(0).toBool(), qPrintable(spy.first().at(1).toString()));
    auto *layer = activeMeshLayer(window->canvas());
    QVERIFY(layer);
    QCOMPARE(layer->sourcePath(), source);
    QCOMPARE(readBytes(source), sourceBefore);
    QCOMPARE(readBytes(model), modelBefore);
    QVERIFY(layer->generatedArtifacts());
    QString error;
    if (changeSource) {
        const QByteArray edited = sourceBefore + ";; changed externally after import\n";
        QVERIFY(writeText(source, QString::fromUtf8(edited)));
        QVERIFY(!window->save(&error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(readBytes(source), edited);
        QCOMPARE(readBytes(model), modelBefore);
        QVERIFY(window->hasChanges());
        QVERIFY(layer->generatedArtifacts()); // The rejected Save retains the draft.
    } else {
        QVERIFY2(window->save(&error), qPrintable(error));
        const auto saved = mesh::InpMeshReader::read(source);
        QVERIFY2(saved.hasMesh, qPrintable(saved.errorMsg));
        QCOMPARE(int(saved.mesh.vertices.size()), m_meshVerts);
        QVERIFY2(window->save(&error), qPrintable(error));
        QVERIFY(!window->hasChanges());
    }
}

QTEST_MAIN(TestMeshImport)
#include "test_meshimport.moc"
