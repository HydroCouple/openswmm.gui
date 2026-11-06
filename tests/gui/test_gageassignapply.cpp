/**
 * @file      test_gageassignapply.cpp
 * @brief     Apply-path contract of Model → Tools → Assign Rain Gages
 *         (workplans/RAINGAGE_ASSIGNMENT_PERF_AND_RDII_PLAN_2026-10-02.md).
 *
 * Drives the real dialog against tests/gui/data/gage_assign_rdii.inp:
 *   1. One Apply is ONE undo step, raises ONE geometryChanged (the bulk scope's
 *      single refresh instead of one per object), and undo restores every
 *      subcatchment's gage.
 *   2. A generated (interpolated) series keeps the SOURCE timestamps. Source
 *      series are read as UTC; writing them back through a local-time
 *      QDateTime shifted every generated entry by the machine's UTC offset.
 *   3. Undoing an interpolated run removes every generated gage AND its engine
 *      table — no orphan series left to reappear in the written INP.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "selection/selectionmanager.h"
#include "ui/dialogs/assignraingagesdialog.h"

#include <QDir>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalSpy>
#include <QTest>

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_gages.h>
#include <openswmm/engine/openswmm_subcatchments.h>
#include <openswmm/engine/openswmm_tables.h>

#include <memory>

namespace
{

/**
 *
 * @brief Resolve gage_assign_rdii.inp under SWMMVIS_GUI_TEST_DATA or the current directory.
 * @return Owned path; no existence/readability check or directory creation.
 *
 */
QString fixturePath()
{
    return QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral(".")))
        .filePath(QStringLiteral("gage_assign_rdii.inp"));
}

/**
 *
 * @brief Find the first descendant widget with matching text after removing ampersands.
 * @tparam T QObject widget type with a text() accessor, such as QPushButton.
 * @param root Nonnull borrowed QWidget searched recursively; root itself is excluded.
 * @param text Exact case-sensitive label without mnemonic ampersands.
 * @return Borrowed descendant pointer or nullptr; valid only while its owner lives.
 * Runs on the GUI thread without changing the widget.
 *
 */
template <class T> T *findByText(QWidget *root, const QString &text)
{
    for (T *w : root->findChildren<T *>())
        if (w->text().remove(QLatin1Char('&')) == text)
            return w;
    return nullptr;
}

/**
 *
 * @brief Read the engine gage identifier assigned to a fixture subcatchment.
 * @param layer Nonnull borrowed loaded model; must outlive this synchronous call.
 * @param subcatch Borrowed null-terminated UTF-8 subcatchment ID.
 * @return Copied gage ID or empty on missing assignment/lookup failure. Engine getter
 * status is not checked; the initialized negative index supplies the empty fallback.
 *
 */
QString gageOf(SWMMModelLayer *layer, const char *subcatch)
{
    SWMM_Engine eng = layer->engine();
    int g = -1;
    swmm_subcatch_get_gage(eng, swmm_subcatch_index(eng, subcatch), &g);
    const char *id = g >= 0 ? swmm_gage_id(eng, g) : nullptr;
    return id ? QString::fromUtf8(id) : QString();
}

/*! First entry time of table \p name, or NaN. */
/**
 *
 * @brief Read a table first abscissa for timestamp-preservation comparison.
 * @param eng Borrowed valid engine with a loaded fixture.
 * @param name Borrowed table ID converted to UTF-8 for synchronous lookup.
 * @return First raw engine table x (SWMM datetime days for this fixture), or NaN
 * when the table is absent or the unchecked point read leaves x unchanged. No write.
 *
 */
double firstTime(SWMM_Engine eng, const QString &name)
{
    const int t = swmm_table_index(eng, name.toUtf8().constData());
    double x = std::numeric_limits<double>::quiet_NaN(), y = 0.0;
    if (t >= 0)
        swmm_table_get_point(eng, t, 0, &x, &y);
    return x;
}

struct Rig {
    MapCanvas canvas;
    SWMMModelLayer *layer = nullptr; // owned by the canvas
    SelectionManager sel;

    /**
     *
     * @brief Load the RDII fixture and transfer its new layer to the canvas on success.
     * @return True after load/addLayer; false on load failure with a still-unparented
     * raw layer allocation (this failure path does not delete it). Requires GUI thread
     * and readable fixturePath(); diagnostics are local and no model file is written.
     *
     */
    bool load()
    {
        layer = new SWMMModelLayer(fixturePath(), nullptr);
        QList<QString> w, e;
        if (!layer->loadModel(w, e))
            return false;
        canvas.addLayer(layer, /*pushUndo=*/false);
        return true;
    }

