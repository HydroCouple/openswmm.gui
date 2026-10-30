/**
 * @file      test_transect_editor_dialog.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BQ Phase 6.7.4 — TransectProvider + TransectRegistry +
 *         TransectListModel + TransectStationTableModel + TransectChartView
 *         + TransectEditorDialog coverage.
 *
 * Initial provider/model/editor scenarios (additional cases below cover lifetime,
 * property binding, undo commands, property-tree layout and closing):
 *
 *  TransectProvider (10):
 * - setAllPoints rejects non-monotone stations + emits mutationRejected.
 * - setElevationAt validates index; emits pointsChanged.
 * - setPointAt enforces strict-monotone with neighbours.
 * - setPointLive clamps station between neighbours; clamped flag set.
 * - insertPoint inserts at sorted position; rejects equal-station collision.
 * - removePointsAt removes duplicate indices once and retains the other points.
 * - setRoughness emits roughnessChanged exactly once on change, none on no-op.
 * - setBankStations / setEncroachmentStations are independent signals.
 * - setModifiers emits modifiersChanged.
 * - setName emits nameChanged; setComments emits commentsChanged.
 *
 *  TransectRegistry (4):
 * - create rejects duplicates (case-insensitive) and empty name.
 * - rename updates byLowerName + emits providerRenamed.
 * - remove emits providerAboutToBeRemoved and leaves the registry empty.
 * - registry rebroadcasts provider mutations via providerMetadataChanged.
 *
 *  TransectListModel (2):
 * - rowCount + data(DisplayRole) match registry contents; rename refreshes
 *      via dataChanged.
 * - setData(EditRole) routes through registry rename; refuses duplicates.
 *
 *  TransectStationTableModel (3):
 * - headerData reflects unitsSuffix override.
 * - setData(col 1) updates elevation; chart-bound side stays consistent.
 * - setData(col 0) X-edit enforced via provider monotonicity (refused at
 *      neighbour collision).
 *
 *  TransectChartView (3):
 * - setProvider rebuilds the channel line for a transect without configured banks.
 * - Channel line populates only between bank stations.
 * - Style setters emit their corresponding notifications.
 *
 *  TransectEditorDialog (2):
 * - Dialog opens with first provider bound; list selection rebinds table
 *      and chart.
 * - Delete confirmation bypass (deleteCurrentSilently) removes provider.
 *
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "dialog_a11y_checks.h"
#include "transect/transectprovider.h"
#include "transect/transectregistry.h"
#include "transect/transectundocommands.h"
#include "ui/dialogs/transecteditordialog.h"
#include "ui/dialogs/transectpropertybag.h"
#include "ui/models/transectlistmodel.h"
#include "ui/models/transectstationtablemodel.h"
#include "ui/widgets/transectchartview.h"

#include <QDialogButtonBox>
#include <QLineSeries>
#include <QListView>
#include <QObject>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableView>
#include <QTest>
#include <QTimer>
#include <QApplication>
#include <QTreeView>
#include <QUndoStack>
#include <QAbstractItemModel>

using openswmmvis::transect::TransectPoint;
using openswmmvis::transect::TransectProvider;
using openswmmvis::transect::TransectRegistry;
using openswmmvis::ui::TransectChartView;
using openswmmvis::ui::TransectEditorDialog;
using openswmmvis::ui::TransectListModel;
using openswmmvis::ui::TransectPropertyBag;
using openswmmvis::ui::TransectStationTableModel;

class TestTransectEditorDialog : public QObject
{
    Q_OBJECT

private slots:

    /**
     *
     * @brief Require registry destruction to hide/disable its editor and empty the station model.
     * Retains the undo stack while deleting the registry/provider; does not invoke a stale
     * callback or delete a provider independently of the registry.
     *
     */
    void ownerRegistryDeletionInvalidatesEditor()
    {
        auto *registry = new TransectRegistry;
        auto *provider = registry->create(QStringLiteral("Owned"));
        QVERIFY(provider);
        QUndoStack stack;
        TransectEditorDialog dialog(registry, nullptr, &stack);
        dialog.show();
        delete registry;
        QVERIFY2(!dialog.isEnabled(),
                 "A closed registry must disable the editor before stale callbacks can mutate it.");
        QVERIFY(!dialog.isVisible());
        QCOMPARE(dialog.tableModel()->rowCount(), 0);
    }

    /**
     *
     * @brief Require undo-stack destruction to hide/disable the editor and empty its model.
     * Checks that the surviving registry still owns the exact original provider; does not
     * attempt an edit or undo operation afterward.
     *
     */
    void undoOwnerDeletionInvalidatesEditor()
    {
        TransectRegistry registry;
        auto *provider = registry.create(QStringLiteral("Owned"));
        QVERIFY(provider);
        auto *stack = new QUndoStack;
        TransectEditorDialog dialog(&registry, nullptr, stack);
        dialog.show();
        delete stack;
        QVERIFY2(!dialog.isEnabled(), "The editor must not retain a dangling undo owner.");
        QVERIFY(!dialog.isVisible());
        QCOMPARE(dialog.tableModel()->rowCount(), 0);
        QVERIFY(registry.findByName(QStringLiteral("Owned")) == provider);
    }

    /**
     *
     * @brief Require a station table accessible name and a nonempty editor description.
     * Checks metadata presence only, without exact wording, screen-reader use or keyboard
     * navigation assertions.
     *
     */
    void editorTableHasAccessiblePurpose()
    {
        TransectRegistry registry;
        auto *provider = registry.create(QStringLiteral("Owned"));
        QUndoStack stack;
        TransectEditorDialog dialog(&registry, nullptr, &stack);
        auto *table = dialog.stationTable();
        QVERIFY(table);
        QVERIFY2(!table->accessibleName().isEmpty(),
                 "Data tables need a specific accessible name, beyond their row/column cells.");
        QVERIFY2(!dialog.accessibleDescription().isEmpty(),
                 "Live editors must explain that Close does not undo applied edits.");
    }

    // ── TransectProvider ────────────────────────────────────────────────────

    /**
     *
     * @brief Require a descending station pair to be refused with one rejection and sorted points
     * to succeed.
     * Checks accepted count three; does not separately compare values after rejection or
     * exercise equal/non-finite stations.
     *
     */
    void setAllPoints_RejectsNonMonotone()
    {
        TransectProvider p(QStringLiteral("T1"));
        QSignalSpy rejected(&p, &TransectProvider::mutationRejected);
        QVERIFY(!p.setAllPoints({{0.0, 100.0}, {2.0, 99.0}, {1.0, 98.0}}));
        QCOMPARE(rejected.size(), 1);
        QVERIFY(p.setAllPoints({{0.0, 100.0}, {1.0, 99.0}, {2.0, 98.0}}));
        QCOMPARE(p.pointCount(), 3);
    }

    /**
     *
     * @brief Require an elevation edit to store 4.5 and emit once while index 99 is refused.
     * Uses two points and does not inspect rejection signal counts, payloads or non-finite
     * values; units remain the provider's authored station/elevation units.
     *
     */
    void setElevationAt_ValidatesIndex()
    {
        TransectProvider p(QStringLiteral("T2"));
        QVERIFY(p.setAllPoints({{0.0, 10.0}, {1.0, 5.0}}));
        QSignalSpy changed(&p, &TransectProvider::pointsChanged);
        QVERIFY(p.setElevationAt(1, 4.5));
        QCOMPARE(changed.size(), 1);
        QCOMPARE(p.pointAt(1).elevation, 4.5);
        QVERIFY(!p.setElevationAt(99, 0.0));
    }

    /**
     *
     * @brief Require a middle station colliding with either neighbour to fail and station 0.5 to
     * succeed.
     * The right test equals station two rather than exceeding it; checks stored station
     * only, without elevation, signal or non-finite assertions.
     *
     */
    void setPointAt_EnforcesNeighbourBounds()
    {
        TransectProvider p(QStringLiteral("T3"));
        QVERIFY(p.setAllPoints({{0.0, 5.0}, {1.0, 4.0}, {2.0, 3.0}}));
        // Equal-to-left → refused.
        QVERIFY(!p.setPointAt(1, 0.0, 4.0));
        // Greater-than-right → refused.
        QVERIFY(!p.setPointAt(1, 2.0, 4.0));
        // In between → OK.
        QVERIFY(p.setPointAt(1, 0.5, 4.0));
        QCOMPARE(p.pointAt(1).station, 0.5);
    }

    /**
     *
     * @brief Require an oversized live drag to clamp strictly inside neighbouring stations and
     * report clamping.
     * Then accepts station 0.5 without clamping. Checks returned flag and bounds, without
     * pinning clamp epsilon or sending actual mouse events.
     *
     */
    void setPointLive_ClampsBetweenNeighbours()
    {
        TransectProvider p(QStringLiteral("T4"));
        QVERIFY(p.setAllPoints({{0.0, 10.0}, {1.0, 5.0}, {2.0, 8.0}}));
        bool clamped = false;
        // Try to push idx 1 past the right neighbour at 2.0 — gets clamped.
        QVERIFY(p.setPointLive(1, 5.0, 5.0, &clamped));
        QVERIFY(clamped);
        QVERIFY(p.pointAt(1).station < 2.0);
        QVERIFY(p.pointAt(1).station > 0.0);

        // Within bounds → no clamp.
        clamped = false;
        QVERIFY(p.setPointLive(1, 0.5, 5.0, &clamped));
        QVERIFY(!clamped);
        QCOMPARE(p.pointAt(1).station, 0.5);
    }

    /**
     *
     * @brief Require station one to insert at sorted index one and an identical station to return
     * minus one.
     * Checks one rejection signal for collision; does not inspect vector size or undo
     * history after refusal.
     *
     */
    void insertPoint_SortedInsertRejectsCollision()
    {
        TransectProvider p(QStringLiteral("T5"));
        QVERIFY(p.setAllPoints({{0.0, 0.0}, {2.0, 0.0}, {4.0, 0.0}}));
        QCOMPARE(p.insertPoint(1.0, 0.0), 1);
        QCOMPARE(p.pointAt(1).station, 1.0);

        // Collision rejected.
        QSignalSpy rejected(&p, &TransectProvider::mutationRejected);
        QCOMPARE(p.insertPoint(1.0, 0.0), -1);
        QCOMPARE(rejected.size(), 1);
    }

    /**
     *
     * @brief Require duplicate indices one/one/three to remove each point once and retain
     * stations zero/two.
     * Uses ascending-with-duplicates input; this case does not separately test reverse
     * ordering, invalid indices or removal of every station.
     *
     */
    void removePointsAt_HandlesDuplicates()
    {
        TransectProvider p(QStringLiteral("T6"));
        QVERIFY(p.setAllPoints({{0.0, 0.0}, {1.0, 0.0}, {2.0, 0.0}, {3.0, 0.0}}));
        // Duplicates + unsorted should still leave 2 points.
        p.removePointsAt({1, 1, 3});
        QCOMPARE(p.pointCount(), 2);
        QCOMPARE(p.pointAt(0).station, 0.0);
        QCOMPARE(p.pointAt(1).station, 2.0);
    }

    /**
     *
     * @brief Require default and repeated roughness assignments to emit nothing and one changed
     * assignment to emit once.
     * Checks signal count only, without payloads, roughness validation or undo.
     *
     */
    void setRoughness_EmitsOnceOnChange()
    {
        TransectProvider p(QStringLiteral("T7"));
        QSignalSpy spy(&p, &TransectProvider::roughnessChanged);
        p.setRoughness(0.030, 0.030, 0.030); // matches default → no-op
        QCOMPARE(spy.size(), 0);
        p.setRoughness(0.040, 0.030, 0.030);
        QCOMPARE(spy.size(), 1);
        p.setRoughness(0.040, 0.030, 0.030); // no change
        QCOMPARE(spy.size(), 1);
    }

    /**
     *
     * @brief Require bank and encroachment assignments to emit only their respective signals.
     * Checks left-bank ten and left-encroachment eight plus signal counts; does not compare
     * right-side getters, geometry rendering or undo history.
     *
     */
    void bankAndEncroachment_AreIndependent()
    {
        TransectProvider p(QStringLiteral("T8"));
        QSignalSpy bankSpy(&p, &TransectProvider::bankStationsChanged);
        QSignalSpy encSpy(&p, &TransectProvider::encroachmentStationsChanged);
        p.setBankStations(10.0, 20.0);
        QCOMPARE(bankSpy.size(), 1);
        QCOMPARE(encSpy.size(), 0);
        p.setEncroachmentStations(8.0, 22.0);
        QCOMPARE(bankSpy.size(), 1);
        QCOMPARE(encSpy.size(), 1);
        QCOMPARE(p.xLeftBank(), 10.0);
        QCOMPARE(p.xLeftEncroachment(), 8.0);
    }

    /**
     *
     * @brief Require default modifiers to be silent and a changed triple to emit once with stored
     * values.
     * Pins station multiplier two, elevation offset one and meander factor 1.5; no invalid
     * value or hydraulic calculation coverage.
     *
     */
    void setModifiers_EmitsSignal()
    {
        TransectProvider p(QStringLiteral("T9"));
        QSignalSpy spy(&p, &TransectProvider::modifiersChanged);
        p.setModifiers(1.0, 0.0, 1.0); // defaults — no change
        QCOMPARE(spy.size(), 0);
        p.setModifiers(2.0, 1.0, 1.5);
        QCOMPARE(spy.size(), 1);
        QCOMPARE(p.stationMultiplier(), 2.0);
        QCOMPARE(p.elevationOffset(), 1.0);
        QCOMPARE(p.meanderFactor(), 1.5);
    }

    /**
     *
     * @brief Require unchanged name/comments to be silent and each real edit to emit once.
     * Uses a standalone provider; does not test registry indexing, exact signal arguments
     * or input-file persistence.
     *
     */
    void nameAndComments_EmitOnChange()
    {
        TransectProvider p(QStringLiteral("T10"));
        QSignalSpy nameSpy(&p, &TransectProvider::nameChanged);
        QSignalSpy comSpy(&p, &TransectProvider::commentsChanged);
        p.setName(QStringLiteral("T10")); // no change
        QCOMPARE(nameSpy.size(), 0);
        p.setName(QStringLiteral("T10-renamed"));
        QCOMPARE(nameSpy.size(), 1);

        p.setComments(QStringLiteral("hello"));
        QCOMPARE(comSpy.size(), 1);
        p.setComments(QStringLiteral("hello"));
        QCOMPARE(comSpy.size(), 1);
    }

    // ── TransectRegistry ────────────────────────────────────────────────────

    /**
     *
     * @brief Require exact/case-folded duplicate and empty names to be refused after one
     * successful create.
     * Checks the final count remains one; does not inspect rejection diagnostics or engine
     * attachment.
     *
     */
    void registry_CreateRejectsDuplicates()
    {
        TransectRegistry reg;
        QVERIFY(reg.create(QStringLiteral("A")) != nullptr);
        QCOMPARE(reg.create(QStringLiteral("A")), nullptr); // exact dup
        QCOMPARE(reg.create(QStringLiteral("a")), nullptr); // case-insensitive
        QCOMPARE(reg.create(QString()), nullptr);           // empty
        QCOMPARE(reg.providerCount(), 1);
    }

    /**
     *
     * @brief Require rename to replace old lookup, accept uppercase lookup and reject another
     * provider's collision.
     * Checks one successful rename signal and pointer identity; does not check the signal
     * payload or failed provider's stored name separately.
     *
     */
    void registry_RenameSyncsByLowerName()
    {
        TransectRegistry reg;
        auto *p = reg.create(QStringLiteral("Alpha"));
        QSignalSpy renamed(&reg, &TransectRegistry::providerRenamed);
        QVERIFY(reg.rename(p, QStringLiteral("Beta")));
        QCOMPARE(renamed.size(), 1);
        QVERIFY(reg.findByName(QStringLiteral("BETA")) == p);
        QCOMPARE(reg.findByName(QStringLiteral("Alpha")), nullptr);
        // Conflicting rename refused.
        auto *p2 = reg.create(QStringLiteral("Gamma"));
        QVERIFY(!reg.rename(p2, QStringLiteral("Beta")));
    }

    /**
     *
     * @brief Require removal of the only provider to emit once and empty its registry.
     * Signal count does not prove callback ordering relative to deletion; no removed pointer
     * is dereferenced and no undo is tested.
     *
     */
    void registry_RemoveEmitsAboutToBeRemoved()
    {
        TransectRegistry reg;
        auto *p = reg.create(QStringLiteral("X"));
        QSignalSpy spy(&reg, &TransectRegistry::providerAboutToBeRemoved);
        reg.remove(p);
        QCOMPARE(spy.size(), 1);
        QCOMPARE(reg.providerCount(), 0);
    }

    /**
     *
     * @brief Require roughness and bank changes to produce at least two registry metadata
     * notifications.
     * Uses one provider and does not pin exact count, payload or signal order.
     *
     */
    void registry_RebroadcastsMetadataChanges()
    {
        TransectRegistry reg;
        auto *p = reg.create(QStringLiteral("M"));
        QSignalSpy spy(&reg, &TransectRegistry::providerMetadataChanged);
        p->setRoughness(0.05, 0.05, 0.05);
        p->setBankStations(1.0, 9.0);
        QVERIFY(spy.size() >= 2);
    }

    // ── TransectListModel ───────────────────────────────────────────────────

    /**
     *
     * @brief Require registry additions and rename to update model row count/display label and
     * emit dataChanged.
     * Checks first-row identity and at least one notification; does not check insertion
     * signal ranges or a sorted/filtering proxy.
     *
     */
    void listModel_ReflectsRegistryAndRenames()
    {
        TransectRegistry reg;
        TransectListModel m;
        m.setRegistry(&reg);
        QCOMPARE(m.rowCount(), 0);
        auto *p = reg.create(QStringLiteral("L1"));
        reg.create(QStringLiteral("L2"));
        QCOMPARE(m.rowCount(), 2);
        QCOMPARE(m.data(m.index(0)).toString(), QStringLiteral("L1"));

        QSignalSpy changed(&m, &QAbstractItemModel::dataChanged);
        QVERIFY(reg.rename(p, QStringLiteral("L1-renamed")));
        QCOMPARE(m.data(m.index(0)).toString(), QStringLiteral("L1-renamed"));
        QVERIFY(changed.size() >= 1);
    }

    /**
     *
     * @brief Require EditRole rename through the list model to update lookup and reject a
     * collision.
     * Checks the Z lookup after renaming A; no undo or post-refusal model value assertion.
     *
     */
    void listModel_SetDataRoutesThroughRegistry()
    {
        TransectRegistry reg;
        TransectListModel m;
        m.setRegistry(&reg);
        reg.create(QStringLiteral("A"));
        reg.create(QStringLiteral("B"));
        QVERIFY(m.setData(m.index(0), QStringLiteral("Z"), Qt::EditRole));
        QCOMPARE(reg.findByName(QStringLiteral("Z"))->name(), QStringLiteral("Z"));
        // Duplicate refused.
        QVERIFY(!m.setData(m.index(0), QStringLiteral("B"), Qt::EditRole));
    }

    // ── TransectStationTableModel ───────────────────────────────────────────

    /**
     *
     * @brief Require both horizontal station/elevation headers to include the chosen m suffix.
     * Does not bind points, assert full labels or convert stored values.
     *
     */
    void tableModel_HeaderHonoursUnitsSuffix()
    {
        TransectStationTableModel m;
        m.setUnitsSuffix(QStringLiteral("(m)"));
        QVERIFY(m.headerData(0, Qt::Horizontal).toString().contains(QStringLiteral("(m)")));
        QVERIFY(m.headerData(1, Qt::Horizontal).toString().contains(QStringLiteral("(m)")));
    }

    /**
     *
     * @brief Require a row-one elevation edit through EditRole to reach provider and model
     * display as 4.5.
     * Uses three points; no file round trip, chart assertion or undo is performed.
     *
     */
    void tableModel_SetDataElevationRoundtrip()
    {
        TransectProvider p(QStringLiteral("TT"));
        QVERIFY(p.setAllPoints({{0.0, 10.0}, {1.0, 5.0}, {2.0, 8.0}}));
        TransectStationTableModel m;
        m.setProvider(&p);
        QVERIFY(m.setData(m.index(1, 1), 4.5, Qt::EditRole));
        QCOMPARE(p.pointAt(1).elevation, 4.5);
        QCOMPARE(m.data(m.index(1, 1)).toDouble(), 4.5);
    }

    /**
     *
     * @brief Require moving the middle station beyond its right neighbour to fail without
     * changing station one.
     * Then accepts station 0.5. Checks provider station values only, without signal counts,
     * live-drag clamping or elevation comparisons.
     *
     */
    void tableModel_SetDataXEditRefusedOnCollision()
    {
        TransectProvider p(QStringLiteral("TT2"));
        QVERIFY(p.setAllPoints({{0.0, 0.0}, {1.0, 0.0}, {2.0, 0.0}}));
        TransectStationTableModel m;
        m.setProvider(&p);
        // Try to push middle station past the right neighbour.
        QVERIFY(!m.setData(m.index(1, 0), 2.5, Qt::EditRole));
        QCOMPARE(p.pointAt(1).station, 1.0);
        // OK move within bounds.
        QVERIFY(m.setData(m.index(1, 0), 0.5, Qt::EditRole));
        QCOMPARE(p.pointAt(1).station, 0.5);
    }

    // ── TransectChartView ───────────────────────────────────────────────────

    /**
     *
     * @brief Require a no-bank three-point transect to populate the channel and follow an
     * elevation edit.
     * Checks channel count and changed middle y value; does not assert overbank emptiness
     * or interactively drag a handle.
     *
     */
    void chartView_RebuildsTopographyFromProvider()
    {
        // Without bank stations set, the channel section absorbs everything —
        // left and right overbanks come up empty. This pins the "no banks
        // configured" path.
        TransectChartView view;
        TransectProvider p(QStringLiteral("CV"));
        QVERIFY(p.setAllPoints({{0.0, 10.0}, {5.0, 0.0}, {10.0, 10.0}}));
        view.setProvider(&p);
        QVERIFY(view.channelLine() != nullptr);
        QCOMPARE(view.channelLine()->count(), 3);
        // Modify a point and confirm the channel series re-syncs.
        QVERIFY(p.setElevationAt(1, 1.0));
        QCOMPARE(view.channelLine()->at(1).y(), 1.0);
    }

    /**
     *
     * @brief Require bank-aligned topography to form two-point shoulders and channel vertices
     * within banks.
     * Checks shoulder endpoints at zero/three and seven/ten, and channel x bounds with
     * 1e-6 tolerance. Since input vertices already align with banks, this case alone does
     * not test interpolation at a new station.
     *
     */
    void chartView_SplitsAtBankStations()
    {
        // Bank stations partition the topography into three series:
        //   left overbank  : stations ≤ xLeftBank   (overbank colour)
        //   channel        : stations ∈ [xL, xR]    (channel colour)
        //   right overbank : stations ≥ xRightBank  (overbank colour)
        // Interpolated cut vertices land exactly at each bank station so the
        // three series meet pixel-perfectly at the bank markers.
        TransectChartView view;
        TransectProvider p(QStringLiteral("CV2"));
        QVERIFY(p.setAllPoints({{0.0, 10.0}, {3.0, 0.0}, {7.0, 0.0}, {10.0, 10.0}}));
        p.setBankStations(3.0, 7.0);
        view.setProvider(&p);

        QVERIFY(view.leftOverbankLine() != nullptr);
        QVERIFY(view.channelLine() != nullptr);
        QVERIFY(view.rightOverbankLine() != nullptr);

        // Left shoulder: covers (0.0 → 3.0), so 2 vertices.
        QCOMPARE(view.leftOverbankLine()->count(), 2);
        QCOMPARE(view.leftOverbankLine()->at(0).x(), 0.0);
        QCOMPARE(view.leftOverbankLine()->at(1).x(), 3.0);

        // Right shoulder: covers (7.0 → 10.0), so 2 vertices.
        QCOMPARE(view.rightOverbankLine()->count(), 2);
        QCOMPARE(view.rightOverbankLine()->at(0).x(), 7.0);
        QCOMPARE(view.rightOverbankLine()->at(1).x(), 10.0);

        // Channel: covers [3.0, 7.0]; we passed coincident bank-aligned
        // vertices, so it's exactly 2 points.
        QVERIFY(view.channelLine()->count() >= 2);
        for (int i = 0; i < view.channelLine()->count(); ++i) {
            const auto pt = view.channelLine()->at(i);
            QVERIFY(pt.x() >= 3.0 - 1e-6);
            QVERIFY(pt.x() <= 7.0 + 1e-6);
        }
    }

    /**
     *
     * @brief Require moving banks inside existing segments to refresh shoulder counts and channel
     * endpoints.
     * Tightens banks from two/eight to three/seven, checks shoulders do not shrink and
     * channel endpoints equal new banks; does not compare interpolated elevations.
     *
     */
    void chartView_BankChangeReslicesTopography()
    {
        // Moving the bank stations must re-cut all three series — no stale
        // shoulder/channel data.
        TransectChartView view;
        TransectProvider p(QStringLiteral("CV3"));
        QVERIFY(p.setAllPoints({{0.0, 10.0}, {2.0, 4.0}, {5.0, 0.0}, {8.0, 4.0}, {10.0, 10.0}}));
        p.setBankStations(2.0, 8.0);
        view.setProvider(&p);
        const int leftCntInitial = view.leftOverbankLine()->count();
        const int rightCntInitial = view.rightOverbankLine()->count();

        // Tighten the channel: shoulders should grow.
        p.setBankStations(3.0, 7.0);
        QVERIFY(view.leftOverbankLine()->count() >= leftCntInitial);
        QVERIFY(view.rightOverbankLine()->count() >= rightCntInitial);
        // Channel endpoints must hit the new bank stations exactly.
        QVERIFY(view.channelLine()->count() >= 2);
        QCOMPARE(view.channelLine()->at(0).x(), 3.0);
        QCOMPARE(view.channelLine()->at(view.channelLine()->count() - 1).x(), 7.0);
    }

    /**
     *
     * @brief Require overbank/channel/fill/handle-size setters to each emit one notification.
     * Reads back overbank green and size twelve only; no pen rendering or all-property
     * round-trip assertion is made.
     *
     */
    void chartView_StylePropertiesEmitSignals()
    {
        TransectChartView view;
        QSignalSpy overSpy(&view, &TransectChartView::overbankColorChanged);
        QSignalSpy chanSpy(&view, &TransectChartView::channelColorChanged);
        QSignalSpy fillSpy(&view, &TransectChartView::groundFillColorChanged);
        QSignalSpy sizeSpy(&view, &TransectChartView::handleSizeChanged);
        view.setOverbankColor(QColor("#00FF00"));
        view.setChannelColor(QColor("#FF00FF"));
        view.setGroundFillColor(QColor(0, 0, 0, 80));
        view.setHandleSize(12);
        QCOMPARE(overSpy.size(), 1);
        QCOMPARE(chanSpy.size(), 1);
        QCOMPARE(fillSpy.size(), 1);
        QCOMPARE(sizeSpy.size(), 1);
        QCOMPARE(view.overbankColor(), QColor("#00FF00"));
        QCOMPARE(view.handleSize(), 12);
    }

    // ── TransectEditorDialog ────────────────────────────────────────────────

    /**
     *
     * @brief Require initial binding to the first provider and list selection to rebind table and
     * chart to the second.
     * Checks non-null child controls, row count and provider identities with a null model
     * layer; no engine persistence or actual mouse click is used.
     *
     */
    void dialog_BindsFirstProvider_AndSelectionRebinds()
    {
        TransectRegistry reg;
        auto *p1 = reg.create(QStringLiteral("First"));
        auto *p2 = reg.create(QStringLiteral("Second"));
        QVERIFY(p1->setAllPoints({{0.0, 10.0}, {5.0, 0.0}, {10.0, 10.0}}));
        QVERIFY(p2->setAllPoints({{0.0, 20.0}, {5.0, 5.0}, {10.0, 20.0}}));

        QUndoStack stack;
        TransectEditorDialog dlg(&reg, /*layer*/ nullptr, &stack);

        QVERIFY(dlg.listView() != nullptr);
        QVERIFY(dlg.listModel() != nullptr);
        QVERIFY(dlg.tableModel() != nullptr);
        QVERIFY(dlg.chartView() != nullptr);

        QCOMPARE(dlg.listModel()->rowCount(), 2);
        QCOMPARE(dlg.currentProvider(), p1);
        QCOMPARE(dlg.tableModel()->rowCount(), 3);

        // Select row 1 → bind p2.
        dlg.listView()->setCurrentIndex(dlg.listModel()->index(1));
        QCOMPARE(dlg.currentProvider(), p2);
        QCOMPARE(dlg.tableModel()->provider(), p2);
        QCOMPARE(dlg.chartView()->provider(), p2);
    }

    /**
     *
     * @brief Require silent deletion of the sole selected provider to empty its registry and
     * clear current binding.
     * Bypasses confirmation and does not inspect undo history or table/chart contents.
     *
     */
    void dialog_DeleteSilently_RemovesProvider()
    {
        TransectRegistry reg;
        reg.create(QStringLiteral("Solo"));
        QCOMPARE(reg.providerCount(), 1);

        QUndoStack stack;
        TransectEditorDialog dlg(&reg, nullptr, &stack);
        QVERIFY(dlg.currentProvider() != nullptr);
        dlg.deleteCurrentSilently();
        QCOMPARE(reg.providerCount(), 0);
        QCOMPARE(dlg.currentProvider(), nullptr);
    }

    /**
     *
     * @brief Require property-bag roughness edits to reach the provider and provider edits to
     * refresh bag getters.
     * Binds without an undo stack, checks left/right/channel values; no property tree edit
     * or file serialization is exercised.
     *
     */
    void propertyBag_ProviderRoundTrip()
    {
        TransectProvider p(QStringLiteral("Bag"));
        TransectPropertyBag bag;
        bag.bind(&p, nullptr);
        QCOMPARE(bag.nLeftBank(), p.nLeftBank());
        // Push from bag → provider.
        bag.setNLeftBank(0.07);
        QCOMPARE(p.nLeftBank(), 0.07);
        // Push from provider → bag.
        p.setRoughness(0.05, 0.06, 0.08);
        QCOMPARE(bag.nLeftBank(), 0.05);
        QCOMPARE(bag.nRightBank(), 0.06);
        QCOMPARE(bag.nChannel(), 0.08);
    }

    // ── Undo / redo commands ────────────────────────────────────────────────

    /**
     *
     * @brief Require pushing roughness to apply all three values and undo to restore the original
     * triple.
     * Redo checks left/channel values; the stack owns the heap command while the provider
     * outlives it. No command merging or removed-provider behavior is tested.
     *
     */
    void undoCommand_RoughnessRoundTrip()
    {
        TransectProvider p(QStringLiteral("U1"));
        p.setRoughness(0.030, 0.030, 0.030);
        QUndoStack stack;
        stack.push(new openswmmvis::transect::SetRoughnessCommand(&p, 0.05, 0.06, 0.08));
        QCOMPARE(p.nLeftBank(), 0.05);
        QCOMPARE(p.nRightBank(), 0.06);
        QCOMPARE(p.nChannel(), 0.08);

        stack.undo();
        QCOMPARE(p.nLeftBank(), 0.030);
        QCOMPARE(p.nRightBank(), 0.030);
        QCOMPARE(p.nChannel(), 0.030);

        stack.redo();
        QCOMPARE(p.nLeftBank(), 0.05);
        QCOMPARE(p.nChannel(), 0.08);
    }

    /**
     *
     * @brief Require separate bank/encroachment commands to apply independently and undo in
     * reverse order.
     * Checks all initial endpoints, restored left encroachment zero and preserved left bank,
     * then restored left bank zero; no redo or full right-side undo comparison.
     *
     */
    void undoCommand_BankAndEncroachmentIndependent()
    {
        TransectProvider p(QStringLiteral("U2"));
        QUndoStack stack;
        stack.push(new openswmmvis::transect::SetBankStationsCommand(&p, 1.0, 9.0));
        stack.push(new openswmmvis::transect::SetEncroachmentStationsCommand(&p, 0.5, 9.5));
        QCOMPARE(p.xLeftBank(), 1.0);
        QCOMPARE(p.xRightBank(), 9.0);
        QCOMPARE(p.xLeftEncroachment(), 0.5);
        QCOMPARE(p.xRightEncroachment(), 9.5);

        stack.undo(); // undoes encroachment
        QCOMPARE(p.xLeftEncroachment(), 0.0);
        QCOMPARE(p.xLeftBank(), 1.0); // bank unaffected
        stack.undo();                 // undoes bank
        QCOMPARE(p.xLeftBank(), 0.0);
    }

    /**
     *
     * @brief Require a modifier command to apply its triple and undo to restore all default
     * values.
     * Checks station multiplier/elevation offset/meander factor; no redo or invalid input.
     *
     */
    void undoCommand_ModifiersRoundTrip()
    {
        TransectProvider p(QStringLiteral("U3"));
        QUndoStack stack;
        stack.push(new openswmmvis::transect::SetModifiersCommand(&p, 2.0, 1.0, 1.5));
        QCOMPARE(p.stationMultiplier(), 2.0);
        QCOMPARE(p.elevationOffset(), 1.0);
        QCOMPARE(p.meanderFactor(), 1.5);

        stack.undo();
        QCOMPARE(p.stationMultiplier(), 1.0); // default
        QCOMPARE(p.elevationOffset(), 0.0);
        QCOMPARE(p.meanderFactor(), 1.0);
    }

    /**
     *
     * @brief Require a comments command to replace original text and undo to restore it.
     * Uses a standalone provider and stack-owned command; no redo or file round trip.
     *
     */
    void undoCommand_CommentsRoundTrip()
    {
        TransectProvider p(QStringLiteral("U4"));
        p.setComments(QStringLiteral("original"));
        QUndoStack stack;
        stack.push(
            new openswmmvis::transect::SetCommentsCommand(&p, QStringLiteral("new comment")));
        QCOMPARE(p.comments(), QStringLiteral("new comment"));
        stack.undo();
        QCOMPARE(p.comments(), QStringLiteral("original"));
    }

    /**
     *
     * @brief Require a finalized station move command to apply, undo and redo both station and
     * elevation.
     * Models the drag-finalize values directly at point one; does not send chart mouse
     * events or test the live-drag revert signal path itself.
     *
     */
    void undoCommand_MoveStationPointRoundTrip()
    {
        // Models the chart-drag finalize path: pre-drag values are captured
        // on press, post-drag values applied via setPointLive during drag,
        // then mouseReleaseEvent reverts to pre-drag and emits the signal;
        // the dialog pushes one command whose redo() reapplies the move.
        TransectProvider p(QStringLiteral("U5"));
        QVERIFY(p.setAllPoints({{0.0, 10.0}, {5.0, 0.0}, {10.0, 10.0}}));

        QUndoStack stack;
        // Pre-drag state at idx 1 = (5.0, 0.0). Imagine dragging to (4.5, -1.0).
        // The chart view reverted to (5.0, 0.0) before pushing.
        QCOMPARE(p.pointAt(1).station, 5.0);
        QCOMPARE(p.pointAt(1).elevation, 0.0);
        stack.push(new openswmmvis::transect::MoveStationPointCommand(&p, 1, /*old*/ 5.0, 0.0,
                                                                      /*new*/ 4.5, -1.0));
        QCOMPARE(p.pointAt(1).station, 4.5);
        QCOMPARE(p.pointAt(1).elevation, -1.0);

        stack.undo();
        QCOMPARE(p.pointAt(1).station, 5.0);
        QCOMPARE(p.pointAt(1).elevation, 0.0);

        stack.redo();
        QCOMPARE(p.pointAt(1).station, 4.5);
        QCOMPARE(p.pointAt(1).elevation, -1.0);
    }

    /**
     *
     * @brief Require insertion command to add station one/elevation five and undo to preserve the
     * original two stations.
     * Checks count and ordered station values; no redo or rejected collision command.
     *
     */
    void undoCommand_InsertStationRoundTrip()
    {
        TransectProvider p(QStringLiteral("U6"));
        QVERIFY(p.setAllPoints({{0.0, 0.0}, {2.0, 0.0}}));
        QUndoStack stack;
        stack.push(new openswmmvis::transect::InsertStationCommand(&p, 1.0, 5.0));
        QCOMPARE(p.pointCount(), 3);
        QCOMPARE(p.pointAt(1).station, 1.0);
        QCOMPARE(p.pointAt(1).elevation, 5.0);

        stack.undo();
        QCOMPARE(p.pointCount(), 2);
        // Original points still intact.
        QCOMPARE(p.pointAt(0).station, 0.0);
        QCOMPARE(p.pointAt(1).station, 2.0);
    }

    /**
     *
     * @brief Require deletion of two middle points to retain endpoints and undo to restore sorted
     * points/elevations.
     * Checks all restored stations and the two middle elevations; no duplicate/invalid
     * indices or redo assertion.
     *
     */
    void undoCommand_DeleteStationsRoundTrip()
    {
        TransectProvider p(QStringLiteral("U7"));
        QVERIFY(p.setAllPoints({{0.0, 10.0}, {3.0, 5.0}, {6.0, 0.0}, {9.0, 5.0}}));
        QUndoStack stack;
        stack.push(new openswmmvis::transect::DeleteStationsCommand(&p, {1, 2}));
        QCOMPARE(p.pointCount(), 2);
        QCOMPARE(p.pointAt(0).station, 0.0);
        QCOMPARE(p.pointAt(1).station, 9.0);

        stack.undo();
        QCOMPARE(p.pointCount(), 4);
        // Order restored — insertPoint sorts by station so we expect
        // {0, 3, 6, 9} with original elevations.
        QCOMPARE(p.pointAt(0).station, 0.0);
        QCOMPARE(p.pointAt(1).station, 3.0);
        QCOMPARE(p.pointAt(1).elevation, 5.0);
        QCOMPARE(p.pointAt(2).station, 6.0);
        QCOMPARE(p.pointAt(2).elevation, 0.0);
        QCOMPARE(p.pointAt(3).station, 9.0);
    }

    /**
     *
     * @brief Require a registry-backed rename command and its undo to update both provider name
     * and lookup.
     * Checks Beta then Alpha identity; no collision, old-name absence or redo assertion.
     *
     */
    void undoCommand_RenameRoundTripViaRegistry()
    {
        TransectRegistry reg;
        auto *p = reg.create(QStringLiteral("Alpha"));
        QUndoStack stack;
        stack.push(
            new openswmmvis::transect::RenameTransectCommand(&reg, p, QStringLiteral("Beta")));
        QCOMPARE(p->name(), QStringLiteral("Beta"));
        QVERIFY(reg.findByName(QStringLiteral("Beta")) == p);

        stack.undo();
        QCOMPARE(p->name(), QStringLiteral("Alpha"));
        QVERIFY(reg.findByName(QStringLiteral("Alpha")) == p);
    }

    /**
     *
     * @brief Require four undos to restore name, left roughness and the full point vector after
     * mixed edits.
     * Pushes rename, roughness, move and endpoint deletion; checks final restored state
     * rather than every intermediate undo result or right/channel roughness separately.
     *
     */
    void undoStack_RevertsMixedSequence()
    {
        // End-to-end: a realistic gesture sequence (rename + edit roughness
        // + drag a point + delete a row) must collapse back to original
        // state after as many undos as there were pushes.
        TransectRegistry reg;
        auto *p = reg.create(QStringLiteral("Mix"));
        QVERIFY(p->setAllPoints({{0.0, 10.0}, {5.0, 0.0}, {10.0, 10.0}}));
        const auto initialPoints = p->allPoints();
        const auto initialN_L = p->nLeftBank();

        QUndoStack stack;
        stack.push(new openswmmvis::transect::RenameTransectCommand(&reg, p,
                                                                    QStringLiteral("Mix-renamed")));
        stack.push(new openswmmvis::transect::SetRoughnessCommand(p, 0.07, 0.07, 0.05));
        stack.push(new openswmmvis::transect::MoveStationPointCommand(p, 1, 5.0, 0.0, 4.0, -2.0));
        stack.push(new openswmmvis::transect::DeleteStationsCommand(p, {0}));
        QCOMPARE(p->pointCount(), 2);

        stack.undo(); // restore deleted row
        stack.undo(); // unmove
        stack.undo(); // restore roughness
        stack.undo(); // restore name

        QCOMPARE(p->name(), QStringLiteral("Mix"));
        QCOMPARE(p->nLeftBank(), initialN_L);
        QCOMPARE(p->allPoints(), initialPoints);
    }

    // ── Property tree layout ────────────────────────────────────────────────
    //
    // The QPropertyModel groups Q_PROPERTYs under a class-header row.
    // rebuildPropertyTree_() reparents the tree at that class index so the
    // 10 fields appear as flat top-level rows — no collapsed
    // "TransectPropertyBag" header, no extra disclosure click needed.
    /**
     *
     * @brief Require the dialog property tree to use a valid class-root index containing ten
     * direct rows.
     * Checks count and non-null tree/model; does not compare labels, collapse state,
     * property editing or accessibility in this case.
     *
     */
    void dialog_PropertyTree_ShowsTenFlatRows()
    {
        TransectRegistry reg;
        auto *p = reg.create(QStringLiteral("Pt"));
        QVERIFY(p);
        QVERIFY(p->setAllPoints({{0.0, 10.0}, {5.0, 0.0}, {10.0, 10.0}}));

        QUndoStack stack;
        TransectEditorDialog dlg(&reg, /*layer*/ nullptr, &stack);
        auto *tree = dlg.propertyTree();
        QVERIFY(tree);
        auto *m = tree->model();
        QVERIFY(m);

        const QModelIndex root = tree->rootIndex();
        QVERIFY(root.isValid());         // not the invisible model root
        QCOMPARE(m->rowCount(root), 10); // 10 Q_PROPERTYs of TransectPropertyBag
    }

    // The editor used to be dismissable only from the title bar. The Close
    // box must hide it, and must not be the default button — Enter in a
    // station/elevation cell has to commit the cell, never close the editor.
    /**
     *
     * @brief Require an exposed editor's Close button to be nondefault/non-auto-default and hide
     * the dialog.
     * Also runs shared dialog accessibility assertions; does not send Enter from an edited
     * station cell or verify settings persistence.
     *
     */
    void closeButton_HidesDialog()
    {
        TransectRegistry reg;
        auto *p = reg.create(QStringLiteral("T1"));
        QVERIFY(p);
        QVERIFY(p->setAllPoints({{0.0, 10.0}, {5.0, 0.0}, {10.0, 10.0}}));
        QUndoStack stack;
        TransectEditorDialog dlg(&reg, /*layer*/ nullptr, &stack);
        dlg.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dlg));

        auto *box = dlg.findChild<QDialogButtonBox *>(QStringLiteral("transect_closeBox"));
        QVERIFY(box);
        auto *btn = box->button(QDialogButtonBox::Close);
        QVERIFY(btn);
        QVERIFY(!btn->isDefault());
        QVERIFY(!btn->autoDefault());
        swmmvis_test::assertDialogA11y(&dlg);

        btn->click();
        QTRY_VERIFY(!dlg.isVisible());
    }
};

QTEST_MAIN(TestTransectEditorDialog)
#include "test_transect_editor_dialog.moc"
