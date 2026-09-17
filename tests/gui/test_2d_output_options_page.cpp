/*!
 * \file test_2d_output_options_page.cpp
 * \brief E1 (2026-09-07) — the Simulation Options 2D page Output group:
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

QString ext(SWMM_Engine e, const char *key)
{
    char buf[4096] = {};
    if (swmm_options_get_ext(e, key, buf, sizeof(buf)) != 0) return {};
    return QString::fromUtf8(buf).trimmed();
}

template <class W>
W *child(QObject *root, const char *name)
{
    W *w = root->findChild<W *>(QLatin1String(name));
    return w;
}

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
    void defaultsSeedAndApplyIsIdentity()
    {
        auto layer = openLayer(fixture(QStringLiteral("mini_2d.inp")));
        QVERIFY(layer);
        SimulationOptionsDialog dlg(layer->engine(), layer.get(), QStringLiteral("6.0.0"),
                                    nullptr, nullptr);

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
        auto *list  = child<QListWidget>(&dlg, "report2DSpeciesList");
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
