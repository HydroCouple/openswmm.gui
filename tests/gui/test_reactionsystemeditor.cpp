/**
 * @file      test_reactionsystemeditor.cpp
 * @brief     G-B2/G-C1 — the Reaction System editor, driven through its widgets.
 *
 * @details Dependency-light (Qt Widgets + the swmm_reaction_* /
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
 *          4. Bad text gates the tab switch; Discard restores
 *             the canonical text and unblocks.
 *          5. Per-element initial rows commit; species removal refusal
 *             (still referenced) reports rather than corrupting.
 *          6. Sources opens whole-file editing and preserves the extended sections.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "ui/dialogs/reactionsystemeditordialog.h"
#include "ui/widgets/reactionexpressionedit.h"

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

namespace
{

/**
 *
 * @brief Allocate a BUILDING engine and attempt to add two junctions, a conduit and TSS
 * pollutant.
 * @return Caller-owned native engine, released with swmm_engine_destroy(). Debug-asserts
 * allocation; individual add statuses are unchecked, and release builds have no explicit
 * null guard. No link endpoints, initialization or simulation are configured.
 *
 */
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

/**
 *
 * @brief Find a named descendant reaction-editor control using Qt's recursive typed lookup.
 * @tparam T QObject-derived control type.
 * @param dlg Borrowed live editor containing the controls.
 * @param name Borrowed Latin-1 object name, consumed during lookup.
 * @return Borrowed first child or null; no assertion here. Test callers assume required
 * controls exist. Lifetime follows the dialog; no ownership transfer or click.
 *
 */
template <typename T> T *child(ReactionSystemEditorDialog &dlg, const char *name)
{
    return dlg.findChild<T *>(QLatin1String(name));
}

} // namespace

class TestReactionSystemEditor : public QObject
{
    Q_OBJECT
private slots:
    void duplicateIdentityDoesNotOverwriteSavedRows();
    void invalidationStopsExpressionEditorsAndLateWrites();
    void constructsWithNullEngine();
    void structuredCrudReachesEngine();
    void fileTabShowsStructuredEdits();
    void fileTabEditsReachEngine();
    void badTextGatesTheTabSwitch();
    void initOverridesCommit();
    void sourcesTabOpensConfiguration();
    void sourceTablesApplyAtomicallyAndRetainUnownedReactorSections();
    void extendedSectionsSurviveFileAndStructuredEdits();
    void survivingOverrideRemainsEditable();
    void removingUncommittedRowPreservesOtherOverride();
    void changingOverrideIdentityReplacesOriginal();
};

/**
 *
 * @brief Require null-engine construction to leave wroteAnyChanges false.
 * Does not invoke editing controls, show the dialog or test late engine attachment.
 *
 */
void TestReactionSystemEditor::constructsWithNullEngine()
{
    ReactionSystemEditorDialog dlg(nullptr, nullptr);
    QCOMPARE(dlg.wroteAnyChanges(), false);
}

/**
 *
 * @brief Require widget-driven species/coefficient/term creation and expression edits to reach
 * native state.
 * Checks duplicate species refusal/status, pipe RATE expression text/form, referenced
 * species removal refusal and wroteAnyChanges. No reaction evaluation or hydraulic run;
 * raw engine is destroyed on the success path while the dialog still exists.
 *
 */
void TestReactionSystemEditor::structuredCrudReachesEngine()
{
    SWMM_Engine e = makeEngine();
    ReactionSystemEditorDialog dlg(e, nullptr);

    // Species add.
    child<QLineEdit>(dlg, "rx_newSpeciesName")->setText(QStringLiteral("HOCL"));
    child<QPushButton>(dlg, "rx_addSpeciesBtn")->click();
    QCOMPARE(swmm_reaction_species_count(e), 1);
    QCOMPARE(child<QTableWidget>(dlg, "rx_speciesTable")->rowCount(), 1);

    // Duplicate refused, with a status message.
    child<QLineEdit>(dlg, "rx_newSpeciesName")->setText(QStringLiteral("HOCL"));
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
    QCOMPARE(exprTable->rowCount(), 2); // 1 species x 2 scopes
    exprTable->item(0, 3)->setText(QStringLiteral("-0.1 * HOCL"));
    int form = -1;
    char expr[256];
    QCOMPARE(swmm_reaction_expr_get(e, SWMM_RXN_SCOPE_PIPE, 0, &form, expr, 256), SWMM_OK);
    QCOMPARE(form, SWMM_RXN_FORM_RATE);
    QCOMPARE(QString::fromUtf8(expr), QStringLiteral("-0.1 * HOCL"));

    // Species removal refused while referenced.
    auto *speciesTable = child<QTableWidget>(dlg, "rx_speciesTable");
    speciesTable->setCurrentCell(0, 0);
    child<QPushButton>(dlg, "rx_removeSpeciesBtn")->click();
    QCOMPARE(swmm_reaction_species_count(e), 1); // survived
    QVERIFY(child<QLabel>(dlg, "rx_status")->text().contains(QStringLiteral("references")));

    QVERIFY(dlg.wroteAnyChanges());
    swmm_engine_destroy(e);
}

