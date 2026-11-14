/**
 * @file      test_registrylistmodels.cpp
 * @brief     Review E-08: every registry-backed list model announces its row inserts and removals
 *            while its row count changes, as QAbstractItemModel requires.
 *
 * @details The models used to read their rows straight from the registry, which appends a new
 *          provider before announcing it and announces a removal before removing it, so
 *          beginInsertRows saw the row already there and endRemoveRows saw it still there.
 *          QAbstractItemModelTester checks each model while a provider is created and removed.
 *          The model input is copied to tests/output/gui_review_2026-11-13/registry_list_models/.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   Apache-2.0
 */

#include "aquifer/aquiferregistry.h"
#include "controls/controlruleregistry.h"
#include "inlet/inletregistry.h"
#include "landuse/landuseregistry.h"
#include "layers/swmmmodellayer.h"
#include "lid/lidcontrolregistry.h"
#include "pollutant/pollutantregistry.h"
#include "snowpack/snowpackregistry.h"
#include "street/streetregistry.h"
#include "transect/transectregistry.h"
#include "ui/models/aquiferlistmodel.h"
#include "ui/models/inletlistmodel.h"
#include "ui/models/landuselistmodel.h"
#include "ui/models/lidcontrollistmodel.h"
#include "ui/models/pollutantlistmodel.h"
#include "ui/models/rulelistmodel.h"
#include "ui/models/snowpacklistmodel.h"
#include "ui/models/streetlistmodel.h"
#include "ui/models/transectlistmodel.h"

#include <QAbstractItemModelTester>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTest>
#include <memory>

namespace {

std::unique_ptr<SWMMModelLayer> fixture()
{
    const QDir out(QStringLiteral(SWMMVIS_REVIEW_OUTPUT_DIR "/registry_list_models"));
    QDir().mkpath(out.path());
    const QString path = out.filePath(QStringLiteral("model.inp"));
    QFile::remove(path);
    if (!QFile::copy(QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA"))
                         .filePath(QStringLiteral("data_object_table_fixture.inp")),
                     path))
        return {};
    auto layer = std::make_unique<SWMMModelLayer>(path);
    QList<QString> warnings, errors;
    return layer->loadModel(warnings, errors) ? std::move(layer) : nullptr;
}

/*! Create a provider (appended) and remove it, then remove the first existing one, under the
 *  model tester; the row count must follow each announced change. */
template <class Registry, class Create>
void exercise(QAbstractItemModel &model, Registry *registry, Create create)
{
    QAbstractItemModelTester tester(&model,
                                    QAbstractItemModelTester::FailureReportingMode::QtTest);
    QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted),
        removed(&model, &QAbstractItemModel::rowsRemoved);
    const int before = model.rowCount();
    auto *added = create(registry, QStringLiteral("E08_NEW"));
    QVERIFY(added);
    QCOMPARE(model.rowCount(), before + 1);
    QCOMPARE(inserted.count(), 1);
    QCOMPARE(model.index(model.rowCount() - 1, 0).data().toString(), QStringLiteral("E08_NEW"));
    registry->remove(added);
    QCOMPARE(model.rowCount(), before);
    QCOMPARE(removed.count(), 1);
    if (before > 0) {
        const QString second = before > 1 ? model.index(1, 0).data().toString() : QString();
        registry->remove(registry->providers().first());
        QCOMPARE(model.rowCount(), before - 1);
        QCOMPARE(removed.count(), 2);
        if (before > 1)
            QCOMPARE(model.index(0, 0).data().toString(), second);
    }
}

template <class Registry, class ListModel>
void exerciseFamily(QObject *registryObject)
{
    auto *registry = qobject_cast<Registry *>(registryObject);
    QVERIFY(registry);
    ListModel model;
    model.setRegistry(registry);
    exercise(model, registry, [](Registry *r, const QString &n) { return r->create(n); });
}

} // namespace

class TestRegistryListModels : public QObject
{
    Q_OBJECT
private slots:
    void pollutants()
    {
        auto layer = fixture(); QVERIFY(layer);
        exerciseFamily<openswmmvis::pollutant::PollutantRegistry, openswmmvis::ui::PollutantListModel>(
            layer->ensurePollutantRegistry());
    }
    void aquifers()
    {
        auto layer = fixture(); QVERIFY(layer);
        exerciseFamily<openswmmvis::aquifer::AquiferRegistry, openswmmvis::ui::AquiferListModel>(
            layer->ensureAquiferRegistry());
    }
    void inlets()
    {
        auto layer = fixture(); QVERIFY(layer);
        exerciseFamily<openswmmvis::inlet::InletRegistry, openswmmvis::ui::InletListModel>(
            layer->ensureInletRegistry());
    }
    void landUses()
    {
        auto layer = fixture(); QVERIFY(layer);
        exerciseFamily<openswmmvis::landuse::LandUseRegistry, openswmmvis::ui::LandUseListModel>(
            layer->ensureLandUseRegistry());
    }
    void lidControls()
    {
        auto layer = fixture(); QVERIFY(layer);
        exerciseFamily<openswmmvis::lid::LidControlRegistry, openswmmvis::ui::LidControlListModel>(
            layer->ensureLidControlRegistry());
    }
    void snowpacks()
    {
        auto layer = fixture(); QVERIFY(layer);
        exerciseFamily<openswmmvis::snowpack::SnowpackRegistry, openswmmvis::ui::SnowpackListModel>(
            layer->ensureSnowpackRegistry());
    }
    void streets()
    {
        auto layer = fixture(); QVERIFY(layer);
        exerciseFamily<openswmmvis::street::StreetRegistry, openswmmvis::ui::StreetListModel>(
            layer->ensureStreetRegistry());
    }
    void transects()
    {
        auto layer = fixture(); QVERIFY(layer);
        exerciseFamily<openswmmvis::transect::TransectRegistry, openswmmvis::ui::TransectListModel>(
            layer->ensureTransectRegistry());
    }
    void rules()
    {
        auto layer = fixture(); QVERIFY(layer);
        auto *registry =
            qobject_cast<openswmmvis::controls::ControlRuleRegistry *>(layer->ensureControlRuleRegistry());
        QVERIFY(registry);
        openswmmvis::ui::RuleListModel model(layer.get());
        exercise(model, registry, [](openswmmvis::controls::ControlRuleRegistry *r, const QString &n) {
            return r->create(n, QStringLiteral("RULE %1\nIF SIMULATION TIME > 1\nTHEN LINK C1 STATUS = OPEN\n").arg(n));
        });
    }
};

QTEST_MAIN(TestRegistryListModels)
#include "test_registrylistmodels.moc"
