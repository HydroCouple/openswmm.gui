/*!
 * \file test_crs_inp_roundtrip.cpp
 * \brief The [OPTIONS] CRS is the model's coordinate system of record.
 *
 * Two rules, both gated here:
 *  1. On open, a CRS in the .inp wins over the .oswp sidecar's copy. The
 *     sidecar's CRS applies only when the .inp carries none.
 *  2. On save, the layer's assigned CRS is written to [OPTIONS] CRS —
 *     whichever path assigned it (CRS picker, Simulation Options page,
 *     canvas reprojection, sidecar). Two things must NOT change: a file
 *     that already declares its CRS keeps that line untouched, and a file
 *     that never had one (auto-derived local CRS) gains none.
 *
 * Measured 2026-09-12 before the fix: the canvas "Reproject" path set the
 * engine's spatial frame only, so the saved .inp declared the OLD CRS over
 * the NEW coordinates; and the sidecar restore overwrote the .inp's CRS.
 *
 * All output goes to <repo>/tests/output/crs_inp_roundtrip/, never a temp dir.
 */

#include "layers/swmmmodellayer.h"
#include "map/spatialreferencesystem.h"
#include "project/openswmmvisworkspace.h"
#include "project/projectserializer.h"
#include "swmmvisprojectwindow.h"

#include <QDir>
#include <QFile>
#include <QObject>
#include <QString>
#include <QTest>
#include <QTextStream>

namespace {

constexpr const char *kInpCrs      = "EPSG:26985";
constexpr int         kAssignedCode = 32618;   // EPSG:32618

QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
}

//! Reviewable output root: <repo>/tests/output/crs_inp_roundtrip.
QString outDir()
{
    QDir d(dataDir());              // tests/gui/data
    d.cdUp();                       // tests/gui
    d.cdUp();                       // tests
    const QString out = d.filePath(QStringLiteral("output/crs_inp_roundtrip"));
    QDir().mkpath(out);
    return out;
}

//! Minimal valid deck; \p crs empty → no CRS line at all.
bool writeDeck(const QString &path, const QString &crs)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return false;
    QTextStream ts(&f);
    ts << "[TITLE]\nCRS round-trip gate deck\n\n"
       << "[OPTIONS]\n"
       << "FLOW_UNITS           CFS\nFLOW_ROUTING         KINWAVE\n"
       << "START_DATE           01/01/2026\nSTART_TIME           00:00:00\n"
       << "END_DATE             01/01/2026\nEND_TIME             00:30:00\n"
       << "ROUTING_STEP         5\nREPORT_STEP          00:05:00\n";
    if (!crs.isEmpty())
        ts << "CRS                  " << crs << "\n";
    ts << "\n[JUNCTIONS]\nJ0     10.0 10 0.5 0 0\n\n"
       << "[OUTFALLS]\nOUT 7.0 FREE  NO\n\n"
       << "[CONDUITS]\nC1 J0 OUT 400 0.013 0 0 0\n\n"
       << "[XSECTIONS]\nC1 CIRCULAR 1.5 0 0 0\n\n"
       << "[COORDINATES]\nJ0 0 0\nOUT 400 0\n\n"
       << "[REPORT]\nINPUT NO\n";
    return true;
}

//! The CRS line inside [OPTIONS] only — [TITLE] text may itself start with "CRS".
QString crsLineInOptions(const QString &inp)
{
    QFile f(inp);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    QTextStream ts(&f);
    bool inOptions = false;
    while (!ts.atEnd()) {
        const QString line = ts.readLine();
        if (line.startsWith(QLatin1Char('['))) {
            inOptions = line.startsWith(QStringLiteral("[OPTIONS]"));
            continue;
        }
        if (inOptions && line.startsWith(QStringLiteral("CRS"))
            && line.size() > 3 && line.at(3).isSpace())
            return line.simplified();
    }
    return {};
}

//! Window on \p deckPath, model loaded; nullptr + \p why on failure.
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

QString layerAuthority(SWMMVisProjectWindow *w)
{
    const auto *srs = w->modelLayer() ? w->modelLayer()->srs() : nullptr;
    return srs ? srs->toAuthority() : QString();
}

void assignEpsg(SWMMVisProjectWindow *w, int code)
{
    auto *layer = w->modelLayer();
    layer->setSRS(SpatialReferenceSystem::fromAuthCode(QStringLiteral("EPSG"),
                                                       code, layer), true);
}

} // namespace

class TestCrsInpRoundTrip : public QObject
{
    Q_OBJECT
private slots:

    /*! Rule 1 — .inp CRS beats the sidecar's. A sidecar written while the
     *  layer sat on EPSG:32618 must NOT re-label a model whose .inp says
     *  EPSG:26985. */
    void sidecarDoesNotOverrideInpCrs()
    {
        const QString deck = outDir() + QStringLiteral("/with_crs.inp");
        const QString oswp = outDir() + QStringLiteral("/with_crs_assigned.oswp");
        QVERIFY(writeDeck(deck, QString::fromLatin1(kInpCrs)));

        QString why, err;
        // Author a sidecar carrying a DIFFERENT CRS than the .inp.
        {
            SWMMVisProjectWindow *w = openWindow(deck, &why);
            QVERIFY2(w, qPrintable(why));
            QCOMPARE(layerAuthority(w), QString::fromLatin1(kInpCrs));
            assignEpsg(w, kAssignedCode);
            QVERIFY2(ProjectSerializer::saveToFile(oswp, w, &err), qPrintable(err));
            delete w;
        }
        // Fresh open + sidecar apply: the .inp's CRS must survive.
        SWMMVisProjectWindow *w = openWindow(deck, &why);
        QVERIFY2(w, qPrintable(why));
        QVERIFY2(ProjectSerializer::applyFromFile(oswp, w, &err), qPrintable(err));
        QCOMPARE(layerAuthority(w), QString::fromLatin1(kInpCrs));
        delete w;
    }

