/**
 * @file      test_plotvariablepickerdialog.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Pins the PlotVariablePickerDialog (the Plot Time Series toolbar
 *         dialog) and the shared canonical attribute lists it is built on.
 *
 * Engine-free: the dialog takes pre-mapped plot::ObjectRefs plus an
 * optional IRunLayer availability probe, so a stub layer with a blacklist
 * exercises the supportsAttribute gating without touching a .out file.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/dialogs/plotvariablepickerdialog.h"
#include "plot/irunlayer.h"
#include "plot/plotattribute.h"

#include "dialog_a11y_checks.h"

#include <QDialogButtonBox>
#include <QLineEdit>
#include <QObject>
#include <QPushButton>
#include <QTest>
#include <QTreeWidget>

using namespace openswmmvis::plot;
using openswmmvis::ui::PlotVariablePickerDialog;

namespace
{

// Stub layer that rejects a fixed blacklist — models a .out that lacks
// e.g. PET and link volume.
class StubRunLayer : public IRunLayer
{
public:
    /**
     *
     * @brief Return this availability-only stub's fixed display name.
     * @return Owned Stub string; no output file or scenario registry lookup.
     *
     */
    QString scenarioName() const override { return QStringLiteral("Stub"); }
    /**
     *
     * @brief Return SI metadata for the stub without converting any data.
     * @return UnitSystem::SI, independent of the dialog's separately supplied units.
     *
     */
    UnitSystem unitSystem() const override { return UnitSystem::SI; }
    /**
     *
     * @brief Return zero as unused start-date metadata for a stub with no periods.
     * @return Julian-value sentinel zero; not an actual sampled run start date.
     *
     */
    double startDateJulian() const override { return 0.0; }
    /**
     *
     * @brief Report that this availability-only stub has no sampled periods.
     * @return Zero, without loading or inspecting an output file.
     *
     */
    int periodCount() const override { return 0; }
    /**
     *
     * @brief Return unused report-step metadata for the period-free stub.
     * @return Zero seconds; callers must not use it as a positive interpolation interval.
     *
     */
    int reportStepSeconds() const override { return 0; }
    /**
     *
     * @brief Mark every series request as unavailable without reading data.
     * @param ref Borrowed object reference, deliberately ignored by this availability stub.
     * @param attribute Requested variable, deliberately ignored; availability probes use
     * supportsAttribute() separately.
     * @param out Borrowed mutable result; sets ok false and errorMessage to stub, leaving
     * all other existing fields unchanged. No references are retained after return.
     *
     */
    void getSeriesAt(const ObjectRef &ref, PlotAttribute attribute, SeriesData &out) const override
    {
        out.ok = false;
        out.errorMessage = QStringLiteral("stub");
    }
    /**
     *
     * @brief Apply the fixed attribute blacklist used to test disabled picker leaves.
     * @param a Canonical attribute value, without object-kind validation.
     * @return False only for SystemPET and LinkVolume; true even for otherwise unknown
     * values. Does not imply getSeriesAt() can produce actual samples.
     *
     */
    bool supportsAttribute(PlotAttribute a) const override
    {
        return a != PlotAttribute::SystemPET && a != PlotAttribute::LinkVolume;
    }
};

// Y2b-2 (amendment D-Y4): a run that carries two pollutants + age. The
// dialog must grow species leaves from the run's descriptor list — and
// ONLY per-feature groups, never the System group.
class SpeciesStubRunLayer : public StubRunLayer
{
public:
    /**
     *
     * @brief Build canonical descriptors extended with two pollutants and reserved water age.
     * @param kind Object family forwarded to the shared descriptor builder.
     * @return Owned descriptors for TSS, Lead and __WATER_AGE__ where the family supports
     * species; system families retain their canonical non-species set. No run file is read.
     *
     */
    QVector<ResultDescriptor> resultDescriptorsForKind(ObjectRef::Kind kind) const override
    {
        return openswmmvis::plot::resultDescriptorsForKind(
            kind, QStringList{QStringLiteral("TSS"), QStringLiteral("Lead"),
                              QStringLiteral("__WATER_AGE__")});
    }
};

/**
 *
 * @brief Find the picker variable tree by exact object name.
 * @param dlg Borrowed live picker dialog.
 * @return Borrowed variableTree child. Debug-asserts presence; release builds can return
 * null if the dialog structure changes. Pointer expires with child/dialog lifetime.
 *
 */
