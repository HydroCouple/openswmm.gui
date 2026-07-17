/*!
 * \file   sublayerstyledialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice S3 — Sublayer style editor implementation.
 *         Slice S4 P4 — refactored to contextual tabs by Q_CLASSINFO group.
 */
#include "ui/dialogs/sublayerstyledialog.h"

#include "render/isublayer.h"
#include "render/sublayerstyle.h"
#include "layers/swmm2dmeshlayer.h"
#include "ui/widgets/stylepropertydelegate.h"

#include <qpropertymodel.h>
#include <qpropertyitemdelegate.h>

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QMetaObject>
#include <QMetaProperty>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QTabWidget>
#include <QTreeView>
#include <QVBoxLayout>

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>

namespace openswmmvis::ui {

using OpenSWMM::Render::ISublayer;
using OpenSWMM::Render::SublayerStyle;

namespace {

// Walk the style bag's metaobject and produce a (group → properties) map
// from the Q_CLASSINFO("group:<prop>", "<group>") tags each style bag
// declares (e.g. NodeMarkerStyle, ConduitArrowStyle, …). Properties with
// no group tag go into "General" so they remain editable.
QHash<QString, QStringList> groupPropertiesByClassInfo(const QObject *obj)
{
    QHash<QString, QStringList> out;
    if (!obj) return out;
    const QMetaObject *mo = obj->metaObject();

    // Build a property-name → group-name index from Q_CLASSINFO tags.
    QHash<QString, QString> propToGroup;
    for (int i = 0; i < mo->classInfoCount(); ++i) {
        const QMetaClassInfo ci = mo->classInfo(i);
        const QString name = QString::fromLatin1(ci.name());
        if (!name.startsWith(QStringLiteral("group:"))) continue;
        propToGroup.insert(name.mid(6), QString::fromLatin1(ci.value()));
    }

    // Walk only "own" Q_PROPERTYs (skip QObject::objectName); enumerate
    // each into its group bucket. Preserves declaration order via
    // append-to-list semantics, which the QPropertyModel relies on for
    // tab consistency.
    const int firstOwn = mo->propertyOffset();
    const int total    = mo->propertyCount();
    for (int p = firstOwn; p < total; ++p) {
        const QMetaProperty mp = mo->property(p);
        const QString name = QString::fromLatin1(mp.name());
        const QString group = propToGroup.value(name, QStringLiteral("General"));
        out[group].append(name);
    }
    return out;
}

// Filter that hides every property row except the ones in a fixed set.
//
// QPropertyModel is a CLASS-HIERARCHY reflection model, not a flat property
// list: top-level rows are QObjectClassPropertyItem nodes (one per class in
// the inheritance chain) whose DisplayRole on column 0 returns the
// className (e.g. "ConduitLineStyle"), NOT a property name. The actual
// Q_PROPERTY rows sit as children under the deepest class node.
//
// We therefore:
//   1) Enable setRecursiveFilteringEnabled(true) on the proxy so a class
//      node survives whenever any descendant matches the filter.
//   2) Accept top-level class nodes unconditionally; recursive filter
//      hides ones with no surviving children.
//   3) Match property names only at the leaf (Q_PROPERTY) level.
class GroupFilterProxy : public QSortFilterProxyModel
{
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    void setAllowedProperties(QStringList names)
    {
        m_allowed = QSet<QString>(names.cbegin(), names.cend());
        invalidateFilter();
    }
protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override
    {
        // Top-level class nodes — recursive filtering decides visibility.
        if (!parent.isValid()) return true;
        const QModelIndex idx = sourceModel()->index(row, 0, parent);
        const QString name = idx.data(Qt::DisplayRole).toString();
        return m_allowed.contains(name);
    }
private:
    QSet<QString> m_allowed;
};

// Build a single tab — QTreeView over a filtered proxy on the shared
// QPropertyModel. The proxy filters down to the property names this tab
// owns. Returning the QWidget gives the caller (the dialog ctor) the
// child to addTab() with.
QWidget *makeGroupTab(QPropertyModel *pm,
                      const QStringList &propNames,
                      QWidget *parent)
{
    auto *view = new QTreeView(parent);
    view->setAlternatingRowColors(true);
    view->setEditTriggers(QAbstractItemView::AllEditTriggers);
    view->setSelectionMode(QAbstractItemView::SingleSelection);
    view->setRootIsDecorated(false);
    view->setItemsExpandable(false);

    auto *proxy = new GroupFilterProxy(view);
    proxy->setSourceModel(pm);
    proxy->setAllowedProperties(propNames);
    view->setModel(proxy);
    // Enum Q_PROPERTYs render as named combos and ClassificationScheme rows
    // get the popup editor — both via the project's QPropertyItemDelegate.
    view->setItemDelegate(makeStyleDelegate(view));

    view->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    view->header()->setStretchLastSection(true);
    view->header()->setMinimumSectionSize(90);  // stop columns collapsing on a narrow dialog
    view->expandAll();
    return view;
}

} // namespace

SublayerStyleDialog::SublayerStyleDialog(ISublayer *sublayer, QWidget *parent)
    : QDialog(parent), m_sublayer(sublayer)
{
    setWindowFlags(windowFlags() | Qt::Tool);
    setAttribute(Qt::WA_DeleteOnClose);
    resize(480, 520);

    SublayerStyle *style = m_sublayer ? m_sublayer->style() : nullptr;

    if (m_sublayer)
        setWindowTitle(tr("%1 — Style").arg(m_sublayer->displayName()));
    else
        // Slice Z.8 — user-facing wording moves away from "sublayer".
        setWindowTitle(tr("Style"));

    // Snapshot for Cancel rollback.
    if (style) m_snapshot = style->toJson();

    auto *root = new QVBoxLayout(this);

    // P4 — contextual tabbed editor. Walk Q_CLASSINFO("group:<prop>",
    // "<group>") tags on the style bag and produce one tab per group.
    // Each tab is a QTreeView over a QSortFilterProxyModel that filters
    // a shared QPropertyModel down to that group's property names.
    // Properties with no group tag fall into a "General" tab so the
    // editor never silently hides a property the user might want.
    auto *pm = new QPropertyModel(style, this);

    QHash<QString, QStringList> groups = groupPropertiesByClassInfo(style);
    // Terrain meshes always classify by bed elevation — the generic
    // 'attribute' field (a real choice on 2D results layers) is inert for
    // mesh sublayers, so don't offer it.
    if (m_sublayer && qobject_cast<::SWMM2DMeshLayer *>(m_sublayer->parent()))
        for (auto it = groups.begin(); it != groups.end(); ++it)
            it.value().removeAll(QStringLiteral("attribute"));
    if (groups.size() <= 1) {
        // Degenerate case (one group total): show a single flat view —
        // tabs would just add chrome with no signal. Re-use the original
        // m_tree field so the back-compat path stays trivial.
        m_tree = new QTreeView(this);
        m_tree->setAlternatingRowColors(true);
        m_tree->setEditTriggers(QAbstractItemView::AllEditTriggers);
        m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
        m_tree->setRootIsDecorated(false);
        m_tree->setItemsExpandable(false);
        m_tree->setModel(pm);
        m_tree->setItemDelegate(makeStyleDelegate(m_tree));
        m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        m_tree->header()->setStretchLastSection(true);
        m_tree->header()->setMinimumSectionSize(90);  // stop columns collapsing on a narrow dialog
        m_tree->expandAll();
        root->addWidget(m_tree, 1);
    } else {
        auto *tabs = new QTabWidget(this);
        // Stable tab order — sort group names lexicographically with one
        // exception: "General" (the no-group fallback) always last so
        // editor focus lands on a "real" group on first open.
        QStringList names = groups.keys();
        names.sort(Qt::CaseInsensitive);
        names.removeAll(QStringLiteral("General"));
        if (groups.contains(QStringLiteral("General")))
            names.append(QStringLiteral("General"));

        for (const QString &g : names) {
            tabs->addTab(makeGroupTab(pm, groups.value(g), this), g);
        }
        root->addWidget(tabs, 1);
    }

    auto *bb = new QDialogButtonBox(
        QDialogButtonBox::Cancel | QDialogButtonBox::Close,
        this);
    bb->button(QDialogButtonBox::Close)->setDefault(true);
    bb->button(QDialogButtonBox::Cancel)
        ->setToolTip(tr("Revert edits applied since the dialog opened"));
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(bb->button(QDialogButtonBox::Close), &QPushButton::clicked,
            this, &QDialog::accept);
    root->addWidget(bb);

    if (m_sublayer)
        connect(m_sublayer.data(), &QObject::destroyed, this, &QDialog::close);
}

void SublayerStyleDialog::reject()
{
    if (m_sublayer && m_sublayer->style())
        m_sublayer->style()->fromJson(m_snapshot);
    QDialog::reject();
}

} // namespace openswmmvis::ui
