/*!
 * \file   test_control_rule_models.cpp
 * \brief  Slice BR Phase 6.8.1 — Qt-contract tests for the control-rule MVC
 *         layer (ControlRuleProvider + ControlRuleRegistry + RuleListModel).
 *
 * Tests focus on the surface verifiable without instantiating a full
 * `SWMMModelLayer` (which has nanoflann, OGR, scene rendering as transitive
 * deps). End-to-end engine round-trip is verified by the engine
 * `test_engine_control_rule_validate` binary (BR-02) plus manual smoke of
 * the running app once the editor dialog lands.
 *
 * What this file pins:
 *   - ControlRuleProvider name / body setters emit the right signals
 *   - Setting the body resets validation to Pending (auto-invalidate)
 *   - Validation cache round-trips through setValidation
 *   - ControlRuleRegistry: create / hasName / findByName / remove /
 *     rename (including case-only rename + duplicate rejection)
 *   - Provider rename ripples to the lower-name index (lookup by new name)
 *   - clear() empties the registry without crashing
 *   - RuleListModel: null-layer safety, rowCount when registry empty
 *
 * Tests that need a live `SWMMModelLayer::applyControlRule*` (rename /
 * remove through the model's setData / removeRows) are covered by the
 * dialog-level + engine round-trip tests; pinning them here would require
 * a layer instance which drags in the full GUI link surface.
 */

#include <QObject>
#include <QPointer>
#include <QSignalSpy>
#include <QString>
#include <QTest>

#include "controls/controlruleprovider.h"
#include "controls/controlruleregistry.h"
#include "ui/models/rulelistmodel.h"

using openswmmvis::controls::ControlRuleProvider;
using openswmmvis::controls::ControlRuleRegistry;
using openswmmvis::controls::ValidationState;

class TestControlRuleModels : public QObject
{
    Q_OBJECT

private slots:

    // ── ControlRuleProvider ─────────────────────────────────────────────

    void providerNameSetterEmitsSignal()
    {
        ControlRuleProvider p(QStringLiteral("R1"),
                              QStringLiteral("RULE R1\nIF NODE J1 DEPTH > 5\nTHEN PUMP P1 STATUS = ON"));
        QSignalSpy spy(&p, &ControlRuleProvider::nameChanged);
        p.setName(QStringLiteral("R1Renamed"));
        QCOMPARE(p.name(), QStringLiteral("R1Renamed"));
        QCOMPARE(spy.count(), 1);
        // Setting same name is a no-op.
        p.setName(QStringLiteral("R1Renamed"));
        QCOMPARE(spy.count(), 1);
    }

    void providerBodySetterResetsValidation()
    {
        ControlRuleProvider p(QStringLiteral("R"), QStringLiteral("RULE R\n"));
        p.setValidation(ValidationState::Valid);
        QCOMPARE(p.validationState(), ValidationState::Valid);
        QSignalSpy bodySpy(&p, &ControlRuleProvider::bodyChanged);
        QSignalSpy valSpy (&p, &ControlRuleProvider::validationChanged);
        p.setBody(QStringLiteral("RULE R\nIF NODE J1 DEPTH > 0\nTHEN PUMP P1 STATUS = OFF"));
        QCOMPARE(bodySpy.count(), 1);
        QCOMPARE(valSpy.count(),  1);
        QCOMPARE(p.validationState(), ValidationState::Pending);
        QCOMPARE(p.lastError(),       QString());
        QCOMPARE(p.lastErrorLine(),   -1);
    }

    void providerValidationCacheRoundTrips()
    {
        ControlRuleProvider p(QStringLiteral("R"), QStringLiteral(""));
        QSignalSpy spy(&p, &ControlRuleProvider::validationChanged);
        p.setValidation(ValidationState::Invalid, QStringLiteral("missing THEN"), 3);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(p.validationState(), ValidationState::Invalid);
        QCOMPARE(p.lastError(),       QStringLiteral("missing THEN"));
        QCOMPARE(p.lastErrorLine(),   3);
        // Setting the same verdict + message is a no-op.
        p.setValidation(ValidationState::Invalid, QStringLiteral("missing THEN"), 3);
        QCOMPARE(spy.count(), 1);
    }