QTreeWidget *treeOf(PlotVariablePickerDialog &dlg)
{
    auto *tree = dlg.findChild<QTreeWidget *>(QStringLiteral("variableTree"));
    Q_ASSERT(tree);
    return tree;
}

/**
 *
 * @brief Find the Ok button in the first descendant dialog button box.
 * @param dlg Borrowed live picker dialog.
 * @return Borrowed button or null if Ok is absent. Debug-asserts the box, then dereferences
 * it; a missing box is not handled safely in release builds. No button click occurs.
 *
 */
QPushButton *okButtonOf(PlotVariablePickerDialog &dlg)
{
    auto *bb = dlg.findChild<QDialogButtonBox *>();
    Q_ASSERT(bb);
    return bb->button(QDialogButtonBox::Ok);
}

/**
 *
 * @brief Find the first push button with exactly matching display text.
 * @param dlg Borrowed live picker dialog.
 * @param text Borrowed exact text including mnemonic ampersands; no trimming/translation.
 * @return Borrowed child or null; caller retains dialog lifetime and checks before use.
 *
 */
QPushButton *pushButtonNamed(PlotVariablePickerDialog &dlg, const QString &text)
{
    const auto buttons = dlg.findChildren<QPushButton *>();
    for (QPushButton *b : buttons)
        if (b->text() == text)
            return b;
    return nullptr;
}

} // namespace

class TestPlotVariablePickerDialog : public QObject
{
    Q_OBJECT

private slots:
    // Shared canonical lists — the regression fence for the 4-site refactor.
    void attributeListsPinned();
    void attributesForKindDispatch();

    // Dialog behaviour.
    void systemOnlyWhenNoFeatures();
    void groupsPerSelectedFeature();
    void groupCheckCascadesToLeaves();
    void unsupportedAttributesDisabledWithTooltip();
    void selectAllSkipsDisabled();
    void invertFlipsCheckedState();
    void filterHidesNonMatchingLeaves();
    void nullAvailabilityEnablesEverything();
    void speciesLeavesComeFromTheRun();
    void dialogA11y();
};

/**
 *
 * @brief Pin canonical attribute counts, duplicate freedom, system classification and menu-order
 * anchors.
 * Checks node6/link5/subcatch17/system15 and selected snow units even under US metadata;
 * does not compare every full list element or read any numerical output samples.
 *
 */
void TestPlotVariablePickerDialog::attributeListsPinned()
{
    QCOMPARE(nodePlotAttributes().size(), 6);
    QCOMPARE(linkPlotAttributes().size(), 5);
    QCOMPARE(subcatchPlotAttributes().size(), 17);
    QCOMPARE(unitsFor(PlotAttribute::SubcatchSnowSWE, UnitSystem::US), QStringLiteral("m"));
    QCOMPARE(unitsFor(PlotAttribute::SubcatchSnowSurfaceTemp, UnitSystem::US),
             QStringLiteral("°C"));
    QCOMPARE(systemPlotAttributes().size(), 15);

    // No duplicates within any list, and every system entry really is one.
    for (const auto *list : {&nodePlotAttributes(), &linkPlotAttributes(),
                             &subcatchPlotAttributes(), &systemPlotAttributes()}) {
        for (int i = 0; i < list->size(); ++i)
            for (int j = i + 1; j < list->size(); ++j)
                QVERIFY((*list)[i] != (*list)[j]);
    }
    for (PlotAttribute a : systemPlotAttributes())
        QVERIFY(isSystemAttribute(a));

    // Canonical order anchors (user-facing menu order).
    QCOMPARE(systemPlotAttributes().first(), PlotAttribute::SystemRainfall);
    QCOMPARE(systemPlotAttributes().last(), PlotAttribute::SystemTemperature);
    QCOMPARE(nodePlotAttributes().first(), PlotAttribute::NodeDepth);
    QCOMPARE(linkPlotAttributes().first(), PlotAttribute::LinkFlow);
    QCOMPARE(subcatchPlotAttributes().first(), PlotAttribute::SubcatchRainfall);
}

/**
 *
 * @brief Require object-family dispatch to return the canonical lists by reference identity.
 * Checks node/link/subcatch/system and all mesh element families; Unknown/Observed must
 * be empty. No runtime availability or optional-backend loading occurs.
 *
 */
