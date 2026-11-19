/**
 * @file      test_crs_inp_roundtrip.cpp
 * @brief     The [OPTIONS] CRS is the model's coordinate system of record.
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
 * All output goes to &lt;repo&gt;/tests/output/crs_inp_roundtrip/, never a temp dir.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
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

namespace
{

constexpr const char *kInpCrs = "EPSG:26985";
constexpr int kAssignedCode = 32618; // EPSG:32618

/** @brief Resolve CRS persistence fixtures.
 * @return SWMMVIS_GUI_TEST_DATA or ".". */
QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
}

//! Reviewable output root: &lt;repo&gt;/tests/output/crs_inp_roundtrip.
/** @brief Create the retained CRS evidence directory relative to test data.
 * @return output/crs_inp_roundtrip after moving two parents above the data directory; directory navigation/creation errors are unchecked. */
QString outDir()
{
    QDir d(dataDir()); // tests/gui/data
    d.cdUp();          // tests/gui
    d.cdUp();          // tests
    const QString out = d.filePath(QStringLiteral("output/crs_inp_roundtrip"));
    QDir().mkpath(out);
    return out;
}

//! Minimal valid deck; \p crs empty → no CRS line at all.
/** @brief Stream a small CFS deck with an optional OPTIONS CRS line.
 * @param[in] path Destination truncated as text; parent must already exist.
 * @param[in] crs CRS text written verbatim, or empty to omit the line.
 * @return False on file-open failure, otherwise true after streaming; write/flush failures are unchecked. */
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
/** @brief Find a CRS declaration only inside an exactly spelled OPTIONS section.
 * @param[in] inp Text input path read line by line.
 * @return First uppercase CRS line followed by whitespace, simplified, or empty when missing/open fails. Matching is case-sensitive and does not accept indented section/keyword lines. */
QString crsLineInOptions(const QString &inp)
{
    QFile f(inp);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    QTextStream ts(&f);
    bool inOptions = false;
    while (!ts.atEnd()) {
        const QString line = ts.readLine();
        if (line.startsWith(QLatin1Char('['))) {
            inOptions = line.startsWith(QStringLiteral("[OPTIONS]"));
            continue;
        }
        if (inOptions && line.startsWith(QStringLiteral("CRS")) && line.size() > 3 &&
            line.at(3).isSpace())
            return line.simplified();
    }
    return {};
}

//! Window on \p deckPath, model loaded; nullptr + \p why on failure.
/** @brief Allocate a workspace/window and synchronously open its model.
 * @param[in] deckPath Model path to load.
 * @param[out] why Required writable diagnostic assigned on workspace/load failure, unchanged on success.
 * @return Newly allocated parentless window or nullptr. Caller deletes successful windows; workspace allocation is not explicitly reclaimed by this helper and a failed window is not deleted here. */
SWMMVisProjectWindow *openWindow(const QString &deckPath, QString *why)
{
    auto *workspace = OpenSWMMVisWorkspace::newInstance(QString(), nullptr);
    if (!workspace) {
        *why = QStringLiteral("workspace null");
        return nullptr;
    }
    auto *window = new SWMMVisProjectWindow(workspace, deckPath, nullptr);
    QList<QString> warnings, errors;
    if (!window->loadModel(warnings, errors)) {
        *why = QStringLiteral("loadModel failed: %1").arg(errors.join(QLatin1String("; ")));
        return nullptr;
    }
    return window;
}

/** @brief Read the current model layer SRS authority.
 * @param[in] w Required live project window.
 * @return Authority string or empty if model/SRS is absent. */
QString layerAuthority(SWMMVisProjectWindow *w)
{
    const auto *srs = w->modelLayer() ? w->modelLayer()->srs() : nullptr;
    return srs ? srs->toAuthority() : QString();
}

/** @brief Assign an owned EPSG SRS directly to the model layer.
 * @param[in,out] w Required window with a live model layer.
 * @param[in] code EPSG code passed to fromAuthCode; creation failure is not checked.
 * The layer owns the resulting SRS; this helper does not drive the CRS picker or coordinate reprojection. */
void assignEpsg(SWMMVisProjectWindow *w, int code)
{
    auto *layer = w->modelLayer();
    layer->setSRS(SpatialReferenceSystem::fromAuthCode(QStringLiteral("EPSG"), code, layer), true);
}

} // namespace

