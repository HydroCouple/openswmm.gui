/**
 * @file      test_control_rule_models.cpp
 * @brief     Slice BR Phase 6.8.1 — Qt-contract tests for the control-rule MVC
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
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
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

    /**
     *
     * @brief Require a provider name edit to store the name and emit once, while repeating it is
     * silent.
     * Uses a standalone provider, without checking registry indexing or native header rewrite.
     *
     */
    void providerNameSetterEmitsSignal()
    {
        ControlRuleProvider p(
            QStringLiteral("R1"),
            QStringLiteral("RULE R1\nIF NODE J1 DEPTH > 5\nTHEN PUMP P1 STATUS = ON"));
        QSignalSpy spy(&p, &ControlRuleProvider::nameChanged);
        p.setName(QStringLiteral("R1Renamed"));
        QCOMPARE(p.name(), QStringLiteral("R1Renamed"));
        QCOMPARE(spy.count(), 1);
        // Setting same name is a no-op.
        p.setName(QStringLiteral("R1Renamed"));
        QCOMPARE(spy.count(), 1);
    }

    /**
     *
     * @brief Require changing a previously valid body to emit body/validation changes and reset
     * its cache to Pending.
     * Checks cleared error text and line minus one; no actual engine validation occurs.
     *
     */
    void providerBodySetterResetsValidation()
    {
        ControlRuleProvider p(QStringLiteral("R"), QStringLiteral("RULE R\n"));
        p.setValidation(ValidationState::Valid);
        QCOMPARE(p.validationState(), ValidationState::Valid);
        QSignalSpy bodySpy(&p, &ControlRuleProvider::bodyChanged);
        QSignalSpy valSpy(&p, &ControlRuleProvider::validationChanged);
        p.setBody(QStringLiteral("RULE R\nIF NODE J1 DEPTH > 0\nTHEN PUMP P1 STATUS = OFF"));
        QCOMPARE(bodySpy.count(), 1);
        QCOMPARE(valSpy.count(), 1);
        QCOMPARE(p.validationState(), ValidationState::Pending);
        QCOMPARE(p.lastError(), QString());
        QCOMPARE(p.lastErrorLine(), -1);
    }

    /**
     *
     * @brief Require invalid verdict/message/line to round-trip and an identical assignment to
     * emit nothing further.
     * Checks missing-THEN text and line3; no parser or native engine is invoked.
     *
     */
    void providerValidationCacheRoundTrips()
    {
        ControlRuleProvider p(QStringLiteral("R"), QStringLiteral(""));
        QSignalSpy spy(&p, &ControlRuleProvider::validationChanged);
        p.setValidation(ValidationState::Invalid, QStringLiteral("missing THEN"), 3);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(p.validationState(), ValidationState::Invalid);
        QCOMPARE(p.lastError(), QStringLiteral("missing THEN"));
        QCOMPARE(p.lastErrorLine(), 3);
        // Setting the same verdict + message is a no-op.
        p.setValidation(ValidationState::Invalid, QStringLiteral("missing THEN"), 3);
        QCOMPARE(spy.count(), 1);
    }

    // ── ControlRuleRegistry ─────────────────────────────────────────────

    /**
     *
     * @brief Require create to emit once, retain one provider and support case-insensitive
     * lookup.
     * Checks identity and absent Other; no rule parsing, body validation or native mirroring.
     *
     */
    void registryCreateAndLookup()
    {
        ControlRuleRegistry reg;
        QSignalSpy addSpy(&reg, &ControlRuleRegistry::providerAdded);
        auto *p = reg.create(
            QStringLiteral("PumpOn"),
            QStringLiteral("RULE PumpOn\nIF NODE J1 DEPTH > 5\nTHEN PUMP P1 STATUS = ON"));
        QVERIFY(p != nullptr);
        QCOMPARE(addSpy.count(), 1);
        QCOMPARE(reg.providerCount(), 1);
        QVERIFY(reg.hasName(QStringLiteral("PumpOn")));
        QVERIFY(reg.hasName(QStringLiteral("pumpon"))); // case-insensitive lookup
        QCOMPARE(reg.findByName(QStringLiteral("PumpOn")), p);
        QVERIFY(!reg.hasName(QStringLiteral("Other")));
    }

    /**
     *
     * @brief Require exact/case-folded duplicate and empty names to fail without growing the
     * registry.
     * Checks one surviving provider; no duplicate body merge or diagnostic assertion.
     *
     */
    void registryRejectsDuplicateName()
    {
        ControlRuleRegistry reg;
        QVERIFY(reg.create(QStringLiteral("R1"), QStringLiteral("RULE R1\n")) != nullptr);
        QCOMPARE(reg.create(QStringLiteral("R1"), QStringLiteral("RULE R1\n")), nullptr);
        QCOMPARE(reg.create(QStringLiteral("r1"), QStringLiteral("RULE r1\n")), nullptr);
        QCOMPARE(reg.create(QStringLiteral(""), QStringLiteral("")), nullptr);
        QCOMPARE(reg.providerCount(), 1);
    }

    /**
     *
     * @brief Require successful and case-only rename to update lookup/identity and collision
     * refusal to preserve the current name.
     * Checks one initial rename signal; no native RULE-header rewrite or signal count after
     * case-only rename is asserted.
     *
     */
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
        QVERIFY(!reg.rename(p, QStringLiteral("Other"))); // q already owns it
        QCOMPARE(p->name(), QStringLiteral("r1renamed")); // unchanged
    }

    /**
     *
     * @brief Require removal to emit once, empty the registry and remove name lookup.
     * Count alone does not prove signal ordering relative to destruction; no removed pointer
     * is dereferenced or native table deleted in this isolated suite.
     *
     */
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

    /**
     *
     * @brief Require clearing three providers to emit three removal notifications and leave count
     * zero.
     * Does not inspect signal arguments/order or engine state.
     *
     */
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

    /**
     *
     * @brief Require a null-layer list model to expose no rows and refuse invalid rename/remove
     * operations.
     * Checks missing lookup/name results; link stubs below are intentionally not invoked.
     *
     */
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

    /**
     *
     * @brief Require an invalid index on a null-layer model to have no item flags.
     * Does not check any valid populated row or editing capability.
     *
     */
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
struct SWMMKdTrees {
};

