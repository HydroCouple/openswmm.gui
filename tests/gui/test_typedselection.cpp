/*!
 * \file   test_typedselection.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Coverage for commit a35798e (fix(selection): typed per-kind
 *         selection and visibility) against
 *         tests/gui/data/typed_selection_fixture.inp — a model with two
 *         deliberate name collisions (rain gage + subcatchment both named
 *         "S1"; junction + conduit both named "X1") so that per-kind
 *         selection/visibility bleed regressions are caught.
 *
 *         Each test slot loads its own fresh SWMMModelLayer so slots are
 *         independent of execution order.
 */
#include "layers/swmmmodellayer.h"

#include <QDir>
#include <QFile>
#include <QObject>
#include <QSignalSpy>
#include <QTest>

#include <memory>

using SelectedElement = SWMMModelLayer::SelectedElement;
using Category = SWMMModelLayer::Category;

namespace {

QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
}

QString fixturePath()
{
    return QDir(dataDir()).filePath(QStringLiteral("typed_selection_fixture.inp"));
}

// Fresh layer + loaded model, per test slot so slots don't leak state into
// each other via execution order.
std::unique_ptr<SWMMModelLayer> openFixtureLayer()
{
    auto layer = std::make_unique<SWMMModelLayer>(fixturePath(), nullptr);
    QList<QString> warnings, errors;
    if (!layer->loadModel(warnings, errors))
        return nullptr;
    return layer;
}

// Row of the first object named `name` in category `c` (-1 if not found).
// objectNameAt()/categoryCount() are per-category, so this cannot confuse
// same-named objects of a DIFFERENT category — exactly the property this
// commit relies on.
int rowForName(const SWMMModelLayer &layer, Category c, const QString &name)
{
    const int n = layer.categoryCount(c);
    for (int row = 0; row < n; ++row)
        if (layer.objectNameAt(c, row) == name)
            return row;
    return -1;
}

} // namespace

class TestTypedSelection : public QObject
{
    Q_OBJECT

private slots:

    void initTestCase()
    {
        QVERIFY2(QFile::exists(fixturePath()),
                 "typed_selection_fixture.inp missing from the gui-test data dir");
    }

    // 1. Typed select doesn't bleed: selecting the CATCHMENT "S1" must not
    //    also mark the same-named GAGE "S1" as selected.
    void typedSelectDoesNotBleed()
    {
        auto layer = openFixtureLayer();
        QVERIFY(layer != nullptr);

        layer->setSelectedElements({ SelectedElement{ QStringLiteral("S1"),
                                                       SWMMModelLayer::kKindCatch } });

        const auto &sel = layer->selectedElements();
        QCOMPARE(sel.size(), 1);
        QCOMPARE(sel[0].name, QStringLiteral("S1"));
        QCOMPARE(sel[0].kinds, SWMMModelLayer::kKindCatch);
        QVERIFY(!(sel[0].kinds & SWMMModelLayer::kKindGage));

        QCOMPARE(layer->selectedElementNames(), QStringList{ QStringLiteral("S1") });
    }

    // 2. Legacy name-only selection carries no kind info, so it must select
    //    every kind bearing the name (kKindAll).
    void legacySelectMapsToAllKinds()
    {
        auto layer = openFixtureLayer();
        QVERIFY(layer != nullptr);

        layer->setSelectedElementNames({ QStringLiteral("S1") });

        const auto &sel = layer->selectedElements();
        QCOMPARE(sel.size(), 1);
        QCOMPARE(sel[0].name, QStringLiteral("S1"));
        QCOMPARE(sel[0].kinds, SWMMModelLayer::kKindAll);
    }

    // 3. Two typed entries for the same name (catchment + gage) must
    //    dedup to one entry in the name mirror but keep both typed entries,
    //    in order, in the canonical vector.
    void mirrorDedup()
    {
        auto layer = openFixtureLayer();
        QVERIFY(layer != nullptr);

        const QVector<SelectedElement> both = {
            SelectedElement{ QStringLiteral("S1"), SWMMModelLayer::kKindCatch },
            SelectedElement{ QStringLiteral("S1"), SWMMModelLayer::kKindGage },
        };
        layer->setSelectedElements(both);

        QCOMPARE(layer->selectedElementNames(), QStringList{ QStringLiteral("S1") });

        const auto &sel = layer->selectedElements();
        QCOMPARE(sel.size(), 2);
        QCOMPARE(sel[0].kinds, SWMMModelLayer::kKindCatch);
        QCOMPARE(sel[1].kinds, SWMMModelLayer::kKindGage);
    }