    /*! Run the dialog's Apply with the given method. */
    /**
     *
     * @brief Invoke the real assignment dialog Apply and wait up to 30 s for worker completion.
     * @param interpolated True selects the natural-neighbor mode; false retains default
     * nearest mode. Requires successful load(), expected translated-label widgets and
     * living layer/canvas/selection. Pushes assignment undo work and mutates the model.
     * A timeout records QtTest failure and returns from this helper; it does not abort
     * the calling slot itself. No success value or detailed plan error is returned.
     *
     */
    void apply(bool interpolated)
    {
        openswmmvis::ui::AssignRainGagesDialog dlg(layer, &canvas, &sel);
        if (interpolated)
            findByText<QRadioButton>(
                &dlg, QStringLiteral("Natural-neighbour interpolation (creates gages)"))
                ->setChecked(true);
        findByText<QPushButton>(&dlg, QStringLiteral("Apply"))->click();
        // The plan is computed on a worker; Apply lands when it finishes.
        QTRY_VERIFY_WITH_TIMEOUT(!dlg.busy(), 30000);
    }
};

} // namespace

class TestGageAssignApply : public QObject
{
    Q_OBJECT
private slots:
    /**
     *
     * @brief Require nearest assignment to create one undo step/geometry refresh, choose
     * west/east gages and undo to both original assignments.
     *
     */
    void nearestIsOneUndoStepAndOneRefresh();
    /**
     *
     * @brief Require generated NNG_ tables to retain the source first engine timestamp and at
     * least one generated series.
     * Reads the RDII fixture through the GUI dialog and modifies only the loaded model.
     *
     */
    void interpolatedSeriesKeepSourceTimes();
    /**
     *
     * @brief Require undo to restore original gage/table counts and west assignment, then redo to
     * recreate a generated assignment.
     *
     */
    void undoInterpolatedLeavesNoOrphans();
};

void TestGageAssignApply::nearestIsOneUndoStepAndOneRefresh()
{
    Rig rig;
    QVERIFY(rig.load());
    QCOMPARE(gageOf(rig.layer, "SUB_W"), QStringLiteral("GN"));

    QSignalSpy geom(rig.layer, &SWMMModelLayer::geometryChanged);
    const int before = rig.canvas.undoStack()->count();
    rig.apply(false);

    QCOMPARE(rig.canvas.undoStack()->count(), before + 1);
    QCOMPARE(geom.count(), 1);
    QCOMPARE(gageOf(rig.layer, "SUB_W"), QStringLiteral("GW"));
    QCOMPARE(gageOf(rig.layer, "SUB_E"), QStringLiteral("GE"));

    rig.canvas.undoStack()->undo();
    QCOMPARE(gageOf(rig.layer, "SUB_W"), QStringLiteral("GN"));
    QCOMPARE(gageOf(rig.layer, "SUB_E"), QStringLiteral("GN"));
}

void TestGageAssignApply::interpolatedSeriesKeepSourceTimes()
{
    Rig rig;
    QVERIFY(rig.load());
    rig.apply(true);

    SWMM_Engine eng = rig.layer->engine();
    const double src = firstTime(eng, QStringLiteral("TS_W"));
    QVERIFY(std::isfinite(src));

    // Every generated series starts where the sources do (all three share the
    // same first entry, 01/01/2020 00:00).
    int generated = 0;
    for (int i = 0, n = swmm_table_count(eng); i < n; ++i) {
        const char *id = swmm_table_id(eng, i);
        const QString name = id ? QString::fromUtf8(id) : QString();
        if (!name.startsWith(QStringLiteral("NNG_")))
            continue;
        ++generated;
        QCOMPARE(firstTime(eng, name), src);
    }
    QVERIFY(generated > 0);
}

void TestGageAssignApply::undoInterpolatedLeavesNoOrphans()
{
    Rig rig;
    QVERIFY(rig.load());
    SWMM_Engine eng = rig.layer->engine();
    const int gages0 = swmm_gage_count(eng);
    const int tables0 = swmm_table_count(eng);

    rig.apply(true);
    QVERIFY(swmm_gage_count(eng) > gages0);

    rig.canvas.undoStack()->undo();
    QCOMPARE(swmm_gage_count(eng), gages0);
    QCOMPARE(swmm_table_count(eng), tables0);
    QCOMPARE(gageOf(rig.layer, "SUB_W"), QStringLiteral("GN"));

    // Redo puts the same result back.
    rig.canvas.undoStack()->redo();
    QVERIFY(swmm_gage_count(eng) > gages0);
    QVERIFY(gageOf(rig.layer, "SUB_W").startsWith(QStringLiteral("NNG_")));
}

QTEST_MAIN(TestGageAssignApply)
#include "test_gageassignapply.moc"
