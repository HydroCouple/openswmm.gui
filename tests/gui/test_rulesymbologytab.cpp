/*!
 * \file   test_rulesymbologytab.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Tests for Slice Z.3 — RuleSymbologyTab widget.
 *
 *         Success criterion (RENDERING_RULE_MODEL_PLAN.md §16, Z.3):
 *           - Active Rule combo + Rule List populate from a RuleList.
 *           - Per-row visibility checkbox writes through to Rule.
 *           - Active-index sync is bidirectional (combo ↔ list ↔ model).
 *           - Drag-reorder writes through to RuleList::move.
 *           - [+] / [Duplicate] / [Delete] / [↑] / [↓] buttons mutate
 *             the model.
 *
 *         Tests run under QT_QPA_PLATFORM=offscreen (set by
 *         add_swmmvis_gui_test). No editor-binding tests here — that
 *         lands in the follow-up Z.3a slice.
 */

#include <QApplication>
#include <QComboBox>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>
#include <QtTest/QtTest>

#include "render/renderers/categorizedrenderer.h"
#include "render/renderers/graduatedrenderer.h"
#include "render/renderers/singlesymbolrenderer.h"
#include "render/rule.h"
#include "render/rulelist.h"
#include "ui/dialogs/rulesymbologytab.h"

using namespace OpenSWMM::Render;
using openswmmvis::ui::RuleSymbologyTab;

class TestRuleSymbologyTab : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();

    void empty_listShowsPlaceholderBody();
    void populated_comboHasOneEntryPerRule();
    void populated_listHasOneRowPerRule();
    void populated_listRowsHaveCheckboxes();
    void populated_listRowsReflectVisibility();

    void comboChange_updatesModelActiveIndex();
    void listRowChange_updatesModelActiveIndex();
    void modelActiveIndexChange_updatesUi();

    void checkboxToggle_writesThroughToRule();
    void ruleVisibilityChange_updatesCheckbox();
    void ruleNameChange_updatesComboAndList();

    void addButton_appendsNewRule();
    void duplicateButton_clonesActiveRule();
    void deleteButton_removesActiveRule();
    void moveUpButton_movesActiveRule();
    void moveDownButton_movesActiveRule();

    void buttons_disabledForEmptyList();
    void upButton_disabledForFirstRow();
    void downButton_disabledForLastRow();

    // Z.3a — Renderer-class picker.
    void rendererCombo_populatedWithFourChoices();
    void rendererCombo_disabledForEmptyList();
    void rendererCombo_reflectsActiveRuleRenderer();
    void rendererCombo_picksGraduatedSwapsRulesRenderer();
    void rendererCombo_syncsWhenRulesRendererReplacedExternally();
    void rendererCombo_updatesOnActiveRuleChange();

private:
    // QApplication is required for QWidget construction; created in
    // initTestCase and torn down by Qt's auto-management at process exit.
    // We hold it via a static singleton in main() (QTEST_MAIN supplies it).
};

void TestRuleSymbologyTab::initTestCase()
{
    // No-op — QTEST_MAIN already constructs QApplication.
}

// ── Population ───────────────────────────────────────────────────────

void TestRuleSymbologyTab::empty_listShowsPlaceholderBody()
{
    RuleList rl;
    RuleSymbologyTab tab(&rl);
    auto *combo = tab.findChild<QComboBox *>();
    auto *list  = tab.findChild<QListWidget *>();
    QVERIFY(combo);
    QVERIFY(list);
    QCOMPARE(combo->count(), 0);
    QCOMPARE(list->count(), 0);
}

void TestRuleSymbologyTab::populated_comboHasOneEntryPerRule()
{
    RuleList rl;
    rl.append(std::make_unique<Rule>(QStringLiteral("Junctions"), nullptr));
    rl.append(std::make_unique<Rule>(QStringLiteral("Conduits"),  nullptr));
    rl.append(std::make_unique<Rule>(QStringLiteral("Subcatch"),  nullptr));

    RuleSymbologyTab tab(&rl);
    auto *combo = tab.findChild<QComboBox *>();
    QCOMPARE(combo->count(), 3);
    QCOMPARE(combo->itemText(0), QStringLiteral("Junctions"));
    QCOMPARE(combo->itemText(2), QStringLiteral("Subcatch"));
}

