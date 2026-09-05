/*!
 * \file   test_simulationoptions_roundtrip.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Baseline: opening the Simulation Options dialog and pressing Apply
 *         with NO edits must write nothing.
 *
 * This is the safety net for the dialog restructure
 * (workplans/SIMULATION_OPTIONS_AND_PROCESS_GATING_PLAN_2026-09-04.md §3.4,
 * EXECUTION_PLAN_… P0/P3): every page's read()/write() pair must be the
 * identity on an unedited model. Two independent assertions per deck:
 *
 *   1. `wroteAnyChanges()` is false after Apply — the dialog's own dirty count
 *      (writeIfChanged's numeric-aware compare) saw nothing to write.
 *   2. The model serialised BEFORE the dialog existed and AFTER Apply is
 *      byte-identical in every [OPTIONS]-class section — [OPTIONS],
 *      [REPORT], [FILES], [EVENTS], [2D_OPTIONS], [TITLE] — so a write the
 *      dirty count missed (e.g. a value re-formatted to the same number)
 *      still fails here, with a readable diff of the offending section.
 *
 * Decks: a 1D DYNWAVE deck, a 2D deck, and an FV variant derived from the 1D
 * deck (there is no FV fixture yet). All outputs go next to the fixtures
 * (reviewable, CLAUDE.md §4.1).
 */
#include "layers/swmmmodellayer.h"
#include "ui/dialogs/simulationoptionsdialog.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_model.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QObject>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QTest>

#include <memory>

namespace {

QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
}

QString fixture(const QString &name) { return QDir(dataDir()).filePath(name); }

std::unique_ptr<SWMMModelLayer> openLayer(const QString &path)
{
    auto layer = std::make_unique<SWMMModelLayer>(path, nullptr);
    QList<QString> warnings, errors;
    if (!layer->loadModel(warnings, errors)) return nullptr;
    return layer;
}

QString readAll(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(f.readAll());
}

//! Sections the dialog owns — the only ones whose text must be unchanged.
//! Object sections ([JUNCTIONS] …) are deliberately excluded so an engine
//! writer quirk elsewhere cannot fail this test.
const QStringList kOwnedSections = {
    QStringLiteral("TITLE"), QStringLiteral("OPTIONS"), QStringLiteral("REPORT"),
    QStringLiteral("FILES"), QStringLiteral("EVENTS"), QStringLiteral("2D_OPTIONS"),
};

//! Section name → its normalised body (trimmed lines, comments/blank removed).
QMap<QString, QStringList> ownedSections(const QString &text)
{
    QMap<QString, QStringList> out;
    QString current;
    for (const QString &raw : text.split('\n')) {
        const QString line = raw.trimmed();
        if (line.startsWith('[')) {
            current = line.mid(1, line.indexOf(']') - 1).toUpper();
            continue;
        }
        if (current.isEmpty() || line.isEmpty() || line.startsWith(';')) continue;
        if (kOwnedSections.contains(current))
            out[current] << line.simplified();
    }
    return out;
}

//! Copy \p src to \p dst with FLOW_ROUTING rewritten (FV variant).
bool writeRoutingVariant(const QString &src, const QString &dst, const QString &routing)
{
    QString text = readAll(src);
    if (text.isEmpty()) return false;
    text.replace(QRegularExpression(QStringLiteral("^FLOW_ROUTING\\s+\\S+"),
                                    QRegularExpression::MultilineOption),
                 QStringLiteral("FLOW_ROUTING         %1").arg(routing));
    QFile f(dst);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return false;
    f.write(text.toUtf8());
    return true;
}

} // namespace