/**
 *
 * @brief Require entering File after adding ZED to display the name and REACTION_SPECIES section.
 * Checks serialized text contains both, without comparing full text, writing a file or
 * leaving File with a new text draft.
 *
 */
void TestReactionSystemEditor::fileTabShowsStructuredEdits()
{
    SWMM_Engine e = makeEngine();
    ReactionSystemEditorDialog dlg(e, nullptr);
    child<QLineEdit>(dlg, "rx_newSpeciesName")->setText(QStringLiteral("ZED"));
    child<QPushButton>(dlg, "rx_addSpeciesBtn")->click();

    auto *tabs = child<QTabWidget>(dlg, "rx_tabs");
    const int fileIdx = tabs->count() - 2; // File is before Sources
    tabs->setCurrentIndex(fileIdx);
    const QString text = child<QPlainTextEdit>(dlg, "rx_fileEdit")->toPlainText();
    QVERIFY2(text.contains(QStringLiteral("ZED")),
             "structured edit must be visible on the File tab");
    QVERIFY(text.contains(QStringLiteral("[REACTION_SPECIES]")));
    swmm_engine_destroy(e);
}

/**
 *
 * @brief Require leaving File to apply an appended kx coefficient with value0.5 and permit the
 * tab switch.
 * Checks native coefficient count/name/value; does not check is-parameter flag, file I/O
 * or numerical reaction evaluation.
 *
 */
void TestReactionSystemEditor::fileTabEditsReachEngine()
{
    SWMM_Engine e = makeEngine();
    ReactionSystemEditorDialog dlg(e, nullptr);
    child<QLineEdit>(dlg, "rx_newSpeciesName")->setText(QStringLiteral("ZED"));
    child<QPushButton>(dlg, "rx_addSpeciesBtn")->click();

    auto *tabs = child<QTabWidget>(dlg, "rx_tabs");
    const int fileIdx = tabs->count() - 2;
    tabs->setCurrentIndex(fileIdx);
    auto *edit = child<QPlainTextEdit>(dlg, "rx_fileEdit");
    QString text = edit->toPlainText();
    text += QStringLiteral("\n[REACTION_COEFFICIENTS]\nPARAMETER kx 0.5\n");
    edit->setPlainText(text);
    tabs->setCurrentIndex(0); // leave: applies
    QCOMPARE(tabs->currentIndex(), 0);
    QCOMPARE(swmm_reaction_coeff_count(e), 1);
    char name[64];
    int is_param = 0;
    double v = 0;
    QCOMPARE(swmm_reaction_coeff_get(e, 0, name, 64, &is_param, &v), SWMM_OK);
    QCOMPARE(QString::fromUtf8(name), QStringLiteral("kx"));
    QCOMPARE(v, 0.5);
    swmm_engine_destroy(e);
}

/**
 *
 * @brief Require an invalid one-argument MIN expression to keep File selected until Discard.
 * Checks MIN diagnostic, then successful switch after discarding text; does not click
 * Close or separately compare the restored canonical document.
 *
 */