void TestRuleSymbologyTab::populated_listHasOneRowPerRule()
{
    RuleList rl;
    rl.append(std::make_unique<Rule>(QStringLiteral("A"), nullptr));
    rl.append(std::make_unique<Rule>(QStringLiteral("B"), nullptr));

    RuleSymbologyTab tab(&rl);
    auto *list = tab.findChild<QListWidget *>();
    QCOMPARE(list->count(), 2);
    QCOMPARE(list->item(0)->text(), QStringLiteral("A"));
    QCOMPARE(list->item(1)->text(), QStringLiteral("B"));
}

void TestRuleSymbologyTab::populated_listRowsHaveCheckboxes()
{
    RuleList rl;
    rl.append(std::make_unique<Rule>(QStringLiteral("A"), nullptr));

    RuleSymbologyTab tab(&rl);
    auto *list = tab.findChild<QListWidget *>();
    QVERIFY(list->item(0)->flags() & Qt::ItemIsUserCheckable);
}

void TestRuleSymbologyTab::populated_listRowsReflectVisibility()
{
    RuleList rl;
    auto a = std::make_unique<Rule>(QStringLiteral("A"), nullptr);
    auto b = std::make_unique<Rule>(QStringLiteral("B"), nullptr);
    b->setVisible(false);
    rl.append(std::move(a));
    rl.append(std::move(b));

    RuleSymbologyTab tab(&rl);
    auto *list = tab.findChild<QListWidget *>();
    QCOMPARE(list->item(0)->checkState(), Qt::Checked);
    QCOMPARE(list->item(1)->checkState(), Qt::Unchecked);
}

// ── Active-index sync ────────────────────────────────────────────────

void TestRuleSymbologyTab::comboChange_updatesModelActiveIndex()
{
    RuleList rl;
    rl.append(std::make_unique<Rule>(QStringLiteral("A"), nullptr));
    rl.append(std::make_unique<Rule>(QStringLiteral("B"), nullptr));

    RuleSymbologyTab tab(&rl);
    auto *combo = tab.findChild<QComboBox *>();
    combo->setCurrentIndex(1);
    QCOMPARE(rl.activeIndex(), 1);
}

void TestRuleSymbologyTab::listRowChange_updatesModelActiveIndex()
{
    RuleList rl;
    rl.append(std::make_unique<Rule>(QStringLiteral("A"), nullptr));
    rl.append(std::make_unique<Rule>(QStringLiteral("B"), nullptr));

    RuleSymbologyTab tab(&rl);
    auto *list = tab.findChild<QListWidget *>();
    list->setCurrentRow(1);
    QCOMPARE(rl.activeIndex(), 1);
}

void TestRuleSymbologyTab::modelActiveIndexChange_updatesUi()
{
    RuleList rl;
    rl.append(std::make_unique<Rule>(QStringLiteral("A"), nullptr));
    rl.append(std::make_unique<Rule>(QStringLiteral("B"), nullptr));

    RuleSymbologyTab tab(&rl);
    auto *combo = tab.findChild<QComboBox *>();
    auto *list  = tab.findChild<QListWidget *>();

    rl.setActiveIndex(1);
    QCOMPARE(combo->currentIndex(), 1);
    QCOMPARE(list->currentRow(), 1);
}

// ── Visibility checkbox ──────────────────────────────────────────────

void TestRuleSymbologyTab::checkboxToggle_writesThroughToRule()
{
    RuleList rl;
    Rule *r = rl.append(std::make_unique<Rule>(QStringLiteral("A"), nullptr));

    RuleSymbologyTab tab(&rl);
    auto *list = tab.findChild<QListWidget *>();

    list->item(0)->setCheckState(Qt::Unchecked);
    QCOMPARE(r->isVisible(), false);

    list->item(0)->setCheckState(Qt::Checked);
    QCOMPARE(r->isVisible(), true);
}

void TestRuleSymbologyTab::ruleVisibilityChange_updatesCheckbox()
{
    RuleList rl;
    Rule *r = rl.append(std::make_unique<Rule>(QStringLiteral("A"), nullptr));

    RuleSymbologyTab tab(&rl);
    auto *list = tab.findChild<QListWidget *>();

    r->setVisible(false);
    QCOMPARE(list->item(0)->checkState(), Qt::Unchecked);
    r->setVisible(true);
    QCOMPARE(list->item(0)->checkState(), Qt::Checked);
}

void TestRuleSymbologyTab::ruleNameChange_updatesComboAndList()
{
    RuleList rl;
    Rule *r = rl.append(std::make_unique<Rule>(QStringLiteral("Original"), nullptr));

    RuleSymbologyTab tab(&rl);
    auto *combo = tab.findChild<QComboBox *>();
    auto *list  = tab.findChild<QListWidget *>();

    r->setName(QStringLiteral("Renamed"));
    QCOMPARE(combo->itemText(0), QStringLiteral("Renamed"));
    QCOMPARE(list->item(0)->text(), QStringLiteral("Renamed"));
}