class TestSimulationOptionsRoundTrip : public QObject
{
    Q_OBJECT

private:
    //! The whole contract for one deck.
    void roundTrip(const QString &deck, const QString &stem)
    {
        auto layer = openLayer(deck);
        QVERIFY2(layer, qPrintable(QStringLiteral("cannot open %1").arg(deck)));
        SWMM_Engine e = layer->engine();
        QVERIFY(e);

        // 1. Serialise BEFORE the dialog touches anything.
        const QString before = fixture(stem + QStringLiteral("_before.inp"));
        QCOMPARE(swmm_model_write(e, before.toUtf8().constData()), 0);

        // 2. Open the dialog exactly as SWMMVis::onSimulationOptions does
        //    (no project window: notes/HTML persistence is guarded on it).
        SimulationOptionsDialog dlg(e, layer.get(), QStringLiteral("6.0.0"),
                                    /*projectWindow*/ nullptr, /*parent*/ nullptr);
        QVERIFY(!dlg.wroteAnyChanges());

        // 3. Apply with no edits. onApply is a private slot; the meta-call is
        //    how the OK/Apply buttons reach it.
        QVERIFY(QMetaObject::invokeMethod(&dlg, "onApply", Qt::DirectConnection));

        // 4a. The dialog's own dirty count saw nothing.
        QVERIFY2(!dlg.wroteAnyChanges(),
                 "Apply with no edits reported changes — some page's write() "
                 "is not the identity of its read()");

        // 4b. The owned sections are textually unchanged.
        const QString after = fixture(stem + QStringLiteral("_after.inp"));
        QCOMPARE(swmm_model_write(e, after.toUtf8().constData()), 0);
        const auto a = ownedSections(readAll(before));
        const auto b = ownedSections(readAll(after));
        for (const QString &sec : kOwnedSections) {
            const QStringList la = a.value(sec), lb = b.value(sec);
            if (la == lb) continue;
            // Readable diff: the first differing / missing lines of each side.
            QStringList onlyBefore, onlyAfter;
            for (const QString &l : la) if (!lb.contains(l)) onlyBefore << l;
            for (const QString &l : lb) if (!la.contains(l)) onlyAfter << l;
            QFAIL(qPrintable(QStringLiteral(
                "[%1] changed by an unedited Apply on %2\n  before-only: %3\n  after-only:  %4")
                .arg(sec, QFileInfo(deck).fileName(),
                     onlyBefore.mid(0, 6).join(QStringLiteral(" | ")),
                     onlyAfter.mid(0, 6).join(QStringLiteral(" | ")))));
        }
    }

private slots:
    void dynwave1D()
    {
        roundTrip(fixture(QStringLiteral("typed_selection_fixture.inp")),
                  QStringLiteral("simopts_roundtrip_dynwave"));
    }

    void twoD()
    {
        roundTrip(fixture(QStringLiteral("mini_2d.inp")),
                  QStringLiteral("simopts_roundtrip_2d"));
    }

    void finiteVolume()
    {
        const QString variant = fixture(QStringLiteral("simopts_roundtrip_fv_variant.inp"));
        QVERIFY(writeRoutingVariant(fixture(QStringLiteral("typed_selection_fixture.inp")),
                                    variant, QStringLiteral("FV")));
        roundTrip(variant, QStringLiteral("simopts_roundtrip_fv"));
    }

    //! A real edit must still be detected — guards against the test passing
    //! because writeToEngine silently writes nothing.
    void editIsDetected()
    {
        auto layer = openLayer(fixture(QStringLiteral("typed_selection_fixture.inp")));
        QVERIFY(layer);
        SimulationOptionsDialog dlg(layer->engine(), layer.get(), QStringLiteral("6.0.0"),
                                    nullptr, nullptr);
        // Flip a key on the engine behind the dialog's back, then Apply: the
        // dialog writes its (stale) widget value back and must count it.
        QCOMPARE(swmm_options_set(layer->engine(), "ROUTING_STEP", "17"), 0);
        QVERIFY(QMetaObject::invokeMethod(&dlg, "onApply", Qt::DirectConnection));
        QVERIFY(dlg.wroteAnyChanges());
    }
};

QTEST_MAIN(TestSimulationOptionsRoundTrip)
#include "test_simulationoptions_roundtrip.moc"
