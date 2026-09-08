/*!
 * \file test_processes_hub.cpp
 * \brief U1 (2026-09-07) — Simulation Options: the Transport-by-domain
 *        matrix on the Models page, the 2D page Processes group (E2 keys)
 *        and the [PROCESS_COMPONENTS] table on Files / Output / Plugins.
 *
 * Contract:
 *   - The matrix is engine-computed (swmm_get_transport_matrix): a 2D deck
 *     with one pollutant shows the 2D Pollutants cell "on (1)".
 *   - The 2D column mirrors the 2D page's TRANSPORT_* boxes both ways, and
 *     Apply writes the key; re-open shows the box unticked and a second
 *     unedited Apply is the identity.
 *   - The Processes group seeds AUTO / same-as-WET_STEP / NONE / LOST /
 *     forced-only and an unedited Apply writes nothing.
 *   - The components table stages a registration that lands on Apply and a
 *     removal that lands on Apply.
 */
#include "layers/swmmmodellayer.h"
#include "ui/dialogs/processcomponentsmodel.h"
#include "ui/dialogs/simulationoptionsdialog.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_process_components.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QTableView>
#include <QTableWidget>
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
    char buf[512] = {};
    if (swmm_options_get_ext(e, key, buf, sizeof buf) != SWMM_OK) return {};
    return QString::fromUtf8(buf).trimmed();
}

template <class W>
W *child(QObject *root, const char *name)
{
    return root->findChild<W *>(QLatin1String(name));
}

} // namespace

class TestProcessesHub : public QObject
{
    Q_OBJECT

private slots:
    void matrixIsEngineComputedAndMirrorsThe2DBoxes()
    {
        auto layer = openLayer(fixture(QStringLiteral("mini_2d.inp")));
        QVERIFY(layer);
        SWMM_Engine e = layer->engine();

        // Give the deck a pollutant so the 2D Pollutants cell is live.
        QCOMPARE(swmm_pollutant_add(e, "Cu", 0), SWMM_OK);

        SimulationOptionsDialog dlg(e, layer.get(), QStringLiteral("6.0.0"), nullptr, nullptr);
        auto *table = child<QTableWidget>(&dlg, "transportMatrixTable");
        QVERIFY(table);
        QCOMPARE(table->rowCount(), SWMM_TRANSPORT_DOMAIN_COUNT);
        QCOMPARE(table->columnCount(), SWMM_TRANSPORT_CLASS_COUNT);
        auto *cell2D = table->item(SWMM_TRANSPORT_DOMAIN_SURFACE_2D, SWMM_TRANSPORT_CLASS_POLLUTANTS);
        QVERIFY(cell2D);
        QVERIFY2(cell2D->text().startsWith(QStringLiteral("on")), qPrintable(cell2D->text()));
        QCOMPARE(cell2D->checkState(), Qt::Checked);
        // Groundwater is n/a with a reason.
        auto *gw = table->item(SWMM_TRANSPORT_DOMAIN_GROUNDWATER, SWMM_TRANSPORT_CLASS_POLLUTANTS);
        QVERIFY(gw);
        QCOMPARE(gw->text(), QStringLiteral("n/a"));
        QVERIFY(!gw->toolTip().isEmpty());

        // Untick the matrix cell → the 2D page box follows.
        auto *box = child<QCheckBox>(&dlg, "transport2DBox_TRANSPORT_POLLUTANTS");
        QVERIFY(box);
        QVERIFY(box->isChecked());
        cell2D->setCheckState(Qt::Unchecked);
        QVERIFY(!box->isChecked());
        QCOMPARE(table->item(SWMM_TRANSPORT_DOMAIN_SURFACE_2D,
                             SWMM_TRANSPORT_CLASS_POLLUTANTS)->text(),
                 QStringLiteral("off"));

        // Tick the box → the cell follows.
        box->setChecked(true);
        QCOMPARE(table->item(SWMM_TRANSPORT_DOMAIN_SURFACE_2D,
                             SWMM_TRANSPORT_CLASS_POLLUTANTS)->checkState(), Qt::Checked);

        // Apply with the box off writes TRANSPORT_POLLUTANTS NO.
        box->setChecked(false);
        QVERIFY(QMetaObject::invokeMethod(&dlg, "onApply", Qt::DirectConnection));
        QVERIFY(dlg.wroteAnyChanges());
        QCOMPARE(ext(e, "TRANSPORT_POLLUTANTS"), QStringLiteral("NO"));
        // After the re-read the engine matrix says DISABLED_BY_USER.
        QCOMPARE(table->item(SWMM_TRANSPORT_DOMAIN_SURFACE_2D,
                             SWMM_TRANSPORT_CLASS_POLLUTANTS)->text(), QStringLiteral("off"));

        SimulationOptionsDialog dlg2(e, layer.get(), QStringLiteral("6.0.0"), nullptr, nullptr);
        auto *box2 = child<QCheckBox>(&dlg2, "transport2DBox_TRANSPORT_POLLUTANTS");
        QVERIFY(box2);
        QVERIFY(!box2->isChecked());
        QVERIFY(QMetaObject::invokeMethod(&dlg2, "onApply", Qt::DirectConnection));
        QVERIFY(!dlg2.wroteAnyChanges());
    }

