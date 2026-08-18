/*!
 * \file   test_attributetableschema.cpp
 * \brief  Attribute Table column-schema contract.
 *
 * Guards three properties the schema has to keep:
 *
 *   1. The conduit cross-section block (XSection compound cell, inline
 *      Geom 1-4, Barrels) is present, carries the right delegates, and
 *      stays EDITABLE for every shape. It used to blank + grey each geom
 *      the live shape didn't consume, so a CIRCULAR conduit showed three
 *      permanently empty uneditable columns — which reads as broken
 *      editors, and threw away a width typed before a shape switch.
 *      Only the picker-owned index slots (IRREGULAR / STREET geom1,
 *      CUSTOM geom2) stay locked.
 *
 *   2. Post-run "dynamics" statistics form a contiguous suffix of every
 *      category's schema — never interleaved with the editable inputs.
 *
 *   3. The per-category attribute coverage that the engine C API exposes
 *      is actually surfaced (conduit Slope, subcatchment Tag, storage
 *      exfiltration triple, outfall RouteTo).
 */

#include "layers/swmmmodellayer.h"
#include "ui/panels/attributetablepanel.h"
#include "ui/panels/swmmattributetablemodel.h"
#include "ui/properties/xsectshapegeom.h"

#include <openswmm/engine/openswmm_gages.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_subcatchments.h>

#include <algorithm>   // std::reverse — ordering-override pin

#include <QComboBox>
#include <QDir>
#include <QLineEdit>
#include <QLocale>
#include <QObject>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QTest>

#include <memory>

using openswmmvis::ColumnSpec;
using openswmmvis::EditorKind;

namespace {

QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
}

std::unique_ptr<SWMMModelLayer> openLayer()
{
    auto layer = std::make_unique<SWMMModelLayer>(
        QDir(dataDir()).filePath(QStringLiteral("typed_selection_fixture.inp")),
        nullptr);
    QList<QString> warnings, errors;
    if (!layer->loadModel(warnings, errors)) return nullptr;
    return layer;
}

//! Column index for a spec key, or -1.
int colFor(const QList<ColumnSpec> &specs, const QString &key)
{
    for (int i = 0; i < specs.size(); ++i)
        if (specs[i].key == key) return i;
    return -1;
}

//! Engine-side id for a category's entity at engine index `idx`.
const char *engineIdFor(SWMM_Engine eng, SWMMModelLayer::Category cat, int idx)
{
    switch (cat) {
    case SWMMModelLayer::CatJunctions: case SWMMModelLayer::CatOutfalls:
    case SWMMModelLayer::CatStorage:   case SWMMModelLayer::CatDividers:
        return swmm_node_id(eng, idx);
    case SWMMModelLayer::CatConduits:  case SWMMModelLayer::CatPumps:
    case SWMMModelLayer::CatOrifices:  case SWMMModelLayer::CatWeirs:
    case SWMMModelLayer::CatOutlets:
        return swmm_link_id(eng, idx);
    case SWMMModelLayer::CatSubcatchments: return swmm_subcatch_id(eng, idx);
    case SWMMModelLayer::CatRainGages:     return swmm_gage_id(eng, idx);
    default: return nullptr;
    }
}

//! Every SWMM category the attribute table can bind to.
QList<SWMMModelLayer::Category> allCategories()
{
    return {
        SWMMModelLayer::CatJunctions, SWMMModelLayer::CatOutfalls,
        SWMMModelLayer::CatStorage,   SWMMModelLayer::CatDividers,
        SWMMModelLayer::CatConduits,  SWMMModelLayer::CatPumps,
        SWMMModelLayer::CatOrifices,  SWMMModelLayer::CatWeirs,
        SWMMModelLayer::CatOutlets,   SWMMModelLayer::CatSubcatchments,
        SWMMModelLayer::CatRainGages,
    };
}

