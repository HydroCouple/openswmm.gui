/*!
 * \file   test_transect_editor_dialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BQ Phase 6.7.4 — TransectProvider + TransectRegistry +
 *         TransectListModel + TransectStationTableModel + TransectChartView
 *         + TransectEditorDialog coverage.
 *
 * Coverage (~24 cases):
 *
 *  TransectProvider (10):
 *   1. setAllPoints rejects non-monotone stations + emits mutationRejected.
 *   2. setElevationAt validates index; emits pointsChanged.
 *   3. setPointAt enforces strict-monotone with neighbours.
 *   4. setPointLive clamps station between neighbours; clamped flag set.
 *   5. insertPoint inserts at sorted position; rejects equal-station collision.
 *   6. removePointsAt safely handles duplicate + reverse-sorted indices.
 *   7. setRoughness emits roughnessChanged exactly once on change, none on no-op.
 *   8. setBankStations / setEncroachmentStations are independent signals.
 *   9. setModifiers emits modifiersChanged.
 *  10. setName emits nameChanged; setComments emits commentsChanged.
 *
 *  TransectRegistry (4):
 *  11. create rejects duplicates (case-insensitive) and empty name.
 *  12. rename updates byLowerName + emits providerRenamed.
 *  13. remove emits providerAboutToBeRemoved before deletion.
 *  14. registry rebroadcasts provider mutations via providerMetadataChanged.
 *
 *  TransectListModel (2):
 *  15. rowCount + data(DisplayRole) match registry contents; rename refreshes
 *      via dataChanged.
 *  16. setData(EditRole) routes through registry rename; refuses duplicates.
 *
 *  TransectStationTableModel (3):
 *  17. headerData reflects unitsSuffix override.
 *  18. setData(col 1) updates elevation; chart-bound side stays consistent.
 *  19. setData(col 0) X-edit enforced via provider monotonicity (refused at
 *      neighbour collision).
 *
 *  TransectChartView (3):
 *  20. setProvider rebuilds overbankLine to match station–elevation pairs.
 *  21. Channel line populates only between bank stations.
 *  22. setOverbankColor emits overbankColorChanged and updates pen.
 *
 *  TransectEditorDialog (2):
 *  23. Dialog opens with first provider bound; list selection rebinds table
 *      and chart.
 *  24. Delete confirmation bypass (deleteCurrentSilently) removes provider.
 */

#include "transect/transectprovider.h"
#include "transect/transectregistry.h"
#include "transect/transectundocommands.h"
#include "ui/dialogs/transecteditordialog.h"
#include "ui/dialogs/transectpropertybag.h"
#include "ui/models/transectlistmodel.h"
#include "ui/models/transectstationtablemodel.h"
#include "ui/widgets/transectchartview.h"

