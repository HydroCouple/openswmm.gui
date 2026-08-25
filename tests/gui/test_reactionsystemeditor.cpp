/*!
 * \file test_reactionsystemeditor.cpp
 * \brief G-B2/G-C1 — the Reaction System editor, driven through its widgets.
 *
 * \details Dependency-light (Qt Widgets + the swmm_reaction_* /
 *          swmm_process_component_* ABI), constructed against a synthetic
 *          BUILDING engine — the WaterAgeSourcesDialog contract.
 *
 *          The claims:
 *          1. Structured CRUD reaches the engine: species add (duplicate
 *             refused with a status message), coefficient add, term add,
 *             expression set through the table.
 *          2. MVC sync into the File tab: a structured edit is visible in
 *             the serialized text on entry (D-GC1).
 *          3. Reverse sync: text edited on the File tab lands in the
 *             engine when leaving the tab.
 *          4. Bad text GATES the tab switch and Close; Discard restores
 *             the canonical text and unblocks.
 *          5. Per-element initial rows commit; species removal refusal
 *             (still referenced) reports rather than corrupting.
 *          6. The Sources tab is disabled (engine rejects the section).
 */

#include "ui/dialogs/reactionsystemeditordialog.h"

#include <openswmm/engine/openswmm_engine.h>

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QObject>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTabWidget>
#include <QTest>

using OpenSWMMVis::ReactionSystemEditorDialog;

namespace {

SWMM_Engine makeEngine()
{
    SWMM_Engine e = swmm_engine_new();
    Q_ASSERT(e != nullptr);
    swmm_node_add(e, "J0", 0 /*JUNCTION*/);
    swmm_node_add(e, "J1", 0 /*JUNCTION*/);
    swmm_link_add(e, "C1", 0 /*CONDUIT*/);
    swmm_pollutant_add(e, "TSS", 0 /*MG/L*/);
    return e;
}

template <typename T>
T *child(ReactionSystemEditorDialog &dlg, const char *name)
{
    return dlg.findChild<T *>(QLatin1String(name));
}

} // namespace

class TestReactionSystemEditor : public QObject
{
    Q_OBJECT
private slots:
    void constructsWithNullEngine();
    void structuredCrudReachesEngine();
    void fileTabShowsStructuredEdits();
    void fileTabEditsReachEngine();
    void badTextGatesTheTabSwitch();
    void initOverridesCommit();
    void sourcesTabIsDisabled();
};

void TestReactionSystemEditor::constructsWithNullEngine()
{
    ReactionSystemEditorDialog dlg(nullptr, nullptr);
    QCOMPARE(dlg.wroteAnyChanges(), false);
}

void TestReactionSystemEditor::structuredCrudReachesEngine()
{
    SWMM_Engine e = makeEngine();
    ReactionSystemEditorDialog dlg(e, nullptr);

    // Species add.
    child<QLineEdit>(dlg, "rx_newSpeciesName")->setText(
        QStringLiteral("HOCL"));
    child<QPushButton>(dlg, "rx_addSpeciesBtn")->click();
    QCOMPARE(swmm_reaction_species_count(e), 1);
    QCOMPARE(child<QTableWidget>(dlg, "rx_speciesTable")->rowCount(), 1);

    // Duplicate refused, with a status message.
    child<QLineEdit>(dlg, "rx_newSpeciesName")->setText(
        QStringLiteral("HOCL"));
    child<QPushButton>(dlg, "rx_addSpeciesBtn")->click();
    QCOMPARE(swmm_reaction_species_count(e), 1);
    QVERIFY(!child<QLabel>(dlg, "rx_status")->text().isEmpty());

    // Coefficient + term.
    child<QLineEdit>(dlg, "rx_newCoeffName")->setText(QStringLiteral("k1"));
    child<QPushButton>(dlg, "rx_addCoeffBtn")->click();
    QCOMPARE(swmm_reaction_coeff_count(e), 1);
    child<QLineEdit>(dlg, "rx_newTermName")->setText(QStringLiteral("AMM"));
    child<QPushButton>(dlg, "rx_addTermBtn")->click();
    QCOMPARE(swmm_reaction_term_count(e), 1);

    // Expression through the table (row 0 = HOCL/Pipes, column 3).
    auto *exprTable = child<QTableWidget>(dlg, "rx_exprTable");
    QCOMPARE(exprTable->rowCount(), 2);            // 1 species x 2 scopes
    exprTable->item(0, 3)->setText(QStringLiteral("-0.1 * HOCL"));
    int form = -1;
    char expr[256];
    QCOMPARE(swmm_reaction_expr_get(e, SWMM_RXN_SCOPE_PIPE, 0, &form, expr,
                                    256), SWMM_OK);
    QCOMPARE(form, SWMM_RXN_FORM_RATE);
    QCOMPARE(QString::fromUtf8(expr), QStringLiteral("-0.1 * HOCL"));

    // Species removal refused while referenced.
    auto *speciesTable = child<QTableWidget>(dlg, "rx_speciesTable");
    speciesTable->setCurrentCell(0, 0);
    child<QPushButton>(dlg, "rx_removeSpeciesBtn")->click();
    QCOMPARE(swmm_reaction_species_count(e), 1);   // survived
    QVERIFY(child<QLabel>(dlg, "rx_status")->text().contains(
        QStringLiteral("references")));

    QVERIFY(dlg.wroteAnyChanges());
    swmm_engine_destroy(e);
}

