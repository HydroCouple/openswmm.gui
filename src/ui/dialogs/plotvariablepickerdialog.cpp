/*!
 * \file   plotvariablepickerdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/plotvariablepickerdialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

namespace openswmmvis::ui {

using namespace openswmmvis::plot;

namespace {

// Leaf payload roles (mirrors the seriestree convention: enums as ints).
constexpr int kRoleAttr = Qt::UserRole;      ///< int(PlotAttribute)
constexpr int kRoleKind = Qt::UserRole + 1;  ///< int(ObjectRef::Kind)
constexpr int kRoleName = Qt::UserRole + 2;  ///< object name (unused for System)
constexpr int kRoleSpecies = Qt::UserRole + 3;  ///< species NAME (Y2b-2); empty for fixed attrs

QString groupLabelFor(const ObjectRef &ref)
{
    switch (ref.kind) {
    case ObjectRef::Kind::Node:     return QObject::tr("Node %1").arg(ref.name);
    case ObjectRef::Kind::Link:     return QObject::tr("Link %1").arg(ref.name);
    case ObjectRef::Kind::Subcatch: return QObject::tr("Subcatchment %1").arg(ref.name);
    default:                        return ref.name;
    }
}

bool isCheckableLeaf(const QTreeWidgetItem *item)
{
    return item->childCount() == 0 &&
           (item->flags() & Qt::ItemIsUserCheckable) &&
           (item->flags() & Qt::ItemIsEnabled);
}

} // namespace

PlotVariablePickerDialog::PlotVariablePickerDialog(
    const QVector<ObjectRef> &features,
    const IRunLayer *availability,
    UnitSystem units,
    QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Plot Variables"));
    // Named so DialogLayoutWatcher persists the geometry; check states are
    // deliberately fresh on every open.
    setObjectName(QStringLiteral("PlotVariablePickerDialog"));
    resize(460, 560);

    auto *root = new QVBoxLayout(this);

    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(tr("Filter variables…"));
    m_filter->setClearButtonEnabled(true);
    root->addWidget(m_filter);

    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("variableTree"));
    m_tree->setHeaderHidden(true);
    m_tree->setSelectionMode(QAbstractItemView::NoSelection);
    m_tree->setUniformRowHeights(true);
    root->addWidget(m_tree, 1);

    auto *btnRow = new QHBoxLayout();
    auto *allBtn    = new QPushButton(tr("Select &All"),  this);
    auto *noneBtn   = new QPushButton(tr("Select &None"), this);
    auto *invertBtn = new QPushButton(tr("&Invert"),      this);
    btnRow->addWidget(allBtn);
    btnRow->addWidget(noneBtn);
    btnRow->addWidget(invertBtn);
    btnRow->addStretch();
    root->addLayout(btnRow);

    m_buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    root->addWidget(m_buttons);

    buildTree(features, availability, units);

    connect(allBtn,    &QPushButton::clicked, this, [this]{ setAllChecked(true);  });
    connect(noneBtn,   &QPushButton::clicked, this, [this]{ setAllChecked(false); });
    connect(invertBtn, &QPushButton::clicked, this, [this]{ invertChecked();      });
    connect(m_filter,  &QLineEdit::textChanged,
            this, &PlotVariablePickerDialog::applyFilter);
    connect(m_tree, &QTreeWidget::itemChanged,
            this, [this](QTreeWidgetItem *, int){ updateOkEnabled(); });
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    m_tree->expandAll();
    updateOkEnabled();
}

void PlotVariablePickerDialog::addAttributeLeaf(QTreeWidgetItem *group,
                                                const ObjectRef &ref,
                                                PlotAttribute attr,
                                                const IRunLayer *availability,
                                                UnitSystem units)
{
    auto *leaf = new QTreeWidgetItem(group);
    leaf->setText(0, labelWithUnits(attr, units));
    leaf->setData(0, kRoleAttr, static_cast<int>(attr));
    leaf->setData(0, kRoleKind, static_cast<int>(ref.kind));
    leaf->setData(0, kRoleName, ref.name);
    leaf->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
    leaf->setCheckState(0, Qt::Unchecked);
    if (availability && !availability->supportsAttribute(attr)) {
        leaf->setFlags(leaf->flags() & ~Qt::ItemIsEnabled);
        leaf->setToolTip(0, tr("Not available in this results file."));
    }
}

void PlotVariablePickerDialog::addDescriptorLeaf(
    QTreeWidgetItem *group, const ObjectRef &ref, const ResultDescriptor &d,
    const IRunLayer *availability, UnitSystem units)
{
    if (!d.isSpecies()) {
        addAttributeLeaf(group, ref, d.attr, availability, units);
        return;
    }
    // A species leaf (Y2b-2): labelled and united by the Y2a authorities
    // (age reads hours, never mg/L) and keyed by NAME (D-G1).
    auto *leaf = new QTreeWidgetItem(group);
    leaf->setText(0, QStringLiteral("%1 (%2)")
                         .arg(d.label(), d.unitLabel(units)));
    leaf->setData(0, kRoleAttr,
                  static_cast<int>(PlotAttribute::Unknown));
    leaf->setData(0, kRoleKind, static_cast<int>(ref.kind));
    leaf->setData(0, kRoleName, ref.name);
    leaf->setData(0, kRoleSpecies, d.species);
    leaf->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
    leaf->setCheckState(0, Qt::Unchecked);
}

void PlotVariablePickerDialog::buildTree(const QVector<ObjectRef> &features,
                                         const IRunLayer *availability,
                                         UnitSystem units)
{
    const QSignalBlocker blocker(m_tree);

    auto *sysGroup = new QTreeWidgetItem(m_tree);
    sysGroup->setText(0, tr("System Variables"));
    sysGroup->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable |
                       Qt::ItemIsAutoTristate);
    sysGroup->setCheckState(0, Qt::Unchecked);
    for (PlotAttribute a : systemPlotAttributes())
        addAttributeLeaf(sysGroup, ObjectRef::forSystem(), a,
                         availability, units);

    for (const ObjectRef &ref : features) {
        // Y2b-2: the run decides what is plottable — the fixed set plus
        // any species the open .out carries. With no availability layer
        // the fixed set alone appears (a legacy run's behaviour).
        const QVector<ResultDescriptor> descriptors =
            availability ? availability->resultDescriptorsForKind(ref.kind)
                         : resultDescriptorsForKind(ref.kind, QStringList());
        if (descriptors.isEmpty()) continue;
        auto *group = new QTreeWidgetItem(m_tree);
        group->setText(0, groupLabelFor(ref));
        group->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable |
                        Qt::ItemIsAutoTristate);
        group->setCheckState(0, Qt::Unchecked);
        for (const ResultDescriptor &d : descriptors)
            addDescriptorLeaf(group, ref, d, availability, units);
    }
}

QVector<PlotVariablePickerDialog::Entry>
PlotVariablePickerDialog::checkedEntries() const
{
    QVector<Entry> out;
    for (int g = 0; g < m_tree->topLevelItemCount(); ++g) {
        const QTreeWidgetItem *group = m_tree->topLevelItem(g);
        for (int i = 0; i < group->childCount(); ++i) {
            const QTreeWidgetItem *leaf = group->child(i);
            if (leaf->checkState(0) != Qt::Checked) continue;
            Entry e;
            e.attribute = static_cast<PlotAttribute>(
                leaf->data(0, kRoleAttr).toInt());
            e.species = leaf->data(0, kRoleSpecies).toString();
            const auto kind = static_cast<ObjectRef::Kind>(
                leaf->data(0, kRoleKind).toInt());
            e.ref = (kind == ObjectRef::Kind::System)
                        ? ObjectRef::forSystem()
                        : ObjectRef(kind, leaf->data(0, kRoleName).toString());
            out.push_back(e);
        }
    }
    return out;
}

void PlotVariablePickerDialog::setAllChecked(bool checked)
{
    const QSignalBlocker blocker(m_tree);
    for (int g = 0; g < m_tree->topLevelItemCount(); ++g) {
        QTreeWidgetItem *group = m_tree->topLevelItem(g);
        for (int i = 0; i < group->childCount(); ++i) {
            QTreeWidgetItem *leaf = group->child(i);
            if (isCheckableLeaf(leaf) && !leaf->isHidden())
                leaf->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
        }
    }
    updateOkEnabled();
}

void PlotVariablePickerDialog::invertChecked()
{
    const QSignalBlocker blocker(m_tree);
    for (int g = 0; g < m_tree->topLevelItemCount(); ++g) {
        QTreeWidgetItem *group = m_tree->topLevelItem(g);
        for (int i = 0; i < group->childCount(); ++i) {
            QTreeWidgetItem *leaf = group->child(i);
            if (isCheckableLeaf(leaf) && !leaf->isHidden())
                leaf->setCheckState(0, leaf->checkState(0) == Qt::Checked
                                           ? Qt::Unchecked : Qt::Checked);
        }
    }
    updateOkEnabled();
}

void PlotVariablePickerDialog::applyFilter(const QString &text)
{
    const QString needle = text.trimmed();
    for (int g = 0; g < m_tree->topLevelItemCount(); ++g) {
        QTreeWidgetItem *group = m_tree->topLevelItem(g);
        // A group whose title matches shows all of its children.
        const bool groupHit = needle.isEmpty() ||
            group->text(0).contains(needle, Qt::CaseInsensitive);
        bool anyChildVisible = false;
        for (int i = 0; i < group->childCount(); ++i) {
            QTreeWidgetItem *leaf = group->child(i);
            const bool hit = groupHit ||
                leaf->text(0).contains(needle, Qt::CaseInsensitive);
            leaf->setHidden(!hit);
            anyChildVisible = anyChildVisible || hit;
        }
        group->setHidden(!anyChildVisible);
    }
}

void PlotVariablePickerDialog::updateOkEnabled()
{
    QPushButton *ok = m_buttons->button(QDialogButtonBox::Ok);
    if (!ok) return;
    bool anyChecked = false;
    for (int g = 0; g < m_tree->topLevelItemCount() && !anyChecked; ++g) {
        const QTreeWidgetItem *group = m_tree->topLevelItem(g);
        for (int i = 0; i < group->childCount(); ++i) {
            if (group->child(i)->checkState(0) == Qt::Checked) {
                anyChecked = true;
                break;
            }
        }
    }
    ok->setEnabled(anyChecked);
}

} // namespace openswmmvis::ui