#include <QLineSeries>
#include <QListView>
#include <QObject>
#include <QSignalSpy>
#include <QTableView>
#include <QTest>
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

    // ── TransectProvider ────────────────────────────────────────────────────

    void setAllPoints_RejectsNonMonotone()
    {
        TransectProvider p(QStringLiteral("T1"));
        QSignalSpy rejected(&p, &TransectProvider::mutationRejected);
        QVERIFY(!p.setAllPoints({{0.0, 100.0}, {2.0, 99.0}, {1.0, 98.0}}));
        QCOMPARE(rejected.size(), 1);
        QVERIFY(p.setAllPoints({{0.0, 100.0}, {1.0, 99.0}, {2.0, 98.0}}));
        QCOMPARE(p.pointCount(), 3);
    }

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

    void setRoughness_EmitsOnceOnChange()
    {
        TransectProvider p(QStringLiteral("T7"));
        QSignalSpy spy(&p, &TransectProvider::roughnessChanged);
        p.setRoughness(0.030, 0.030, 0.030);   // matches default → no-op
        QCOMPARE(spy.size(), 0);
        p.setRoughness(0.040, 0.030, 0.030);
        QCOMPARE(spy.size(), 1);
        p.setRoughness(0.040, 0.030, 0.030);   // no change
        QCOMPARE(spy.size(), 1);
    }

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

    void setModifiers_EmitsSignal()
    {
        TransectProvider p(QStringLiteral("T9"));
        QSignalSpy spy(&p, &TransectProvider::modifiersChanged);
        p.setModifiers(1.0, 0.0, 1.0);   // defaults — no change
        QCOMPARE(spy.size(), 0);
        p.setModifiers(2.0, 1.0, 1.5);
        QCOMPARE(spy.size(), 1);
        QCOMPARE(p.stationMultiplier(), 2.0);
        QCOMPARE(p.elevationOffset(), 1.0);
        QCOMPARE(p.meanderFactor(), 1.5);
    }

    void nameAndComments_EmitOnChange()
    {
        TransectProvider p(QStringLiteral("T10"));
        QSignalSpy nameSpy(&p, &TransectProvider::nameChanged);
        QSignalSpy comSpy (&p, &TransectProvider::commentsChanged);
        p.setName(QStringLiteral("T10"));   // no change
        QCOMPARE(nameSpy.size(), 0);
        p.setName(QStringLiteral("T10-renamed"));
        QCOMPARE(nameSpy.size(), 1);

        p.setComments(QStringLiteral("hello"));
        QCOMPARE(comSpy.size(), 1);
        p.setComments(QStringLiteral("hello"));
        QCOMPARE(comSpy.size(), 1);
    }

    // ── TransectRegistry ────────────────────────────────────────────────────

    void registry_CreateRejectsDuplicates()
    {
        TransectRegistry reg;
        QVERIFY(reg.create(QStringLiteral("A")) != nullptr);
        QCOMPARE(reg.create(QStringLiteral("A")), nullptr);   // exact dup
        QCOMPARE(reg.create(QStringLiteral("a")), nullptr);   // case-insensitive
        QCOMPARE(reg.create(QString()), nullptr);             // empty
        QCOMPARE(reg.providerCount(), 1);
    }

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

    void registry_RemoveEmitsAboutToBeRemoved()
    {
        TransectRegistry reg;
        auto *p = reg.create(QStringLiteral("X"));
        QSignalSpy spy(&reg, &TransectRegistry::providerAboutToBeRemoved);
        reg.remove(p);
        QCOMPARE(spy.size(), 1);
        QCOMPARE(reg.providerCount(), 0);
    }

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

    void tableModel_HeaderHonoursUnitsSuffix()
    {
        TransectStationTableModel m;
        m.setUnitsSuffix(QStringLiteral("(m)"));
        QVERIFY(m.headerData(0, Qt::Horizontal).toString().contains(QStringLiteral("(m)")));
        QVERIFY(m.headerData(1, Qt::Horizontal).toString().contains(QStringLiteral("(m)")));
    }

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

        QVERIFY(view.leftOverbankLine()  != nullptr);
        QVERIFY(view.channelLine()       != nullptr);
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

    void chartView_BankChangeReslicesTopography()
    {
        // Moving the bank stations must re-cut all three series — no stale
        // shoulder/channel data.
        TransectChartView view;
        TransectProvider p(QStringLiteral("CV3"));
        QVERIFY(p.setAllPoints({{0.0, 10.0}, {2.0, 4.0}, {5.0, 0.0}, {8.0, 4.0}, {10.0, 10.0}}));
        p.setBankStations(2.0, 8.0);
        view.setProvider(&p);
        const int leftCntInitial  = view.leftOverbankLine()->count();
        const int rightCntInitial = view.rightOverbankLine()->count();

        // Tighten the channel: shoulders should grow.
        p.setBankStations(3.0, 7.0);
        QVERIFY(view.leftOverbankLine()->count()  >= leftCntInitial);
        QVERIFY(view.rightOverbankLine()->count() >= rightCntInitial);
        // Channel endpoints must hit the new bank stations exactly.
        QVERIFY(view.channelLine()->count() >= 2);
        QCOMPARE(view.channelLine()->at(0).x(), 3.0);
        QCOMPARE(view.channelLine()->at(view.channelLine()->count() - 1).x(), 7.0);
    }

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
        QCOMPARE(bag.nLeftBank(),  0.05);
        QCOMPARE(bag.nRightBank(), 0.06);
        QCOMPARE(bag.nChannel(),   0.08);
    }

    // ── Undo / redo commands ────────────────────────────────────────────────

    void undoCommand_RoughnessRoundTrip()
    {
        TransectProvider p(QStringLiteral("U1"));
        p.setRoughness(0.030, 0.030, 0.030);
        QUndoStack stack;
        stack.push(new openswmmvis::transect::SetRoughnessCommand(
            &p, 0.05, 0.06, 0.08));
        QCOMPARE(p.nLeftBank(),  0.05);
        QCOMPARE(p.nRightBank(), 0.06);
        QCOMPARE(p.nChannel(),   0.08);

        stack.undo();
        QCOMPARE(p.nLeftBank(),  0.030);
        QCOMPARE(p.nRightBank(), 0.030);
        QCOMPARE(p.nChannel(),   0.030);

        stack.redo();
        QCOMPARE(p.nLeftBank(),  0.05);
        QCOMPARE(p.nChannel(),   0.08);
    }

    void undoCommand_BankAndEncroachmentIndependent()
    {
        TransectProvider p(QStringLiteral("U2"));
        QUndoStack stack;
        stack.push(new openswmmvis::transect::SetBankStationsCommand(&p, 1.0, 9.0));
        stack.push(new openswmmvis::transect::SetEncroachmentStationsCommand(&p, 0.5, 9.5));
        QCOMPARE(p.xLeftBank(),  1.0);
        QCOMPARE(p.xRightBank(), 9.0);
        QCOMPARE(p.xLeftEncroachment(),  0.5);
        QCOMPARE(p.xRightEncroachment(), 9.5);

        stack.undo();  // undoes encroachment
        QCOMPARE(p.xLeftEncroachment(),  0.0);
        QCOMPARE(p.xLeftBank(),          1.0);   // bank unaffected
        stack.undo();  // undoes bank
        QCOMPARE(p.xLeftBank(), 0.0);
    }

    void undoCommand_ModifiersRoundTrip()
    {
        TransectProvider p(QStringLiteral("U3"));
        QUndoStack stack;
        stack.push(new openswmmvis::transect::SetModifiersCommand(&p, 2.0, 1.0, 1.5));
        QCOMPARE(p.stationMultiplier(), 2.0);
        QCOMPARE(p.elevationOffset(),   1.0);
        QCOMPARE(p.meanderFactor(),     1.5);

        stack.undo();
        QCOMPARE(p.stationMultiplier(), 1.0);   // default
        QCOMPARE(p.elevationOffset(),   0.0);
        QCOMPARE(p.meanderFactor(),     1.0);
    }

    void undoCommand_CommentsRoundTrip()
    {
        TransectProvider p(QStringLiteral("U4"));
        p.setComments(QStringLiteral("original"));
        QUndoStack stack;
        stack.push(new openswmmvis::transect::SetCommentsCommand(
            &p, QStringLiteral("new comment")));
        QCOMPARE(p.comments(), QStringLiteral("new comment"));
        stack.undo();
        QCOMPARE(p.comments(), QStringLiteral("original"));
    }

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
        QCOMPARE(p.pointAt(1).station,   5.0);
        QCOMPARE(p.pointAt(1).elevation, 0.0);
        stack.push(new openswmmvis::transect::MoveStationPointCommand(
            &p, 1, /*old*/ 5.0, 0.0, /*new*/ 4.5, -1.0));
        QCOMPARE(p.pointAt(1).station,   4.5);
        QCOMPARE(p.pointAt(1).elevation, -1.0);

        stack.undo();
        QCOMPARE(p.pointAt(1).station,   5.0);
        QCOMPARE(p.pointAt(1).elevation, 0.0);

        stack.redo();
        QCOMPARE(p.pointAt(1).station,   4.5);
        QCOMPARE(p.pointAt(1).elevation, -1.0);
    }

    void undoCommand_InsertStationRoundTrip()
    {
        TransectProvider p(QStringLiteral("U6"));
        QVERIFY(p.setAllPoints({{0.0, 0.0}, {2.0, 0.0}}));
        QUndoStack stack;
        stack.push(new openswmmvis::transect::InsertStationCommand(&p, 1.0, 5.0));
        QCOMPARE(p.pointCount(), 3);
        QCOMPARE(p.pointAt(1).station,   1.0);
        QCOMPARE(p.pointAt(1).elevation, 5.0);

        stack.undo();
        QCOMPARE(p.pointCount(), 2);
        // Original points still intact.
        QCOMPARE(p.pointAt(0).station, 0.0);
        QCOMPARE(p.pointAt(1).station, 2.0);
    }

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
        QCOMPARE(p.pointAt(0).station,  0.0);
        QCOMPARE(p.pointAt(1).station,  3.0);
        QCOMPARE(p.pointAt(1).elevation, 5.0);
        QCOMPARE(p.pointAt(2).station,  6.0);
        QCOMPARE(p.pointAt(2).elevation, 0.0);
        QCOMPARE(p.pointAt(3).station,  9.0);
    }

    void undoCommand_RenameRoundTripViaRegistry()
    {
        TransectRegistry reg;
        auto *p = reg.create(QStringLiteral("Alpha"));
        QUndoStack stack;
        stack.push(new openswmmvis::transect::RenameTransectCommand(
            &reg, p, QStringLiteral("Beta")));
        QCOMPARE(p->name(), QStringLiteral("Beta"));
        QVERIFY(reg.findByName(QStringLiteral("Beta")) == p);

        stack.undo();
        QCOMPARE(p->name(), QStringLiteral("Alpha"));
        QVERIFY(reg.findByName(QStringLiteral("Alpha")) == p);
    }

    void undoStack_RevertsMixedSequence()
    {
        // End-to-end: a realistic gesture sequence (rename + edit roughness
        // + drag a point + delete a row) must collapse back to original
        // state after as many undos as there were pushes.
        TransectRegistry reg;
        auto *p = reg.create(QStringLiteral("Mix"));
        QVERIFY(p->setAllPoints({{0.0, 10.0}, {5.0, 0.0}, {10.0, 10.0}}));
        const auto initialPoints = p->allPoints();
        const auto initialN_L    = p->nLeftBank();

        QUndoStack stack;
        stack.push(new openswmmvis::transect::RenameTransectCommand(
            &reg, p, QStringLiteral("Mix-renamed")));
        stack.push(new openswmmvis::transect::SetRoughnessCommand(
            p, 0.07, 0.07, 0.05));
        stack.push(new openswmmvis::transect::MoveStationPointCommand(
            p, 1, 5.0, 0.0, 4.0, -2.0));
        stack.push(new openswmmvis::transect::DeleteStationsCommand(p, {0}));
        QCOMPARE(p->pointCount(), 2);

        stack.undo();   // restore deleted row
        stack.undo();   // unmove
        stack.undo();   // restore roughness
        stack.undo();   // restore name

        QCOMPARE(p->name(),      QStringLiteral("Mix"));
        QCOMPARE(p->nLeftBank(), initialN_L);
        QCOMPARE(p->allPoints(), initialPoints);
    }

    // ── Property tree layout ────────────────────────────────────────────────
    //
    // The QPropertyModel groups Q_PROPERTYs under a class-header row.
    // rebuildPropertyTree_() reparents the tree at that class index so the
    // 10 fields appear as flat top-level rows — no collapsed
    // "TransectPropertyBag" header, no extra disclosure click needed.
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
        QVERIFY(root.isValid());   // not the invisible model root
        QCOMPARE(m->rowCount(root), 10);   // 10 Q_PROPERTYs of TransectPropertyBag
    }
};

QTEST_MAIN(TestTransectEditorDialog)
#include "test_transect_editor_dialog.moc"
