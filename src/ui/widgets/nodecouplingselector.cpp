/*!
 * \file   nodecouplingselector.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/widgets/nodecouplingselector.h"

#include "layers/swmmmodellayer.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QToolButton>
#include <QVBoxLayout>

using nodecoupling2d::State;

// ---------------------------------------------------------------------------
// NodeCouplingTableModel
// ---------------------------------------------------------------------------

NodeCouplingTableModel::NodeCouplingTableModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

void NodeCouplingTableModel::load(SWMMModelLayer *layer)
{
    beginResetModel();
    m_rows.clear();
    m_index.clear();
    if (layer && layer->engine()) {
        const SWMM_Engine e = layer->engine();
        for (int c = SWMMModelLayer::CatJunctions; c <= SWMMModelLayer::CatDividers; ++c) {
            const auto cat = static_cast<SWMMModelLayer::Category>(c);
            for (int i = 0; i < layer->categoryCount(cat); ++i) {
                const QString name = layer->objectNameAt(cat, i);
                Row r;
                if (name.isEmpty() || !nodecoupling2d::read(e, name, &r.original)) continue;
                r.edited = r.original;
                r.category = c;
                r.defaultType = nodecoupling2d::defaultType(e, name);
                r.allowed = nodecoupling2d::allowedTypes(e, name);
                switch (c) {
                case SWMMModelLayer::CatJunctions:
                    r.categoryLabel = r.defaultType == nodecoupling2d::None
                                          ? tr("Junction (inlet / virtual)") : tr("Junction");
                    break;
                case SWMMModelLayer::CatOutfalls: r.categoryLabel = tr("Outfall"); break;
                case SWMMModelLayer::CatStorage:  r.categoryLabel = tr("Storage"); break;
                default:                          r.categoryLabel = tr("Divider"); break;
                }
                r.extra.resize(m_extraHeaders.size());
                m_index.insert(name, m_rows.size());
                m_rows.append(r);
            }
        }
    }
    endResetModel();
    emit countsChanged();
}

void NodeCouplingTableModel::setExtraColumns(const QStringList &headers)
{
    beginResetModel();
    m_extraHeaders = headers;
    for (Row &r : m_rows) r.extra.resize(headers.size());
    endResetModel();
}

void NodeCouplingTableModel::setExtraValues(const QString &node, const QStringList &values)
{
    const int r = rowOf(node);
    if (r < 0) return;
    for (int i = 0; i < m_extraHeaders.size(); ++i)
        m_rows[r].extra[i] = values.value(i);
    emit dataChanged(index(r, ColFirstExtra), index(r, columnCount() - 1));
}

int NodeCouplingTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

int NodeCouplingTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColFirstExtra + m_extraHeaders.size();
}

QVariant NodeCouplingTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size()) return {};
    const Row &r = m_rows[index.row()];
    const bool changed = r.edited != r.original;
    if (role == CategoryRole) return r.category;
    if (role == AllowedTypesRole) return QVariant::fromValue(r.allowed);
    if (role == Qt::FontRole && changed && index.column() <= ColCoupleable) {
        QFont f; f.setBold(true); return f;
    }
    switch (index.column()) {
    case ColName:
        if (role == Qt::DisplayRole) return r.edited.node;
        break;
    case ColCategory:
        if (role == Qt::DisplayRole) return r.categoryLabel;
        break;
    case ColType:
        if (role == Qt::DisplayRole)
            return r.edited.authored ? nodecoupling2d::typeLabel(r.edited.type)
                                     : tr("%1 (default)").arg(nodecoupling2d::typeLabel(r.edited.type));
        if (role == Qt::EditRole) return r.edited.type;
        break;
    case ColCoupleable:
        if (role == Qt::CheckStateRole)
            return r.edited.type != nodecoupling2d::None ? Qt::Checked : Qt::Unchecked;
        break;
    default:
        if (role == Qt::DisplayRole) return r.extra.value(index.column() - ColFirstExtra);
        break;
    }
    return {};
}

bool NodeCouplingTableModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || index.row() >= m_rows.size()) return false;
    if (index.column() == ColCoupleable && role == Qt::CheckStateRole) {
        setCoupleable({index.row()}, value.toInt() == Qt::Checked);
        return true;
    }
    if (index.column() == ColType && role == Qt::EditRole) {
        setType({index.row()}, value.toInt());
        return true;
    }
    return false;
}

Qt::ItemFlags NodeCouplingTableModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags f = QAbstractTableModel::flags(index);
    if (index.column() == ColCoupleable) f |= Qt::ItemIsUserCheckable;
    if (index.column() == ColType) f |= Qt::ItemIsEditable;
    return f;
}

QVariant NodeCouplingTableModel::headerData(int section, Qt::Orientation o, int role) const
{
    if (o != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (section) {
    case ColName:       return tr("Node");
    case ColCategory:   return tr("Category");
    case ColType:       return tr("2D coupling");
    case ColCoupleable: return tr("Coupleable");
    default:            return m_extraHeaders.value(section - ColFirstExtra);
    }
}

void NodeCouplingTableModel::stage(int row, const State &s)
{
    if (m_rows[row].edited == s) return;
    m_rows[row].edited = s;
    emit dataChanged(index(row, 0), index(row, ColCoupleable));
}

void NodeCouplingTableModel::setCoupleable(const QList<int> &rows, bool on)
{
    for (int r : rows) {
        if (r < 0 || r >= m_rows.size()) continue;
        State s = m_rows[r].edited;
        if (on == (s.type != nodecoupling2d::None)) continue;
        if (!on) {
            s.authored = true;
            s.type = nodecoupling2d::None;
            s.params.clear();
        } else if (m_rows[r].defaultType != nodecoupling2d::None) {
            // Back to the class default — an unauthored row when that is all
            // the original row said.
            s.authored = m_rows[r].original.authored
                         && m_rows[r].original.type != nodecoupling2d::None;
            s.type = s.authored ? m_rows[r].original.type : m_rows[r].defaultType;
            s.params = s.authored ? m_rows[r].original.params : QMap<QString, QString>{};
        } else {
            s.authored = true;
            s.type = nodecoupling2d::Manhole;
            s.params.clear();
        }
        stage(r, s);
    }
    emit countsChanged();
}

void NodeCouplingTableModel::setType(const QList<int> &rows, int type)
{
    for (int r : rows) {
        if (r < 0 || r >= m_rows.size() || !m_rows[r].allowed.contains(type)) continue;
        State s = m_rows[r].edited;
        if (s.authored && s.type == type) continue;
        // Parameters belong to a structure type; a new type starts bare.
        if (s.type != type) s.params.clear();
        s.authored = true;
        s.type = type;
        stage(r, s);
    }
    emit countsChanged();
}

void NodeCouplingTableModel::resetToDefault(const QList<int> &rows)
{
    for (int r : rows) {
        if (r < 0 || r >= m_rows.size()) continue;
        State s;
        s.node = m_rows[r].edited.node;
        s.authored = false;
        s.type = m_rows[r].defaultType;
        stage(r, s);
    }
    emit countsChanged();
}

bool NodeCouplingTableModel::isCoupleable(const QString &node) const
{
    const int r = rowOf(node);
    return r >= 0 && m_rows[r].edited.type != nodecoupling2d::None;
}

int NodeCouplingTableModel::coupleableCount() const
{
    int n = 0;
    for (const Row &r : m_rows) n += r.edited.type != nodecoupling2d::None;
    return n;
}

QVector<State> NodeCouplingTableModel::changedStates() const
{
    QVector<State> out;
    for (const Row &r : m_rows)
        if (r.edited != r.original) out.append(r.edited);
    return out;
}

// ---------------------------------------------------------------------------
// Editor pieces
// ---------------------------------------------------------------------------

namespace {

/*! Combo of the row's allowed types for the "2D coupling" column. */
class CouplingTypeDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &,
                          const QModelIndex &index) const override
    {
        auto *combo = new QComboBox(parent);
        const auto allowed = index.data(NodeCouplingTableModel::AllowedTypesRole).value<QList<int>>();
        for (int t : allowed) combo->addItem(nodecoupling2d::typeLabel(t), t);
        return combo;
    }
    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        auto *combo = static_cast<QComboBox *>(editor);
        combo->setCurrentIndex(combo->findData(index.data(Qt::EditRole)));
    }
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override
    {
        model->setData(index, static_cast<QComboBox *>(editor)->currentData(), Qt::EditRole);
    }
};

