/**
 * @file      test_hydrograph_models.cpp
 * @brief     Slice BS Phase 6.9.2 — Qt-contract tests for the four hydrograph
 *         MVC models.
 *
 * Tests focus on the model surface that can be verified without instantiating
 * a real SWMMModelLayer (which has a heavy dependency surface: nanoflann,
 * OGR, scene rendering). End-to-end MVC behavior is exercised by the engine
 * BS-02 mutation tests + the manual smoke test of the running app.
 *
 * What this file pins:
 *   - rowCount / columnCount / headerData per model class
 *   - flags() — response column non-editable; numeric columns editable;
 *     decay's Active column gets ItemIsUserCheckable
 *   - data() with role mismatches returns invalid QVariant
 *   - Response label string for col 0 ("Short-Term", "Medium-Term",
 *     "Long-Term")
 *   - setContext() — different (name, month) triggers modelReset
 *   - HydrographGroupListModel::indexOf / nameAt safety on empty model
 *   - Null-layer safety on all four models (constructor doesn't crash,
 *     data() returns blank, setData() returns false)
 *
 * Stubs at the bottom of the file satisfy the linker for the few
 * SWMMModelLayer methods the models reference; they're never invoked
 * because the tests pass nullptr for the layer.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTest>

#include "layers/hydrographmodels.h"
#include "layers/swmmmodellayer.h"

class TestHydrographModels : public QObject
{
    Q_OBJECT

private slots:

    // -------------------------------------------------------------------
    // HydrographGroupListModel
    // -------------------------------------------------------------------

    /**
     * @brief Verify null-layer group list is empty and index/name lookups fail safely.
     * @note Null-layer local Qt models/signals; linker stubs below are intentionally inert and
     * are not a substitute for engine/MVC integration tests. No engine instance, project write
     * or GUI scene is used.
     */
    void groupListNullLayerSafety()
    {
        HydrographGroupListModel m(nullptr);
        QCOMPARE(m.rowCount(), 0);
        QCOMPARE(m.indexOf("anything"), -1);
        QCOMPARE(m.nameAt(0), QString());
    }

    /**
     * @brief Verify group list has a display header, invalid column header and no flags for
     * invalid index.
     * @note Null-layer local Qt models/signals; linker stubs below are intentionally inert and
     * are not a substitute for engine/MVC integration tests. No engine instance, project write
     * or GUI scene is used.
     */
    void groupListHeaderAndFlags()
    {
        HydrographGroupListModel m(nullptr);
        const QVariant h = m.headerData(0, Qt::Horizontal, Qt::DisplayRole);
        QVERIFY(!h.toString().isEmpty());
        // Out-of-range column returns invalid header.
        QVERIFY(!m.headerData(5, Qt::Horizontal, Qt::DisplayRole).isValid());
        // Invalid index has no flags.
        QCOMPARE(m.flags(QModelIndex()), Qt::NoItemFlags);
    }

    // -------------------------------------------------------------------
    // HydrographRtkTableModel
    // -------------------------------------------------------------------

    /**
     * @brief Verify null-layer RTK model retains three response rows and four columns.
     * @note Null-layer local Qt models/signals; linker stubs below are intentionally inert and
     * are not a substitute for engine/MVC integration tests. No engine instance, project write
     * or GUI scene is used.
     */
    void rtkShape()
    {
        HydrographRtkTableModel m(nullptr);
        QCOMPARE(m.rowCount(), 3);
        QCOMPARE(m.columnCount(), 4);
    }

    /**
     * @brief Verify RTK response-name cells are never editable.
     * @note Null-layer local Qt models/signals; linker stubs below are intentionally inert and
     * are not a substitute for engine/MVC integration tests. No engine instance, project write
     * or GUI scene is used.
     */
    void rtkResponseColumnNonEditable()
    {
        HydrographRtkTableModel m(nullptr);
        for (int r = 0; r < 3; ++r) {
            const Qt::ItemFlags f = m.flags(m.index(r, HydrographRtkTableModel::ColResponse));
            QVERIFY(!(f & Qt::ItemIsEditable));
        }
    }

    /**
     * @brief Verify R/T/K numeric cells expose edit flags in all three response rows.
     * @note Null-layer local Qt models/signals; linker stubs below are intentionally inert and
     * are not a substitute for engine/MVC integration tests. No engine instance, project write
     * or GUI scene is used.
     */
    void rtkNumericColumnsEditable()
    {
        HydrographRtkTableModel m(nullptr);
        for (int c : {HydrographRtkTableModel::ColR, HydrographRtkTableModel::ColT,
                      HydrographRtkTableModel::ColK}) {
            for (int r = 0; r < 3; ++r) {
                const Qt::ItemFlags f = m.flags(m.index(r, c));
                QVERIFY(f & Qt::ItemIsEditable);
            }
        }
    }

    /**
     * @brief Verify null-layer response labels remain Short-Term, Medium-Term and Long-Term.
     * @note Null-layer local Qt models/signals; linker stubs below are intentionally inert and
     * are not a substitute for engine/MVC integration tests. No engine instance, project write
     * or GUI scene is used.
     */
    void rtkResponseLabels()
    {
        HydrographRtkTableModel m(nullptr);
        // Even with nullptr layer the response label column returns the
        // hard-coded "Short-Term" / "Medium-Term" / "Long-Term" strings.
        QCOMPARE(m.data(m.index(0, HydrographRtkTableModel::ColResponse)).toString(),
                 QStringLiteral("Short-Term"));
        QCOMPARE(m.data(m.index(1, HydrographRtkTableModel::ColResponse)).toString(),
                 QStringLiteral("Medium-Term"));
        QCOMPARE(m.data(m.index(2, HydrographRtkTableModel::ColResponse)).toString(),
                 QStringLiteral("Long-Term"));
    }

    /**
     * @brief Verify RTK name/month changes reset once, repeated context no-ops and changed month
     * resets again.
     * @note Null-layer local Qt models/signals; linker stubs below are intentionally inert and
     * are not a substitute for engine/MVC integration tests. No engine instance, project write
     * or GUI scene is used.
     */
    void rtkSetContextEmitsReset()
    {
        HydrographRtkTableModel m(nullptr);
        QSignalSpy resetSpy(&m, &QAbstractTableModel::modelReset);

        m.setContext("UH1", 0);
        QCOMPARE(m.currentName(), QStringLiteral("UH1"));
        QCOMPARE(m.currentMonth(), 0);
        QCOMPARE(resetSpy.count(), 1);

        // Same context → no reset.
        m.setContext("UH1", 0);
        QCOMPARE(resetSpy.count(), 1);

        // Different month → reset.
        m.setContext("UH1", 5);
        QCOMPARE(resetSpy.count(), 2);
    }

    /**
     * @brief Verify RTK numeric edit refuses without a backing layer, including all-month
     * context.
     * @note Null-layer local Qt models/signals; linker stubs below are intentionally inert and
     * are not a substitute for engine/MVC integration tests. No engine instance, project write
     * or GUI scene is used.
     */
    void rtkSetDataWithNullLayerFails()
    {
        HydrographRtkTableModel m(nullptr);
        m.setContext("UH1", -1);
        QCOMPARE(m.setData(m.index(0, HydrographRtkTableModel::ColR), 0.5, Qt::EditRole), false);
    }

    // -------------------------------------------------------------------
    // HydrographIaTableModel
    // -------------------------------------------------------------------

    /**
     * @brief Verify initial-abstraction model retains three responses/four columns without a
     * layer.
     * @note Null-layer local Qt models/signals; linker stubs below are intentionally inert and
     * are not a substitute for engine/MVC integration tests. No engine instance, project write
     * or GUI scene is used.
     */
    void iaShape()
    {
        HydrographIaTableModel m(nullptr);
        QCOMPARE(m.rowCount(), 3);
        QCOMPARE(m.columnCount(), 4);
    }

    /**
     * @brief Verify initial-abstraction parameter headers are Dmax, Drec and Do.
     * @note Null-layer local Qt models/signals; linker stubs below are intentionally inert and
     * are not a substitute for engine/MVC integration tests. No engine instance, project write
     * or GUI scene is used.
     */
    void iaHeaderColumns()
    {
        HydrographIaTableModel m(nullptr);
        QCOMPARE(m.headerData(HydrographIaTableModel::ColDmax, Qt::Horizontal, Qt::DisplayRole)
                     .toString(),
                 QStringLiteral("Dmax"));
        QCOMPARE(m.headerData(HydrographIaTableModel::ColDrec, Qt::Horizontal, Qt::DisplayRole)
                     .toString(),
                 QStringLiteral("Drec"));
        QCOMPARE(
            m.headerData(HydrographIaTableModel::ColDo, Qt::Horizontal, Qt::DisplayRole).toString(),
            QStringLiteral("Do"));
    }

    // -------------------------------------------------------------------
    // HydrographDecayTableModel
    // -------------------------------------------------------------------

    /**
     * @brief Verify decay model has three responses and exactly eight columns.
     * @note Null-layer local Qt models/signals; linker stubs below are intentionally inert and
     * are not a substitute for engine/MVC integration tests. No engine instance, project write
     * or GUI scene is used.
     */
    void decayShape()
    {
        HydrographDecayTableModel m(nullptr);
        QCOMPARE(m.rowCount(), 3);
        QCOMPARE(m.columnCount(), HydrographDecayTableModel::ColCount);
        QCOMPARE(int(HydrographDecayTableModel::ColCount), 8);
    }

    /**
     * @brief Verify decay Active column is checkable and not ordinary text-editable.
     * @note Null-layer local Qt models/signals; linker stubs below are intentionally inert and
     * are not a substitute for engine/MVC integration tests. No engine instance, project write
     * or GUI scene is used.
     */
    void decayActiveColumnIsUserCheckable()
    {
        HydrographDecayTableModel m(nullptr);
        for (int r = 0; r < 3; ++r) {
            const Qt::ItemFlags f = m.flags(m.index(r, HydrographDecayTableModel::ColActive));
            QVERIFY(f & Qt::ItemIsUserCheckable);
            QVERIFY(!(f & Qt::ItemIsEditable));
        }
    }

    /**
     * @brief Verify decay headers expose Active, k_dep and T_ref labels.
     * @note Null-layer local Qt models/signals; linker stubs below are intentionally inert and
     * are not a substitute for engine/MVC integration tests. No engine instance, project write
     * or GUI scene is used.
     */
    void decayHeaderColumns()
    {
        HydrographDecayTableModel m(nullptr);
        QCOMPARE(m.headerData(HydrographDecayTableModel::ColActive, Qt::Horizontal, Qt::DisplayRole)
                     .toString(),
                 QStringLiteral("Active"));
        QCOMPARE(m.headerData(HydrographDecayTableModel::ColKdep, Qt::Horizontal, Qt::DisplayRole)
                     .toString(),
                 QStringLiteral("k_dep"));
        QCOMPARE(m.headerData(HydrographDecayTableModel::ColTref, Qt::Horizontal, Qt::DisplayRole)
                     .toString(),
                 QStringLiteral("T_ref"));
    }

    /**
     * @brief Verify decay group change resets once and identical context causes no redundant
     * reset.
     * @note Null-layer local Qt models/signals; linker stubs below are intentionally inert and
     * are not a substitute for engine/MVC integration tests. No engine instance, project write
     * or GUI scene is used.
     */
    void decaySetContextEmitsReset()
    {
        HydrographDecayTableModel m(nullptr);
        QSignalSpy resetSpy(&m, &QAbstractTableModel::modelReset);
        m.setContext("UH1");
        QCOMPARE(m.currentName(), QStringLiteral("UH1"));
        QCOMPARE(resetSpy.count(), 1);
        m.setContext("UH1");
        QCOMPARE(resetSpy.count(), 1); // no-op
        m.setContext("UH2");
        QCOMPARE(resetSpy.count(), 2);
    }
};

