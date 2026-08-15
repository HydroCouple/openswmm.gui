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

#include <openswmm/engine/openswmm_links.h>

#include <QComboBox>
#include <QDir>
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

};

QTEST_MAIN(TestAttributeTableSchema)
#include "test_attributetableschema.moc"
