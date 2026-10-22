/**
 * @file      test_2d_output_options_page.cpp
 * @brief     E1 (2026-09-07) — the Simulation Options 2D page Output group:
 *        OUTPUT_PRECISION / OUTPUT_COMPRESSION / REPORT_2D_STEP /
 *        REPORT_2D_VARIABLES / REPORT_2D_SPECIES read from and written to
 *        the engine through swmm_options_get_ext / set_ext.
 *
 * Contract:
 *   - A deck without the keys seeds the defaults (FLOAT32, 4, same-as-
 *     REPORT_STEP, DEFAULT mask, ALL species) and an unedited Apply writes
 *     nothing (the round-trip test covers the .inp text; this one checks the
 *     widgets).
 *   - Each widget edit lands on its key with the engine's spelling, and a
 *     deck carrying the keys hydrates the widgets.
 *   - REPORT_2D_STEP is compared in seconds ("00:10:00" vs 600) and the
 *     variables key as a mask ("DEFAULT" vs its expanded list), so a
 *     re-open + Apply after an edit is still the identity.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "layers/swmmmodellayer.h"
#include "ui/dialogs/simulationoptionsdialog.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_model.h>

#include <qcustomeditors.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QTest>

#include <memory>

namespace
{

/**
 *
 * @brief Resolve the shared 2D-output-option input fixture directory.
 * @return SWMMVIS_GUI_TEST_DATA or cwd fallback text, without existence checks,
 * normalization or directory creation. Does not modify environment/settings.
 *
 */
QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
}

/**
 *
 * @brief Join one fixture filename to the configured shared data directory.
 * @param name Borrowed relative input filename including extension.
 * @return Owned path; no open, cleanup or validity check.
 *
 */
QString fixture(const QString &name)
{
    return QDir(dataDir()).filePath(name);
}

/**
 *
 * @brief Own/load a model layer for live output-option widget tests.
 * @param path Borrowed input model filename, copied by the constructed layer.
 * @return Owned loaded model layer or null on failed load, destroying the partial
 * layer. Warning/error lists are discarded; no assertion or simulation run here.
 * Requires engine/model/GUI dependencies, and caller must outlive dialogs borrowing it.
 *
 */
std::unique_ptr<SWMMModelLayer> openLayer(const QString &path)
{
    auto layer = std::make_unique<SWMMModelLayer>(path, nullptr);
    QList<QString> warnings, errors;
    if (!layer->loadModel(warnings, errors))
        return nullptr;
    return layer;
}

/**
 *
 * @brief Read one extension option through the engine into a fixed local buffer.
 * @param e Borrowed live engine valid for option queries.
 * @param key Borrowed null-terminated option key.
 * @return Trimmed UTF-8 value or empty on getter failure; failed/empty values are
 * indistinguishable. Buffer capacity 4096 bytes bounds accepted results. No mutation.
 *
 */
QString ext(SWMM_Engine e, const char *key)
{
    char buf[4096] = {};
    if (swmm_options_get_ext(e, key, buf, sizeof(buf)) != 0)
        return {};
    return QString::fromUtf8(buf).trimmed();
}

/**
 *
 * @brief Find a typed descendant by its fixture object name.
 * @tparam W QObject-derived child type, normally a widget/editor type.
 * @param root Borrowed nonnull live QObject whose descendants are searched recursively.
 * @param name Borrowed null-terminated Latin-1 object name.
 * @return Borrowed first matching typed child or null; valid only while owned object
 * survives. No assertion, creation, ownership transfer or UI mutation.
 *
 */
template <class W> W *child(QObject *root, const char *name)
{
    W *w = root->findChild<W *>(QLatin1String(name));
    return w;
}

/**
 *
 * @brief Find the first list row containing a bracketed option token.
 * @param list Borrowed nonnull live list widget.
 * @param token Borrowed token inserted into [token] and matched case-sensitively.
 * @return Borrowed item pointer or null; list retains ownership. This is substring
 * matching on display text, not metadata equality; invalid after row removal/clear.
 *
 */
QListWidgetItem *itemByToken(QListWidget *list, const QString &token)
{
    for (int r = 0; r < list->count(); ++r)
        if (list->item(r)->text().contains(QStringLiteral("[%1]").arg(token)))
            return list->item(r);
    return nullptr;
}

} // namespace