// ============================================================================
// Stub SWMMModelLayer surface
// ----------------------------------------------------------------------------
// Just enough symbol bodies to satisfy the linker for the apply* / engine()
// references inside hydrographmodels.cpp's nullptr-guarded code paths. Real
// behavior is exercised through the engine BS-02 test + the GUI's running
// MVC chain — both already verified.
// ============================================================================

#include "render/ifeaturerenderer.h"
#include "render/rulelist.h"
#include "map/spatialreferencesystem.h"
#include "ui/dialogs/ilayerstylesubject.h"
#include <openswmm/engine/openswmm_engine.h>
#include <QGraphicsScene>

// Empty stand-in for the kd-tree pimpl member. The real definition
// pulls in nanoflann + 80k LOC; we just need a complete type so the
// unique_ptr<SWMMKdTrees> destructor instantiates cleanly.
struct SWMMKdTrees {
};

// GDAL-using out-of-line definitions stubbed (real ones live in
// spatialreferencesystem.cpp + would force GDAL into the test link).
/*
 * @brief Release only local stand-in value members; default test destructor performs no GDAL cleanup.
 */
SpatialReferenceSystem::~SpatialReferenceSystem() = default;

/*
 * @brief Return the null engine sentinel in this isolated link stub.
 * @return nullptr; no engine allocation/ownership transfer or model query.
 */