void TestPlotVariablePickerDialog::attributesForKindDispatch()
{
    QCOMPARE(&attributesForKind(ObjectRef::Kind::Node), &nodePlotAttributes());
    QCOMPARE(&attributesForKind(ObjectRef::Kind::Link), &linkPlotAttributes());
    QCOMPARE(&attributesForKind(ObjectRef::Kind::Subcatch), &subcatchPlotAttributes());
    QCOMPARE(&attributesForKind(ObjectRef::Kind::System), &systemPlotAttributes());
    QVERIFY(attributesForKind(ObjectRef::Kind::Unknown).isEmpty());
    QCOMPARE(&attributesForKind(ObjectRef::Kind::Mesh2DCell), &mesh2DCellPlotAttributes());
    QCOMPARE(&attributesForKind(ObjectRef::Kind::Mesh2DEdge), &mesh2DEdgePlotAttributes());
    QCOMPARE(&attributesForKind(ObjectRef::Kind::Mesh2DVertex), &mesh2DVertexPlotAttributes());
    QVERIFY(attributesForKind(ObjectRef::Kind::Observed).isEmpty());
}

/**
 *
 * @brief Require an empty feature selection to show only fifteen system leaves with no checked
 * entries and disabled Ok.
 * Uses null availability and SI labels; does not accept or display the dialog modally.
 *
 */
void TestPlotVariablePickerDialog::systemOnlyWhenNoFeatures()
{
    PlotVariablePickerDialog dlg({}, nullptr, UnitSystem::SI);
    QTreeWidget *tree = treeOf(dlg);
    QCOMPARE(tree->topLevelItemCount(), 1);
    QCOMPARE(tree->topLevelItem(0)->childCount(), 15);
    QVERIFY(dlg.checkedEntries().isEmpty());
    QVERIFY(!okButtonOf(dlg)->isEnabled());
}

/**
 *
 * @brief Require system plus node/link/subcatch groups with canonical child counts in supplied
 * order.
 * Checks J1 text and counts15/6/5/17 under US units; no check-state or support probe.
 *
 */
void TestPlotVariablePickerDialog::groupsPerSelectedFeature()
{
    const QVector<ObjectRef> features = {
        ObjectRef::forNode(QStringLiteral("J1")),
        ObjectRef::forLink(QStringLiteral("C1")),
        ObjectRef::forSubcatch(QStringLiteral("S1")),
    };
    PlotVariablePickerDialog dlg(features, nullptr, UnitSystem::US);
    QTreeWidget *tree = treeOf(dlg);
    QCOMPARE(tree->topLevelItemCount(), 4); // System + 3 features
    QCOMPARE(tree->topLevelItem(0)->childCount(), 15);
    QCOMPARE(tree->topLevelItem(1)->childCount(), 6);  // Node J1
    QCOMPARE(tree->topLevelItem(2)->childCount(), 5);  // Link C1
    QCOMPARE(tree->topLevelItem(3)->childCount(), 17); // Subcatchment S1
    QVERIFY(tree->topLevelItem(1)->text(0).contains(QStringLiteral("J1")));
}

/**
 *
 * @brief Require checking the node group to select every node attribute in canonical order and
 * enable Ok.
 * Checks entry kind/name/attribute, then adding the first system leaf must prepend one
 * system entry. Does not assert the system reference name separately or accept dialog.
 *
 */
void TestPlotVariablePickerDialog::groupCheckCascadesToLeaves()
{
    const QVector<ObjectRef> features = {
        ObjectRef::forNode(QStringLiteral("J1")),
    };
    PlotVariablePickerDialog dlg(features, nullptr, UnitSystem::SI);
    QTreeWidget *tree = treeOf(dlg);

    QTreeWidgetItem *nodeGroup = tree->topLevelItem(1);
    nodeGroup->setCheckState(0, Qt::Checked); // AutoTristate cascade

    const auto entries = dlg.checkedEntries();
    QCOMPARE(entries.size(), nodePlotAttributes().size());
    for (int i = 0; i < entries.size(); ++i) {
        QCOMPARE(entries[i].ref.kind, ObjectRef::Kind::Node);
        QCOMPARE(entries[i].ref.name, QStringLiteral("J1"));
        QCOMPARE(entries[i].attribute, nodePlotAttributes()[i]);
    }
    QVERIFY(okButtonOf(dlg)->isEnabled());

    // System leaves reconstruct as Kind::System with no name.
    tree->topLevelItem(0)->child(0)->setCheckState(0, Qt::Checked);
    const auto withSys = dlg.checkedEntries();
    QCOMPARE(withSys.size(), entries.size() + 1);
    QCOMPARE(withSys.first().ref.kind, ObjectRef::Kind::System);
    QCOMPARE(withSys.first().attribute, systemPlotAttributes().first());
}