    // 4. selectionChanged fires on a real change, and does NOT fire when
    //    the new selection is identical to the current one.
    void selectionChangedSignal()
    {
        auto layer = openFixtureLayer();
        QVERIFY(layer != nullptr);

        QSignalSpy spy(layer.get(), &SWMMModelLayer::selectionChanged);
        QVERIFY(spy.isValid());

        layer->setSelectedElements({ SelectedElement{ QStringLiteral("J1"),
                                                       SWMMModelLayer::kKindNode } });
        QCOMPARE(spy.count(), 1);

        // Identical selection (same name, same kind) — no-op, no signal.
        layer->setSelectedElements({ SelectedElement{ QStringLiteral("J1"),
                                                       SWMMModelLayer::kKindNode } });
        QCOMPARE(spy.count(), 1);

        // Genuinely different selection fires again.
        layer->setSelectedElements({ SelectedElement{ QStringLiteral("O1"),
                                                       SWMMModelLayer::kKindNode } });
        QCOMPARE(spy.count(), 2);
    }

    // 5. Typed hide only clears the visibility bit for that ONE kind —
    //    hiding the gage "S1" must leave the subcatchment "S1" visible.
    void typedHideIsPerKind()
    {
        auto layer = openFixtureLayer();
        QVERIFY(layer != nullptr);

        const int gageRow = rowForName(*layer, SWMMModelLayer::CatRainGages,
                                       QStringLiteral("S1"));
        QVERIFY(gageRow >= 0);

        layer->setObjectVisibleAt(SWMMModelLayer::CatRainGages, gageRow, false);

        QVERIFY(!layer->isObjectVisible(QStringLiteral("S1"), SWMMModelLayer::CatRainGages));
        QVERIFY(layer->isObjectVisible(QStringLiteral("S1"), SWMMModelLayer::CatSubcatchments));
    }

    // 6. Legacy (name-only) hide affects every kind bearing the name; a
    //    subsequent typed UNhide of one kind only restores that kind's bit
    //    (mask bit clearing), leaving the other kind still hidden.
    void legacyHideIsAllKind()
    {
        auto layer = openFixtureLayer();
        QVERIFY(layer != nullptr);

        layer->setObjectVisible(QStringLiteral("S1"), false);
        QVERIFY(!layer->isObjectVisible(QStringLiteral("S1"), SWMMModelLayer::CatRainGages));
        QVERIFY(!layer->isObjectVisible(QStringLiteral("S1"), SWMMModelLayer::CatSubcatchments));

        const int gageRow = rowForName(*layer, SWMMModelLayer::CatRainGages,
                                       QStringLiteral("S1"));
        QVERIFY(gageRow >= 0);
        layer->setObjectVisibleAt(SWMMModelLayer::CatRainGages, gageRow, true);

        QVERIFY(layer->isObjectVisible(QStringLiteral("S1"), SWMMModelLayer::CatRainGages));
        QVERIFY(!layer->isObjectVisible(QStringLiteral("S1"), SWMMModelLayer::CatSubcatchments));
    }

    // 7. Hit-testing (identifyAt) must skip only the kind that is hidden:
    //    hiding the subcatchment must not stop a click on the gage's own
    //    coordinate from finding the gage, and vice versa.
    void hitTestingSkipsOnlyHiddenKind()
    {
        auto layer = openFixtureLayer();
        QVERIFY(layer != nullptr);

        // Fixture coordinates (see typed_selection_fixture.inp):
        //   gage "S1"          [SYMBOLS]  (-2000, -2000)
        //   subcatchment "S1"  [Polygons] centred on (-2000, 2000)
        const double gageX = -2000.0, gageY = -2000.0;
        const double catchX = -2000.0, catchY = 2000.0;
        const double tol = 1.0;

        const int catchRow = rowForName(*layer, SWMMModelLayer::CatSubcatchments,
                                        QStringLiteral("S1"));
        const int gageRow  = rowForName(*layer, SWMMModelLayer::CatRainGages,
                                        QStringLiteral("S1"));
        QVERIFY(catchRow >= 0);
        QVERIFY(gageRow >= 0);

        // Hide the subcatchment (typed) — a click on the gage must still
        // resolve to the gage.
        layer->setObjectVisibleAt(SWMMModelLayer::CatSubcatchments, catchRow, false);
        QVariantMap hit = layer->identifyAt(gageX, gageY, tol);
        QCOMPARE(hit.value(QStringLiteral("elementType")).toString(),
                 QStringLiteral("RainGage"));

        // Unhide the subcatchment, hide the gage instead — a click inside
        // the polygon must still resolve to the subcatchment.
        layer->setObjectVisibleAt(SWMMModelLayer::CatSubcatchments, catchRow, true);
        layer->setObjectVisibleAt(SWMMModelLayer::CatRainGages, gageRow, false);
        hit = layer->identifyAt(catchX, catchY, tol);
        QCOMPARE(hit.value(QStringLiteral("elementType")).toString(),
                 QStringLiteral("Subcatchment"));
    }