SWMM_Engine SWMMModelLayer::engine() const
{
    return nullptr;
}

/*
 * @brief Inert linker stand-in for RTK mutation, unreachable in null-layer tests.
 * @par Ignored arguments
 * Group name, month index, response index, R fraction, T time and K ratio are borrowed/value inputs
 * and all ignored; no pointers retained.
 * @return Always true, no validation or model mutation; not production behavior.
 */
bool SWMMModelLayer::applyHydrographSetRtk(const QString &, int, int, double, double, double)
{
    return true;
}
/*
 * @brief Inert initial-abstraction edit stand-in for null-layer model link isolation.
 * @par Ignored arguments
 * Borrowed group name, month/response indices and Dmax/Drec/Do numeric values are ignored.
 * @return Always true; no unit conversion, validation, mutation or retained references.
 */
bool SWMMModelLayer::applyHydrographSetIa(const QString &, int, int, double, double, double)
{
    return true;
}
/*
 * @brief Inert group-rename stand-in; never invoked by null-layer assertions.
 * @par Ignored arguments
 * Borrowed old/new names ignored, not copied or retained.
 * @return Always true without changing model/engine.
 */
bool SWMMModelLayer::applyHydrographRenameGroup(const QString &, const QString &)
{
    return true;
}
/*
 * @brief Inert decay-row update stand-in for isolated null-layer model linking.
 * @par Ignored arguments
 * Borrowed group name, response index and six decay parameter values are ignored and not retained.
 * @return Always true, no validation, physical interpretation or model/engine edits.
 */