void TestReactionSystemEditor::fileTabShowsStructuredEdits()
{
    SWMM_Engine e = makeEngine();
    ReactionSystemEditorDialog dlg(e, nullptr);
    child<QLineEdit>(dlg, "rx_newSpeciesName")->setText(
        QStringLiteral("ZED"));
    child<QPushButton>(dlg, "rx_addSpeciesBtn")->click();

    auto *tabs = child<QTabWidget>(dlg, "rx_tabs");
    const int fileIdx = tabs->count() - 2;         // File is before Sources
    tabs->setCurrentIndex(fileIdx);
    const QString text =
        child<QPlainTextEdit>(dlg, "rx_fileEdit")->toPlainText();
    QVERIFY2(text.contains(QStringLiteral("ZED")),
             "structured edit must be visible on the File tab");
    QVERIFY(text.contains(QStringLiteral("[REACTION_SPECIES]")));
    swmm_engine_destroy(e);
}

void TestReactionSystemEditor::fileTabEditsReachEngine()
{
    SWMM_Engine e = makeEngine();
    ReactionSystemEditorDialog dlg(e, nullptr);
    child<QLineEdit>(dlg, "rx_newSpeciesName")->setText(
        QStringLiteral("ZED"));
    child<QPushButton>(dlg, "rx_addSpeciesBtn")->click();

    auto *tabs = child<QTabWidget>(dlg, "rx_tabs");
    const int fileIdx = tabs->count() - 2;
    tabs->setCurrentIndex(fileIdx);
    auto *edit = child<QPlainTextEdit>(dlg, "rx_fileEdit");
    QString text = edit->toPlainText();
    text += QStringLiteral(
        "\n[REACTION_COEFFICIENTS]\nPARAMETER kx 0.5\n");
    edit->setPlainText(text);
    tabs->setCurrentIndex(0);                      // leave: applies
    QCOMPARE(tabs->currentIndex(), 0);
    QCOMPARE(swmm_reaction_coeff_count(e), 1);
    char name[64];
    int is_param = 0;
    double v = 0;
    QCOMPARE(swmm_reaction_coeff_get(e, 0, name, 64, &is_param, &v),
             SWMM_OK);
    QCOMPARE(QString::fromUtf8(name), QStringLiteral("kx"));
    QCOMPARE(v, 0.5);
    swmm_engine_destroy(e);
}

void TestReactionSystemEditor::badTextGatesTheTabSwitch()
{
    SWMM_Engine e = makeEngine();
    ReactionSystemEditorDialog dlg(e, nullptr);
    child<QLineEdit>(dlg, "rx_newSpeciesName")->setText(
        QStringLiteral("ZED"));
    child<QPushButton>(dlg, "rx_addSpeciesBtn")->click();

    auto *tabs = child<QTabWidget>(dlg, "rx_tabs");
    const int fileIdx = tabs->count() - 2;
    tabs->setCurrentIndex(fileIdx);
    auto *edit = child<QPlainTextEdit>(dlg, "rx_fileEdit");
    edit->setPlainText(edit->toPlainText() +
                       QStringLiteral("\n[REACTION_PIPES]\n"
                                      "RATE ZED MIN(ZED)\n"));
    tabs->setCurrentIndex(0);
    QVERIFY2(tabs->currentIndex() == fileIdx,
             "bad text must gate the tab switch");
    QVERIFY(child<QLabel>(dlg, "rx_fileStatus")->text().contains(
        QStringLiteral("MIN")));

    // Discard restores the canonical text and unblocks.
    child<QPushButton>(dlg, "rx_discardTextBtn")->click();
    tabs->setCurrentIndex(0);
    QCOMPARE(tabs->currentIndex(), 0);
    swmm_engine_destroy(e);
}

void TestReactionSystemEditor::initOverridesCommit()
{
    SWMM_Engine e = makeEngine();
    ReactionSystemEditorDialog dlg(e, nullptr);
    child<QLineEdit>(dlg, "rx_newSpeciesName")->setText(
        QStringLiteral("HOCL"));
    child<QPushButton>(dlg, "rx_addSpeciesBtn")->click();

    child<QPushButton>(dlg, "rx_addInitBtn")->click();
    auto *tbl = child<QTableWidget>(dlg, "rx_initOverrideTable");
    QCOMPARE(tbl->rowCount(), 1);
    auto *spin = qobject_cast<QDoubleSpinBox *>(tbl->cellWidget(0, 3));
    QVERIFY(spin);
    spin->setValue(1.25);                          // commits the row
    QCOMPARE(swmm_reaction_init_elem_count(e), 1);
    int il = -1, ei = -1, si = -1;
    double v = 0;
    QCOMPARE(swmm_reaction_init_elem_get(e, 0, &il, &ei, &si, &v), SWMM_OK);
    QCOMPARE(v, 1.25);

    tbl->setCurrentCell(0, 0);
    child<QPushButton>(dlg, "rx_removeInitBtn")->click();
    QCOMPARE(swmm_reaction_init_elem_count(e), 0);
    swmm_engine_destroy(e);
}

void TestReactionSystemEditor::sourcesTabIsDisabled()
{
    SWMM_Engine e = makeEngine();
    ReactionSystemEditorDialog dlg(e, nullptr);
    auto *tabs = child<QTabWidget>(dlg, "rx_tabs");
    QVERIFY(!tabs->isTabEnabled(tabs->count() - 1));
    swmm_engine_destroy(e);
}

QTEST_MAIN(TestReactionSystemEditor)
#include "test_reactionsystemeditor.moc"
