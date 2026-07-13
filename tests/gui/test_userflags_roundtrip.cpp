/*!
 * \file   test_userflags_roundtrip.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Phase 5 of docs/USER_FLAGS_UI_PLAN_2026-06-03.md — full INP
 *         round-trip fidelity from a committed fixture
 *         (tests/gui/data/userflags_fixture.inp):
 *
 *           open → verify defs/values (mixed-case names normalised,
 *           quoted strings unquoted, negative integers, %g reals) →
 *           edit via UserFlagsModel → save → reopen → verify edits and
 *           untouched values survived.
 *
 *         The saved INP lands in the gui-test data directory
 *         (userflags_phase5_out.inp) so the file is reviewable after
 *         the run, per the repo guideline on transparent test file IO.
 */
#include "ui/models/userflagsmodel.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_model.h>

#include <QDir>
#include <QFile>
#include <QObject>
#include <QTest>

using openswmmvis::ui::UserFlagsModel;
using FlagType = UserFlagsModel::FlagType;

namespace {

QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
}

SWMM_Engine openModel(const QString &path)
{
    // MUST be swmm_engine_create(), not swmm_engine_new(). The two differ by the
    // lifecycle state they leave the engine in:
    //   swmm_engine_create() -> CREATED   (the state swmm_engine_open() requires)
    //   swmm_engine_new()    -> BUILDING  (for programmatic model construction)
    // SWMMEngine::open() hard-rejects anything but CREATED/CLOSED, so
    // new() + open() can NEVER succeed — it always returns SWMM_ERR_WRONG_STATE.
    SWMM_Engine eng = swmm_engine_create();
    if (!eng) return nullptr;
    if (swmm_engine_open(eng, path.toUtf8().constData(), nullptr, nullptr,
                         nullptr) != SWMM_OK) {
        // Surface the engine's own diagnosis. Without this the failure surfaces
        // as a bare "eng != nullptr returned FALSE", which says nothing about
        // why — exactly what made this bug expensive to find in CI.
        qWarning("swmm_engine_open(%s) failed: rc=%d msg=%s",
                 qPrintable(path),
                 swmm_get_last_error(eng),
                 swmm_get_last_error_msg(eng));
        swmm_engine_destroy(eng);
        return nullptr;
    }
    return eng;
}

} // namespace

class TestUserFlagsRoundTrip : public QObject
{
    Q_OBJECT

private slots:

    void fixtureRoundTrip()
    {
        const QString fixture =
            QDir(dataDir()).filePath(QStringLiteral("userflags_fixture.inp"));
        QVERIFY2(QFile::exists(fixture),
                 "userflags_fixture.inp missing from the gui-test data dir");

        // ---- Open + verify parse fidelity --------------------------------
        SWMM_Engine eng = openModel(fixture);
        QVERIFY(eng != nullptr);
        {
            UserFlagsModel model(eng);
            const auto &defs = model.defs();
            QCOMPARE(defs.size(), 4);
            // Mixed-case fixture names are normalised to uppercase.
            QCOMPARE(defs[0].name, QStringLiteral("INSPECTED"));
            QCOMPARE(defs[0].type, FlagType::Boolean);
            QCOMPARE(defs[0].description,
                     QStringLiteral("Has the object been field-inspected?"));
            QCOMPARE(defs[1].name, QStringLiteral("PRIORITY"));
            QCOMPARE(defs[1].type, FlagType::Integer);
            QCOMPARE(defs[2].type, FlagType::Real);
            QCOMPARE(defs[3].type, FlagType::String);

            // Values — including case-insensitive lookup via the model.
            bool found = false;
            QCOMPARE(model.value(QStringLiteral("NODE"), QStringLiteral("J1"),
                                 QStringLiteral("inspected"), &found),
                     QStringLiteral("YES"));
            QVERIFY(found);
            QCOMPARE(model.value(QStringLiteral("node"), QStringLiteral("J1"),
                                 QStringLiteral("PRIORITY"), nullptr),
                     QStringLiteral("-5"));
            QCOMPARE(model.value(QStringLiteral("LINK"),
                                 QStringLiteral("C_MAIN"),
                                 QStringLiteral("ROUGHNESS_ADJ"), nullptr),
                     QStringLiteral("1e-06"));   // %g form of 1e-6
            // Quoted string with an embedded space, unquoted on parse.
            QCOMPARE(model.value(QStringLiteral("LINK"),
                                 QStringLiteral("C_MAIN"),
                                 QStringLiteral("ASSET_ID"), nullptr),
                     QStringLiteral("AM 00341"));

            // ---- Edit: clear one value, change another -------------------
            QVERIFY(model.clearValue(QStringLiteral("NODE"),
                                     QStringLiteral("J1"),
                                     QStringLiteral("INSPECTED")));
            QVERIFY(model.setValue(QStringLiteral("LINK"),
                                   QStringLiteral("C_MAIN"),
                                   QStringLiteral("ASSET_ID"),
                                   QStringLiteral("B 2")));
        }

        // ---- Save to the reviewable data dir ------------------------------
        const QString outPath =
            QDir(dataDir()).filePath(QStringLiteral("userflags_phase5_out.inp"));
        QVERIFY(swmm_model_write(eng, outPath.toUtf8().constData()) == SWMM_OK);
        swmm_engine_destroy(eng);
        QVERIFY(QFile::exists(outPath));

        // ---- Reopen + verify the edits and the untouched values ----------
        SWMM_Engine eng2 = openModel(outPath);
        QVERIFY(eng2 != nullptr);
        {
            UserFlagsModel model(eng2);
            QCOMPARE(model.defs().size(), 4);

            // Cleared value stayed cleared (absent from [USER_FLAG_VALUES]).
            bool found = true;
            model.value(QStringLiteral("NODE"), QStringLiteral("J1"),
                        QStringLiteral("INSPECTED"), &found);
            QVERIFY(!found);

            // Edited value survived (quoted on write, unquoted on parse).
            QCOMPARE(model.value(QStringLiteral("LINK"),
                                 QStringLiteral("C_MAIN"),
                                 QStringLiteral("ASSET_ID"), nullptr),
                     QStringLiteral("B 2"));

            // Untouched values survived verbatim.
            QCOMPARE(model.value(QStringLiteral("NODE"), QStringLiteral("J1"),
                                 QStringLiteral("PRIORITY"), nullptr),
                     QStringLiteral("-5"));
            QCOMPARE(model.value(QStringLiteral("LINK"),
                                 QStringLiteral("C_MAIN"),
                                 QStringLiteral("ROUGHNESS_ADJ"), nullptr),
                     QStringLiteral("1e-06"));
        }
        swmm_engine_destroy(eng2);
    }
};

QTEST_APPLESS_MAIN(TestUserFlagsRoundTrip)
#include "test_userflags_roundtrip.moc"