//! Dynamics columns are the ones whose engine tag names a statistics
//! accessor. Keeping the test keyed on the tag (not on a hardcoded list of
//! labels) means a newly added statistic is covered automatically.
bool isDynamics(const ColumnSpec &s)
{
    return s.setter.contains(QStringLiteral("_stat_"));
}

} // namespace

class TestAttributeTableSchema : public QObject
{
    Q_OBJECT

private slots:

    // =====================================================================
    // 1 — conduit cross-section block
    // =====================================================================

    void conduitCrossSectionBlockIsPresent()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMMAttributeTableModel model;
        model.setSource(layer.get(), SWMMModelLayer::CatConduits);
        const auto specs = model.columnSpecs();

        QVERIFY(colFor(specs, QStringLiteral("XSection")) >= 0);
        QVERIFY(colFor(specs, QStringLiteral("Barrels")) >= 0);
        for (int k = 1; k <= 4; ++k)
            QVERIFY2(colFor(specs, QStringLiteral("Geom %1").arg(k)) >= 0,
                     qPrintable(QStringLiteral("Geom %1 column missing").arg(k)));

        QCOMPARE(specs[colFor(specs, QStringLiteral("XSection"))].editor,
                 EditorKind::Compound);
        QCOMPARE(specs[colFor(specs, QStringLiteral("Barrels"))].editor,
                 EditorKind::Integer);
        QCOMPARE(specs[colFor(specs, QStringLiteral("Geom 1"))].editor,
                 EditorKind::Numeric);
    }

    //! The regression this file exists for: on a CIRCULAR conduit, Geom 2-4
    //! are unused by the shape but must still show their stored value and
    //! accept an edit.
    void geomCellsStayEditableOnCircular()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMMAttributeTableModel model;
        model.setSource(layer.get(), SWMMModelLayer::CatConduits);
        const auto specs = model.columnSpecs();
        QVERIFY(model.rowCount() > 0);

        // Row 0 of the fixture is a CIRCULAR conduit — only geom1 "applies".
        const int shapeCol = colFor(specs, QStringLiteral("Geom 1"));
        QVERIFY(shapeCol >= 0);
        QVERIFY(!openswmmvis::xsectGeomApplies(SWMM_XSECT_CIRCULAR, 2));

        for (int k = 1; k <= 4; ++k) {
            const int c = colFor(specs, QStringLiteral("Geom %1").arg(k));
            const QModelIndex idx = model.index(0, c);
            QVERIFY2(model.flags(idx) & Qt::ItemIsEditable,
                     qPrintable(QStringLiteral("Geom %1 not editable on CIRCULAR")
                                    .arg(k)));
            QVERIFY2(model.data(idx, Qt::DisplayRole).isValid(),
                     qPrintable(QStringLiteral("Geom %1 blank on CIRCULAR").arg(k)));
        }
    }

    //! A geom the shape doesn't use is still a real stored number: writing it
    //! must reach the engine and survive a re-read.
    void geomEditRoundTripsThroughEngine()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMMAttributeTableModel model;
        model.setSource(layer.get(), SWMMModelLayer::CatConduits);
        const auto specs = model.columnSpecs();
        const int c = colFor(specs, QStringLiteral("Geom 2"));
        QVERIFY(c >= 0);

        const QModelIndex idx = model.index(0, c);
        QVERIFY(model.setData(idx, 2.75, Qt::EditRole));

        const QString name = model.objectNameAt(0);
        const int li = swmm_link_index(layer->engine(), name.toUtf8().constData());
        QVERIFY(li >= 0);
        int shape = 0; double g1 = 0, g2 = 0, g3 = 0, g4 = 0;
        QCOMPARE(swmm_link_get_xsect(layer->engine(), li, &shape,
                                     &g1, &g2, &g3, &g4), SWMM_OK);
        QCOMPARE(g2, 2.75);
        // The shape itself must not have moved.
        QCOMPARE(shape, SWMM_XSECT_CIRCULAR);
    }

    //! IRREGULAR geom1 is a transect-list index, not a dimension — an inline
    //! numeric editor there would silently re-point the section, so it stays
    //! locked while the other three geoms remain editable.
    void pickerIndexGeomStaysLocked()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMMAttributeTableModel model;
        model.setSource(layer.get(), SWMMModelLayer::CatConduits);
        const auto specs = model.columnSpecs();

        const QString name = model.objectNameAt(0);
        const int li = swmm_link_index(layer->engine(), name.toUtf8().constData());
        QVERIFY(li >= 0);
        QCOMPARE(swmm_link_set_xsect(layer->engine(), li,
                                     SWMM_XSECT_IRREGULAR, 0, 0, 0, 0), SWMM_OK);
        model.reload();

        const int g1 = colFor(specs, QStringLiteral("Geom 1"));
        QVERIFY(!(model.flags(model.index(0, g1)) & Qt::ItemIsEditable));
        for (int k = 2; k <= 4; ++k) {
            const int c = colFor(specs, QStringLiteral("Geom %1").arg(k));
            QVERIFY2(model.flags(model.index(0, c)) & Qt::ItemIsEditable,
                     qPrintable(QStringLiteral("Geom %1 locked on IRREGULAR").arg(k)));
        }
    }

    // =====================================================================
    // 2 — dynamics columns are a contiguous suffix
    // =====================================================================

    void dynamicsColumnsAreAppendedLast()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        for (const auto cat : allCategories()) {
            SWMMAttributeTableModel model;
            model.setSource(layer.get(), cat);
            const auto specs = model.columnSpecs();

            int firstDynamic = -1;
            for (int i = 0; i < specs.size(); ++i) {
                if (isDynamics(specs[i])) {
                    if (firstDynamic < 0) firstDynamic = i;
                } else if (firstDynamic >= 0) {
                    QFAIL(qPrintable(
                        QStringLiteral("category %1: input column '%2' at %3 sits "
                                        "AFTER the dynamics block (starts at %4)")
                            .arg(int(cat)).arg(specs[i].key).arg(i).arg(firstDynamic)));
                }
            }
            // Whatever is in the block must be read-only — a statistic is not
            // an input, and commitValueDirect() refuses ReadOnly writes.
            for (int i = std::max(firstDynamic, 0); i < specs.size(); ++i) {
                if (!isDynamics(specs[i])) continue;
                QCOMPARE(specs[i].editor, EditorKind::ReadOnly);
                if (model.rowCount() > 0)
                    QVERIFY(!(model.flags(model.index(0, i)) & Qt::ItemIsEditable));
            }
        }
    }

    void everyRoutedCategoryHasADynamicsBlock()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        // Rain gages are the one category the engine keeps no statistics for.
        for (const auto cat : allCategories()) {
            if (cat == SWMMModelLayer::CatRainGages) continue;
            SWMMAttributeTableModel model;
            model.setSource(layer.get(), cat);
            const auto specs = model.columnSpecs();
            int n = 0;
            for (const auto &s : specs) if (isDynamics(s)) ++n;
            QVERIFY2(n > 0, qPrintable(QStringLiteral("category %1 has no dynamics "
                                                       "columns").arg(int(cat))));
        }
        SWMMAttributeTableModel gages;
        gages.setSource(layer.get(), SWMMModelLayer::CatRainGages);
        for (const auto &s : gages.columnSpecs())
            QVERIFY(!isDynamics(s));
    }

    void pumpsCarryTheirUtilisationStatistics()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMMAttributeTableModel model;
        model.setSource(layer.get(), SWMMModelLayer::CatPumps);
        const auto specs = model.columnSpecs();
        // Shared link block plus the three pump-only ones.
        QVERIFY(colFor(specs, QStringLiteral("Max flow (stat)")) >= 0);
        QVERIFY(colFor(specs, QStringLiteral("Pump cycles")) >= 0);
        QVERIFY(colFor(specs, QStringLiteral("Pump on time (hr)")) >= 0);
        QVERIFY(colFor(specs, QStringLiteral("Pump volume")) >= 0);
    }

    // =====================================================================
    // 3 — attribute coverage the engine API offers
    // =====================================================================

    void conduitExposesComputedSlope()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMMAttributeTableModel model;
        model.setSource(layer.get(), SWMMModelLayer::CatConduits);
        const auto specs = model.columnSpecs();
        const int c = colFor(specs, QStringLiteral("Slope"));
        QVERIFY2(c >= 0, "conduit Slope column missing");
        QCOMPARE(specs[c].editor, EditorKind::ReadOnly);

        // Reads through the getter-only tag, not identifyByName.
        const QModelIndex idx = model.index(0, c);
        QVERIFY(!(model.flags(idx) & Qt::ItemIsEditable));
        bool ok = false;
        const double slope = model.data(idx, Qt::DisplayRole).toDouble(&ok);
        QVERIFY(ok);
        QVERIFY(slope > 0.0);   // fixture drops 5 ft over 1000 ft
    }

    void subcatchmentExposesTag()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMMAttributeTableModel model;
        model.setSource(layer.get(), SWMMModelLayer::CatSubcatchments);
        const auto specs = model.columnSpecs();
        const int c = colFor(specs, QStringLiteral("Tag"));
        QVERIFY2(c >= 0, "subcatchment Tag column missing");
        QVERIFY(model.rowCount() > 0);

        const QModelIndex idx = model.index(0, c);
        QVERIFY(model.flags(idx) & Qt::ItemIsEditable);
        QVERIFY(model.setData(idx, QStringLiteral("basin-7"), Qt::EditRole));
        QCOMPARE(model.data(idx, Qt::DisplayRole).toString(),
                 QStringLiteral("basin-7"));
    }

    void storageExposesExfiltrationParameters()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMMAttributeTableModel model;
        model.setSource(layer.get(), SWMMModelLayer::CatStorage);
        const auto specs = model.columnSpecs();
        for (const char *key : {"Exfil suction", "Exfil conduct.", "Exfil deficit"})
            QVERIFY2(colFor(specs, QString::fromLatin1(key)) >= 0,
                     qPrintable(QStringLiteral("storage %1 column missing")
                                    .arg(QString::fromLatin1(key))));
    }

    void outfallExposesRouteTo()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMMAttributeTableModel model;
        model.setSource(layer.get(), SWMMModelLayer::CatOutfalls);
        const auto specs = model.columnSpecs();
        const int c = colFor(specs, QStringLiteral("Route to"));
        QVERIFY2(c >= 0, "outfall Route To column missing");
        QCOMPARE(specs[c].editor, EditorKind::Compound);
    }

    // =====================================================================
    // Panel-level: the delegates actually reach the view
    // =====================================================================

    void panelInstallsCrossSectionDelegates()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        AttributeTablePanel panel;
        panel.setProject(layer.get(), nullptr, nullptr);
        panel.refresh();

        auto *combo = panel.findChild<QComboBox *>();
        QVERIFY(combo);
        int catIdx = -1;
        for (int i = 0; i < combo->count(); ++i)
            if (combo->itemText(i).startsWith(QStringLiteral("Conduits")))
                catIdx = i;
        QVERIFY(catIdx >= 0);
        combo->setCurrentIndex(catIdx);

        auto *view = panel.findChild<QTableView *>();
        QVERIFY(view);
        auto *m = view->model();
        QVERIFY(m);

        auto headerCol = [&](const QString &label) {
            for (int c = 0; c < m->columnCount(); ++c)
                if (m->headerData(c, Qt::Horizontal).toString() == label) return c;
            return -1;
        };

        const int xs = headerCol(QStringLiteral("Cross Section"));
        QVERIFY(xs >= 0);
        QVERIFY(!view->isColumnHidden(xs));
        auto *del = qobject_cast<QStyledItemDelegate *>(
            view->itemDelegateForColumn(xs));
        QVERIFY2(del, "no delegate on the Cross Section column");
        // The compound cell renders a summary plus the "Edit…" affordance.
        const QString shown =
            del->displayText(m->index(0, xs).data(Qt::EditRole), QLocale());
        QVERIFY2(shown.contains(QStringLiteral("Edit")),
                 qPrintable(QStringLiteral("Cross Section cell shows '%1'").arg(shown)));

        for (int k = 1; k <= 4; ++k) {
            const int c = headerCol(QStringLiteral("Geom %1").arg(k));
            QVERIFY(c >= 0);
            QVERIFY(!view->isColumnHidden(c));
            QVERIFY(view->itemDelegateForColumn(c) != nullptr);
        }
    }

    // =====================================================================
    // Panel-level: the query bar filters on the columns it names
    // =====================================================================
    //
    // The filter proxy used to materialise EVERY column of every row (twice)
    // to test a predicate that reads one. It now resolves the predicate's
    // field names to columns once and reads only those. That is only a
    // performance change if the accepted-row set is unchanged, so these pin
    // the semantics that made the old build "work": a field is matchable by
    // its ColumnSpec key, by its label, or by the unit-suffixed header the
    // view displays.

    void queryAcceptsKeyLabelAndHeaderSpellings()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        AttributeTablePanel panel;
        panel.setProject(layer.get(), nullptr, nullptr);
        panel.refresh();

        auto *combo = panel.findChild<QComboBox *>();
        QVERIFY(combo);
        int catIdx = -1;
        for (int i = 0; i < combo->count(); ++i)
            if (combo->itemText(i).startsWith(QStringLiteral("Junctions")))
                catIdx = i;
        QVERIFY(catIdx >= 0);
        combo->setCurrentIndex(catIdx);

        auto *view = panel.findChild<QTableView *>();
        auto *edit = panel.findChild<QLineEdit *>();
        QVERIFY(view && edit);
        auto *m = view->model();
        QVERIFY(m);
        const int unfiltered = m->rowCount();
        QVERIFY2(unfiltered > 0, "fixture has no junctions");

        // "Invert elev" is the ColumnSpec key; "Invert Elev" the label; the
        // header adds a unit suffix. All three must select the same rows.
        SWMMAttributeTableModel probe;
        probe.setSource(layer.get(), SWMMModelLayer::CatJunctions);
        const auto specs = probe.columnSpecs();
        const int col = colFor(specs, QStringLiteral("Invert elev"));
        QVERIFY2(col >= 0, "fixture schema lost the Invert elev column");

        QString header;
        for (int c = 0; c < m->columnCount(); ++c)
            if (m->headerData(c, Qt::Horizontal).toString()
                    .startsWith(specs[col].label))
                header = m->headerData(c, Qt::Horizontal).toString();
        QVERIFY(!header.isEmpty());

        // Plain decimals only — the tokenizer has no scientific notation,
        // and a parse error makes Apply return early WITHOUT touching the
        // filter, which would leave every assertion below reading the
        // previous query's result instead of this one's.
        auto matchCount = [&](const QString &field, const char *op) {
            edit->setText(QStringLiteral("[%1] %2 -99999").arg(field, op));
            QTest::keyClick(edit, Qt::Key_Return);
            return view->model()->rowCount();
        };

        // A predicate true for every row keeps every row...
        QCOMPARE(matchCount(specs[col].key,   ">"), unfiltered);
        QCOMPARE(matchCount(specs[col].label, ">"), unfiltered);
        QCOMPARE(matchCount(header,           ">"), unfiltered);

        // ...and the same predicate inverted drops them all, which proves
        // the column is actually being READ rather than silently skipped
        // (a skipped column would leave the row count at `unfiltered` in
        // both directions).
        QCOMPARE(matchCount(specs[col].key,   "<"), 0);
        QCOMPARE(matchCount(specs[col].label, "<"), 0);
        QCOMPARE(matchCount(header,           "<"), 0);
    }

    //! A field naming no column matches nothing — same as before, when an
    //! unknown key simply produced an absent QVariant. Compound columns are
    //! deliberately unresolvable (their value is an edit-ref struct, and
    //! reading one runs an engine-wide scan), so they land here too.
    void queryOnUnresolvableFieldMatchesNothing()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        AttributeTablePanel panel;
        panel.setProject(layer.get(), nullptr, nullptr);
        panel.refresh();

        auto *view = panel.findChild<QTableView *>();
        auto *edit = panel.findChild<QLineEdit *>();
        QVERIFY(view && edit);

        edit->setText(QStringLiteral("[No Such Column] = 1"));
        QTest::keyClick(edit, Qt::Key_Return);
        QCOMPARE(view->model()->rowCount(), 0);
    }

    // =====================================================================
    // 4 — SoA index == engine index
    // =====================================================================
    //
    // SWMMAttributeTableModel::data() resolves a row's engine index with
    // SWMMModelLayer::soaIndexAt() instead of re-deriving it from the
    // object's name. That is only correct while the layer's SoA index and
    // the engine's index are the same number. The invariant is already
    // load-bearing elsewhere (applyNodeDelete feeds a swmm_node_index()
    // result straight into m_nodes.removeAt()), but nothing pinned it —
    // and a violation would be silent: the cell would show a NEIGHBOURING
    // object's value rather than going blank.

    void soaIndexMatchesEngineIndex()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMM_Engine eng = layer->engine();
        QVERIFY(eng);

        for (auto cat : allCategories()) {
            const int n = layer->categoryCount(cat);
            for (int row = 0; row < n; ++row) {
                const QString name = layer->objectNameAt(cat, row);
                const int idx = layer->soaIndexAt(cat, row);
                QVERIFY2(idx >= 0,
                         qPrintable(QStringLiteral("cat %1 row %2: no index")
                                        .arg(int(cat)).arg(row)));
                QCOMPARE(QString::fromUtf8(engineIdFor(eng, cat, idx)), name);
            }
        }
    }

    //! The same invariant with a user-defined row ordering installed.
    //! This is the case the 2026-08-13 attempt at an index-based fetch got
    //! wrong: objectNameAt honours m_objectOrderOverrides and the helper it
    //! was paired with did not, so the two disagreed under a sorted
    //! category. soaIndexAt is written as objectNameAt's structural twin
    //! precisely so this passes.
    void soaIndexHonoursOrderingOverride()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMM_Engine eng = layer->engine();
        QVERIFY(eng);

        const auto cat = SWMMModelLayer::CatJunctions;
        const int n = layer->categoryCount(cat);
        QVERIFY2(n >= 2, "fixture needs >= 2 junctions to permute");

        // Reverse the default order — a permutation, so setObjectOrder
        // accepts it.
        QVector<int> order = layer->objectOrder(cat);
        if (order.isEmpty()) {
            order.reserve(n);
            for (int r = 0; r < n; ++r) order << layer->soaIndexAt(cat, r);
        }
        const QString firstBefore = layer->objectNameAt(cat, 0);
        std::reverse(order.begin(), order.end());
        layer->setObjectOrder(cat, order);

        // setObjectOrder rejects a non-permutation by silently returning,
        // which would leave this test asserting the DEFAULT path twice and
        // pin nothing. Prove the override actually took effect first.
        QVERIFY2(layer->objectNameAt(cat, 0) != firstBefore,
                 "setObjectOrder did not reorder — the override path below "
                 "would be untested");

        for (int row = 0; row < n; ++row) {
            const QString name = layer->objectNameAt(cat, row);
            const int idx = layer->soaIndexAt(cat, row);
            QVERIFY(idx >= 0);
            QCOMPARE(QString::fromUtf8(engineIdFor(eng, cat, idx)), name);
        }
    }

};

QTEST_MAIN(TestAttributeTableSchema)
#include "test_attributetableschema.moc"