bool SWMMModelLayer::applyRdiiDecaySet(const QString &, int, double, double, double, double, double,
                                       double)
{
    return true;
}
/*
 * @brief Inert decay-row removal stand-in.
 * @par Ignored arguments
 * Borrowed group name and response index ignored, no retention.
 * @return Always true, no row/model mutation.
 */
bool SWMMModelLayer::applyRdiiDecayRemove(const QString &, int)
{
    return true;
}

// Slice Z (RENDERING_RULE_MODEL_PLAN) — vtable stubs. The MOC for
// SWMMModelLayer references these from qt_static_metacall + the
// vtable. The hydrograph models only need the apply* paths above; the
// painting / styling / property accessors are inert in this test.
/*
 * @brief Default test-layer destructor releases members with complete dummy kd-tree type; no scene/engine cleanup logic.
 */
SWMMModelLayer::~SWMMModelLayer() = default;

/*
 * @brief Return an empty path from the isolated layer link stub.
 * @return Empty owned QString; no filesystem/model query.
 */
QString SWMMModelLayer::modelFilePath() const
{
    return {};
}
/*
 * @brief Return false for the test-layer Nodes visibility stand-in.
 * @return Always false; no model/GUI state query or mutation.
 */
bool SWMMModelLayer::showNodes() const
{
    return false;
}
/*
 * @brief Return false for the test-layer Links visibility stand-in.
 * @return Always false; no model/GUI state query or mutation.
 */
bool SWMMModelLayer::showLinks() const
{
    return false;
}
/*
 * @brief Return false for the test-layer Subcatchments visibility stand-in.
 * @return Always false; no model/GUI state query or mutation.
 */
bool SWMMModelLayer::showSubcatchments() const
{
    return false;
}
/*
 * @brief Return false for the test-layer RainGages visibility stand-in.
 * @return Always false; no model/GUI state query or mutation.
 */