    // ── ControlRuleRegistry ─────────────────────────────────────────────

    void registryCreateAndLookup()
    {
        ControlRuleRegistry reg;
        QSignalSpy addSpy(&reg, &ControlRuleRegistry::providerAdded);
        auto *p = reg.create(QStringLiteral("PumpOn"),
                              QStringLiteral("RULE PumpOn\nIF NODE J1 DEPTH > 5\nTHEN PUMP P1 STATUS = ON"));
        QVERIFY(p != nullptr);
        QCOMPARE(addSpy.count(), 1);
        QCOMPARE(reg.providerCount(), 1);
        QVERIFY(reg.hasName(QStringLiteral("PumpOn")));
        QVERIFY(reg.hasName(QStringLiteral("pumpon")));   // case-insensitive lookup
        QCOMPARE(reg.findByName(QStringLiteral("PumpOn")), p);
        QVERIFY(!reg.hasName(QStringLiteral("Other")));
    }

    void registryRejectsDuplicateName()
    {
        ControlRuleRegistry reg;
        QVERIFY(reg.create(QStringLiteral("R1"), QStringLiteral("RULE R1\n")) != nullptr);
        QCOMPARE(reg.create(QStringLiteral("R1"),  QStringLiteral("RULE R1\n")), nullptr);
        QCOMPARE(reg.create(QStringLiteral("r1"),  QStringLiteral("RULE r1\n")), nullptr);
        QCOMPARE(reg.create(QStringLiteral(""),    QStringLiteral("")),           nullptr);
        QCOMPARE(reg.providerCount(), 1);
    }

    void registryRenameUpdatesNameIndex()
    {
        ControlRuleRegistry reg;
        auto *p = reg.create(QStringLiteral("R1"), QStringLiteral("RULE R1\n"));
        QSignalSpy renSpy(&reg, &ControlRuleRegistry::providerRenamed);
        QVERIFY(reg.rename(p, QStringLiteral("R1Renamed")));
        QCOMPARE(renSpy.count(), 1);
        QVERIFY(reg.hasName(QStringLiteral("R1Renamed")));
        QVERIFY(!reg.hasName(QStringLiteral("R1")));
        // Case-only rename keeps the same provider identity.
        QVERIFY(reg.rename(p, QStringLiteral("r1renamed")));
        QCOMPARE(p->name(), QStringLiteral("r1renamed"));
        // Collision is rejected.
        auto *q = reg.create(QStringLiteral("Other"), QStringLiteral("RULE Other\n"));
        QVERIFY(q != nullptr);
        QVERIFY(!reg.rename(p, QStringLiteral("Other")));   // q already owns it
        QCOMPARE(p->name(), QStringLiteral("r1renamed"));   // unchanged
    }

    void registryRemoveEmitsAboutToBeRemoved()
    {
        ControlRuleRegistry reg;
        auto *p = reg.create(QStringLiteral("R"), QStringLiteral("RULE R\n"));
        QSignalSpy spy(&reg, &ControlRuleRegistry::providerAboutToBeRemoved);
        reg.remove(p);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(reg.providerCount(), 0);
        QVERIFY(!reg.hasName(QStringLiteral("R")));
    }

    void registryClearEmptiesEverything()
    {
        ControlRuleRegistry reg;
        reg.create(QStringLiteral("A"), QStringLiteral("RULE A\n"));
        reg.create(QStringLiteral("B"), QStringLiteral("RULE B\n"));
        reg.create(QStringLiteral("C"), QStringLiteral("RULE C\n"));
        QCOMPARE(reg.providerCount(), 3);
        QSignalSpy spy(&reg, &ControlRuleRegistry::providerAboutToBeRemoved);
        reg.clear();
        QCOMPARE(spy.count(), 3);
        QCOMPARE(reg.providerCount(), 0);
    }

    // ── RuleListModel ───────────────────────────────────────────────────

