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
#include "swmmvisprojectwindow.h"

#include <QDir>
#include <QFile>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTest>

namespace {

QString outDir()
{
    QDir().mkpath(QStringLiteral("test_savewarnings_output"));
    return QStringLiteral("test_savewarnings_output");
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