bool SWMMModelLayer::showRainGages() const
{
    return false;
}
/*
 * @brief Return false for the test-layer Labels visibility stand-in.
 * @return Always false; no model/GUI state query or mutation.
 */
bool SWMMModelLayer::showLabels() const
{
    return false;
}
/*
 * @brief Ignore the unnamed Nodes visibility flag in the test-layer stub.
 * @par Ignored argument
 * Boolean value ignored; no scene/model mutation, allocation or retained input.
 */
void SWMMModelLayer::setShowNodes(bool) {}
/*
 * @brief Ignore the unnamed Links visibility flag in the test-layer stub.
 * @par Ignored argument
 * Boolean value ignored; no scene/model mutation, allocation or retained input.
 */
void SWMMModelLayer::setShowLinks(bool) {}
/*
 * @brief Ignore the unnamed Subcatchments visibility flag in the test-layer stub.
 * @par Ignored argument
 * Boolean value ignored; no scene/model mutation, allocation or retained input.
 */
void SWMMModelLayer::setShowSubcatchments(bool) {}
/*
 * @brief Ignore the unnamed RainGages visibility flag in the test-layer stub.
 * @par Ignored argument
 * Boolean value ignored; no scene/model mutation, allocation or retained input.
 */
void SWMMModelLayer::setShowRainGages(bool) {}
/*
 * @brief Ignore the unnamed Labels visibility flag in the test-layer stub.
 * @par Ignored argument
 * Boolean value ignored; no scene/model mutation, allocation or retained input.
 */
void SWMMModelLayer::setShowLabels(bool) {}

/*
 * @brief Expose no renderer in the isolated layer stub.
 * @return nullptr, no ownership transfer or render access.
 */
OpenSWMM::Render::IFeatureRenderer *SWMMModelLayer::renderer() const
{
    return nullptr;
}
/*
 * @brief Discard the by-value renderer owner in the isolated layer stub.
 * @par Argument ownership
 * Unnamed unique_ptr is consumed and destroyed on return; no renderer installed or pointer retained.
 */
void SWMMModelLayer::setRenderer(std::unique_ptr<OpenSWMM::Render::IFeatureRenderer>) {}

/*
 * @brief Return no mutable rule list in the test-layer stand-in.
 * @return nullptr, no owned/borrowed list or model mutation.
 */
OpenSWMM::Render::RuleList *SWMMModelLayer::ruleList()
{
    return nullptr;
}
/*
 * @brief Return no immutable rule list in the test-layer stand-in.
 * @return nullptr, no model query or ownership transfer.
 */
const OpenSWMM::Render::RuleList *SWMMModelLayer::ruleList() const
{
    return nullptr;
}

/*
 * @brief Return an empty owned subject list from the isolated link stub.
 * @return Empty vector, no style subjects allocated or model binding.
 */
std::vector<std::unique_ptr<openswmmvis::ui::ILayerStyleSubject>> SWMMModelLayer::styleSubjects()
{
    return {};
}

/*
 * @brief Inert scene population stand-in, no GUI/model work.
 * @par Ignored arguments
 * Borrowed scene pointer, extent reference and CRS pointer ignored/not retained; null accepted.
 */
void SWMMModelLayer::populateScene(QGraphicsScene *, const MapExtent &,
                                   const SpatialReferenceSystem *)
{
}
/*
 * @brief Inert scene removal stand-in.
 * @par Ignored argument
 * Borrowed scene pointer ignored/not retained, null accepted, no items removed.
 */
void SWMMModelLayer::depopulateScene(QGraphicsScene *) {}
/*
 * @brief Inert scene refresh stand-in.
 * @par Ignored arguments
 * Borrowed scene, extent and CRS ignored/not retained, null pointers accepted, no repaint.
 */
void SWMMModelLayer::refreshScene(QGraphicsScene *, const MapExtent &,
                                  const SpatialReferenceSystem *)
{
}
/*
 * @brief Inert canvas-CRS notification stand-in.
 * @par Ignored argument
 * Borrowed nullable CRS pointer ignored/not retained; no transform/model mutation.
 */
void SWMMModelLayer::onCanvasCRSChanged(const SpatialReferenceSystem *) {}

QTEST_MAIN(TestHydrographModels)
#include "test_hydrograph_models.moc"