/**
 *
 * @brief Require probe availability to disable exactly PET and link volume with nonempty
 * explanations.
 * Compares each leaf enabled flag against supportsAttribute(); no series request or
 * file read occurs and tooltip wording is not pinned.
 *
 */
void TestPlotVariablePickerDialog::unsupportedAttributesDisabledWithTooltip()
{
    StubRunLayer probe;
    const QVector<ObjectRef> features = {
        ObjectRef::forLink(QStringLiteral("C1")),
    };
    PlotVariablePickerDialog dlg(features, &probe, UnitSystem::SI);
    QTreeWidget *tree = treeOf(dlg);

    int disabled = 0;
    for (int g = 0; g < tree->topLevelItemCount(); ++g) {
        const QTreeWidgetItem *group = tree->topLevelItem(g);
        for (int i = 0; i < group->childCount(); ++i) {
            const QTreeWidgetItem *leaf = group->child(i);
            const auto attr = static_cast<PlotAttribute>(leaf->data(0, Qt::UserRole).toInt());
            const bool enabled = leaf->flags() & Qt::ItemIsEnabled;
            QCOMPARE(enabled, probe.supportsAttribute(attr));
            if (!enabled) {
                ++disabled;
                QVERIFY(!leaf->toolTip(0).isEmpty());
            }
        }
    }
    QCOMPARE(disabled, 2); // SystemPET + LinkVolume
}

/**
 *
 * @brief Require Select All to check eighteen supported leaves and Select None to clear
 * them/disable Ok.
 * Checks both blacklisted attributes are absent, using real helper buttons; does not
 * exercise filtered or partially hidden groups.
 *
 */
void TestPlotVariablePickerDialog::selectAllSkipsDisabled()
{
    StubRunLayer probe;
    const QVector<ObjectRef> features = {
        ObjectRef::forLink(QStringLiteral("C1")),
    };
    PlotVariablePickerDialog dlg(features, &probe, UnitSystem::SI);

    QPushButton *all = pushButtonNamed(dlg, QStringLiteral("Select &All"));
    QVERIFY(all);
    all->click();

    const auto entries = dlg.checkedEntries();
    QCOMPARE(entries.size(), 15 + 5 - 2); // minus SystemPET + LinkVolume
    for (const auto &e : entries) {
        QVERIFY(e.attribute != PlotAttribute::SystemPET);
        QVERIFY(e.attribute != PlotAttribute::LinkVolume);
    }

    QPushButton *none = pushButtonNamed(dlg, QStringLiteral("Select &None"));
    QVERIFY(none);
    none->click();
    QVERIFY(dlg.checkedEntries().isEmpty());
    QVERIFY(!okButtonOf(dlg)->isEnabled());
}

/**
 *
 * @brief Require Invert to uncheck the one selected system attribute and select the other
 * fourteen.
 * Uses fifteen available system leaves; no disabled-leaf or filtering scenario.
 *
 */
void TestPlotVariablePickerDialog::invertFlipsCheckedState()
{
    PlotVariablePickerDialog dlg({}, nullptr, UnitSystem::SI);
    QTreeWidget *tree = treeOf(dlg);
    tree->topLevelItem(0)->child(0)->setCheckState(0, Qt::Checked);

    QPushButton *invert = pushButtonNamed(dlg, QStringLiteral("&Invert"));
    QVERIFY(invert);
    invert->click();

    const auto entries = dlg.checkedEntries();
    QCOMPARE(entries.size(), 14); // everything except the one that was checked
    for (const auto &e : entries)
        QVERIFY(e.attribute != systemPlotAttributes().first());
}

/**
 *
 * @brief Require Rainfall search to leave one visible system leaf and clearing to restore
 * fifteen.
 * Checks hidden flags only, without clearing selection, asserting group visibility or
 * matching case/regex behavior beyond the supplied exact search text.
 *
 */
