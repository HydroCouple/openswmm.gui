/*!
 * \file   test_plotvariablepickerdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Pins the PlotVariablePickerDialog (the Plot Time Series toolbar
 *         dialog) and the shared canonical attribute lists it is built on.
 *
 * Engine-free: the dialog takes pre-mapped plot::ObjectRefs plus an
 * optional IRunLayer availability probe, so a stub layer with a blacklist
 * exercises the supportsAttribute gating without touching a .out file.
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

namespace {

// Stub layer that rejects a fixed blacklist — models a .out that lacks
// e.g. PET and link volume.
class StubRunLayer : public IRunLayer
{
public:
    QString    scenarioName()      const override { return QStringLiteral("Stub"); }
    UnitSystem unitSystem()        const override { return UnitSystem::SI; }
    double     startDateJulian()   const override { return 0.0; }
    int        periodCount()       const override { return 0; }
    int        reportStepSeconds() const override { return 0; }
    void       getSeriesAt(const ObjectRef&, PlotAttribute, SeriesData& out) const override
    { out.ok = false; out.errorMessage = QStringLiteral("stub"); }
    bool       supportsAttribute(PlotAttribute a) const override
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
    QVector<ResultDescriptor> resultDescriptorsForKind(
        ObjectRef::Kind kind) const override
    {
        return openswmmvis::plot::resultDescriptorsForKind(
            kind, QStringList{QStringLiteral("TSS"), QStringLiteral("Lead"),
                              QStringLiteral("__WATER_AGE__")});
    }
};

QTreeWidget *treeOf(PlotVariablePickerDialog &dlg)
{
    auto *tree = dlg.findChild<QTreeWidget *>(QStringLiteral("variableTree"));
    Q_ASSERT(tree);
    return tree;
}

QPushButton *okButtonOf(PlotVariablePickerDialog &dlg)
{
    auto *bb = dlg.findChild<QDialogButtonBox *>();
    Q_ASSERT(bb);
    return bb->button(QDialogButtonBox::Ok);
}

QPushButton *pushButtonNamed(PlotVariablePickerDialog &dlg, const QString &text)
{
    const auto buttons = dlg.findChildren<QPushButton *>();
    for (QPushButton *b : buttons)
        if (b->text() == text) return b;
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

void TestPlotVariablePickerDialog::attributeListsPinned()
{
    QCOMPARE(nodePlotAttributes().size(),     6);
    QCOMPARE(linkPlotAttributes().size(),     5);
    QCOMPARE(subcatchPlotAttributes().size(), 5);
    QCOMPARE(systemPlotAttributes().size(),  14);

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
    QCOMPARE(systemPlotAttributes().last(),  PlotAttribute::SystemTemperature);
    QCOMPARE(nodePlotAttributes().first(),   PlotAttribute::NodeDepth);
    QCOMPARE(linkPlotAttributes().first(),   PlotAttribute::LinkFlow);
    QCOMPARE(subcatchPlotAttributes().first(), PlotAttribute::SubcatchRainfall);
}

void TestPlotVariablePickerDialog::attributesForKindDispatch()
{
    QCOMPARE(&attributesForKind(ObjectRef::Kind::Node),     &nodePlotAttributes());
    QCOMPARE(&attributesForKind(ObjectRef::Kind::Link),     &linkPlotAttributes());
    QCOMPARE(&attributesForKind(ObjectRef::Kind::Subcatch), &subcatchPlotAttributes());
    QCOMPARE(&attributesForKind(ObjectRef::Kind::System),   &systemPlotAttributes());
    QVERIFY(attributesForKind(ObjectRef::Kind::Unknown).isEmpty());
    QVERIFY(attributesForKind(ObjectRef::Kind::Mesh2DCell).isEmpty());
    QVERIFY(attributesForKind(ObjectRef::Kind::Observed).isEmpty());
}

void TestPlotVariablePickerDialog::systemOnlyWhenNoFeatures()
{
    PlotVariablePickerDialog dlg({}, nullptr, UnitSystem::SI);
    QTreeWidget *tree = treeOf(dlg);
    QCOMPARE(tree->topLevelItemCount(), 1);
    QCOMPARE(tree->topLevelItem(0)->childCount(), 14);
    QVERIFY(dlg.checkedEntries().isEmpty());
    QVERIFY(!okButtonOf(dlg)->isEnabled());
}

void TestPlotVariablePickerDialog::groupsPerSelectedFeature()
{
    const QVector<ObjectRef> features = {
        ObjectRef::forNode(QStringLiteral("J1")),
        ObjectRef::forLink(QStringLiteral("C1")),
        ObjectRef::forSubcatch(QStringLiteral("S1")),
    };
    PlotVariablePickerDialog dlg(features, nullptr, UnitSystem::US);
    QTreeWidget *tree = treeOf(dlg);
    QCOMPARE(tree->topLevelItemCount(), 4);          // System + 3 features
    QCOMPARE(tree->topLevelItem(0)->childCount(), 14);
    QCOMPARE(tree->topLevelItem(1)->childCount(), 6);   // Node J1
    QCOMPARE(tree->topLevelItem(2)->childCount(), 5);   // Link C1
    QCOMPARE(tree->topLevelItem(3)->childCount(), 5);   // Subcatchment S1
    QVERIFY(tree->topLevelItem(1)->text(0).contains(QStringLiteral("J1")));
}

void TestPlotVariablePickerDialog::groupCheckCascadesToLeaves()
{
    const QVector<ObjectRef> features = {
        ObjectRef::forNode(QStringLiteral("J1")),
    };
    PlotVariablePickerDialog dlg(features, nullptr, UnitSystem::SI);
    QTreeWidget *tree = treeOf(dlg);

    QTreeWidgetItem *nodeGroup = tree->topLevelItem(1);
    nodeGroup->setCheckState(0, Qt::Checked);        // AutoTristate cascade

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
            const auto attr = static_cast<PlotAttribute>(
                leaf->data(0, Qt::UserRole).toInt());
            const bool enabled = leaf->flags() & Qt::ItemIsEnabled;
            QCOMPARE(enabled, probe.supportsAttribute(attr));
            if (!enabled) {
                ++disabled;
                QVERIFY(!leaf->toolTip(0).isEmpty());
            }
        }
    }
    QCOMPARE(disabled, 2);   // SystemPET + LinkVolume
}

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
    QCOMPARE(entries.size(), 14 + 5 - 2);   // minus SystemPET + LinkVolume
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

void TestPlotVariablePickerDialog::invertFlipsCheckedState()
{
    PlotVariablePickerDialog dlg({}, nullptr, UnitSystem::SI);
    QTreeWidget *tree = treeOf(dlg);
    tree->topLevelItem(0)->child(0)->setCheckState(0, Qt::Checked);

    QPushButton *invert = pushButtonNamed(dlg, QStringLiteral("&Invert"));
    QVERIFY(invert);
    invert->click();

    const auto entries = dlg.checkedEntries();
    QCOMPARE(entries.size(), 13);   // everything except the one that was checked
    for (const auto &e : entries)
        QVERIFY(e.attribute != systemPlotAttributes().first());
}

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
        if (!sys->child(i)->isHidden()) ++visible;
    QCOMPARE(visible, 1);   // only "Rainfall (mm/hr)"

    filter->clear();
    visible = 0;
    for (int i = 0; i < sys->childCount(); ++i)
        if (!sys->child(i)->isHidden()) ++visible;
    QCOMPARE(visible, 14);
}

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

void TestPlotVariablePickerDialog::speciesLeavesComeFromTheRun()
{
    // The amendment's razor, dialog edition: a run with 2 pollutants +
    // age serves the hydraulic set + 3 for a node group; the System
    // group must NOT grow (no per-species system column exists).
    SpeciesStubRunLayer probe;
    const QVector<ObjectRef> features{ObjectRef::forNode(QStringLiteral("J1"))};
    PlotVariablePickerDialog dlg(features, &probe, UnitSystem::SI);
    QTreeWidget *tree = treeOf(dlg);
    QCOMPARE(tree->topLevelItemCount(), 2);   // System + J1

    const QTreeWidgetItem *sys  = tree->topLevelItem(0);
    const QTreeWidgetItem *node = tree->topLevelItem(1);
    QCOMPARE(sys->childCount(), systemPlotAttributes().size());
    QCOMPARE(node->childCount(), nodePlotAttributes().size() + 3);

    // The age leaf reads in HOURS through the Y2a authorities — a picker
    // that offered "__WATER_AGE__ (mg/L)" would mislabel the axis.
    const QTreeWidgetItem *ageLeaf =
        node->child(node->childCount() - 1);
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

void TestPlotVariablePickerDialog::dialogA11y()
{
    PlotVariablePickerDialog dlg({ObjectRef::forNode(QStringLiteral("J1"))},
                                 nullptr, UnitSystem::SI);
    swmmvis_test::assertDialogA11y(&dlg);
}

QTEST_MAIN(TestPlotVariablePickerDialog)
#include "test_plotvariablepickerdialog.moc"