/*! Name search + category filter. */
class CouplingFilterProxy : public QSortFilterProxyModel
{
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    int category = -1;
    void setCategory(int c) { category = c; invalidateFilter(); }
protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override
    {
        const QModelIndex i = sourceModel()->index(row, 0, parent);
        if (category >= 0 && i.data(NodeCouplingTableModel::CategoryRole).toInt() != category)
            return false;
        return QSortFilterProxyModel::filterAcceptsRow(row, parent);
    }
};

} // namespace

QTableView *NodeCouplingSelectorDialog::buildEditor(QWidget *host, NodeCouplingTableModel *model,
                                                    const QStringList &mapSelection,
                                                    QLabel **readout)
{
    auto *lay = new QVBoxLayout(host);
    lay->setContentsMargins(0, 0, 0, 0);

    auto *filterRow = new QHBoxLayout;
    auto *search = new QLineEdit(host);
    search->setPlaceholderText(QObject::tr("Search nodes…"));
    search->setClearButtonEnabled(true);
    auto *category = new QComboBox(host);
    category->addItem(QObject::tr("All categories"), -1);
    category->addItem(QObject::tr("Junctions"), int(SWMMModelLayer::CatJunctions));
    category->addItem(QObject::tr("Outfalls"), int(SWMMModelLayer::CatOutfalls));
    category->addItem(QObject::tr("Storage"), int(SWMMModelLayer::CatStorage));
    category->addItem(QObject::tr("Dividers"), int(SWMMModelLayer::CatDividers));
    filterRow->addWidget(search, 1);
    filterRow->addWidget(category);
    lay->addLayout(filterRow);

    auto *proxy = new CouplingFilterProxy(host);
    proxy->setSourceModel(model);
    proxy->setFilterKeyColumn(NodeCouplingTableModel::ColName);
    proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    QObject::connect(search, &QLineEdit::textChanged, proxy,
                     [proxy](const QString &t) { proxy->setFilterFixedString(t); });
    QObject::connect(category, &QComboBox::currentIndexChanged, proxy,
                     [proxy, category](int) { proxy->setCategory(category->currentData().toInt()); });

    auto *view = new QTableView(host);
    view->setModel(proxy);
    view->setSelectionBehavior(QAbstractItemView::SelectRows);
    view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    view->setSortingEnabled(true);
    view->setItemDelegateForColumn(NodeCouplingTableModel::ColType, new CouplingTypeDelegate(view));
    view->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    view->horizontalHeader()->setStretchLastSection(true);
    view->verticalHeader()->hide();
    lay->addWidget(view, 1);

    // Selected rows in SOURCE-model coordinates.
    auto selectedRows = [view, proxy]() {
        QList<int> rows;
        for (const QModelIndex &i : view->selectionModel()->selectedRows())
            rows.append(proxy->mapToSource(i).row());
        return rows;
    };

    auto *bulk = new QHBoxLayout;
    auto *on = new QPushButton(QObject::tr("Coupleable"), host);
    auto *off = new QPushButton(QObject::tr("Not coupleable"), host);
    auto *typeBtn = new QToolButton(host);
    typeBtn->setText(QObject::tr("Structure"));
    typeBtn->setPopupMode(QToolButton::InstantPopup);
    auto *typeMenu = new QMenu(typeBtn);
    for (int t : {nodecoupling2d::Manhole, nodecoupling2d::Grate, nodecoupling2d::Curb,
                  nodecoupling2d::Open, nodecoupling2d::Outfall})
        typeMenu->addAction(nodecoupling2d::typeLabel(t), host,
                            [model, selectedRows, t] { model->setType(selectedRows(), t); });
    typeBtn->setMenu(typeMenu);
    auto *reset = new QPushButton(QObject::tr("Default"), host);
    reset->setToolTip(QObject::tr("Drop the selected nodes' coupling entries — "
                                  "each takes its category default."));
    auto *fromMap = new QPushButton(QObject::tr("Select map selection"), host);
    fromMap->setEnabled(!mapSelection.isEmpty());
    auto *invert = new QPushButton(QObject::tr("Invert selection"), host);
    for (QWidget *w : {static_cast<QWidget *>(on), static_cast<QWidget *>(off),
                       static_cast<QWidget *>(typeBtn), static_cast<QWidget *>(reset),
                       static_cast<QWidget *>(fromMap), static_cast<QWidget *>(invert)})
        bulk->addWidget(w);
    bulk->addStretch();
    lay->addLayout(bulk);
    QObject::connect(on, &QPushButton::clicked, host, [model, selectedRows] { model->setCoupleable(selectedRows(), true); });
    QObject::connect(off, &QPushButton::clicked, host, [model, selectedRows] { model->setCoupleable(selectedRows(), false); });
    QObject::connect(reset, &QPushButton::clicked, host, [model, selectedRows] { model->resetToDefault(selectedRows()); });
    QObject::connect(fromMap, &QPushButton::clicked, host, [view, proxy, model, mapSelection] {
        QItemSelection sel;
        for (const QString &n : mapSelection) {
            const int r = model->rowOf(n);
            if (r < 0) continue;
            const QModelIndex i = proxy->mapFromSource(model->index(r, 0));
            if (i.isValid()) sel.select(i, i);
        }
        view->selectionModel()->select(sel, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    });
    QObject::connect(invert, &QPushButton::clicked, host, [view, proxy] {
        QItemSelection all(proxy->index(0, 0), proxy->index(proxy->rowCount() - 1, 0));
        view->selectionModel()->select(all, QItemSelectionModel::Toggle | QItemSelectionModel::Rows);
    });

    auto *label = new QLabel(host);
    auto refresh = [label, model] {
        label->setText(QObject::tr("%1 of %2 nodes coupleable")
                           .arg(model->coupleableCount()).arg(model->rowCount()));
    };
    QObject::connect(model, &NodeCouplingTableModel::countsChanged, label, refresh);
    refresh();
    lay->addWidget(label);
    if (readout) *readout = label;
    return view;
}

// ---------------------------------------------------------------------------
// NodeCouplingSelectorDialog
// ---------------------------------------------------------------------------

NodeCouplingSelectorDialog::NodeCouplingSelectorDialog(SWMMModelLayer *layer,
                                                       const QStringList &mapSelection,
                                                       QWidget *parent)
    : QDialog(parent), m_layer(layer), m_model(new NodeCouplingTableModel(this))
{
    setWindowTitle(tr("Coupleable nodes"));
    resize(640, 520);
    m_model->load(layer);

    auto *lay = new QVBoxLayout(this);
    auto *intro = new QLabel(tr("Which 1D nodes may exchange water with the 2D surface, and "
                                "through what structure. Nodes that are not coupleable get no "
                                "mesh coupling when the mesh is generated or remapped."), this);
    intro->setWordWrap(true);
    lay->addWidget(intro);
    auto *host = new QWidget(this);
    buildEditor(host, m_model, mapSelection, &m_readout);
    lay->addWidget(host, 1);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel, this);
    lay->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this] {
        QString error;
        if (!applyEdits(&error)) QMessageBox::warning(this, windowTitle(), error);
    });
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        QString error;
        if (applyEdits(&error)) accept();
        else QMessageBox::warning(this, windowTitle(), error);
    });
}

bool NodeCouplingSelectorDialog::applyEdits(QString *error)
{
    if (!m_layer) return false;
    const auto changed = m_model->changedStates();
    if (changed.isEmpty()) return true;
    if (!m_layer->pushNodeCoupling2DEdits(changed, tr("Set Node Coupling"), error)) return false;
    m_model->load(m_layer);
    return true;
}