    // 8. Rename migration is kind-scoped.
    //
    //    applyRename() takes an optional kindHint (kKindNode/Link/Gage/
    //    Catch) so a caller that knows which kind it means — as every real
    //    UI call site does — can rename the CATCHMENT "S1" without
    //    disturbing the same-named GAGE "S1", and vice versa. Without a
    //    hint (kKindAll, the default), it falls back to a best-effort
    //    priority scan (node -> link -> gage -> catchment) for callers
    //    that don't know the kind.
    //
    //    Whichever kind actually gets renamed in the engine, the
    //    selection/hidden-state migration only moves the bit(s) matching
    //    THAT kind — an entry for a different, same-named kind that was
    //    NOT renamed keeps pointing at the old name (it's still correct:
    //    that object still has the old name).
    void renameMigratesSelectionAndHiddenState()
    {
        auto layer = openFixtureLayer();
        QVERIFY(layer != nullptr);

        // Select the CATCHMENT "S1" (not the gage).
        layer->setSelectedElements({ SelectedElement{ QStringLiteral("S1"),
                                                       SWMMModelLayer::kKindCatch } });
        // Hide the CONDUIT "X1" (not the junction).
        const int linkRow = rowForName(*layer, SWMMModelLayer::CatConduits,
                                       QStringLiteral("X1"));
        QVERIFY(linkRow >= 0);
        layer->setObjectVisibleAt(SWMMModelLayer::CatConduits, linkRow, false);
        QVERIFY(!layer->isObjectVisible(QStringLiteral("X1"), SWMMModelLayer::CatConduits));

        // Hinted rename: target the CATCHMENT specifically, not the gage
        // that a bare priority scan would otherwise pick.
        QVERIFY(layer->applyRename(QStringLiteral("S1"), QStringLiteral("S1x"),
                                   SWMMModelLayer::kKindCatch));
        // Unhinted rename: ambiguous, falls back to the priority scan,
        // which finds the JUNCTION "X1" before the conduit "X1".
        QVERIFY(layer->applyRename(QStringLiteral("X1"), QStringLiteral("X1x")));

        // ---- What applyRename() actually renamed in the engine ----------
        // Catchment renamed (hinted): a click inside its polygon now
        // resolves to the RENAMED name "S1x".
        QVariantMap catchHit = layer->identifyAt(-2000.0, 2000.0, 1.0);
        QCOMPARE(catchHit.value(QStringLiteral("elementType")).toString(),
                 QStringLiteral("Subcatchment"));
        QCOMPARE(catchHit.value(QStringLiteral("elementName")).toString(),
                 QStringLiteral("S1x"));
        // Gage did NOT rename (the hint kept it out of scope) — a click
        // on its coordinate still resolves to the OLD name "S1".
        QVariantMap gageHit = layer->identifyAt(-2000.0, -2000.0, 1.0);
        QCOMPARE(gageHit.value(QStringLiteral("elementType")).toString(),
                 QStringLiteral("RainGage"));
        QCOMPARE(gageHit.value(QStringLiteral("elementName")).toString(),
                 QStringLiteral("S1"));

        // Junction moved to (1000,0) (tier-1 point priority wins over the
        // conduit sharing that endpoint): a click there resolves to the
        // RENAMED node "X1x".
        QVariantMap nodeHit = layer->identifyAt(1000.0, 0.0, 1.0);
        QCOMPARE(nodeHit.value(QStringLiteral("elementType")).toString(),
                 QStringLiteral("Node"));
        QCOMPARE(nodeHit.value(QStringLiteral("elementName")).toString(),
                 QStringLiteral("X1x"));
        // Conduit did NOT rename (ambiguous scan picked the node): still
        // findable by its old name, not by the new one. A name-based
        // lookup rather than identifyAt() — the conduit is deliberately
        // still HIDDEN at this point (see below), so a click on it would
        // correctly find nothing.
        QVERIFY(rowForName(*layer, SWMMModelLayer::CatConduits,
                           QStringLiteral("X1")) >= 0);
        QVERIFY(rowForName(*layer, SWMMModelLayer::CatConduits,
                           QStringLiteral("X1x")) < 0);

        // ---- Selection migrated for exactly the renamed kind -------------
        // {"S1", kKindCatch} moves to {"S1x", kKindCatch} because the
        // catchment — the kind that entry actually selects — is the kind
        // that was renamed.
        const auto &sel = layer->selectedElements();
        QCOMPARE(sel.size(), 1);
        QCOMPARE(sel[0].name, QStringLiteral("S1x"));
        QCOMPARE(sel[0].kinds, SWMMModelLayer::kKindCatch);
        QVERIFY(!layer->selectedElementNames().contains(QStringLiteral("S1")));

        // ---- Hidden state untouched for a kind that was NOT renamed ------
        // The hidden entry belongs to the conduit (kKindLink); the X1
        // rename actually renamed the junction (kKindNode), so the
        // conduit's hidden state stays under the old name "X1" — it is
        // still correct, because the conduit is still named "X1".
        QVERIFY(!layer->isObjectVisible(QStringLiteral("X1"), SWMMModelLayer::CatConduits));
        // No spurious hidden entry was created for the renamed-away node
        // name "X1x" (nothing of the hidden kind, kKindLink, moved there).
        QVERIFY(layer->isObjectVisible(QStringLiteral("X1x"), SWMMModelLayer::CatConduits));
    }