    /*! Rule 1, other half — with NO CRS in the .inp the sidecar's copy is
     *  the only source and must still apply. */
    void sidecarAppliesWhenInpHasNoCrs()
    {
        const QString deck = outDir() + QStringLiteral("/no_crs.inp");
        const QString oswp = outDir() + QStringLiteral("/no_crs_assigned.oswp");
        QVERIFY(writeDeck(deck, QString()));

        QString why, err;
        {
            SWMMVisProjectWindow *w = openWindow(deck, &why);
            QVERIFY2(w, qPrintable(why));
            assignEpsg(w, kAssignedCode);
            QVERIFY2(ProjectSerializer::saveToFile(oswp, w, &err), qPrintable(err));
            delete w;
        }
        SWMMVisProjectWindow *w = openWindow(deck, &why);
        QVERIFY2(w, qPrintable(why));
        QVERIFY(!w->modelLayer()->crsAssigned());   // defaulted on open, not assigned
        QVERIFY2(ProjectSerializer::applyFromFile(oswp, w, &err), qPrintable(err));
        QCOMPARE(layerAuthority(w), QStringLiteral("EPSG:%1").arg(kAssignedCode));
        QVERIFY(w->modelLayer()->crsAssigned());    // the sidecar's CRS IS an assignment
        delete w;
    }

    /*! Rule 2 — an assigned CRS on a model that had none is written. This is
     *  the CRS-picker-on-open / sidecar case. */
    void assignedCrsIsWrittenWhenInpHadNone()
    {
        const QString deck  = outDir() + QStringLiteral("/no_crs_src.inp");
        const QString saved = outDir() + QStringLiteral("/no_crs_assigned_saved.inp");
        QVERIFY(writeDeck(deck, QString()));

        QString why, err;
        SWMMVisProjectWindow *w = openWindow(deck, &why);
        QVERIFY2(w, qPrintable(why));
        assignEpsg(w, kAssignedCode);
        QVERIFY2(w->saveAs(saved, &err), qPrintable(err));
        QCOMPARE(crsLineInOptions(saved),
                 QStringLiteral("CRS EPSG:%1").arg(kAssignedCode));
        delete w;
    }

    /*! Rule 2 — the canvas "Reproject" outcome: the layer's SRS changes on a
     *  model whose .inp declared another CRS. The saved file must declare the
     *  NEW one, and reopening it must read the new one back. */
    void reassignedCrsReplacesTheInpLine()
    {
        const QString deck  = outDir() + QStringLiteral("/with_crs_src.inp");
        const QString saved = outDir() + QStringLiteral("/with_crs_reassigned_saved.inp");
        QVERIFY(writeDeck(deck, QString::fromLatin1(kInpCrs)));

        QString why, err;
        {
            SWMMVisProjectWindow *w = openWindow(deck, &why);
            QVERIFY2(w, qPrintable(why));
            assignEpsg(w, kAssignedCode);
            QVERIFY2(w->saveAs(saved, &err), qPrintable(err));
            delete w;
        }
        QCOMPARE(crsLineInOptions(saved),
                 QStringLiteral("CRS EPSG:%1").arg(kAssignedCode));
        SWMMVisProjectWindow *w = openWindow(saved, &why);
        QVERIFY2(w, qPrintable(why));
        QCOMPARE(layerAuthority(w), QStringLiteral("EPSG:%1").arg(kAssignedCode));
        delete w;
    }

    /*! Must-not-change — an unedited save keeps the .inp's own CRS line. */
    void unchangedCrsIsPreservedOnSave()
    {
        const QString deck  = outDir() + QStringLiteral("/with_crs_unedited.inp");
        const QString saved = outDir() + QStringLiteral("/with_crs_unedited_saved.inp");
        QVERIFY(writeDeck(deck, QString::fromLatin1(kInpCrs)));

        QString why, err;
        SWMMVisProjectWindow *w = openWindow(deck, &why);
        QVERIFY2(w, qPrintable(why));
        QVERIFY2(w->saveAs(saved, &err), qPrintable(err));
        QCOMPARE(crsLineInOptions(saved), QStringLiteral("CRS ") + QString::fromLatin1(kInpCrs));
        delete w;
    }

    /*! Must-not-change — a model with no CRS gets a DEFAULT on open (from
     *  [MAP] UNITS, else the preferences' EPSG code), and that default must
     *  never be written back: the saved file gains no CRS line. This is the
     *  case an isLocal() guard alone would miss — the preferences default
     *  is a real EPSG code, not a local CRS. */
    void defaultedCrsIsNeverWritten()
    {
        const QString deck  = outDir() + QStringLiteral("/no_crs_default.inp");
        const QString saved = outDir() + QStringLiteral("/no_crs_default_saved.inp");
        QVERIFY(writeDeck(deck, QString()));

        QString why, err;
        SWMMVisProjectWindow *w = openWindow(deck, &why);
        QVERIFY2(w, qPrintable(why));
        QVERIFY(w->modelLayer()->srs());              // something was defaulted...
        QVERIFY(!w->modelLayer()->crsAssigned());     // ...but nothing was assigned
        QVERIFY2(w->saveAs(saved, &err), qPrintable(err));
        QCOMPARE(crsLineInOptions(saved), QString());
        delete w;
    }
};

QTEST_MAIN(TestCrsInpRoundTrip)
#include "test_crs_inp_roundtrip.moc"