void TestReactionSystemEditor::badTextGatesTheTabSwitch()
{
    SWMM_Engine e = makeEngine();
    ReactionSystemEditorDialog dlg(e, nullptr);
    child<QLineEdit>(dlg, "rx_newSpeciesName")->setText(QStringLiteral("ZED"));
    child<QPushButton>(dlg, "rx_addSpeciesBtn")->click();

    auto *tabs = child<QTabWidget>(dlg, "rx_tabs");
    const int fileIdx = tabs->count() - 2;
    tabs->setCurrentIndex(fileIdx);
    auto *edit = child<QPlainTextEdit>(dlg, "rx_fileEdit");
    edit->setPlainText(edit->toPlainText() + QStringLiteral("\n[REACTION_PIPES]\n"
                                                            "RATE ZED MIN(ZED)\n"));
    tabs->setCurrentIndex(0);
    QVERIFY2(tabs->currentIndex() == fileIdx, "bad text must gate the tab switch");
    QVERIFY(child<QLabel>(dlg, "rx_fileStatus")->text().contains(QStringLiteral("MIN")));

    // Discard restores the canonical text and unblocks.
    child<QPushButton>(dlg, "rx_discardTextBtn")->click();
    tabs->setCurrentIndex(0);
    QCOMPARE(tabs->currentIndex(), 0);
    swmm_engine_destroy(e);
}

/**
 *
 * @brief Require editing a newly added initial override to commit value1.25 and removal to delete
 * its native row.
 * Checks one table/native row then zero native rows; scope/element/species outputs are
 * read but not compared. No file round trip or simulation.
 *
 */