    // 9. Node/link name collision: selecting the NODE "X1" must not select
    //    the LINK "X1", and vice versa.
    void nodeLinkNameCollision()
    {
        auto layer = openFixtureLayer();
        QVERIFY(layer != nullptr);

        layer->setSelectedElements({ SelectedElement{ QStringLiteral("X1"),
                                                       SWMMModelLayer::kKindNode } });
        {
            const auto &sel = layer->selectedElements();
            QCOMPARE(sel.size(), 1);
            QCOMPARE(sel[0].kinds, SWMMModelLayer::kKindNode);
            QVERIFY(!(sel[0].kinds & SWMMModelLayer::kKindLink));
        }

        layer->setSelectedElements({ SelectedElement{ QStringLiteral("X1"),
                                                       SWMMModelLayer::kKindLink } });
        {
            const auto &sel = layer->selectedElements();
            QCOMPARE(sel.size(), 1);
            QCOMPARE(sel[0].kinds, SWMMModelLayer::kKindLink);
            QVERIFY(!(sel[0].kinds & SWMMModelLayer::kKindNode));
        }
    }

    // 10. Reload: closeEngine() + loadModel() must drop both per-object
    //     HIDDEN state and typed SELECTION state — a similarly-named
    //     object in a different project has no business inheriting either
    //     from the model that used to be open.
    void reloadClearsState()
    {
        auto layer = openFixtureLayer();
        QVERIFY(layer != nullptr);

        const int gageRow = rowForName(*layer, SWMMModelLayer::CatRainGages,
                                       QStringLiteral("S1"));
        QVERIFY(gageRow >= 0);
        layer->setObjectVisibleAt(SWMMModelLayer::CatRainGages, gageRow, false);
        layer->setSelectedElements({ SelectedElement{ QStringLiteral("J1"),
                                                       SWMMModelLayer::kKindNode } });

        QVERIFY(!layer->isObjectVisible(QStringLiteral("S1"), SWMMModelLayer::CatRainGages));
        QVERIFY(!layer->selectedElements().isEmpty());

        layer->closeEngine();
        QList<QString> warnings, errors;
        QVERIFY(layer->loadModel(warnings, errors));

        // Hidden state: reset.
        QVERIFY(layer->isObjectVisible(QStringLiteral("S1"), SWMMModelLayer::CatRainGages));
        QVERIFY(layer->hiddenObjects().isEmpty());

        // Selection state: also reset.
        QVERIFY(layer->selectedElements().isEmpty());
        QVERIFY(layer->selectedElementNames().isEmpty());
    }
};

QTEST_APPLESS_MAIN(TestTypedSelection)
#include "test_typedselection.moc"