    void processesGroupSeedsDefaultsAndWritesEdits()
    {
        auto layer = openLayer(fixture(QStringLiteral("mini_2d.inp")));
        QVERIFY(layer);
        SWMM_Engine e = layer->engine();
        SimulationOptionsDialog dlg(e, layer.get(), QStringLiteral("6.0.0"), nullptr, nullptr);

        auto *mode   = child<QComboBox>(&dlg, "infil2DModeCombo");
        auto *same   = child<QCheckBox>(&dlg, "infil2DStepSameBox");
        auto *method = child<QComboBox>(&dlg, "infil2DMethodCombo");
        auto *dest   = child<QComboBox>(&dlg, "infil2DDestCombo");
        auto *evap   = child<QComboBox>(&dlg, "evap2DCombo");
        QVERIFY(mode && same && method && dest && evap);
        QCOMPARE(mode->currentData().toString(), QStringLiteral("AUTO"));
        QVERIFY2(mode->currentText().contains(QStringLiteral("off")),
                 "no per-cell rows in mini_2d");
        QVERIFY(same->isChecked());
        QCOMPARE(method->currentData().toString(), QStringLiteral("NONE"));
        QCOMPARE(dest->currentData().toString(), QStringLiteral("LOST"));
        QCOMPARE(evap->currentData().toString(), QStringLiteral("YES"));

        QVERIFY(QMetaObject::invokeMethod(&dlg, "onApply", Qt::DirectConnection));
        QVERIFY(!dlg.wroteAnyChanges());

        mode->setCurrentIndex(mode->findData(QStringLiteral("NO")));
        dest->setCurrentIndex(dest->findData(QStringLiteral("SUBCATCH_AQUIFER")));
        evap->setCurrentIndex(evap->findData(QStringLiteral("CLIMATE")));
        QVERIFY(QMetaObject::invokeMethod(&dlg, "onApply", Qt::DirectConnection));
        QVERIFY(dlg.wroteAnyChanges());
        QCOMPARE(ext(e, "INFILTRATION"), QStringLiteral("NO"));
        QCOMPARE(ext(e, "INFIL_DESTINATION"), QStringLiteral("SUBCATCH_AQUIFER"));
        QCOMPARE(ext(e, "EVAPORATION"), QStringLiteral("CLIMATE"));

        SimulationOptionsDialog dlg2(e, layer.get(), QStringLiteral("6.0.0"), nullptr, nullptr);
        QCOMPARE(child<QComboBox>(&dlg2, "infil2DModeCombo")->currentData().toString(),
                 QStringLiteral("NO"));
        QVERIFY(QMetaObject::invokeMethod(&dlg2, "onApply", Qt::DirectConnection));
        QVERIFY(!dlg2.wroteAnyChanges());
    }

    void componentsTableStagesRegistrationsAndRemovals()
    {
        auto layer = openLayer(fixture(QStringLiteral("typed_selection_fixture.inp")));
        QVERIFY(layer);
        SWMM_Engine e = layer->engine();
        const char *kReactions = "org.hydrocouple.openswmm.reactions";
        QCOMPARE(swmm_process_component_find(e, kReactions), -1);

        // The engine's catalogue is what the Id combo offers.
        const auto known = ProcessComponentsModel::knownIds();
        QVERIFY(known.size() >= 6);
        bool hasReactions = false;
        for (const auto &k : known) if (k.id == QLatin1String(kReactions)) hasReactions = true;
        QVERIFY(hasReactions);

        SimulationOptionsDialog dlg(e, layer.get(), QStringLiteral("6.0.0"), nullptr, nullptr);
        auto *view = child<QTableView>(&dlg, "processComponentsView");
        QVERIFY(view);
        auto *model = qobject_cast<ProcessComponentsModel *>(view->model());
        QVERIFY(model);
        QCOMPARE(model->rowCount(), 0);

        const int row = model->appendRow(QString::fromLatin1(kReactions),
                                         QStringLiteral("model.rxn"));
        QCOMPARE(row, 0);
        QVERIFY(model->isDirty());
        QVERIFY(QMetaObject::invokeMethod(&dlg, "onApply", Qt::DirectConnection));
        QVERIFY(dlg.wroteAnyChanges());
        QVERIFY(swmm_process_component_find(e, kReactions) >= 0);
        {
            char id[128] = {}, cfg[512] = {}, res[512] = {};
            QCOMPARE(swmm_process_component_get(e, 0, id, sizeof id, cfg, sizeof cfg,
                                                res, sizeof res), SWMM_OK);
            QCOMPARE(QString::fromUtf8(cfg), QStringLiteral("model.rxn"));
        }
        QVERIFY(!model->isDirty());

        // Unedited second Apply is the identity.
        SimulationOptionsDialog dlg2(e, layer.get(), QStringLiteral("6.0.0"), nullptr, nullptr);
        auto *model2 = qobject_cast<ProcessComponentsModel *>(
            child<QTableView>(&dlg2, "processComponentsView")->model());
        QVERIFY(model2);
        QCOMPARE(model2->rowCount(), 1);
        QVERIFY(QMetaObject::invokeMethod(&dlg2, "onApply", Qt::DirectConnection));
        QVERIFY(!dlg2.wroteAnyChanges());

        // Removal lands on Apply.
        QVERIFY(model2->removeRows(0, 1));
        QVERIFY(QMetaObject::invokeMethod(&dlg2, "onApply", Qt::DirectConnection));
        QCOMPARE(swmm_process_component_find(e, kReactions), -1);
    }
};

QTEST_MAIN(TestProcessesHub)
#include "test_processes_hub.moc"