// SpatialReferenceSystem dtor — full impl lives in
// spatialreferencesystem.cpp and would pull GDAL into the test link.
/**
 *
 * @brief Default the isolated-link SRS destructor so this test need not link the GDAL-backed
 * implementation.
 * Normal C++ member/base destruction applies; no production CRS teardown behavior is
 * implemented or exercised by this null-layer test.
 *
 */
SpatialReferenceSystem::~SpatialReferenceSystem() = default;

/**
 *
 * @brief Return null from the isolated test link stub.
 * @return Null borrowed handle; does not allocate or inspect an engine. This suite
 * passes null layers and never calls this stand-in as production model behavior.
 *
 */
SWMM_Engine SWMMModelLayer::engine() const
{
    return nullptr;
}
/**
 *
 * @brief Return null from the isolated registry link stub without constructing a registry.
 * @return Null; intentionally not a usable replacement for production lazy initialization.
 *
 */
QObject *SWMMModelLayer::ensureControlRuleRegistry()
{
    return nullptr;
}
/**
 *
 * @brief Refuse every rename in the unused isolated-link stand-in.
 * @param oldName Borrowed ignored source identity.
 * @param newName Borrowed ignored requested identity.
 * @param error Optional borrowed diagnostic destination, left unchanged.
 * @return Always false; no native mutation, signal, diagnostic or retained references.
 *
 */
bool SWMMModelLayer::applyControlRuleRename(const QString &oldName, const QString &newName,
                                            QString *error)
{
    return false;
}
/**
 *
 * @brief Refuse every removal in the unused isolated-link stand-in.
 * @param name Borrowed ignored identity.
 * @param error Optional borrowed diagnostic destination, left unchanged.
 * @return Always false without native mutation, signal or retained references.
 *
 */
bool SWMMModelLayer::applyControlRuleRemove(const QString &name, QString *error)
{
    return false;
}

// Slice Z (RENDERING_RULE_MODEL_PLAN) — vtable stubs. Tests pass
// nullptr for the layer so these are never invoked at runtime; they
// exist solely to satisfy the linker against the MOC vtable.
/**
 *
 * @brief Default model member/base destruction for the isolated-link vtable.
 * Does not reproduce custom production model cleanup. Cases use null layers and do not
 * instantiate/destroy a model; this body exists solely for linking.
 *
 */
SWMMModelLayer::~SWMMModelLayer() = default;

/**
 *
 * @brief Return an empty path from the unused model-link stand-in.
 * @return Owned empty QString, without file access or retained model metadata.
 *
 */
QString SWMMModelLayer::modelFilePath() const
{
    return {};
}
/**
 *
 * @brief Report false for node visibility in the unused isolated-link stand-in.
 * @return False independent of state; no renderer access.
 *
 */
bool SWMMModelLayer::showNodes() const
{
    return false;
}
/**
 *
 * @brief Report false for link visibility in the unused isolated-link stand-in.
 * @return False independent of state; no renderer access.
 *
 */
bool SWMMModelLayer::showLinks() const
{
    return false;
}
/**
 *
 * @brief Report false for subcatchment visibility in the unused isolated-link stand-in.
 * @return False independent of state; no renderer access.
 *
 */