// ── Buttons ──────────────────────────────────────────────────────────

void TestRuleSymbologyTab::addButton_appendsNewRule()
{
    RuleList rl;
    RuleSymbologyTab tab(&rl);
    auto buttons = tab.findChildren<QPushButton *>();
    QPushButton *add = nullptr;
    for (auto *b : buttons)
        if (b->text() == QStringLiteral("+")) { add = b; break; }
    QVERIFY(add);

    add->click();
    QCOMPARE(rl.count(), 1);
}

void TestRuleSymbologyTab::duplicateButton_clonesActiveRule()
{
    RuleList rl;
    rl.append(std::make_unique<Rule>(QStringLiteral("Source"), nullptr));

    RuleSymbologyTab tab(&rl);
    auto buttons = tab.findChildren<QPushButton *>();
    QPushButton *dup = nullptr;
    for (auto *b : buttons)
        if (b->text() == QStringLiteral("Duplicate")) { dup = b; break; }
    QVERIFY(dup);

    dup->click();
    QCOMPARE(rl.count(), 2);
    QVERIFY(rl.at(1)->name().contains(QStringLiteral("copy"), Qt::CaseInsensitive));
}

void TestRuleSymbologyTab::deleteButton_removesActiveRule()
{
    RuleList rl;
    rl.append(std::make_unique<Rule>(QStringLiteral("A"), nullptr));
    rl.append(std::make_unique<Rule>(QStringLiteral("B"), nullptr));

    RuleSymbologyTab tab(&rl);
    auto buttons = tab.findChildren<QPushButton *>();
    QPushButton *del = nullptr;
    for (auto *b : buttons)
        if (b->text() == QStringLiteral("Delete")) { del = b; break; }
    QVERIFY(del);

    del->click();  // deletes active (index 0)
    QCOMPARE(rl.count(), 1);
    QCOMPARE(rl.at(0)->name(), QStringLiteral("B"));
}

void TestRuleSymbologyTab::moveUpButton_movesActiveRule()
{
    RuleList rl;
    rl.append(std::make_unique<Rule>(QStringLiteral("A"), nullptr));
    rl.append(std::make_unique<Rule>(QStringLiteral("B"), nullptr));
    rl.setActiveIndex(1);

    RuleSymbologyTab tab(&rl);
    auto buttons = tab.findChildren<QPushButton *>();
    QPushButton *up = nullptr;
    for (auto *b : buttons)
        if (b->text() == QStringLiteral("↑")) { up = b; break; }
    QVERIFY(up);

    up->click();
    QCOMPARE(rl.at(0)->name(), QStringLiteral("B"));
}

void TestRuleSymbologyTab::moveDownButton_movesActiveRule()
{
    RuleList rl;
    rl.append(std::make_unique<Rule>(QStringLiteral("A"), nullptr));
    rl.append(std::make_unique<Rule>(QStringLiteral("B"), nullptr));
    rl.setActiveIndex(0);

    RuleSymbologyTab tab(&rl);
    auto buttons = tab.findChildren<QPushButton *>();
    QPushButton *down = nullptr;
    for (auto *b : buttons)
        if (b->text() == QStringLiteral("↓")) { down = b; break; }
    QVERIFY(down);

    down->click();
    QCOMPARE(rl.at(1)->name(), QStringLiteral("A"));
}

void TestRuleSymbologyTab::buttons_disabledForEmptyList()
{
    RuleList rl;
    RuleSymbologyTab tab(&rl);
    auto buttons = tab.findChildren<QPushButton *>();
    for (auto *b : buttons) {
        if (b->text() == QStringLiteral("Duplicate") ||
            b->text() == QStringLiteral("Delete") ||
            b->text() == QStringLiteral("↑") ||
            b->text() == QStringLiteral("↓"))
        {
            QVERIFY2(!b->isEnabled(),
                     qPrintable(QStringLiteral("%1 should be disabled on empty list")
                                    .arg(b->text())));
        }
    }
}

void TestRuleSymbologyTab::upButton_disabledForFirstRow()
{
    RuleList rl;
    rl.append(std::make_unique<Rule>(QStringLiteral("A"), nullptr));
    rl.append(std::make_unique<Rule>(QStringLiteral("B"), nullptr));
    rl.setActiveIndex(0);

    RuleSymbologyTab tab(&rl);
    auto buttons = tab.findChildren<QPushButton *>();
    for (auto *b : buttons)
        if (b->text() == QStringLiteral("↑"))
            QVERIFY(!b->isEnabled());
}

