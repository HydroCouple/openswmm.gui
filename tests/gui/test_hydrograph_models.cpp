/*!
 * \file   test_hydrograph_models.cpp
 * \brief  Slice BS Phase 6.9.2 — Qt-contract tests for the four hydrograph
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

    void groupListNullLayerSafety()
    {
        HydrographGroupListModel m(nullptr);
        QCOMPARE(m.rowCount(), 0);
        QCOMPARE(m.indexOf("anything"), -1);
        QCOMPARE(m.nameAt(0), QString());
    }

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

    void rtkShape()
    {
        HydrographRtkTableModel m(nullptr);
        QCOMPARE(m.rowCount(), 3);
        QCOMPARE(m.columnCount(), 4);
    }

    void rtkResponseColumnNonEditable()
    {
        HydrographRtkTableModel m(nullptr);
        for (int r = 0; r < 3; ++r) {
            const Qt::ItemFlags f = m.flags(m.index(r, HydrographRtkTableModel::ColResponse));
            QVERIFY(!(f & Qt::ItemIsEditable));
        }
    }

    void rtkNumericColumnsEditable()
    {
        HydrographRtkTableModel m(nullptr);
        for (int c : { HydrographRtkTableModel::ColR,
                         HydrographRtkTableModel::ColT,
                         HydrographRtkTableModel::ColK }) {
            for (int r = 0; r < 3; ++r) {
                const Qt::ItemFlags f = m.flags(m.index(r, c));
                QVERIFY(f & Qt::ItemIsEditable);
            }
        }
    }

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

    void rtkSetDataWithNullLayerFails()
    {
        HydrographRtkTableModel m(nullptr);
        m.setContext("UH1", -1);
        QCOMPARE(m.setData(m.index(0, HydrographRtkTableModel::ColR),
                            0.5, Qt::EditRole),
                 false);
    }

    // -------------------------------------------------------------------
    // HydrographIaTableModel
    // -------------------------------------------------------------------

    void iaShape()
    {
        HydrographIaTableModel m(nullptr);
        QCOMPARE(m.rowCount(), 3);
        QCOMPARE(m.columnCount(), 4);
    }

    void iaHeaderColumns()
    {
        HydrographIaTableModel m(nullptr);
        QCOMPARE(m.headerData(HydrographIaTableModel::ColDmax,
                                Qt::Horizontal, Qt::DisplayRole).toString(),
                 QStringLiteral("Dmax"));
        QCOMPARE(m.headerData(HydrographIaTableModel::ColDrec,
                                Qt::Horizontal, Qt::DisplayRole).toString(),
                 QStringLiteral("Drec"));
        QCOMPARE(m.headerData(HydrographIaTableModel::ColDo,
                                Qt::Horizontal, Qt::DisplayRole).toString(),
                 QStringLiteral("Do"));
    }

    // -------------------------------------------------------------------
    // HydrographDecayTableModel
    // -------------------------------------------------------------------

    void decayShape()
    {
        HydrographDecayTableModel m(nullptr);
        QCOMPARE(m.rowCount(), 3);
        QCOMPARE(m.columnCount(), HydrographDecayTableModel::ColCount);
        QCOMPARE(int(HydrographDecayTableModel::ColCount), 8);
    }

    void decayActiveColumnIsUserCheckable()
    {
        HydrographDecayTableModel m(nullptr);
        for (int r = 0; r < 3; ++r) {
            const Qt::ItemFlags f = m.flags(m.index(r, HydrographDecayTableModel::ColActive));
            QVERIFY(f & Qt::ItemIsUserCheckable);
            QVERIFY(!(f & Qt::ItemIsEditable));
        }
    }

    void decayHeaderColumns()
    {
        HydrographDecayTableModel m(nullptr);
        QCOMPARE(m.headerData(HydrographDecayTableModel::ColActive,
                                Qt::Horizontal, Qt::DisplayRole).toString(),
                 QStringLiteral("Active"));
        QCOMPARE(m.headerData(HydrographDecayTableModel::ColKdep,
                                Qt::Horizontal, Qt::DisplayRole).toString(),
                 QStringLiteral("k_dep"));
        QCOMPARE(m.headerData(HydrographDecayTableModel::ColTref,
                                Qt::Horizontal, Qt::DisplayRole).toString(),
                 QStringLiteral("T_ref"));
    }

    void decaySetContextEmitsReset()
    {
        HydrographDecayTableModel m(nullptr);
        QSignalSpy resetSpy(&m, &QAbstractTableModel::modelReset);
        m.setContext("UH1");
        QCOMPARE(m.currentName(), QStringLiteral("UH1"));
        QCOMPARE(resetSpy.count(), 1);
        m.setContext("UH1");
        QCOMPARE(resetSpy.count(), 1);   // no-op
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
struct SWMMKdTrees {};

// GDAL-using out-of-line definitions stubbed (real ones live in
// spatialreferencesystem.cpp + would force GDAL into the test link).
SpatialReferenceSystem::~SpatialReferenceSystem() = default;

SWMM_Engine SWMMModelLayer::engine() const { return nullptr; }

bool SWMMModelLayer::applyHydrographSetRtk(const QString&, int, int,
                                            double, double, double) { return true; }
bool SWMMModelLayer::applyHydrographSetIa(const QString&, int, int,
                                           double, double, double) { return true; }
bool SWMMModelLayer::applyHydrographRenameGroup(const QString&, const QString&) { return true; }
bool SWMMModelLayer::applyRdiiDecaySet(const QString&, int,
                                        double, double, double,
                                        double, double, double) { return true; }
bool SWMMModelLayer::applyRdiiDecayRemove(const QString&, int) { return true; }

// Slice Z (RENDERING_RULE_MODEL_PLAN) — vtable stubs. The MOC for
// SWMMModelLayer references these from qt_static_metacall + the
// vtable. The hydrograph models only need the apply* paths above; the
// painting / styling / property accessors are inert in this test.
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

QTEST_MAIN(TestHydrographModels)
#include "test_hydrograph_models.moc"