    void listModelNullLayerSafety()
    {
        openswmmvis::ui::RuleListModel m(nullptr);
        QCOMPARE(m.rowCount(), 0);
        QCOMPARE(m.indexOf(QStringLiteral("anything")), -1);
        QCOMPARE(m.nameAt(0), QString());
        // setData on an invalid index is a no-op.
        QVERIFY(!m.setData(m.index(0), QStringLiteral("X"), Qt::EditRole));
        // removeRows on a null-layer model is a no-op.
        QVERIFY(!m.removeRows(0, 1));
    }

    void listModelFlagsContainExpectedBits()
    {
        openswmmvis::ui::RuleListModel m(nullptr);
        // Invalid index has no flags.
        QCOMPARE(m.flags(QModelIndex()), Qt::NoItemFlags);
    }
};

// ============================================================================
// Stub SWMMModelLayer surface
// ----------------------------------------------------------------------------
// Same idiom as test_hydrograph_models.cpp — provide minimal symbol bodies
// for the few `SWMMModelLayer` methods the production rulelistmodel.cpp
// references. The tests above pass `nullptr` for the layer, so these
// stubs are never invoked — they only need to satisfy the linker.
// ============================================================================

#include "layers/swmmmodellayer.h"
#include "map/spatialreferencesystem.h"
#include "render/ifeaturerenderer.h"
#include "render/rulelist.h"
#include "ui/dialogs/ilayerstylesubject.h"
#include <openswmm/engine/openswmm_engine.h>
#include <QGraphicsScene>

// Empty stand-in for the kd-tree pimpl member (nanoflann pulled in by
// the real definition is excessive for this test).
struct SWMMKdTrees {};

// SpatialReferenceSystem dtor — full impl lives in
// spatialreferencesystem.cpp and would pull GDAL into the test link.
SpatialReferenceSystem::~SpatialReferenceSystem() = default;

SWMM_Engine SWMMModelLayer::engine() const { return nullptr; }
QObject *SWMMModelLayer::ensureControlRuleRegistry() { return nullptr; }
bool SWMMModelLayer::applyControlRuleRename(const QString&, const QString&, QString*) { return false; }
bool SWMMModelLayer::applyControlRuleRemove(const QString&, QString*) { return false; }

// Slice Z (RENDERING_RULE_MODEL_PLAN) — vtable stubs. Tests pass
// nullptr for the layer so these are never invoked at runtime; they
// exist solely to satisfy the linker against the MOC vtable.
SWMMModelLayer::~SWMMModelLayer() = default;

QString SWMMModelLayer::modelFilePath()      const { return {}; }
bool    SWMMModelLayer::showNodes()          const { return false; }
bool    SWMMModelLayer::showLinks()          const { return false; }
bool    SWMMModelLayer::showSubcatchments()  const { return false; }
bool    SWMMModelLayer::showRainGages()      const { return false; }
bool    SWMMModelLayer::showLabels()         const { return false; }
void    SWMMModelLayer::setShowNodes(bool)         {}
void    SWMMModelLayer::setShowLinks(bool)         {}
void    SWMMModelLayer::setShowSubcatchments(bool) {}
void    SWMMModelLayer::setShowRainGages(bool)     {}
void    SWMMModelLayer::setShowLabels(bool)        {}

OpenSWMM::Render::IFeatureRenderer *SWMMModelLayer::renderer() const { return nullptr; }
void SWMMModelLayer::setRenderer(std::unique_ptr<OpenSWMM::Render::IFeatureRenderer>) {}

OpenSWMM::Render::RuleList       *SWMMModelLayer::ruleList()       { return nullptr; }
const OpenSWMM::Render::RuleList *SWMMModelLayer::ruleList() const { return nullptr; }

std::vector<std::unique_ptr<openswmmvis::ui::ILayerStyleSubject>>
SWMMModelLayer::styleSubjects() { return {}; }

void SWMMModelLayer::populateScene(QGraphicsScene *, const MapExtent &,
                                    const SpatialReferenceSystem *) {}
void SWMMModelLayer::depopulateScene(QGraphicsScene *) {}
void SWMMModelLayer::refreshScene(QGraphicsScene *, const MapExtent &,
                                   const SpatialReferenceSystem *) {}
void SWMMModelLayer::onCanvasCRSChanged(const SpatialReferenceSystem *) {}

QTEST_GUILESS_MAIN(TestControlRuleModels)
#include "test_control_rule_models.moc"
