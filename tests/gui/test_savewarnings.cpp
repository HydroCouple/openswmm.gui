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