class Test2DOutputOptionsPage : public QObject
{
    Q_OBJECT

private slots:
    /**
     *
     * @brief Load mini_2d.inp and require default output
     * precision/compression/cadence/mask/species widgets with an unedited Apply reporting no
     * writes.
     * Checks mandatory depth row flags and populated size estimate. Requires live dialog/
     * engine support; only in-memory model is touched.
     *
     */
    void defaultsSeedAndApplyIsIdentity()
    {
        auto layer = openLayer(fixture(QStringLiteral("mini_2d.inp")));
        QVERIFY(layer);
        SimulationOptionsDialog dlg(layer->engine(), layer.get(), QStringLiteral("6.0.0"), nullptr,
                                    nullptr);

        auto *prec = child<QComboBox>(&dlg, "output2DPrecisionCombo");
        auto *comp = child<QSpinBox>(&dlg, "output2DCompressionSpin");
        auto *same = child<QCheckBox>(&dlg, "report2DStepSameBox");
        auto *step = child<QCustomTimespanEdit>(&dlg, "report2DStepEdit");
        auto *vars = child<QListWidget>(&dlg, "report2DVarsList");
        auto *allSp = child<QCheckBox>(&dlg, "report2DAllSpeciesBox");
        auto *size = child<QLabel>(&dlg, "output2DSizeLabel");
        QVERIFY(prec && comp && same && step && vars && allSp && size);

        QCOMPARE(prec->currentData().toString(), QStringLiteral("FLOAT32"));
        QCOMPARE(comp->value(), 4);
        QVERIFY(same->isChecked());
        QVERIFY(!step->isEnabled());
        QCOMPARE(vars->count(), swmm_2d_output_variable_count());
        // DEFAULT preset: DEPTH VELOCITY EDGE_FLUX NODE_HEAD SPECIES RAINFALL
        // INFILTRATION ENVELOPES on; COUPLING GRADIENTS CONTINUITY off.
        QCOMPARE(itemByToken(vars, QStringLiteral("DEPTH"))->checkState(), Qt::Checked);
        QVERIFY(!(itemByToken(vars, QStringLiteral("DEPTH"))->flags() & Qt::ItemIsEnabled));
        QCOMPARE(itemByToken(vars, QStringLiteral("EDGE_FLUX"))->checkState(), Qt::Checked);
        QCOMPARE(itemByToken(vars, QStringLiteral("GRADIENTS"))->checkState(), Qt::Unchecked);
        QCOMPARE(itemByToken(vars, QStringLiteral("CONTINUITY"))->checkState(), Qt::Unchecked);
        QVERIFY(allSp->isChecked());
        // The estimate line is populated for a mesh deck.
        QVERIFY(size->text().contains(QStringLiteral("frames")));

        QVERIFY(QMetaObject::invokeMethod(&dlg, "onApply", Qt::DirectConnection));
        QVERIFY(!dlg.wroteAnyChanges());
    }