void TestReactionSystemEditor::initOverridesCommit()
{
    SWMM_Engine e = makeEngine();
    ReactionSystemEditorDialog dlg(e, nullptr);
    child<QLineEdit>(dlg, "rx_newSpeciesName")->setText(QStringLiteral("HOCL"));
    child<QPushButton>(dlg, "rx_addSpeciesBtn")->click();

    child<QPushButton>(dlg, "rx_addInitBtn")->click();
    auto *tbl = child<QTableWidget>(dlg, "rx_initOverrideTable");
    QCOMPARE(tbl->rowCount(), 1);
    auto *spin = qobject_cast<QDoubleSpinBox *>(tbl->cellWidget(0, 3));
    QVERIFY(spin);
    spin->setValue(1.25); // commits the row
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

/**
 *
 * @brief Require the enabled Sources tab's edit-configuration button to navigate to File.
 * Checks tab indices only, without opening a modal dialog or editing source values.
 *
 */
void TestReactionSystemEditor::sourcesTabOpensConfiguration()
{
    SWMM_Engine e = makeEngine();
    ReactionSystemEditorDialog dlg(e, nullptr);
    auto *tabs = child<QTabWidget>(dlg, "rx_tabs");
    QVERIFY(tabs->isTabEnabled(tabs->count() - 1));
    tabs->setCurrentIndex(tabs->count() - 1);
    child<QPushButton>(dlg, "rx_editSourcesBtn")->click();
    QCOMPARE(tabs->currentIndex(), tabs->count() - 2);
    swmm_engine_destroy(e);
}

/**
 *
 * @brief Require source/pattern drafts to commit together while retaining subcatchment reaction
 * text.
 * Accepts SETPOINT/pulse edits, captures canonical serialization, then makes source
 * value negative and pattern values different. Leaving Sources must be blocked and
 * native serialization remain exactly accepted text. Correcting source value permits
 * leaving; no separate final pattern-value assertion or reaction run.
 *
 */
void TestReactionSystemEditor::sourceTablesApplyAtomicallyAndRetainUnownedReactorSections()
{
    auto e = makeEngine();
    char error[1024] = {};
    QCOMPARE(swmm_reactions_apply_text(
                 e, "[REACTION_SPECIES]\nBULK A MG\n[REACTION_SUBCATCHMENTS]\nRATE A -0.01*A\n",
                 error, sizeof(error)),
             SWMM_OK);
    ReactionSystemEditorDialog dlg(e, nullptr);
    auto *tabs = child<QTabWidget>(dlg, "rx_tabs");
    tabs->setCurrentIndex(tabs->count() - 1);
    auto *sources = child<QTableWidget>(dlg, "rx_reaction_sourcesTable");
    auto *patterns = child<QTableWidget>(dlg, "rx_reaction_patternsTable");
    QVERIFY(sources);
    QVERIFY(patterns);
    sources->insertRow(0);
    patterns->insertRow(0);
    const QStringList values = {"SETPOINT", "J0", "A", "2", "pulse"};
    for (int c = 0; c < values.size(); ++c)
        sources->setItem(0, c, new QTableWidgetItem(values[c]));
    patterns->setItem(0, 0, new QTableWidgetItem("pulse"));
    patterns->setItem(0, 1, new QTableWidgetItem("1 0 2"));
    tabs->setCurrentIndex(tabs->count() - 2);
    QCOMPARE(tabs->currentIndex(), tabs->count() - 2);
    auto *file = child<QPlainTextEdit>(dlg, "rx_fileEdit");
    QVERIFY(file->toPlainText().contains("SETPOINT"));
    QVERIFY(file->toPlainText().contains("-0.01*A"));
    const QString accepted = file->toPlainText();
    tabs->setCurrentIndex(tabs->count() - 1);
    sources->item(0, 3)->setText("-5");
    patterns->item(0, 1)->setText("3 4");
    tabs->setCurrentIndex(0);
    QCOMPARE(tabs->currentIndex(), tabs->count() - 1);
    QVERIFY(!child<QLabel>(dlg, "rx_status")->text().isEmpty());
    // Both tables roll back together; the invalid draft remains editable.
    int count = 0;
    QCOMPARE(swmm_reactions_serialize(e, nullptr, 0, &count), SWMM_OK);
    QVERIFY(count > 0);
    QByteArray text(count, '\0');
    swmm_reactions_serialize(e, text.data(), count, &count);
    QCOMPARE(QString::fromUtf8(text.constData()), accepted);
    sources->item(0, 3)->setText("4");
    tabs->setCurrentIndex(0);
    QCOMPARE(tabs->currentIndex(), 0);
    swmm_engine_destroy(e);
}

/**
 *
 * @brief Require File and subsequent coefficient edits to retain extended
 * source/pattern/parameter/report/subcatchment sections.
 * Changes serialized source amount28.317 to56.634, applies text and sets coefficient3.
 * Checks section names, source token and native coefficient value; does not prove full
 * byte identity of each section or serialize to disk.
 *
 */
void TestReactionSystemEditor::extendedSectionsSurviveFileAndStructuredEdits()
{
    SWMM_Engine e = makeEngine();
    const char *text = "[REACTION_SPECIES]\nBULK A MG\n"
                       "[REACTION_COEFFICIENTS]\nPARAMETER k 1\n"
                       "[REACTION_PARAMETERS]\nNODE J0 k 2\n"
                       "[REACTION_PATTERNS]\npulse 1 0\n"
                       "[REACTION_SOURCES]\nMASS J0 A 28.317 pulse\n"
                       "[REACTION_REPORT]\nSPECIES ALL NO\n"
                       "[REACTION_SUBCATCHMENTS]\nRATE A -k * A\n";
    char error[512] = {};
    QCOMPARE(swmm_reactions_apply_text(e, text, error, sizeof(error)), SWMM_OK);
    ReactionSystemEditorDialog dlg(e, nullptr);
    auto *tabs = child<QTabWidget>(dlg, "rx_tabs");
    auto *file = child<QPlainTextEdit>(dlg, "rx_fileEdit");
    tabs->setCurrentIndex(tabs->count() - 2);
    QVERIFY(file->toPlainText().contains(QStringLiteral("[REACTION_SOURCES]")));
    QString edited = file->toPlainText();
    edited.replace(QStringLiteral("28.317"), QStringLiteral("56.634"));
    file->setPlainText(edited);
    tabs->setCurrentIndex(2); // applies text, then edit a structured coefficient
    QCOMPARE(tabs->currentIndex(), 2);
    child<QDoubleSpinBox>(dlg, "rx_coeffValue_0")->setValue(3);
    tabs->setCurrentIndex(tabs->count() - 2);
    const QString after = file->toPlainText();
    for (const auto &section : {"REACTION_SOURCES", "REACTION_PATTERNS", "REACTION_PARAMETERS",
                                "REACTION_REPORT", "REACTION_SUBCATCHMENTS"})
        QVERIFY(after.contains(QLatin1String(section)));
    QVERIFY(after.contains(QStringLiteral("56.634")));
    int param = 0;
    double value = 0;
    char name[64];
    QCOMPARE(swmm_reaction_coeff_get(e, 0, name, 64, &param, &value), SWMM_OK);
    QCOMPARE(value, 3.0);
    swmm_engine_destroy(e);
}

/**
 *
 * @brief Require deleting the first saved override to leave the second row mapped to its original
 * native element.
 * Edits the surviving row to7.5 and checks one native row for element1; no engine reorder,
 * file persistence or undo operation.
 *
 */
void TestReactionSystemEditor::survivingOverrideRemainsEditable()
{
    SWMM_Engine e = makeEngine();
    QCOMPARE(swmm_reaction_species_add(e, "HOCL", 0, "MG", 0, 0), SWMM_OK);
    QCOMPARE(swmm_reaction_init_elem_set(e, 0, 0, 0, 1.25), SWMM_OK);
    QCOMPARE(swmm_reaction_init_elem_set(e, 0, 1, 0, 2.25), SWMM_OK);
    ReactionSystemEditorDialog dlg(e, nullptr);
    auto *table = child<QTableWidget>(dlg, "rx_initOverrideTable");
    QCOMPARE(table->rowCount(), 2);
    table->setCurrentCell(0, 0);
    child<QPushButton>(dlg, "rx_removeInitBtn")->click();
    QCOMPARE(table->rowCount(), 1);
    qobject_cast<QDoubleSpinBox *>(table->cellWidget(0, 3))->setValue(7.5);
    QCOMPARE(swmm_reaction_init_elem_count(e), 1);
    int scope = -1, element = -1, species = -1;
    double value = 0;
    QCOMPARE(swmm_reaction_init_elem_get(e, 0, &scope, &element, &species, &value), SWMM_OK);
    QCOMPARE(element, 1);
    QCOMPARE(value, 7.5);
    swmm_engine_destroy(e);
}

/**
 *
 * @brief Require deleting an uncommitted draft row to preserve the other committed initial
 * override.
 * Creates two drafts, commits the second to element1/value7.5, deletes the first and
 * checks one table/native row with that identity/value; no undo or serialization.
 *
 */
void TestReactionSystemEditor::removingUncommittedRowPreservesOtherOverride()
{
    SWMM_Engine e = makeEngine();
    QCOMPARE(swmm_reaction_species_add(e, "HOCL", 0, "MG", 0, 0), SWMM_OK);
    ReactionSystemEditorDialog dlg(e, nullptr);
    child<QPushButton>(dlg, "rx_addInitBtn")->click();
    child<QPushButton>(dlg, "rx_addInitBtn")->click();
    auto *table = child<QTableWidget>(dlg, "rx_initOverrideTable");
    qobject_cast<QComboBox *>(table->cellWidget(1, 1))->setCurrentIndex(1);
    qobject_cast<QDoubleSpinBox *>(table->cellWidget(1, 3))->setValue(7.5);
    QCOMPARE(swmm_reaction_init_elem_count(e), 1);
    table->setCurrentCell(0, 0);
    child<QPushButton>(dlg, "rx_removeInitBtn")->click();
    QCOMPARE(table->rowCount(), 1);
    QCOMPARE(swmm_reaction_init_elem_count(e), 1);
    int scope = -1, element = -1, species = -1;
    double value = 0;
    QCOMPARE(swmm_reaction_init_elem_get(e, 0, &scope, &element, &species, &value), SWMM_OK);
    QCOMPARE(element, 1);
    QCOMPARE(value, 7.5);
    swmm_engine_destroy(e);
}

/**
 *
 * @brief Require changing a saved row's element selection to replace its original identity
 * without adding a second row.
 * Checks one native override at element1/value7.5; no duplicate-identity conflict here.
 *
 */
void TestReactionSystemEditor::changingOverrideIdentityReplacesOriginal()
{
    SWMM_Engine e = makeEngine();
    QCOMPARE(swmm_reaction_species_add(e, "HOCL", 0, "MG", 0, 0), SWMM_OK);
    QCOMPARE(swmm_reaction_init_elem_set(e, 0, 0, 0, 1.25), SWMM_OK);
    ReactionSystemEditorDialog dlg(e, nullptr);
    auto *table = child<QTableWidget>(dlg, "rx_initOverrideTable");
    qobject_cast<QComboBox *>(table->cellWidget(0, 1))->setCurrentIndex(1);
    qobject_cast<QDoubleSpinBox *>(table->cellWidget(0, 3))->setValue(7.5);
    QCOMPARE(swmm_reaction_init_elem_count(e), 1);
    int scope = -1, element = -1, species = -1;
    double value = 0;
    QCOMPARE(swmm_reaction_init_elem_get(e, 0, &scope, &element, &species, &value), SWMM_OK);
    QCOMPARE(element, 1);
    QCOMPARE(value, 7.5);
    swmm_engine_destroy(e);
}

/**
 *
 * @brief Require engine invalidation before destruction to disable dialog/expression editors and
 * stop delayed mutations.
 * Queues invalid expression/text, invokes invalidation, destroys engine, then exercises
 * validation/vocabulary/late-add calls and waits350ms. Requires unchanged-write flag and
 * Rejected result; does not rely on access to destroyed native storage.
 *
 */
void TestReactionSystemEditor::invalidationStopsExpressionEditorsAndLateWrites()
{
    SWMM_Engine e = makeEngine();
    ReactionSystemEditorDialog dlg(e, nullptr);
    auto *edit = new openswmmvis::ui::ReactionExpressionEdit(e, SWMM_RXN_SCOPE_PIPE, &dlg);
    edit->setPlainText(QStringLiteral("HGW + ("));
    child<QPlainTextEdit>(dlg, "rx_fileEdit")
        ->setPlainText(QStringLiteral("[REACTION_SPECIES]\nHOCL BULK MG\n"));
    QVERIFY(QMetaObject::invokeMethod(&dlg, "invalidateEngine", Qt::DirectConnection));
    swmm_engine_destroy(e);
    QVERIFY(!dlg.isEnabled());
    QVERIFY(!edit->isEnabled());
    edit->validateNow();
    edit->refreshVocabulary();
    QVERIFY(QMetaObject::invokeMethod(&dlg, "onAddInitOverride", Qt::DirectConnection));
    QTest::qWait(350);
    QVERIFY(!dlg.wroteAnyChanges());
    QCOMPARE(dlg.result(), int(QDialog::Rejected));
}

/**
 *
 * @brief Require a duplicate-identity draft to preserve both full-precision native values until
 * identity is corrected.
 * Attempts to retarget the second row to element0 and value7.5; checks original values,
 * unchanged-write flag and diagnostic. Restoring element1 commits7.5 and marks changes;
 * no file persistence or undo history is tested.
 *
 */
void TestReactionSystemEditor::duplicateIdentityDoesNotOverwriteSavedRows()
{
    SWMM_Engine e = makeEngine();
    QCOMPARE(swmm_reaction_species_add(e, "HOCL", 0, "MG", 0, 0), SWMM_OK);
    QCOMPARE(swmm_reaction_init_elem_set(e, 0, 0, 0, 1.234567891), SWMM_OK);
    QCOMPARE(swmm_reaction_init_elem_set(e, 0, 1, 0, 2.345678912), SWMM_OK);
    ReactionSystemEditorDialog dlg(e, nullptr);
    auto *table = child<QTableWidget>(dlg, "rx_initOverrideTable");
    qobject_cast<QComboBox *>(table->cellWidget(1, 1))->setCurrentIndex(0);
    qobject_cast<QDoubleSpinBox *>(table->cellWidget(1, 3))->setValue(7.5);
    QCOMPARE(swmm_reaction_init_elem_count(e), 2);
    int scope = -1, element = -1, species = -1;
    double value = 0;
    QCOMPARE(swmm_reaction_init_elem_get(e, 0, &scope, &element, &species, &value), SWMM_OK);
    QCOMPARE(value, 1.234567891);
    QCOMPARE(swmm_reaction_init_elem_get(e, 1, &scope, &element, &species, &value), SWMM_OK);
    QCOMPARE(value, 2.345678912);
    QVERIFY(!dlg.wroteAnyChanges());
    QVERIFY(!child<QLabel>(dlg, "rx_status")->text().isEmpty());
    qobject_cast<QComboBox *>(table->cellWidget(1, 1))->setCurrentIndex(1);
    QCOMPARE(swmm_reaction_init_elem_get(e, 1, &scope, &element, &species, &value), SWMM_OK);
    QCOMPARE(value, 7.5);
    QVERIFY(dlg.wroteAnyChanges());
    swmm_engine_destroy(e);
}

QTEST_MAIN(TestReactionSystemEditor)
#include "test_reactionsystemeditor.moc"