void TestPlotVariablePickerDialog::filterHidesNonMatchingLeaves()
{
    PlotVariablePickerDialog dlg({}, nullptr, UnitSystem::SI);
    auto *filter = dlg.findChild<QLineEdit *>();
    QVERIFY(filter);
    filter->setText(QStringLiteral("Rainfall"));

    QTreeWidget *tree = treeOf(dlg);
    const QTreeWidgetItem *sys = tree->topLevelItem(0);
    int visible = 0;
    for (int i = 0; i < sys->childCount(); ++i)
        if (!sys->child(i)->isHidden())
            ++visible;
    QCOMPARE(visible, 1); // only "Rainfall (mm/hr)"

    filter->clear();
    visible = 0;
    for (int i = 0; i < sys->childCount(); ++i)
        if (!sys->child(i)->isHidden())
            ++visible;
    QCOMPARE(visible, 15);
}

/**
 *
 * @brief Require every system/node leaf to stay enabled when no run availability probe is
 * supplied.
 * Uses US labels and node J1, without requesting samples or checking entry values.
 *
 */
void TestPlotVariablePickerDialog::nullAvailabilityEnablesEverything()
{
    const QVector<ObjectRef> features = {
        ObjectRef::forNode(QStringLiteral("J1")),
    };
    PlotVariablePickerDialog dlg(features, nullptr, UnitSystem::US);
    QTreeWidget *tree = treeOf(dlg);
    for (int g = 0; g < tree->topLevelItemCount(); ++g) {
        const QTreeWidgetItem *group = tree->topLevelItem(g);
        for (int i = 0; i < group->childCount(); ++i)
            QVERIFY(group->child(i)->flags() & Qt::ItemIsEnabled);
    }
}

/**
 *
 * @brief Require species descriptors to extend node leaves by three while leaving the system
 * count unchanged.
 * Checks the final water-age leaf is labeled in hours and checking it yields exactly
 * one J1 species-by-name entry with Unknown canonical attribute. No concentrations or
 * actual run output are sampled.
 *
 */
void TestPlotVariablePickerDialog::speciesLeavesComeFromTheRun()
{
    // The amendment's razor, dialog edition: a run with 2 pollutants +
    // age serves the hydraulic set + 3 for a node group; the System
    // group must NOT grow (no per-species system column exists).
    SpeciesStubRunLayer probe;
    const QVector<ObjectRef> features{ObjectRef::forNode(QStringLiteral("J1"))};
    PlotVariablePickerDialog dlg(features, &probe, UnitSystem::SI);
    QTreeWidget *tree = treeOf(dlg);
    QCOMPARE(tree->topLevelItemCount(), 2); // System + J1

    const QTreeWidgetItem *sys = tree->topLevelItem(0);
    const QTreeWidgetItem *node = tree->topLevelItem(1);
    QCOMPARE(sys->childCount(), systemPlotAttributes().size());
    QCOMPARE(node->childCount(), nodePlotAttributes().size() + 3);

    // The age leaf reads in HOURS through the Y2a authorities — a picker
    // that offered "__WATER_AGE__ (mg/L)" would mislabel the axis.
    const QTreeWidgetItem *ageLeaf = node->child(node->childCount() - 1);
    QVERIFY(ageLeaf->text(0).contains(QStringLiteral("Water age")));
    QVERIFY(ageLeaf->text(0).contains(QStringLiteral("(h)")));

    // Checking it yields a species entry carried BY NAME (D-G1).
    const_cast<QTreeWidgetItem *>(ageLeaf)->setCheckState(0, Qt::Checked);
    const auto entries = dlg.checkedEntries();
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries[0].species, QStringLiteral("__WATER_AGE__"));
    QCOMPARE(entries[0].attribute, PlotAttribute::Unknown);
    QVERIFY(entries[0].descriptor().isSpecies());
    QCOMPARE(entries[0].ref.name, QStringLiteral("J1"));
}

/**
 *
 * @brief Run the shared accessibility assertions on a node-enabled plot-variable picker.
 * Constructs a local dialog with null availability and SI labels; assertions follow
 * the shared helper contract without accepting a modal selection.
 *
 */
void TestPlotVariablePickerDialog::dialogA11y()
{
    PlotVariablePickerDialog dlg({ObjectRef::forNode(QStringLiteral("J1"))}, nullptr,
                                 UnitSystem::SI);
    swmmvis_test::assertDialogA11y(&dlg);
}

QTEST_MAIN(TestPlotVariablePickerDialog)
#include "test_plotvariablepickerdialog.moc"