    /**
     *
     * @brief Edit precision/compression, 600-second cadence and MINIMAL mask, then require exact
     * extension keys and semantically unchanged re-Apply.
     * Constructs two dialogs over the same opened engine; no file save or simulation.
     *
     */
    void editsLandOnTheirKeys()
    {
        auto layer = openLayer(fixture(QStringLiteral("mini_2d.inp")));
        QVERIFY(layer);
        SWMM_Engine e = layer->engine();
        SimulationOptionsDialog dlg(e, layer.get(), QStringLiteral("6.0.0"), nullptr, nullptr);

        auto *prec = child<QComboBox>(&dlg, "output2DPrecisionCombo");
        auto *comp = child<QSpinBox>(&dlg, "output2DCompressionSpin");
        auto *same = child<QCheckBox>(&dlg, "report2DStepSameBox");
        auto *step = child<QCustomTimespanEdit>(&dlg, "report2DStepEdit");
        auto *vars = child<QListWidget>(&dlg, "report2DVarsList");
        auto *minimal = child<QPushButton>(&dlg, "report2DPreset_MINIMAL");
        QVERIFY(prec && comp && same && step && vars && minimal);

        prec->setCurrentIndex(prec->findData(QStringLiteral("FLOAT64")));
        comp->setValue(0);
        same->setChecked(false);
        QVERIFY(step->isEnabled());
        step->setTotalSeconds(600);
        minimal->click();
        QCOMPARE(itemByToken(vars, QStringLiteral("EDGE_FLUX"))->checkState(), Qt::Unchecked);
        QCOMPARE(itemByToken(vars, QStringLiteral("NODE_HEAD"))->checkState(), Qt::Checked);
        QCOMPARE(itemByToken(vars, QStringLiteral("ENVELOPES"))->checkState(), Qt::Checked);

        QVERIFY(QMetaObject::invokeMethod(&dlg, "onApply", Qt::DirectConnection));
        QVERIFY(dlg.wroteAnyChanges());

        QCOMPARE(ext(e, "OUTPUT_PRECISION"), QStringLiteral("FLOAT64"));
        QCOMPARE(ext(e, "OUTPUT_COMPRESSION"), QStringLiteral("0"));
        QCOMPARE(ext(e, "REPORT_2D_STEP"), QStringLiteral("00:10:00"));
        QCOMPARE(ext(e, "REPORT_2D_VARIABLES"), QStringLiteral("MINIMAL"));

        // Re-open on the edited engine: widgets hydrate from the keys and a
        // second unedited Apply is the identity (seconds vs HH:MM:SS, preset
        // vs token list are compared semantically, not textually).
        SimulationOptionsDialog dlg2(e, layer.get(), QStringLiteral("6.0.0"), nullptr, nullptr);
        auto *prec2 = child<QComboBox>(&dlg2, "output2DPrecisionCombo");
        auto *same2 = child<QCheckBox>(&dlg2, "report2DStepSameBox");
        auto *step2 = child<QCustomTimespanEdit>(&dlg2, "report2DStepEdit");
        auto *vars2 = child<QListWidget>(&dlg2, "report2DVarsList");
        QVERIFY(prec2 && same2 && step2 && vars2);
        QCOMPARE(prec2->currentData().toString(), QStringLiteral("FLOAT64"));
        QVERIFY(!same2->isChecked());
        QCOMPARE(step2->totalSeconds(), qint64(600));
        QCOMPARE(itemByToken(vars2, QStringLiteral("EDGE_FLUX"))->checkState(), Qt::Unchecked);
        QVERIFY(QMetaObject::invokeMethod(&dlg2, "onApply", Qt::DirectConnection));
        QVERIFY(!dlg2.wroteAnyChanges());
    }

    /**
     *
     * @brief Require an undeclared TSS output-filter name to survive dialog hydration/unedited
     * Apply, then switch explicitly to ALL.
     * Edits only the disposable engine extension option and widget selection.
     *
     */
    void speciesSubsetRoundTrips()
    {
        auto layer = openLayer(fixture(QStringLiteral("mini_2d.inp")));
        QVERIFY(layer);
        SWMM_Engine e = layer->engine();
        // Seed a species filter naming a pollutant the deck may not declare:
        // the dialog keeps unknown names so the round trip does not drop them.
        QCOMPARE(swmm_options_set_ext(e, "REPORT_2D_SPECIES", "TSS"), 0);

        SimulationOptionsDialog dlg(e, layer.get(), QStringLiteral("6.0.0"), nullptr, nullptr);
        auto *allSp = child<QCheckBox>(&dlg, "report2DAllSpeciesBox");
        auto *list = child<QListWidget>(&dlg, "report2DSpeciesList");
        QVERIFY(allSp && list);
        QVERIFY(!allSp->isChecked());
        QVERIFY(list->isEnabled());
        const auto hits = list->findItems(QStringLiteral("TSS"), Qt::MatchFixedString);
        QCOMPARE(hits.size(), 1);
        QCOMPARE(hits.first()->checkState(), Qt::Checked);

        QVERIFY(QMetaObject::invokeMethod(&dlg, "onApply", Qt::DirectConnection));
        QVERIFY(!dlg.wroteAnyChanges());
        QCOMPARE(ext(e, "REPORT_2D_SPECIES"), QStringLiteral("TSS"));

        // Back to ALL.
        allSp->setChecked(true);
        QVERIFY(!list->isEnabled());
        QVERIFY(QMetaObject::invokeMethod(&dlg, "onApply", Qt::DirectConnection));
        QVERIFY(dlg.wroteAnyChanges());
        QCOMPARE(ext(e, "REPORT_2D_SPECIES"), QStringLiteral("ALL"));
    }
};

QTEST_MAIN(Test2DOutputOptionsPage)
#include "test_2d_output_options_page.moc"