bool SWMMModelLayer::showSubcatchments() const
{
    return false;
}
/**
 *
 * @brief Report false for rain-gage visibility in the unused isolated-link stand-in.
 * @return False independent of state; no renderer access.
 *
 */
bool SWMMModelLayer::showRainGages() const
{
    return false;
}
/**
 *
 * @brief Report false for label visibility in the unused isolated-link stand-in.
 * @return False independent of state; no renderer access.
 *
 */
bool SWMMModelLayer::showLabels() const
{
    return false;
}
/**
 *
 * @brief Ignore node visibility in the unused isolated-link stand-in.
 * @param show Ignored requested flag; no state change or signal.
 *
 */
void SWMMModelLayer::setShowNodes(bool show) {}
/**
 *
 * @brief Ignore link visibility in the unused isolated-link stand-in.
 * @param show Ignored requested flag; no state change or signal.
 *
 */
void SWMMModelLayer::setShowLinks(bool show) {}
/**
 *
 * @brief Ignore subcatchment visibility in the unused isolated-link stand-in.
 * @param show Ignored requested flag; no state change or signal.
 *
 */
void SWMMModelLayer::setShowSubcatchments(bool show) {}
/**
 *
 * @brief Ignore rain-gage visibility in the unused isolated-link stand-in.
 * @param show Ignored requested flag; no state change or signal.
 *
 */
void SWMMModelLayer::setShowRainGages(bool show) {}
/**
 *
 * @brief Ignore label visibility in the unused isolated-link stand-in.
 * @param show Ignored requested flag; no state change or signal.
 *
 */
void SWMMModelLayer::setShowLabels(bool show) {}

/**
 *
 * @brief Return no renderer from the unused isolated-link stand-in.
 * @return Null borrowed pointer, without allocation or ownership transfer.
 *
 */
OpenSWMM::Render::IFeatureRenderer *SWMMModelLayer::renderer() const
{
    return nullptr;
}
/**
 *
 * @brief Discard an incoming renderer in the unused isolated-link stand-in.
 * @param renderer Owning unique_ptr passed by value; its pointee is destroyed on return,
 * not retained by this stub. Null is accepted; no signal or actual model mutation.
 *
 */
void SWMMModelLayer::setRenderer(std::unique_ptr<OpenSWMM::Render::IFeatureRenderer> renderer) {}

/**
 *
 * @brief Return no mutable render rules from the unused isolated-link stand-in.
 * @return Null borrowed pointer; no allocation or state inspection.
 *
 */
OpenSWMM::Render::RuleList *SWMMModelLayer::ruleList()
{
    return nullptr;
}
/**
 *
 * @brief Return no const render rules from the unused isolated-link stand-in.
 * @return Null borrowed pointer; no allocation or state inspection.
 *
 */
const OpenSWMM::Render::RuleList *SWMMModelLayer::ruleList() const
{
    return nullptr;
}

/**
 *
 * @brief Return no style subjects from the unused isolated-link stand-in.
 * @return Owned empty vector; creates no subjects or borrowed views.
 *
 */
std::vector<std::unique_ptr<openswmmvis::ui::ILayerStyleSubject>> SWMMModelLayer::styleSubjects()
{
    return {};
}

/**
 *
 * @brief Ignore scene population in the unused isolated-link stand-in.
 * @param scene Borrowed possibly null scene, ignored and not retained.
 * @param extent Borrowed ignored extent.
 * @param crs Borrowed possibly null CRS, ignored and not retained.
 * No graphics items, renderer state or signals are created.
 *
 */
void SWMMModelLayer::populateScene(QGraphicsScene *scene, const MapExtent &extent,
                                   const SpatialReferenceSystem *crs)
{
}
/**
 *
 * @brief Ignore scene teardown in the unused isolated-link stand-in.
 * @param scene Borrowed possibly null scene, ignored; no items deleted or refs retained.
 *
 */
void SWMMModelLayer::depopulateScene(QGraphicsScene *scene) {}
/**
 *
 * @brief Ignore scene refresh in the unused isolated-link stand-in.
 * @param scene Borrowed possibly null scene, ignored and not retained.
 * @param extent Borrowed ignored extent.
 * @param crs Borrowed possibly null CRS, ignored and not retained.
 * No rendering or item mutation occurs.
 *
 */
void SWMMModelLayer::refreshScene(QGraphicsScene *scene, const MapExtent &extent,
                                  const SpatialReferenceSystem *crs)
{
}
/**
 *
 * @brief Ignore CRS notifications in the unused isolated-link stand-in.
 * @param crs Borrowed possibly null CRS, ignored; no transformation or retained reference.
 *
 */
void SWMMModelLayer::onCanvasCRSChanged(const SpatialReferenceSystem *crs) {}

QTEST_GUILESS_MAIN(TestControlRuleModels)
#include "test_control_rule_models.moc"