class TestCrsInpRoundTrip : public QObject
{
    Q_OBJECT
private slots:

    /*! Rule 1 — .inp CRS beats the sidecar's. A sidecar written while the
     *  layer sat on EPSG:32618 must NOT re-label a model whose .inp says
     *  EPSG:26985. */
    /** @brief Verify applying a sidecar with a different assignment preserves the CRS declared by the input deck. */
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
    /** @brief Verify sidecar CRS becomes an explicit assignment only when the input deck declares none. */
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
        QVERIFY(!w->modelLayer()->crsAssigned()); // defaulted on open, not assigned
        QVERIFY2(ProjectSerializer::applyFromFile(oswp, w, &err), qPrintable(err));
        QCOMPARE(layerAuthority(w), QStringLiteral("EPSG:%1").arg(kAssignedCode));
        QVERIFY(w->modelLayer()->crsAssigned()); // the sidecar's CRS IS an assignment
        delete w;
    }

    /*! Rule 2 — an assigned CRS on a model that had none is written. This is
     *  the CRS-picker-on-open / sidecar case. */
    /** @brief Verify direct layer assignment writes an OPTIONS CRS line during Save As of a previously unassigned deck. */
    void assignedCrsIsWrittenWhenInpHadNone()
    {
        const QString deck = outDir() + QStringLiteral("/no_crs_src.inp");
        const QString saved = outDir() + QStringLiteral("/no_crs_assigned_saved.inp");
        QVERIFY(writeDeck(deck, QString()));

        QString why, err;
        SWMMVisProjectWindow *w = openWindow(deck, &why);
        QVERIFY2(w, qPrintable(why));
        assignEpsg(w, kAssignedCode);
        QVERIFY2(w->saveAs(saved, &err), qPrintable(err));
        QCOMPARE(crsLineInOptions(saved), QStringLiteral("CRS EPSG:%1").arg(kAssignedCode));
        delete w;
    }

    /*! Rule 2 — the canvas "Reproject" outcome: the layer's SRS changes on a
     *  model whose .inp declared another CRS. The saved file must declare the
     *  NEW one, and reopening it must read the new one back. */
    /** @brief Verify replacing the layer SRS updates the saved OPTIONS line and fresh project reload reads the new authority.
     * Exercises the final assignment state directly rather than performing a canvas reprojection. */
    void reassignedCrsReplacesTheInpLine()
    {
        const QString deck = outDir() + QStringLiteral("/with_crs_src.inp");
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
        QCOMPARE(crsLineInOptions(saved), QStringLiteral("CRS EPSG:%1").arg(kAssignedCode));
        SWMMVisProjectWindow *w = openWindow(saved, &why);
        QVERIFY2(w, qPrintable(why));
        QCOMPARE(layerAuthority(w), QStringLiteral("EPSG:%1").arg(kAssignedCode));
        delete w;
    }

    /*! Must-not-change — an unedited save keeps the .inp's own CRS line. */
    /** @brief Verify an unedited assigned CRS keeps the same simplified OPTIONS declaration after Save As. */
    void unchangedCrsIsPreservedOnSave()
    {
        const QString deck = outDir() + QStringLiteral("/with_crs_unedited.inp");
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
    /** @brief Verify an automatically defaulted, unauthored SRS does not create an OPTIONS CRS line during Save As. */
    void defaultedCrsIsNeverWritten()
    {
        const QString deck = outDir() + QStringLiteral("/no_crs_default.inp");
        const QString saved = outDir() + QStringLiteral("/no_crs_default_saved.inp");
        QVERIFY(writeDeck(deck, QString()));

        QString why, err;
        SWMMVisProjectWindow *w = openWindow(deck, &why);
        QVERIFY2(w, qPrintable(why));
        QVERIFY(w->modelLayer()->srs());          // something was defaulted...
        QVERIFY(!w->modelLayer()->crsAssigned()); // ...but nothing was assigned
        QVERIFY2(w->saveAs(saved, &err), qPrintable(err));
        QCOMPARE(crsLineInOptions(saved), QString());
        delete w;
    }
};

QTEST_MAIN(TestCrsInpRoundTrip)
#include "test_crs_inp_roundtrip.moc"