void TestRuleSymbologyTab::downButton_disabledForLastRow()
{
    RuleList rl;
    rl.append(std::make_unique<Rule>(QStringLiteral("A"), nullptr));
    rl.append(std::make_unique<Rule>(QStringLiteral("B"), nullptr));
    rl.setActiveIndex(1);

    RuleSymbologyTab tab(&rl);
    auto buttons = tab.findChildren<QPushButton *>();
    for (auto *b : buttons)
        if (b->text() == QStringLiteral("↓"))
            QVERIFY(!b->isEnabled());
}

// ── Z.3a — Renderer-class picker ─────────────────────────────────────

namespace {
// Locate the renderer-class combo inside the body region (the second
// QComboBox in the widget tree — the first is the Active Rule combo).
QComboBox *findRendererCombo(QWidget &tab)
{
    const auto combos = tab.findChildren<QComboBox *>();
    return combos.size() >= 2 ? combos[1] : nullptr;
}
} // namespace

void TestRuleSymbologyTab::rendererCombo_populatedWithFourChoices()
{
    RuleList rl;
    RuleSymbologyTab tab(&rl);
    auto *combo = findRendererCombo(tab);
    QVERIFY(combo);
    QCOMPARE(combo->count(), 4);
    QCOMPARE(combo->itemData(0).toString(), QStringLiteral("single"));
    QCOMPARE(combo->itemData(1).toString(), QStringLiteral("graduated"));
    QCOMPARE(combo->itemData(2).toString(), QStringLiteral("categorized"));
    QCOMPARE(combo->itemData(3).toString(), QStringLiteral("rule"));
}

void TestRuleSymbologyTab::rendererCombo_disabledForEmptyList()
{
    RuleList rl;
    RuleSymbologyTab tab(&rl);
    auto *combo = findRendererCombo(tab);
    QVERIFY(combo);
    QVERIFY(!combo->isEnabled());
}

void TestRuleSymbologyTab::rendererCombo_reflectsActiveRuleRenderer()
{
    RuleList rl;
    rl.append(std::make_unique<Rule>(QStringLiteral("g"),
                                     std::make_unique<GraduatedRenderer>()));
    RuleSymbologyTab tab(&rl);
    auto *combo = findRendererCombo(tab);
    QVERIFY(combo);
    QCOMPARE(combo->currentData().toString(), QStringLiteral("graduated"));
}

void TestRuleSymbologyTab::rendererCombo_picksGraduatedSwapsRulesRenderer()
{
    RuleList rl;
    Rule *r = rl.append(std::make_unique<Rule>(QStringLiteral("r"), nullptr));
    QCOMPARE(r->renderer()->rendererId(), QStringLiteral("single"));

    RuleSymbologyTab tab(&rl);
    auto *combo = findRendererCombo(tab);
    const int graduatedIdx = combo->findData(QStringLiteral("graduated"));
    QVERIFY(graduatedIdx >= 0);
    combo->setCurrentIndex(graduatedIdx);
    QCOMPARE(r->renderer()->rendererId(), QStringLiteral("graduated"));
}

void TestRuleSymbologyTab::rendererCombo_syncsWhenRulesRendererReplacedExternally()
{
    RuleList rl;
    Rule *r = rl.append(std::make_unique<Rule>(QStringLiteral("r"), nullptr));

    RuleSymbologyTab tab(&rl);
    auto *combo = findRendererCombo(tab);
    QCOMPARE(combo->currentData().toString(), QStringLiteral("single"));

    r->setRenderer(std::make_unique<CategorizedRenderer>());
    QCOMPARE(combo->currentData().toString(), QStringLiteral("categorized"));
}

void TestRuleSymbologyTab::rendererCombo_updatesOnActiveRuleChange()
{
    RuleList rl;
    rl.append(std::make_unique<Rule>(QStringLiteral("a"),
                                     std::make_unique<SingleSymbolRenderer>()));
    rl.append(std::make_unique<Rule>(QStringLiteral("b"),
                                     std::make_unique<GraduatedRenderer>()));

    RuleSymbologyTab tab(&rl);
    auto *combo = findRendererCombo(tab);
    QCOMPARE(combo->currentData().toString(), QStringLiteral("single"));
    rl.setActiveIndex(1);
    QCOMPARE(combo->currentData().toString(), QStringLiteral("graduated"));
}

QTEST_MAIN(TestRuleSymbologyTab)
#include "test_rulesymbologytab.moc"
