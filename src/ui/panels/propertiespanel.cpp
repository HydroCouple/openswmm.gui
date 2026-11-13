#include "layers/timeserieslayer.h"
#include "ui/panels/featureaggregateselection.h"
#include "layers/gisobjectref.h"
#include "ui/properties/datarecordsref.h"
/**
 * @file      propertiespanel.cpp
 * @brief     A dock widget that displays and edits object attributes using QPropertyModel.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "ui/panels/propertiespanel.h"
#include "ui/panels/swmmattributetablemodel.h"
#include "layers/openswmmvislayer.h"
#include "layers/swmmmodellayer.h"
#include "layers/swmm2dmeshlayer.h"
#include "mesh/meshobjectref.h"
#include "ui/panels/meshattributetablemodel.h"
#include "ui/panels/meshaggregateselection.h"
#include "ui/panels/swmmaggregateselection.h"
#include "ui/panels/dataobjectaggregateselection.h"
#include <type_traits>
#include "map/mapundostack.h"
#include "ui/panels/attributedelegates.h"
#include "ui/processlock.h"
#include "map/mapcanvas.h"
#include "layers/swmmresultslayer.h"    // active-run stats binding
#include "map/tools/maptoolidentify.h"  // IdentifyResult
#include "output/outputstatsregistry.h" // Slice QA.2 (full type for QPointer / connect)
#include "selection/selectionmanager.h" // SWMMObjectRef::ObjectType
#include "ui/properties/dataobjectpickereditor.h"
#include "ui/properties/dataobjectref.h"
#include "ui/properties/nodecompoundeditbutton.h"
#include "ui/properties/nodecompoundeditref.h"
// Phase 3 — subcatchment-side compound-edit ref + cell editor.
#include "ui/properties/subcatchcompoundeditbutton.h"
#include "ui/properties/subcatchcompoundeditref.h"
#include "ui/dialogs/subcatchcompoundeditdialog.h"
#include "ui/dialogs/groundwaterexchangedialog.h"
#include "ui/properties/usageeditbutton.h"
#include "ui/panels/usagetablemodel.h"
// Slice SC.1 — link-side compound-edit ref + cell editor.
#include "ui/dialogs/linkcompoundeditdialog.h"
#include "ui/properties/linkcompoundeditbutton.h"
#include "ui/properties/linkcompoundeditref.h"
#include "ui/properties/linkendpointcombobox.h"
// ATTRIBUTE_EDITOR_WIRING Phase 0 — culvert code inline combobox.
#include "ui/properties/culvertcodecombobox.h"
#include "ui/properties/culvertcoderef.h"
// DA.2 parity — rain gage recording-interval H:MM combo.
#include "ui/properties/rainintervalcombobox.h"
#include "ui/properties/rainintervalref.h"
// USER_FLAGS Phase 4 — per-object "User Flags" row ref + cell editor.
#include "ui/properties/userflagseditbutton.h"
#include "ui/properties/userflagseditref.h"
// Initial-quality UI round — per-element "Initial Quality" row ref + cell editor.
#include "ui/properties/initialqualityeditbutton.h"
#include "ui/properties/initialqualityeditref.h"
// Slice BM.0-Browse-Edit — right-click menu dispatches to editors via the registry.
#include "ui/editors/comprehensiveeditorregistry.h"
#include "ui/dialogs/nodecompoundeditdialog.h"
#include "curve/curveprovider.h"
#include "inlet/inletprovider.h"
#include "ui/properties/swmmlinkpropertyadapter.h"
#include "ui/properties/storageshapegeom.h" // storageGeomApplies (grey out N/A dims)
#include "ui/properties/xsectshapegeom.h"   // xsectGeomApplies (grey out N/A geoms)
#include "ui/properties/swmmnodepropertyadapter.h"
#include "ui/properties/swmmsubcatchpropertyadapter.h"
// Slice DA.2 — non-spatial Data Object adapters.
#include "ui/properties/swmmaquiferpropertyadapter.h"
#include "ui/properties/swmmcontrolrulepropertyadapter.h"
#include "ui/properties/swmmcurvepropertyadapter.h"
#include "ui/properties/swmmhydrographpropertyadapter.h"
#include "ui/properties/swmminletpropertyadapter.h"
#include "ui/properties/swmmlandusepropertyadapter.h"
#include "ui/properties/swmmlidcontrolpropertyadapter.h"
#include "ui/properties/swmmpatternpropertyadapter.h"
#include "ui/properties/swmmpollutantpropertyadapter.h"
#include "ui/properties/swmmraingagepropertyadapter.h"
#include "ui/properties/swmmsnowpackpropertyadapter.h"
#include "ui/properties/swmmstreetpropertyadapter.h"
#include "ui/properties/swmmtimeseriespropertyadapter.h"
#include "ui/properties/swmmtransectpropertyadapter.h"

#include <openswmm/engine/openswmm_gages.h> // swmm_gage_index (X/Y move route)
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_tables.h>
#include <openswmm/engine/openswmm_snow.h>
#include <openswmm/engine/openswmm_nodes.h>

// QPropertyModel library
#ifdef HAVE_QPROPERTYMODEL
#include <qpropertymodel.h>
#include "ui/properties/groupedpropertymodel.h"
#include <qpropertyitem.h>
#include <qpropertyitemdelegate.h>
#include <qvariantpropertyItem.h>
#endif

// 2026-05-29 — header "Open in <Editor>…" button reuses the object
// browser's open-for-edit dispatch so all three surfaces (browser
// double-click, browser right-click "Edit…", attribute-panel header
// button) share one editor instance.

#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTimer>
#include <QTreeView>
#include <QScopedValueRollback>
#include <QSet>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>

#include <cmath>

#ifdef HAVE_QPROPERTYMODEL
// ---------------------------------------------------------------------------
// Slice DA.4.3 — conditional row editability helpers
// ---------------------------------------------------------------------------
//
// We need to grey out the outfall stage-data rows whose property does NOT
// match the currently-selected outfall type (e.g. when type==TIDAL, hide
// editability on Stage Elev. and Stage Time Series rows). The framework
// already exposes per-row Qt::ItemFlags via QPropertyItem::setFlags() —
// the only missing piece is locating the right item by its raw Q_PROPERTY
// name (e.g. "outfallStage"), since the row's `name()` carries the
// human-readable label from the adapter's `displayLabelFor`.

namespace
{

/**
 * @param[in] model Optional borrowed acyclic tree model, null returns invalid.
 * @param[in] label Exact DisplayRole string to match, no trim/case folding.
 * @param[in] parent Starting parent in same model, default root.
 * @return First depth-first column-zero matching index or invalid; O(visited rows), recursively
 *   queries children with no depth/cycle guard. No model mutation or ownership transfer.
 */
QModelIndex findPropertyIndexByDisplayLabel(QAbstractItemModel *model, const QString &label,
                                            const QModelIndex &parent = {})
{
    if (!model)
        return {};
    const int n = model->rowCount(parent);
    for (int i = 0; i < n; ++i) {
        const QModelIndex idx = model->index(i, 0, parent);
        if (model->data(idx, Qt::DisplayRole).toString() == label)
            return idx;
        if (model->hasChildren(idx)) {
            const QModelIndex found = findPropertyIndexByDisplayLabel(model, label, idx);
            if (found.isValid())
                return found;
        }
    }
    return {};
}

/*! Toggle the editable/enabled flags of one property row, located by
 *  raw Q_PROPERTY name. Routes through the adapter's `displayLabelFor`
 *  to translate the name to the displayed label that QPropertyItem
 *  carries. No-op if the row isn't found (e.g. the property is not
 *  surfaced on the current node-kind subclass).
 * @param[in] pm Optional actual QPropertyModel whose internal pointers are QPropertyItem; null
 *   no-op.
 * @param[in] adapter Optional live QObject with invokable displayLabelFor, null no-op.
 * @param[in] rawProperty Exact raw Q_PROPERTY name; invoked label fallback is raw string if
 *   missing/empty.
 * @param[in] editable True ORs Enabled/Editable, false clears BOTH while preserving other item
 *   flags.
 * @note Find first matching display label recursively, unchecked static cast of model internal
 *   pointer to QPropertyItem, then set flags and synchronously invoke onDataChanged for name/value
 *   cells. Missing row/item no-op. No property value/engine write, lookup by label can collide;
 *   ignores invokeMethod status.
 */
void setRowEditable(QPropertyModel *pm, QObject *adapter, const QString &rawProperty, bool editable)
{
    if (!pm || !adapter)
        return;

    QString label;
    QMetaObject::invokeMethod(adapter, "displayLabelFor", Qt::DirectConnection,
                              Q_RETURN_ARG(QString, label), Q_ARG(QString, rawProperty));
    if (label.isEmpty())
        label = rawProperty;

    const QModelIndex idx = findPropertyIndexByDisplayLabel(pm, label);
    if (!idx.isValid())
        return;

    auto *item = static_cast<QPropertyItem *>(idx.internalPointer());
    if (!item)
        return;

    Qt::ItemFlags f = item->flags();
    if (editable)
        f |= (Qt::ItemIsEnabled | Qt::ItemIsEditable);
    else
        f &= ~(Qt::ItemIsEnabled | Qt::ItemIsEditable);
    item->setFlags(f);

    // Nudge the view to re-query flags + repaint the greyed cell.
    QMetaObject::invokeMethod(pm, "onDataChanged", Qt::DirectConnection, Q_ARG(QModelIndex, idx));
    const QModelIndex valIdx = pm->index(idx.row(), 1, idx.parent());
    QMetaObject::invokeMethod(pm, "onDataChanged", Qt::DirectConnection,
                              Q_ARG(QModelIndex, valIdx));
}

// QPropertyModel owns this tree just as it owns reflected QObject properties.
// Each leaf reads and commits through the same mesh field as the Attribute Table.
class MeshFieldProperty final : public QPropertyItem
{
public:
    MeshFieldProperty(MeshAttributeTableModel *source, const QModelIndex &field,
                      const openswmmvis::ColumnSpec &spec, QPropertyItem *parent)
        : QPropertyItem({}, source->headerData(field.column(), Qt::Horizontal).toString(), parent),
          m_source(source), m_field(field),
          m_editor(spec.editor == openswmmvis::EditorKind::Text
                       ? new QStyledItemDelegate(this)
                       : openswmmvis::createAttributeDelegate(spec, this))
    {
    }

    QVariant data(int column, Qt::ItemDataRole role) const override
    {
        if (column == Value)
            return m_field.data(role);
        return QPropertyItem::data(column, role);
    }
    bool setData(const QVariant &value, Qt::ItemDataRole role) override
    {
        return m_field.isValid() && m_source->setData(m_field, value, role);
    }
    Qt::ItemFlags flags() const override { return m_field.flags(); }
    QStyledItemDelegate *editor() const { return m_editor; }

private:
    MeshAttributeTableModel *m_source;
    QPersistentModelIndex m_field;
    QStyledItemDelegate *m_editor;
};

class MeshPropertyGroup final : public QPropertyItem
{
public:
    MeshPropertyGroup(const QString &label, QPropertyItem *parent)
        : QPropertyItem({}, label, parent)
    {
        setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    }
    QVariant data(int column, Qt::ItemDataRole role) const override
    {
        if (column == Property && role == Qt::FontRole) {
            QFont font;
            font.setBold(true);
            return font;
        }
        return QPropertyItem::data(column, role);
    }
    void add(QPropertyItem *field) { m_children.append(field); }
    bool hasChildren() override { return !m_children.isEmpty(); }
};

/*! Properties group of mesh field \p key; single-element and aggregate trees share it. */
QString meshGroupName(MeshAttributeTableModel::Kind kind, const QString &key)
{
    using Kind = MeshAttributeTableModel::Kind;
    if (key.startsWith("infil"))
        return PropertiesPanel::tr("Infiltration");
    if (key.startsWith("surface.") || key == "ownerSource")
        return PropertiesPanel::tr("Surface ownership");
    if (key.startsWith("gwBoundary."))
        return PropertiesPanel::tr("Aquifer boundary");
    if (key.startsWith("sqBoundary."))
        return PropertiesPanel::tr("Boundary inflow quality");
    if (key.startsWith("gw."))
        return PropertiesPanel::tr("Groundwater");
    if (key.startsWith("snow."))
        return PropertiesPanel::tr("Snow");
    if (key.startsWith("coupl"))
        return kind == Kind::Cell ? PropertiesPanel::tr("Coupled 1D nodes")
                                  : PropertiesPanel::tr("Node coupling");
    if (key == "mannings" || key == "initDepth" || key == "initHgl" || key == "initU" ||
        key == "initV" || key == "conveyance")
        return PropertiesPanel::tr("Hydraulics");
    if (kind == Kind::Edge && key != "Edge" && key != "Length" && key != "Boundary")
        return PropertiesPanel::tr("Boundary condition");
    return PropertiesPanel::tr("General");
}

/*! Aquifer boundary Edit…: opens the Edge boundaries page on \p rows' edges of the active
 *  mesh, which judges which of them can take a boundary (plan Phase 4). A switched-off
 *  groundwater process opens it read-only. */
QPushButton *aquiferBoundaryButton(PropertiesPanel *panel, QWidget *parent,
                                   MeshAttributeTableModel *source, SWMM2DMeshLayer *layer,
                                   const QList<int> &rows, SWMMModelLayer *model)
{
    QList<QPair<int, int>> edges;
    int boundary = 0;
    for (int row : rows) {
        int cell = -1, local = -1;
        QString ignored;
        if (!mesh::MeshObjectRef::parseEdge(source->refForRow(row), &ignored, &cell, &local))
            continue;
        edges.append({cell, local});
        boundary += layer->isBoundaryEdge(cell, local) ? 1 : 0;
    }
    auto *button = new QPushButton(PropertiesPanel::tr("Edit aquifer boundaries…"), parent);
    button->setObjectName("editMeshAquiferBoundary");
    const QPointer<SWMMModelLayer> owner(model);
    const auto update = [button, layer, owner, boundary] {
        button->setEnabled(owner && owner->engine() && layer->isActiveMesh() && boundary > 0);
    };
    update();
    button->setToolTip(PropertiesPanel::tr("Opens the aquifer Edge boundaries page on the "
                                           "selected boundary edges (%1) of the active mesh.")
                           .arg(boundary));
    QObject::connect(layer, &SWMM2DMeshLayer::activeMeshChanged, button, update);
    QObject::connect(button, &QPushButton::clicked, panel,
                     [panel, layer, edges] { emit panel->groundwaterEdgesRequested(layer, edges); });
    return button;
}

/*! Boundary inflow quality Edit… (Phase 4d): requests the Surface Quality Boundary inflow tab on
 *  the selected edges; needs the active mesh of an open model, a boundary edge in the selection
 *  and water quality switched on (plan §5). */
QPushButton *boundaryInflowButton(PropertiesPanel *panel, QWidget *parent,
                                  MeshAttributeTableModel *source, SWMM2DMeshLayer *layer,
                                  const QList<int> &rows, SWMMModelLayer *model)
{
    QList<QPair<int, int>> edges;
    int boundary = 0;
    for (int row : rows) {
        int cell = -1, local = -1;
        QString ignored;
        if (!mesh::MeshObjectRef::parseEdge(source->refForRow(row), &ignored, &cell, &local))
            continue;
        edges.append({cell, local});
        boundary += layer->isBoundaryEdge(cell, local) ? 1 : 0;
    }
    auto *button = new QPushButton(PropertiesPanel::tr("Edit boundary inflow quality…"), parent);
    button->setObjectName("editMeshBoundaryInflow");
    const QPointer<SWMMModelLayer> owner(model);
    const auto update = [button, layer, owner, boundary] {
        const QString locked = owner && owner->engine()
                                   ? openswmmvis::ui::processLockReason(
                                         owner->engine(), openswmmvis::ui::LockableProcess::SurfaceQuality)
                                   : QString();
        button->setEnabled(owner && owner->engine() && layer->isActiveMesh() && boundary > 0 &&
                           locked.isEmpty());
        button->setToolTip(locked.isEmpty()
                               ? PropertiesPanel::tr("Opens the Surface Quality Boundary inflow tab "
                                                     "on the selected boundary edges (%1) of the "
                                                     "active mesh.")
                                     .arg(boundary)
                               : locked);
    };
    update();
    QObject::connect(layer, &SWMM2DMeshLayer::activeMeshChanged, button, update);
    QObject::connect(button, &QPushButton::clicked, panel, [panel, layer, edges] {
        emit panel->surfaceQualityEdgesRequested(layer, edges);
    });
    return button;
}

/*! Groundwater Edit…: needs the active mesh of an open model whose 2D groundwater is not
 *  switched off (plan §5); re-gated when the active mesh or the options change. */
void gateGroundwaterButton(QPushButton *button, SWMM2DMeshLayer *layer, SWMMModelLayer *model)
{
    const QPointer<SWMMModelLayer> owner(model);
    const auto update = [button, layer, owner] {
        const SWMM_Engine engine = owner ? owner->engine() : nullptr;
        const QString locked = openswmmvis::ui::processLockReason(
            engine, openswmmvis::ui::LockableProcess::Groundwater2D);
        button->setEnabled(engine && layer->isActiveMesh() && locked.isEmpty());
        button->setToolTip(!locked.isEmpty()
                               ? locked
                               : PropertiesPanel::tr("Groundwater assignments require the "
                                                     "active mesh of an open model."));
    };
    update();
    QObject::connect(layer, &SWMM2DMeshLayer::activeMeshChanged, button, update);
    if (model)
        QObject::connect(model, &SWMMModelLayer::optionsChanged, button, update);
}

// Custom root constructor signature is the QPropertyModel registration contract.
class MeshPropertyRoot final : public QVariantPropertyItem
{
    Q_OBJECT
public:
    Q_INVOKABLE MeshPropertyRoot(const QVariant &value, const QMetaProperty &property,
                                 QObjectClassPropertyItem *parent)
        : QVariantPropertyItem(value, property, parent)
    {
        auto *source = value.value<MeshAttributeTableModel *>();
        const int row = source->focusedRow();
        const auto specs = source->columnSpecs();
        QHash<QString, MeshPropertyGroup *> groups;
        for (int column = 0; column < specs.size(); ++column) {
            const auto &spec = specs[column];
            const QString name = meshGroupName(source->kind(), spec.key);
            auto *group = groups.value(name);
            if (!group) {
                group = new MeshPropertyGroup(name, this);
                groups.insert(name, group);
                m_children.append(group);
            }
            group->add(new MeshFieldProperty(source, source->index(row, column), spec, group));
        }
    }
    bool hasChildren() override { return !m_children.isEmpty(); }
};

/// Value-column role: true while the field shows "Mixed" (targets differ).
constexpr int kMeshMixedRole = Qt::UserRole + 0x4D49;

/*! A field's own delegate, made safe for Mixed fields: opening and leaving the editor without a
 *  choice writes nothing (plan §9: Mixed is never a value or a default). Numeric editors already
 *  start blank and commit only when edited (NumericDelegate). */
class MeshAggregateFieldDelegate final : public QStyledItemDelegate
{
public:
    MeshAggregateFieldDelegate(QStyledItemDelegate *inner, QObject *parent)
        : QStyledItemDelegate(parent), m_inner(inner)
    {
    }
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override
    {
        return m_inner->createEditor(parent, option, index);
    }
    void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option,
                              const QModelIndex &index) const override
    {
        m_inner->updateEditorGeometry(editor, option, index);
    }
    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        m_inner->setEditorData(editor, index);
        const bool mixed = index.data(kMeshMixedRole).toBool();
        editor->setProperty("meshAggregateMixed", mixed);
        if (!mixed)
            return;
        const QString mixedText = PropertiesPanel::tr("Mixed");
        if (auto *combo = qobject_cast<QComboBox *>(editor)) {
            combo->setCurrentIndex(-1);
            combo->setPlaceholderText(mixedText);
        } else if (auto *line = qobject_cast<QLineEdit *>(editor)) {
            line->clear();
            line->setPlaceholderText(mixedText);
            line->setModified(false);
        } else if (auto *spin = qobject_cast<QAbstractSpinBox *>(editor)) {
            if (auto *text = spin->findChild<QLineEdit *>())
                text->setPlaceholderText(mixedText);
        } else if (auto *picker = qobject_cast<DataObjectPickerEditor *>(editor)) {
            // The picker signals only user choices, so any signal touches the field.
            picker->setProperty("meshAggregateTouched", false);
            QObject::connect(picker, &DataObjectPickerEditor::valueChanged, picker,
                             [picker] { picker->setProperty("meshAggregateTouched", true); });
        } else if (editor->property("attributeFileBrowse").toBool()) {
            if (auto *line = editor->findChild<QLineEdit *>()) {
                line->clear(); line->setPlaceholderText(mixedText); line->setModified(false);
            }
        }
        // Any other editor never writes a Mixed field (setModelData).
    }
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override
    {
        if (editor->property("meshAggregateMixed").toBool()) {
            if (qobject_cast<LinkEndpointComboBox *>(editor)) {
                if (!editor->property("meshAggregateTouched").toBool()) return;
            } else if (auto *combo = qobject_cast<QComboBox *>(editor)) {
                if (combo->currentIndex() < 0 && !(combo->isEditable() && combo->lineEdit()->isModified()))
                    return;
            } else if (auto *line = qobject_cast<QLineEdit *>(editor)) {
                if (!line->isModified())
                    return;
            } else if (editor->property("attributeFileBrowse").toBool()) {
                auto *line = editor->findChild<QLineEdit *>();
                if (!line || !line->isModified()) return;
            } else if (!qobject_cast<QAbstractSpinBox *>(editor) &&
                       !editor->property("meshAggregateTouched").toBool()) {
                return;
            }
        }
        m_inner->setModelData(editor, model, index);
    }

private:
    QStyledItemDelegate *m_inner;
};

/*! One shared field: a committed editor value updates every captured target immediately. */
template <typename Selection>
class AggregateFieldProperty final : public QPropertyItem
{
public:
    AggregateFieldProperty(Selection *selection, int column,
                               const openswmmvis::ColumnSpec &spec, QPropertyItem *parent)
        : QPropertyItem({}, selection->source()->headerData(column, Qt::Horizontal).toString(),
                        parent),
          m_selection(selection), m_column(column), m_spec(spec)
    {
        QStyledItemDelegate *inner = spec.editor == openswmmvis::EditorKind::Text
            ? new QStyledItemDelegate(this)
            : openswmmvis::createAttributeDelegate(spec, this);
        if (inner)
            m_editor = new MeshAggregateFieldDelegate(inner, this);
    }

    QVariant data(int column, Qt::ItemDataRole role) const override
    {
        if (column != Value)
            return role == Qt::ToolTipRole ? tooltip() : QPropertyItem::data(column, role);
        if constexpr(std::is_same_v<Selection,DataObjectAggregateSelection>) {
            if(m_spec.key=="records") {if(role==Qt::EditRole)return m_selection->editorData(m_column);if(role==Qt::DisplayRole)return PropertiesPanel::tr("Edit keyed records…");}
        }
        const auto field = m_selection->field(m_column);
        const bool staged = m_selection->isStaged(m_column);
        const bool mixed = field.mixed && !staged;
        switch (int(role)) {
        case kMeshMixedRole:
            return mixed;
        case openswmmvis::kLinkEndpointRole:
        case openswmmvis::kDisabledEnumValuesRole:
        case openswmmvis::kFileColumnOptionsRole:
            if constexpr (std::is_same_v<Selection, SWMMAggregateSelection>)
                return m_selection->editorData(m_column, role);
            return {};
        case Qt::DisplayRole:
            if (staged)
                return displayFor(m_selection->stagedValue(m_column));
            if constexpr (std::is_same_v<Selection, SWMMAggregateSelection>) {
                const auto ref = m_selection->editorData(m_column);
                if (ref.userType() == qMetaTypeId<UserFlagsEditRef>()) return ref.template value<UserFlagsEditRef>().summary;
                if (ref.userType() == qMetaTypeId<NodeCompoundEditRef>()) return ref.template value<NodeCompoundEditRef>().summary;
                if (ref.userType() == qMetaTypeId<UsageEditRef>())
                    return ref.template value<UsageEditRef>().summary;
                if (ref.userType() == qMetaTypeId<LinkCompoundEditRef>())
                    return ref.template value<LinkCompoundEditRef>().summary;
                if (ref.userType() == qMetaTypeId<SubcatchCompoundEditRef>())
                    return ref.template value<SubcatchCompoundEditRef>().summary;
            }
            return mixed ? QVariant(PropertiesPanel::tr("Mixed")) : field.display;
        case Qt::EditRole:
            if (staged)
                return m_selection->stagedValue(m_column);
            if constexpr (std::is_same_v<Selection, SWMMAggregateSelection>) {
                const auto ref = m_selection->editorData(m_column);
                if (ref.userType() == qMetaTypeId<UserFlagsEditRef>()) return ref;
                if (ref.userType() == qMetaTypeId<NodeCompoundEditRef>()) return ref;
                if (ref.userType() == qMetaTypeId<UsageEditRef>() ||
                    ref.userType() == qMetaTypeId<LinkCompoundEditRef>())
                    return ref;
                if (ref.userType() == qMetaTypeId<SubcatchCompoundEditRef>()) return ref;
            }
            return mixed ? mixedSeed() : field.edit;
        case Qt::FontRole:
            if (staged || mixed) {
                QFont font;
                font.setBold(staged);
                font.setItalic(mixed);
                return font;
            }
            return {};
        case Qt::ForegroundRole:
            if (mixed)
                return QApplication::palette().brush(QPalette::Disabled, QPalette::Text);
            return {};
        case Qt::ToolTipRole:
            return tooltip();
        default:
            return {};
        }
    }
    bool setData(const QVariant &value, Qt::ItemDataRole role) override
    {
        if constexpr (std::is_same_v<Selection, DataObjectAggregateSelection>) {
            // A queued schema rebuild must never redirect an old editor to a new column.
            const auto specs = m_selection->source()->columnSpecs();
            if (m_column >= specs.size() || specs[m_column].key != m_spec.key) return false;
        }
        return role == Qt::EditRole && m_selection->setValue(m_column, value);
    }
    Qt::ItemFlags flags() const override
    {
        Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
        if (m_spec.editor != openswmmvis::EditorKind::ReadOnly && editableOnAll())
            f |= Qt::ItemIsEditable;
        return f;
    }
    QStyledItemDelegate *editor() const { return m_editor; }

private:
    bool editableOnAll() const
    {
        const int targets = int(m_selection->rows().size());
        return targets > 0 && m_selection->field(m_column).editable == targets;
    }
    // What a Mixed field hands its editor: blank, never a default.
    QVariant mixedSeed() const
    {
        if constexpr (std::is_same_v<Selection, MeshAggregateSelection>) {
            if (m_spec.editor == openswmmvis::EditorKind::Compound)
                return m_selection->source()->pickerRef(m_spec.key, QString());
        }
        if constexpr (std::is_same_v<Selection, SWMMAggregateSelection>) {
            if (m_spec.editor == openswmmvis::EditorKind::Compound)
                return m_selection->editorData(m_column);
        }
        if (m_spec.editor == openswmmvis::EditorKind::Text)
            return QString();
        return {};
    }
    QVariant displayFor(const QVariant &value) const
    {
        if (value.userType() == qMetaTypeId<DataObjectRef>())
            return value.value<DataObjectRef>().currentName;
        for (const QVariant &choice : m_spec.enumValues) {
            const QVariantList pair = choice.toList();
            if (pair.size() == 2 && pair[1] == value)
                return pair[0];
        }
        return value;
    }
    QString tooltip() const
    {
        if constexpr (std::is_same_v<Selection, SWMMAggregateSelection>) {
            if (m_selection->editorData(m_column).userType() == qMetaTypeId<NodeCompoundEditRef>())
                return PropertiesPanel::tr("Open the editor to change one constituent or pollutant field across these nodes. Other fields and rows are preserved. Each committed field has one Undo step.");
            if (m_selection->editorData(m_column).userType() == qMetaTypeId<SubcatchCompoundEditRef>())
                return PropertiesPanel::tr("Open the editor to change one field across these subcatchments. Other fields and rows are preserved. Each committed value has one Undo step.");
        }
        const int targets = int(m_selection->rows().size());
        const auto field = m_selection->field(m_column);
        QStringList lines;
        if constexpr (std::is_same_v<Selection, SWMMAggregateSelection> || std::is_same_v<Selection, DataObjectAggregateSelection> || std::is_same_v<Selection, FeatureAggregateSelection>) {
            if (!field.reason.isEmpty()) lines << field.reason;
        }
        const QString header =
            m_selection->source()->headerData(m_column, Qt::Horizontal, Qt::ToolTipRole).toString();
        if (!header.isEmpty())
            lines << header;
        if (m_spec.editor != openswmmvis::EditorKind::ReadOnly && field.editable < targets)
            lines << PropertiesPanel::tr("Applies to %1 of the %2 selected elements, so it "
                                         "cannot be edited for all of them here.")
                         .arg(field.editable)
                         .arg(targets);
        else if (field.mixed)
            lines << PropertiesPanel::tr("The %1 selected elements hold different values. "
                                         "Committing a value replaces them all.").arg(targets);
        return lines.join(QLatin1Char('\n'));
    }

    Selection *m_selection;
    int m_column;
    openswmmvis::ColumnSpec m_spec;
    QStyledItemDelegate *m_editor = nullptr;
};
using FeatureAggregateFieldProperty = AggregateFieldProperty<FeatureAggregateSelection>;
using MeshAggregateFieldProperty = AggregateFieldProperty<MeshAggregateSelection>;
using SWMMAggregateFieldProperty = AggregateFieldProperty<SWMMAggregateSelection>;
using DataAggregateFieldProperty = AggregateFieldProperty<DataObjectAggregateSelection>;

// Registered for MeshAggregateSelection * like MeshPropertyRoot is for the single-element model.
class MeshAggregateRoot final : public QVariantPropertyItem
{
    Q_OBJECT
public:
    Q_INVOKABLE MeshAggregateRoot(const QVariant &value, const QMetaProperty &property,
                                  QObjectClassPropertyItem *parent)
        : QVariantPropertyItem(value, property, parent)
    {
        auto *selection = value.value<MeshAggregateSelection *>();
        const auto specs = selection->source()->columnSpecs();
        QHash<QString, MeshPropertyGroup *> groups;
        for (int column = 0; column < specs.size(); ++column) {
            const QString name = meshGroupName(selection->kind(), specs[column].key);
            auto *group = groups.value(name);
            if (!group) {
                group = new MeshPropertyGroup(name, this);
                groups.insert(name, group);
                m_children.append(group);
            }
            group->add(new MeshAggregateFieldProperty(selection, column, specs[column], group));
        }
    }
    bool hasChildren() override { return !m_children.isEmpty(); }
};

class SWMMAggregateRoot final : public QVariantPropertyItem
{
    Q_OBJECT
public:
    Q_INVOKABLE SWMMAggregateRoot(const QVariant &value, const QMetaProperty &property,
                                 QObjectClassPropertyItem *parent)
        : QVariantPropertyItem(value, property, parent)
    {
        auto *selection = value.value<SWMMAggregateSelection *>();
        auto *group = new MeshPropertyGroup(PropertiesPanel::tr("Shared attributes"), this);
        m_children.append(group);
        const auto specs = selection->source()->columnSpecs();
        for (int column = 0; column < specs.size(); ++column)
            if (SWMMAggregateSelection::supports(specs[column]))
                group->add(new SWMMAggregateFieldProperty(selection, column, specs[column], group));
    }
    bool hasChildren() override { return !m_children.isEmpty(); }
};

class DataAggregateRoot final : public QVariantPropertyItem
{
    Q_OBJECT
public:
    Q_INVOKABLE DataAggregateRoot(const QVariant &value, const QMetaProperty &property,
                                 QObjectClassPropertyItem *parent)
        : QVariantPropertyItem(value, property, parent)
    {
        auto *selection = value.value<DataObjectAggregateSelection *>();
        QMap<QString, MeshPropertyGroup *> groups;
        const auto specs = selection->source()->columnSpecs();
        for (int column = 0; column < specs.size(); ++column) {
            if (!DataObjectAggregateSelection::supports(specs[column])) continue;
            const bool lid = selection->source()->category() == SWMMModelLayer::DataLIDControls;
            if (lid || (selection->source()->category() == SWMMModelLayer::DataPatterns && specs[column].key.startsWith("factor/"))) {
                bool applies = false;
                for (int row : selection->rows()) applies |= selection->source()->cellApplies(row, column);
                if (!applies) continue;
            }
            const auto name = lid && specs[column].label.contains(" — ")
                ? specs[column].label.section(" — ", 0, 0) : PropertiesPanel::tr("Shared attributes");
            auto *group = groups.value(name);
            if (!group) { group = new MeshPropertyGroup(name, this); groups.insert(name, group); m_children.append(group); }
            group->add(new DataAggregateFieldProperty(selection, column, specs[column], group));
        }
    }
    bool hasChildren() override { return !m_children.isEmpty(); }
};

class FeatureAggregateRoot final:public QVariantPropertyItem {
    Q_OBJECT
public:
    Q_INVOKABLE FeatureAggregateRoot(const QVariant &value,const QMetaProperty &property,QObjectClassPropertyItem *parent):QVariantPropertyItem(value,property,parent) {
        auto *selection=value.value<FeatureAggregateSelection *>();auto *group=new MeshPropertyGroup(PropertiesPanel::tr("Feature attributes"),this);m_children.append(group);
        const auto specs=selection->source()->columnSpecs();for(int c=0;c<specs.size();++c)group->add(new FeatureAggregateFieldProperty(selection,c,specs[c],group));
    }
    bool hasChildren()override{return !m_children.isEmpty();}
};

QStyledItemDelegate *meshFieldEditor(const QModelIndex &index)
{
    if (index.column() != 1)
        return nullptr;
    auto *item = static_cast<QPropertyItem *>(index.internalPointer());
    if (auto *field = dynamic_cast<MeshFieldProperty *>(item))
        return field->editor();
    if (auto *field = dynamic_cast<MeshAggregateFieldProperty *>(item))
        return field->editor();
    if (auto *field = dynamic_cast<SWMMAggregateFieldProperty *>(item))
        return field->editor();
    if(auto *field=dynamic_cast<FeatureAggregateFieldProperty *>(item))return field->editor();
    if (auto *field = dynamic_cast<DataAggregateFieldProperty *>(item)) return field->editor();
    return nullptr;
}

/*! Feedback C7 — numeric editors step by an amount suited to the field.
 *  QPropertyItemDelegate hands every double the same 6-decimal, 0.1-step
 *  spin box, so Length and Manning's n stepped identically. The attribute
 *  table already knows each field's decimals, range and step (ColumnSpec);
 *  look the row's property up there and apply them. Rows the table does
 *  not describe keep the stock editor. */
class PropertyStepDelegate : public QPropertyItemDelegate
{
public:
    using QPropertyItemDelegate::QPropertyItemDelegate;

    /**
     * @param[in] parent Optional QWidget editor owner, forwarded to base; returned widget managed
     *   by host view.
     * @param[in] option Borrowed style option forwarded unchanged.
     * @param[in] index Current property model index with QPropertyItem-compatible internal pointer;
     *   source item must survive call.
     * @return Base-created editor or null; if direct/descendant double spin and
     *   QVariantPropertyItem raw property maps to ColumnSpec, apply decimals, range ONLY when both
     *   bounds finite, and columnStep. Missing matching type/spec preserves stock editor. No
     *   value/unit conversion, source write or local commit. Optional-library-only delegate, GUI
     *   thread.
     */
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override
    {
        if (auto *editor = meshFieldEditor(index)) {
            QWidget *widget = editor->createEditor(parent, option, index);
            if (auto *endpoint = qobject_cast<LinkEndpointComboBox *>(widget)) {
                auto *delegate = const_cast<PropertyStepDelegate *>(this);
                connect(endpoint, &LinkEndpointComboBox::valueChanged, delegate, [delegate, endpoint] {
                    endpoint->setProperty("meshAggregateTouched", true);
                    emit delegate->commitData(endpoint);
                });
            } else if (auto *combo = qobject_cast<QComboBox *>(widget)) {
                auto *delegate = const_cast<PropertyStepDelegate *>(this);
                connect(combo, &QComboBox::activated, delegate, [delegate, widget] {
                    emit delegate->commitData(widget);
                    emit delegate->closeEditor(widget);
                });
            } else if (auto *picker = qobject_cast<DataObjectPickerEditor *>(widget)) {
                auto *delegate = const_cast<PropertyStepDelegate *>(this);
                connect(picker, &DataObjectPickerEditor::valueChanged, delegate, [delegate, picker] {
                    picker->setProperty("meshAggregateTouched", true);
                    emit delegate->commitData(picker);
                });
            } else if (widget && widget->property("attributeFileBrowse").toBool()) {
                auto *delegate = const_cast<PropertyStepDelegate *>(this);
                if (auto *line = widget->findChild<QLineEdit *>())
                    connect(line, &QLineEdit::editingFinished, delegate, [delegate, widget] { emit delegate->commitData(widget); });
            }
            return widget;
        }
        QWidget *w = QPropertyItemDelegate::createEditor(parent, option, index);
        if (w) {
            if (auto *endpoint = w->findChild<LinkEndpointComboBox *>()) {
                auto *self = const_cast<PropertyStepDelegate *>(this);
                connect(endpoint, &LinkEndpointComboBox::valueChanged, self,
                        [self, w] { emit self->commitData(w); });
            }
        }
        // The library wraps the spin box in its own editor widget.
        auto *spin = qobject_cast<QDoubleSpinBox *>(w);
        if (!spin && w)
            spin = w->findChild<QDoubleSpinBox *>();
        if (!spin)
            return w;
        w->setProperty("propertyNumericTouched", false);
        spin->setKeyboardTracking(false);
        connect(spin, &QDoubleSpinBox::valueChanged, w,
                [w] { w->setProperty("propertyNumericTouched", true); });
        if (auto *line = spin->findChild<QLineEdit *>())
            connect(line, &QLineEdit::textEdited, w,
                    [w] { w->setProperty("propertyNumericTouched", true); });
        // Column 0 shows the adapter's display label ("Length (ft)"), so the
        // raw Q_PROPERTY name comes from the item's meta-property.
        auto *item = static_cast<QPropertyItem *>(index.internalPointer());
        auto *varItem = dynamic_cast<QVariantPropertyItem *>(item);
        if (!varItem)
            return w;
        const QString property = QString::fromLatin1(varItem->metaProperty().name());
        const auto spec = openswmmvis::columnSpecForProperty(property);
        if (!spec)
            return w;
        spin->setDecimals(spec->decimals);
        if (std::isfinite(spec->minValue) && std::isfinite(spec->maxValue))
            spin->setRange(spec->minValue, spec->maxValue);
        spin->setSingleStep(openswmmvis::columnStep(*spec));
        return w;
    }
    void setEditorData(QWidget *widget, const QModelIndex &index) const override
    {
        if (auto *editor = meshFieldEditor(index))
            editor->setEditorData(widget, index);
        else {
            QPropertyItemDelegate::setEditorData(widget, index);
            if (widget->property("propertyNumericTouched").isValid())
                widget->setProperty("propertyNumericTouched", false);
        }
    }
    void setModelData(QWidget *widget, QAbstractItemModel *model,
                      const QModelIndex &index) const override
    {
        if (auto *editor = meshFieldEditor(index))
            editor->setModelData(widget, model, index);
        else {
            if (widget->property("propertyNumericTouched").isValid()) {
                if (!widget->property("propertyNumericTouched").toBool())
                    return;
                auto *spin = qobject_cast<QDoubleSpinBox *>(widget);
                if (!spin) spin = widget->findChild<QDoubleSpinBox *>();
                if (spin) spin->interpretText();
            }
            QPropertyItemDelegate::setModelData(widget, model, index);
        }
    }
};

} // namespace
#endif // HAVE_QPROPERTYMODEL

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

PropertiesPanel::PropertiesPanel(QWidget *parent) : QDockWidget(tr("Properties"), parent)
{
    qApp->installEventFilter(this);
    setupUi();
}

PropertiesPanel::~PropertiesPanel()
{
    qApp->removeEventFilter(this);
}

bool PropertiesPanel::eventFilter(QObject *watched, QEvent *event)
{
    auto *watchedWidget = qobject_cast<QWidget *>(watched);
    QWidget *view = m_treeView;
    if (!view || !watchedWidget)
        return QDockWidget::eventFilter(watched, event);

    const bool isTreeOrDescendant =
        (watchedWidget == view) || view->isAncestorOf(watchedWidget);
    if (!isTreeOrDescendant)
        return QDockWidget::eventFilter(watched, event);

    if (event->type() == QEvent::ShortcutOverride) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            // Keep Enter local to property editing; don't let global shortcuts react.
            event->accept();
            return true;
        }
    }

    if (!m_meshFields && !m_meshAggregate && !m_dataAggregate && !m_featureAggregate && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            // Let delegates commit first, then restore focus to the tree.
            QTimer::singleShot(0, view, [view] { view->setFocus(Qt::OtherFocusReason); });
        }
    }

    return QDockWidget::eventFilter(watched, event);
}

// ---------------------------------------------------------------------------
// UI setup
// ---------------------------------------------------------------------------

void PropertiesPanel::setupUi()
{
    auto *central = new QWidget(this);
    auto *vlay = new QVBoxLayout(central);
    vlay->setContentsMargins(2, 2, 2, 2);
    vlay->setSpacing(4);

    // Layer selector row
    auto *hlay = new QHBoxLayout;
    auto *layerLabel = new QLabel(tr("Layer:"), central);
    layerLabel->setObjectName("propertiesLayerLabel");
    hlay->addWidget(layerLabel);
    m_layerCombo = new QComboBox(central);
    m_layerCombo->setObjectName("propertiesLayerSelector");
    hlay->addWidget(m_layerCombo, 1);
    vlay->addLayout(hlay);
    m_selectionScope = new QComboBox(central);
    m_selectionScope->setObjectName("propertiesSelectionScope");
    m_selectionScope->hide();
    vlay->addWidget(m_selectionScope);

    // 2026-05-29 — "Open in <Editor>…" header button. Visible only when
    // the panel is bound to a non-spatial data object (TS, Curve,
    // Pattern, Hydrograph, Transect, Control) with a shipped CRUD
    // editor. Hidden in every other state (identify, layer props,
    // spatial selection, cleared) so it doesn't add noise to the panel
    // when there's nothing to open. Click dispatches through the
    // object-browser helper so a single editor instance is reused
    // across all three entry points (browser double-click / right-click
    // / attribute-panel button).
    m_openEditorButton = new QPushButton(central);
    m_openEditorButton->setIcon(QIcon(QStringLiteral(":/swmmvis/Layers")));
    m_openEditorButton->setToolTip(tr("Open this object in its dedicated editor."));
    m_openEditorButton->setAccessibleName(tr("Open in editor"));
    m_openEditorButton->hide();
    vlay->addWidget(m_openEditorButton);
    connect(m_openEditorButton, &QPushButton::clicked, this,
            &PropertiesPanel::onOpenInEditorClicked);

    // Rain gage extra: plot/compare rainfall series. Visible only while a
    // rain gage adapter is bound (see showObject's gage block).
    m_plotRainGageButton = new QPushButton(tr("Plot Rainfall…"), central);
    m_plotRainGageButton->setIcon(QIcon(QStringLiteral(":/swmmvis/Chart")));
    m_plotRainGageButton->setToolTip(tr("Visualize and compare rain gage rainfall series."));
    m_plotRainGageButton->hide();
    vlay->addWidget(m_plotRainGageButton);
    connect(m_plotRainGageButton, &QPushButton::clicked, this, [this] {
        // Carry the bound gage so the dialog opens focused on it.
        emit rainfallPlotRequested(m_dataAdapter ? m_dataAdapter->name() : QString());
    });

    // Slice QA.2 — Stats source row. Hidden by default; surfaces only
    // when the bound registry has at least one loaded output and the
    // current adapter exposes stat rows (today: SWMMNodePropertyAdapter
    // and its subclasses). The combo carries the OutputIdentity's
    // stableId as the per-item user data (QUuid stored as QVariant).
    // The first item is always the "(editing engine)" sentinel mapped
    // to the null UUID — picking it restores today's behaviour.
    auto *statsHlay = new QHBoxLayout;
    m_statsSourceLabel = new QLabel(tr("Stats source:"), central);
    m_statsSourceCombo = new QComboBox(central);
    m_statsSourceCombo->setToolTip(
        tr("Pick which loaded output's post-run statistics drive the "
           "node summary rows below. (editing engine) reads the ambient "
           "stats from the engine that produced the most recent in-process "
           "run."));
    statsHlay->addWidget(m_statsSourceLabel);
    statsHlay->addWidget(m_statsSourceCombo, 1);
    vlay->addLayout(statsHlay);
    // Hidden until setStatsRegistry plugs in a non-null registry that
    // actually has loaded outputs; node-selection logic re-evaluates.
    m_statsSourceLabel->hide();
    m_statsSourceCombo->hide();
    connect(m_statsSourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { applyStatsSourceToAdapter(); });

    // Reports a rejected shared-property edit.
    m_meshNote = new QLabel(central);
    m_meshNote->setObjectName(QStringLiteral("meshAggregateNote"));
    m_meshNote->setWordWrap(true);
    m_meshNote->hide();
    vlay->addWidget(m_meshNote);

    // Tree view
    m_treeView = new QTreeView(central);
    m_treeView->setAlternatingRowColors(true);
    m_treeView->setEditTriggers(QAbstractItemView::AllEditTriggers);
    connect(m_treeView, &QTreeView::clicked, this, [this](const QModelIndex &index) {
        if ((m_meshFields || m_meshAggregate || m_swmmAggregate || m_dataAggregate || m_featureAggregate) && index.column() == 1)
            openswmmvis::editClickedMeshField(m_treeView, index);
    });
    vlay->addWidget(m_treeView, 1);

    // Identify which objects a committed field value will update.
    m_aggregateBar = new QWidget(central);
    m_aggregateBar->setObjectName(QStringLiteral("meshAggregateBar"));
    auto *barLayout = new QHBoxLayout(m_aggregateBar);
    barLayout->setContentsMargins(0, 0, 0, 0);
    m_aggregateSummary = new QLabel(m_aggregateBar);
    m_aggregateSummary->setObjectName(QStringLiteral("meshAggregateSummary"));
    m_aggregateSummary->setWordWrap(true);
    barLayout->addWidget(m_aggregateSummary, 1);
    m_aggregateBar->hide();
    vlay->addWidget(m_aggregateBar);

    setWidget(central);

    // Property model / delegate
#ifdef HAVE_QPROPERTYMODEL
    auto *properties = new QPropertyModel(this);
    properties->registerCustomPropertyItemType(qMetaTypeId<DataObjectAggregateSelection *>(), &DataAggregateRoot::staticMetaObject);
    properties->registerCustomPropertyItemType(qMetaTypeId<FeatureAggregateSelection *>(), &FeatureAggregateRoot::staticMetaObject);
    properties->registerCustomPropertyItemType(qMetaTypeId<SWMMAggregateSelection *>(),
                                                  &SWMMAggregateRoot::staticMetaObject);
    properties->registerCustomPropertyItemType(qMetaTypeId<MeshAttributeTableModel *>(),
                                               &MeshPropertyRoot::staticMetaObject);
    properties->registerCustomPropertyItemType(qMetaTypeId<MeshAggregateSelection *>(),
                                               &MeshAggregateRoot::staticMetaObject);
    m_model = properties;
    auto *delegate = new PropertyStepDelegate(this); // feedback C7
    m_delegate = delegate;
    registerLinkEndpointRefConverter();
    delegate->registerCustomTypeEditorCreator(
        static_cast<QMetaType::Type>(qMetaTypeId<LinkEndpointRef>()),
        new QStandardItemEditorCreator<LinkEndpointComboBox>());

    // Slice DB.2 — register the custom editor creator for the node
    // compound-attribute metatype so QPropertyItemDelegate hands out
    // a `NodeCompoundEditButton` whenever a row of that type enters
    // edit mode. The display-side converter (cell text before the
    // user clicks) is registered separately because QMetaType::
    // registerConverter is global state.
    qRegisterMetaType<NodeCompoundEditRef>("NodeCompoundEditRef");
    registerNodeCompoundEditRefConverter();
    delegate->registerCustomTypeEditorCreator(
        QMetaType::Type(qMetaTypeId<NodeCompoundEditRef>()),
        new QStandardItemEditorCreator<NodeCompoundEditButton>());

    // Phase 3 — same dance for the subcatchment-side compound-edit ref
    // (land-use coverage / groundwater / LID usage cells).
    qRegisterMetaType<UsageEditRef>("UsageEditRef");
    registerUsageEditRefConverter();
    delegate->registerCustomTypeEditorCreator(QMetaType::Type(qMetaTypeId<UsageEditRef>()),
                                              new QStandardItemEditorCreator<UsageEditButton>());
    qRegisterMetaType<SubcatchCompoundEditRef>("SubcatchCompoundEditRef");
    registerSubcatchCompoundEditRefConverter();
    delegate->registerCustomTypeEditorCreator(
        QMetaType::Type(qMetaTypeId<SubcatchCompoundEditRef>()),
        new QStandardItemEditorCreator<SubcatchCompoundEditButton>());

    // Slice SC.1 — same dance for the link-side compound-edit ref
    // (cross section / inlet usage cells on conduits).
    qRegisterMetaType<LinkCompoundEditRef>("LinkCompoundEditRef");
    registerLinkCompoundEditRefConverter();
    delegate->registerCustomTypeEditorCreator(
        QMetaType::Type(qMetaTypeId<LinkCompoundEditRef>()),
        new QStandardItemEditorCreator<LinkCompoundEditButton>());

    // ATTRIBUTE_EDITOR_WIRING Phase 0 (2026-06-04) — culvert code left
    // the compound dialog and became an inline combobox. The converter
    // renders the descriptive HDS-5 label in the read-only cell; the
    // editor creator hands out a CulvertCodeComboBox in edit mode.
    qRegisterMetaType<CulvertCodeRef>("CulvertCodeRef");
    registerCulvertCodeRefConverter();
    delegate->registerCustomTypeEditorCreator(
        QMetaType::Type(qMetaTypeId<CulvertCodeRef>()),
        new QStandardItemEditorCreator<CulvertCodeComboBox>());

    // DA.2 parity — rain gage recording interval edits as an H:MM clock
    // combo. Converter renders the H:MM label in the read-only cell; the
    // editor creator hands out an editable RainIntervalComboBox.
    qRegisterMetaType<RainIntervalRef>("RainIntervalRef");
    registerRainIntervalRefConverter();
    delegate->registerCustomTypeEditorCreator(
        QMetaType::Type(qMetaTypeId<RainIntervalRef>()),
        new QStandardItemEditorCreator<RainIntervalComboBox>());

    // Slice DA.4.3 — same dance for `DataObjectRef`, used by the new
    // outfall stage-data picker rows (Tidal Curve, Stage Time Series).
    // Display-side converter renders the picked object name in the cell;
    // edit-side creator gives the user a filtered combobox + "…" button.
    qRegisterMetaType<DataObjectRef>("DataObjectRef");
    registerDataObjectRefConverter();
    delegate->registerCustomTypeEditorCreator(
        QMetaType::Type(qMetaTypeId<DataObjectRef>()),
        new QStandardItemEditorCreator<DataObjectPickerEditor>());

    // Phase 4 of docs/USER_FLAGS_UI_PLAN_2026-06-03.md — same dance for
    // the per-object "User Flags" row (UserFlagsEditRef): converter shows
    // the "n of m set" summary, editor creator hands out the button that
    // opens UserFlagValuesDialog.
    qRegisterMetaType<UserFlagsEditRef>("UserFlagsEditRef");
    registerUserFlagsEditRefConverter();
    delegate->registerCustomTypeEditorCreator(
        QMetaType::Type(qMetaTypeId<UserFlagsEditRef>()),
        new QStandardItemEditorCreator<UserFlagsEditButton>());

    // Initial-quality UI round — same dance for the per-element
    // "Initial Quality" row (InitialQualityEditRef): converter shows the
    // "n set" summary, editor creator hands out the button that opens
    // InitialQualityDialog scoped to the element.
    qRegisterMetaType<InitialQualityEditRef>("InitialQualityEditRef");
    registerInitialQualityEditRefConverter();
    delegate->registerCustomTypeEditorCreator(
        QMetaType::Type(qMetaTypeId<InitialQualityEditRef>()),
        new QStandardItemEditorCreator<InitialQualityEditButton>());
#else
    m_model = new QStandardItemModel(this);
#endif

#ifdef HAVE_QPROPERTYMODEL
    auto *grouped = new GroupedPropertyModel(this);
    grouped->setSourceModel(m_model);
    m_treeView->setModel(grouped);
#else
    m_treeView->setModel(m_model);
#endif
    if (m_delegate)
        m_treeView->setItemDelegate(m_delegate);
    m_treeView->header()->setDefaultSectionSize(150);
    // The tree already scrolls vertically; the only squish on a narrow dock is
    // horizontal column collapse. A minimum section size keeps the name/value
    // columns readable and lets the tree's own horizontal scrollbar take over.
    m_treeView->header()->setMinimumSectionSize(90);
    m_treeView->setColumnWidth(0, 300);

    // Slice BM.0-Browse-Edit (2026-05-25) — right-click on a complex-typed
    // property row offers an "Edit in <editor>…" shortcut to the
    // comprehensive editor for that data category (DataObjectRef rows) or
    // a single "Edit…" for compound-attribute rows (NodeCompoundEditRef).
    m_treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_treeView, &QWidget::customContextMenuRequested, this,
            &PropertiesPanel::onTreeContextMenu);

    connect(m_layerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &PropertiesPanel::onLayerComboIndexChanged);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void PropertiesPanel::showObject(QObject *object, const QString &title)
{
    releaseMeshProperties();
    setWindowTitle(title.isEmpty() ? tr("Properties") : title);
    m_layerCombo->clear();
    m_lastResults.clear();
    // 2026-05-29 — leaving the data-object surface; drop the "Open in
    // <Editor>…" header button so it doesn't leak into layer-property or
    // generic-QObject views.
    m_dataObjectCategory = SWMMModelLayer::NumDataCategories;
    if (m_openEditorButton)
        m_openEditorButton->hide();
    if (m_plotRainGageButton)
        m_plotRainGageButton->hide();

#ifdef HAVE_QPROPERTYMODEL
    auto *pm = qobject_cast<QPropertyModel *>(m_model);
    if (!object) {
        if (pm)
            pm->clear();
        return;
    }
    if (pm)
        pm->setData(QVariant::fromValue(object));
#else
    Q_UNUSED(object);
#endif
    m_treeView->expandAll();
}

void PropertiesPanel::releaseMeshProperties()
{
    if(m_sourceRelationships){m_sourceRelationships->deleteLater();m_sourceRelationships=nullptr;}
    if(m_featureAggregate){m_featureAggregate->source()->valid=false;m_featureAggregate->deleteLater();m_featureAggregate=nullptr;}
    if (m_dataAggregate) { m_dataAggregate->invalidate(); m_dataAggregate->disconnect(this); m_dataAggregate->deleteLater(); m_dataAggregate = nullptr; }
    if (m_selectionScope) m_selectionScope->hide();
    if (m_meshFields) {
        m_meshFields->deleteLater();
        m_meshFields = nullptr;
        m_treeView->setEditTriggers(QAbstractItemView::AllEditTriggers);
    }
    if (m_swmmAggregate) {
        m_swmmAggregate->disconnect(this);
        m_swmmAggregate->deleteLater();
        m_swmmAggregate = nullptr;
    }
    if (m_meshAggregate) {
        m_meshAggregate->disconnect(this);
        m_meshAggregate->source()->disconnect(this);
        m_meshAggregate->deleteLater();
        m_meshAggregate = nullptr;
        m_treeView->setEditTriggers(QAbstractItemView::AllEditTriggers);
    }
    if (m_aggregateBar)
        m_aggregateBar->hide();
    if (m_meshNote)
        m_meshNote->hide();
}

namespace
{
/*! "3 cells" / "1 edge" for a mesh element count. */
QString meshCountText(MeshAttributeTableModel::Kind kind, int n)
{
    using Kind = MeshAttributeTableModel::Kind;
    switch (kind) {
    case Kind::Vertex:
        return n == 1 ? PropertiesPanel::tr("1 vertex") : PropertiesPanel::tr("%1 vertices").arg(n);
    case Kind::Edge:
        return n == 1 ? PropertiesPanel::tr("1 edge") : PropertiesPanel::tr("%1 edges").arg(n);
    case Kind::Cell:
        break;
    }
    return n == 1 ? PropertiesPanel::tr("1 cell") : PropertiesPanel::tr("%1 cells").arg(n);
}
} // namespace

void PropertiesPanel::updateAggregateBar()
{
    if ((!m_meshAggregate && !m_swmmAggregate && !m_dataAggregate) || !m_aggregateBar)
        return;
    const int targets = m_meshAggregate ? m_meshAggregate->rows().size() : m_dataAggregate ? m_dataAggregate->rows().size() : m_swmmAggregate->rows().size();
    const int gone = m_meshAggregate ? m_meshAggregate->unresolvedCount() : m_dataAggregate ? m_dataAggregate->unresolvedCount() : m_swmmAggregate->unresolvedCount();
    QStringList parts{m_meshAggregate ? meshCountText(m_meshAggregate->kind(), targets)
                                    : tr("%1 objects").arg(targets)};
    if (gone)
        parts << tr("%1 no longer exist").arg(gone);
    parts << tr("Edits update all selected objects immediately");
    m_aggregateSummary->setText(parts.join(QStringLiteral(" · ")));
    m_aggregateBar->show();
}

void PropertiesPanel::showAggregateEditResult(const QString &error)
{
    m_meshNote->setText(error);
    m_meshNote->setVisible(!error.isEmpty());
}

openswmmvis::properties::FieldWriter PropertiesPanel::spatialFieldWriter(int category)
{
    using openswmmvis::properties::FieldWrite;
    return [layer = QPointer<SWMMModelLayer>(m_swmmLayer), self = QPointer<PropertiesPanel>(this),
            category](const QString &name, const QString &key, const QVariant &value) {
        if (!layer || !layer->editCanvas())
            return FieldWrite::NotRouted;
        SWMMAttributeTableModel table;
        table.setSource(layer, static_cast<SWMMModelLayer::Category>(category));
        if (!table.columnKeys().contains(key))
            return FieldWrite::NotRouted;
        QString error;
        const bool ok = table.applyScalarEdits(QStringList{name}, {{key, value}},
                                               layer->editCanvas()->undoStack(), &error);
        if (self)
            self->showAggregateEditResult(
                ok ? QString() : (error.isEmpty() ? tr("The edit was not applied.") : error));
        return ok ? FieldWrite::Applied : FieldWrite::Refused;
    };
}

void PropertiesPanel::bindDataAggregate(SWMMModelLayer *layer, int category,
                                        const QStringList &names, QUndoStack *undo)
{
#ifdef HAVE_QPROPERTYMODEL
    if (m_dataAggregate) {
        m_dataAggregate->invalidate();
        m_dataAggregate->deleteLater();
    }
    auto *aggregate = new DataObjectAggregateSelection(
        layer, SWMMModelLayer::DataCategory(category), names, undo, this);
    m_dataAggregate = aggregate;
    auto *pm = qobject_cast<QPropertyModel *>(m_model);
    pm->setData(QVariant::fromValue(aggregate));
    m_treeView->expandAll();
    connect(aggregate, &DataObjectAggregateSelection::editFinished, this,
            &PropertiesPanel::showAggregateEditResult);
    if (category == SWMMModelLayer::DataLIDControls)
        connect(aggregate, &DataObjectAggregateSelection::stagedChanged, aggregate, [this, aggregate, pm] {
            if (m_dataAggregate != aggregate) return;
            pm->setData(QVariant::fromValue(aggregate));
            m_treeView->expandAll();
        }, Qt::QueuedConnection);
    connect(aggregate->source(), &QAbstractItemModel::modelReset, aggregate, [this, aggregate, pm] {
        if (m_dataAggregate != aggregate) return;
        pm->setData(QVariant::fromValue(aggregate));
        m_treeView->expandAll();
    }, Qt::QueuedConnection);
    connect(aggregate, &DataObjectAggregateSelection::stagedChanged, this, [this, aggregate, pm] {
        if (m_dataAggregate == aggregate) {
            if (m_dataAdapter && m_dataObjectCategory == aggregate->source()->category() &&
                aggregate->names().size() == 1) {
                const auto name = aggregate->names().first();
                m_dataAdapter->updateStoredName(name);
                setWindowTitle(tr("Properties — %1").arg(name));
                if (m_openEditorButton) {
                    const auto title =
                        ComprehensiveEditorRegistry::instance().editorTitle(m_dataObjectCategory);
                    m_openEditorButton->setText(
                        tr("Open \"%1\" in %2…").arg(name, title.isEmpty() ? tr("Editor") : title));
                }
            }
            pm->refreshValues();
            updateAggregateBar();
        }
    });
    connect(layer, &SWMMModelLayer::engineAboutToClose, aggregate, [this, aggregate] {
        if (m_dataAggregate == aggregate)
            clear();
    });
    updateAggregateBar();
#else
    Q_UNUSED(layer);
    Q_UNUSED(category);
    Q_UNUSED(names);
    Q_UNUSED(undo);
#endif
}

void PropertiesPanel::showSelection(MapCanvas *canvas, const QList<SWMMObjectRef> &refs)
{
    clear();
    struct Scope {
        QString title;
        QList<SWMMObjectRef> refs;
        int category = -1;
        int dataCategory = -1;
        QMap<SWMMModelLayer::Category, QStringList> targets;
        QPointer<FeatureLayer> featureLayer;
        QVector<qint64> featureIds;
        QPointer<TimeSeriesLayer> observedLayer;
        QStringList observedIds;
    };
    QMap<QString, Scope> scopes;
    const QStringList labels{tr("Junctions"), tr("Outfalls"), tr("Storage"), tr("Dividers"),
                             tr("Conduits"), tr("Pumps"), tr("Orifices"), tr("Weirs"),
                             tr("Outlets"), tr("Subcatchments"), tr("Rain gages")};
    QMap<int, QHash<QString, int>> categories;
    if (m_swmmLayer)
        for (int c = 0; c < SWMMModelLayer::NumCategories; ++c) {
            const auto category = static_cast<SWMMModelLayer::Category>(c);
            for (int kind : {int(SWMMObjectRef::Node), int(SWMMObjectRef::Link),
                             int(SWMMObjectRef::Subcatchment), int(SWMMObjectRef::RainGage)})
                if (SWMMModelLayer::kindBitForCategory(category) & SWMMModelLayer::kindMaskForObjectType(kind))
                    for (int row = 0; row < m_swmmLayer->categoryCount(category); ++row)
                        categories[kind].insert(m_swmmLayer->objectNameAt(category, row), c);
        }
    QSet<SWMMObjectRef> seen;
    for (const auto &ref : refs) {
        if (seen.contains(ref)) continue;
        seen.insert(ref);
        QString meshKey;
        int element = -1, local = -1;
        const bool mesh = mesh::MeshObjectRef::parseVertex(ref, &meshKey, &element) ||
            mesh::MeshObjectRef::parseCell(ref, &meshKey, &element) ||
            mesh::MeshObjectRef::parseEdge(ref, &meshKey, &element, &local);
        if (mesh) {
            const QString key = QStringLiteral("mesh/%1/%2").arg(meshKey).arg(int(ref.objectType));
            auto &scope = scopes[key];
            scope.title = ref.objectType == SWMMObjectRef::MeshEdge ? tr("Mesh edges")
                : ref.objectType == SWMMObjectRef::MeshCell ? tr("Mesh cells") : tr("Mesh vertices");
            if (canvas)
                for (auto *layer : canvas->layers())
                    if (auto *m = qobject_cast<SWMM2DMeshLayer *>(layer);
                        m && mesh::MeshObjectRef::layerKey(m->sourcePath()) == meshKey)
                        scope.title += QStringLiteral(" — ") + m->name();
            scope.refs.append(ref);
            continue;
        }
        QString featureLayerId;long long featureId=-1;
        if(GisObjectRef::parseFeature(ref,&featureLayerId,&featureId)) {
            auto &scope=scopes["features/"+featureLayerId];scope.title=tr("Feature attributes");scope.refs<<ref;scope.featureIds<<featureId;
            if(canvas)for(auto *layer:canvas->layers())if(layer->layerId()==featureLayerId) {scope.title+=QString(" — %1").arg(layer->name());scope.featureLayer=qobject_cast<FeatureLayer *>(layer);break;}
            continue;
        }
        if(ref.objectType==SWMMObjectRef::ObservedSeries) {
            bool found=false;if(canvas)for(auto *l:canvas->layers())if(auto *observed=qobject_cast<TimeSeriesLayer *>(l)) {
                const auto id=observed->seriesIdForRef(ref);if(id.isEmpty())continue;auto &scope=scopes["observed/"+observed->layerId()];scope.title=tr("Observed series — %1").arg(observed->name());scope.refs<<ref;scope.observedLayer=observed;scope.observedIds<<id;found=true;break;
            }
            if(!found){auto &scope=scopes["observed-missing"];scope.title=tr("Unavailable observed series");scope.refs<<ref;}continue;
        }
        const auto dataCategory = DataObjectAttributeTableModel::categoryForType(ref.objectType);
        if (dataCategory != SWMMModelLayer::NumDataCategories) {
            auto &scope = scopes[QString("data/%1").arg(int(dataCategory))];
            scope.dataCategory = dataCategory;
            switch (dataCategory) {
            case SWMMModelLayer::DataCurves:scope.title=tr("Curves");break;
            case SWMMModelLayer::DataTimeSeries:scope.title=tr("Model time series");break;
            case SWMMModelLayer::DataHydrographs:scope.title=tr("Hydrographs");break;
            case SWMMModelLayer::DataControls:scope.title=tr("Control rules");break;
            case SWMMModelLayer::DataLIDControls: scope.title = tr("LID controls"); break;
            case SWMMModelLayer::DataSnowpacks: scope.title = tr("Snowpacks"); break;
            case SWMMModelLayer::DataPatterns: scope.title = tr("Time patterns"); break;
            case SWMMModelLayer::DataTransects: scope.title = tr("Transects"); break;
            case SWMMModelLayer::DataPollutants: scope.title = tr("Pollutants"); break;
            case SWMMModelLayer::DataLandUses: scope.title = tr("Land uses"); break;
            case SWMMModelLayer::DataAquifers: scope.title = tr("Aquifers"); break;
            case SWMMModelLayer::DataStreets: scope.title = tr("Streets"); break;
            case SWMMModelLayer::DataInlets: scope.title = tr("Inlet designs"); break;
            default: break;
            }
            scope.refs << ref; continue;
        }
        const QMap<int, QString> pendingData{{SWMMObjectRef::Curve, tr("Curves")}, {SWMMObjectRef::TimeSeries, tr("Model time series")},
            {SWMMObjectRef::Control, tr("Control rules")},
            {SWMMObjectRef::Hydrograph, tr("Hydrographs")}};
        if (pendingData.contains(ref.objectType)) {
            auto &scope = scopes[QString("pending-data/%1").arg(int(ref.objectType))];
            scope.title = pendingData.value(ref.objectType); scope.refs << ref; continue;
        }
        const int category = categories.value(int(ref.objectType)).value(ref.name, -1);
        const QString key = QStringLiteral("objects/%1").arg(category);
        auto &scope = scopes[key];
        scope.category = category;
        scope.title = category >= 0 ? labels.value(category, tr("Objects")) : tr("Other objects");
        scope.refs.append(ref);
        if (category >= 0)
            scope.targets[static_cast<SWMMModelLayer::Category>(category)].append(ref.name);
    }
    QList<Scope> commonScopes;
    Scope spatial;
    spatial.title = tr("Model objects — common properties");
    QSet<int> spatialKinds;
    for (const auto &scope : scopes) if (scope.category >= 0) {
        spatial.refs.append(scope.refs);
        for (auto it = scope.targets.cbegin(); it != scope.targets.cend(); ++it) {
            spatial.targets.insert(it.key(), it.value());
            spatialKinds.insert(SWMMModelLayer::kindBitForCategory(it.key()));
        }
    }
    if (spatialKinds.size() > 1) commonScopes.append(spatial);
    for (const auto kind : {SWMMObjectRef::Node, SWMMObjectRef::Link}) {
        Scope common;
        common.title = kind == SWMMObjectRef::Node ? tr("Nodes — common properties") : tr("Links — common properties");
        for (const auto &scope : scopes)
            if (!scope.refs.isEmpty() && scope.refs.first().objectType == kind && scope.category >= 0) {
                common.refs.append(scope.refs);
                common.targets.insert(static_cast<SWMMModelLayer::Category>(scope.category),
                                      scope.targets.value(static_cast<SWMMModelLayer::Category>(scope.category)));
            }
        if (common.targets.size() > 1) commonScopes.append(common);
    }
    QList<Scope> dataSubsets;
    for (const auto &scope : scopes) if (m_swmmLayer && (scope.dataCategory == SWMMModelLayer::DataInlets || scope.dataCategory == SWMMModelLayer::DataPollutants || scope.dataCategory == SWMMModelLayer::DataCurves || scope.dataCategory == SWMMModelLayer::DataPatterns || scope.dataCategory == SWMMModelLayer::DataLIDControls || scope.dataCategory == SWMMModelLayer::DataSnowpacks)) {
        QMap<int, Scope> groups; Scope generic; generic.dataCategory = scope.dataCategory; generic.title = tr("Inlet designs — Generic grates");
        for (const auto &ref : scope.refs) {
            int kind = -1; QString label;
            if (scope.dataCategory == SWMMModelLayer::DataInlets) {
                SWMM_InletDesign design{};
                if (swmm_inlet_get_design(m_swmmLayer->engine(), swmm_inlet_index(m_swmmLayer->engine(), ref.name.toUtf8().constData()), &design) != SWMM_OK) continue;
                kind = design.type; label = openswmmvis::inlet::inletTypeLabel(openswmmvis::inlet::InletType(kind));
                if ((kind == SWMM_INLET_GRATE || kind == SWMM_INLET_DROP_GRATE || kind == SWMM_INLET_COMBO) && design.grate_type == SWMM_GRATE_GENERIC) generic.refs << ref;
            } else if (scope.dataCategory == SWMMModelLayer::DataLIDControls) {
                if (swmm_lid_get_type(m_swmmLayer->engine(), swmm_lid_index(m_swmmLayer->engine(), ref.name.toUtf8().constData()), &kind) != SWMM_OK) continue;
                label = QStringList{tr("Bio-Retention Cell"), tr("Rain Garden"), tr("Green Roof"), tr("Infiltration Trench"), tr("Permeable Pavement"), tr("Rain Barrel"), tr("Rooftop Disconnection"), tr("Vegetative Swale"), tr("Storage Node (ordered layers)")}.value(kind);
            } else if (scope.dataCategory == SWMMModelLayer::DataSnowpacks) {
                SWMM_UebSnowpack site{};
                if (swmm_snowpack_get_ueb(m_swmmLayer->engine(), swmm_snowpack_index(m_swmmLayer->engine(), ref.name.toUtf8().constData()), &site) != SWMM_OK) continue;
                kind = site.model;
                if (kind < 0) swmm_snow_get_model(m_swmmLayer->engine(), &kind);
                label = kind == 1 ? tr("UEB energy balance") : tr("Degree day");
            } else if(scope.dataCategory==SWMMModelLayer::DataCurves) {
                const auto r=DataObjectAttributeTableModel::dataRecord(m_swmmLayer,SWMMModelLayer::DataCategory(scope.dataCategory),ref.name);kind=r["type"].toInt();label=openswmmvis::curve::CurveProvider::typeLabel(static_cast<openswmmvis::curve::CurveType>(kind));
            } else if (scope.dataCategory == SWMMModelLayer::DataPatterns) {
                if (swmm_pattern_get_type(m_swmmLayer->engine(), swmm_pattern_index(m_swmmLayer->engine(), ref.name.toUtf8().constData()), &kind) != SWMM_OK) continue;
                label = QStringList{tr("Monthly"), tr("Daily"), tr("Hourly"), tr("Weekend")}.value(kind);
            } else {
                if (swmm_pollutant_get_units(m_swmmLayer->engine(), swmm_pollutant_index(m_swmmLayer->engine(), ref.name.toUtf8().constData()), &kind) != SWMM_OK) continue;
                label = QStringList{"MG/L", "UG/L", "#/L"}.value(kind);
            }
            auto &subset = groups[kind]; subset.dataCategory = scope.dataCategory;
            subset.title = tr("%1 — %2").arg(scope.title, label); subset.refs << ref;
        }
        if (groups.size() > 1) dataSubsets.append(groups.values());
        if (!generic.refs.isEmpty() && generic.refs.size() < scope.refs.size()) dataSubsets << generic;
    }
    // Offer the exterior subset explicitly when a box includes interior edges.
    auto choices = commonScopes + scopes.values() + dataSubsets;
    if (m_swmmLayer && m_swmmLayer->engine())
        for (const auto &scope : scopes) {
            if (scope.category != SWMMModelLayer::CatJunctions)
                continue;
            Scope inlet;
            inlet.category = SWMMModelLayer::CatJunctions;
            for (const auto &ref : scope.refs) {
                int flag = 0;
                swmm_node_is_inlet(
                    m_swmmLayer->engine(),
                    swmm_node_index(m_swmmLayer->engine(), ref.name.toUtf8().constData()), &flag);
                if (flag) {
                    inlet.refs.append(ref);
                    inlet.targets[SWMMModelLayer::CatJunctions].append(ref.name);
                }
            }
            if (!inlet.refs.isEmpty() && inlet.refs.size() < scope.refs.size()) {
                inlet.title =
                    tr("Inlet junctions — %1 excluded").arg(scope.refs.size() - inlet.refs.size());
                choices.append(inlet);
            }
        }

    if (canvas)
        for (const auto &scope : scopes) {
            if (scope.refs.isEmpty() || scope.refs.first().objectType != SWMMObjectRef::MeshEdge) continue;
            QList<SWMMObjectRef> exterior;
            for (const auto &ref : scope.refs) {
                QString key;
                int cell = -1, local = -1;
                mesh::MeshObjectRef::parseEdge(ref, &key, &cell, &local);
                for (auto *layer : canvas->layers())
                    if (auto *m = qobject_cast<SWMM2DMeshLayer *>(layer);
                        m && mesh::MeshObjectRef::layerKey(m->sourcePath()) == key &&
                        m->isBoundaryEdge(cell, local)) { exterior.append(ref); break; }
            }
            if (!exterior.isEmpty() && exterior.size() < scope.refs.size())
                choices.append({tr("Exterior edges — %1 excluded").arg(scope.refs.size() - exterior.size()),
                                exterior, -1});
        }
    m_selectionScope->disconnect(this);
    m_selectionScope->clear();
    m_selectionScope->addItem(tr("%1 objects selected — choose a group to edit").arg(refs.size()));
    for (const auto &scope : choices)
        m_selectionScope->addItem(tr("%1 (%2)").arg(scope.title).arg(scope.refs.size()));
    const auto bind = [this, canvas = QPointer<MapCanvas>(canvas), choices, refs, total = refs.size()](int index) {
        if (index <= 0 || index > choices.size()) {
            clear();
            setWindowTitle(tr("%1 objects selected").arg(total));
        } else {
            const auto scope = choices[index - 1];
            if (scope.refs.first().objectType >= SWMMObjectRef::MeshVertex &&
                scope.refs.first().objectType <= SWMMObjectRef::MeshCell) {
                showMeshSelection(canvas, scope.refs);
            } else if(scope.observedLayer) {
                clear();auto layer=scope.observedLayer;const auto ids=scope.observedIds;const auto dataset=layer->datasetId();
                auto *buttons=new QWidget(m_aggregateBar);m_sourceRelationships=buttons;auto *row=new QHBoxLayout(buttons);row->setContentsMargins(0,0,0,0);
                auto *attach=new QPushButton(tr("Attach to selected feature"),buttons),*detach=new QPushButton(tr("Detach features"),buttons),*browse=new QPushButton(tr("Open source series…"),buttons);
                row->addWidget(attach);row->addWidget(detach);row->addWidget(browse);m_aggregateBar->layout()->addWidget(buttons);
                auto refresh=[this,layer,ids,buttons]{if(m_sourceRelationships!=buttons || !layer)return;
#ifdef HAVE_QPROPERTYMODEL
                    QVariantMap info{{tr("Samples"),tr("Immutable source observations; project relationships are editable below.")}};
                    for(const auto &id:ids)info.insert(layer->seriesCaption(id),layer->associationStatus(id));qobject_cast<QPropertyModel *>(m_model)->setData(info);m_treeView->expandAll();
#endif
                };
                auto valid=[layer,ids,dataset]{if(!layer || layer->datasetId()!=dataset)return false;for(const auto &id:ids)if(!layer->seriesIds().contains(id))return false;return true;};
                connect(attach,&QPushButton::clicked,this,[this,layer,ids,valid,refresh]{QString error;if(!valid())error=tr("The captured collection changed. Select its series again.");else layer->attachToSelectedFeature(ids,&error);showAggregateEditResult(error);refresh();});
                connect(detach,&QPushButton::clicked,this,[this,layer,ids,valid,refresh]{if(!valid()){showAggregateEditResult(tr("The captured collection changed. Select its series again."));return;}layer->detachFeatures(ids);refresh();});
                connect(browse,&QPushButton::clicked,this,[this,layer,ids,valid]{if(valid())layer->showData(this,ids.value(0));});
                if(canvas && canvas->undoStack())connect(canvas->undoStack(),&QUndoStack::indexChanged,buttons,[refresh]{refresh();});
                m_aggregateSummary->setText(tr("%1 source series · Relationship actions use project Undo; source samples stay immutable.").arg(ids.size()));m_aggregateBar->show();refresh();
            } else if(scope.featureLayer) {
                clear();
#ifdef HAVE_QPROPERTYMODEL
                auto *aggregate=new FeatureAggregateSelection(scope.featureLayer,scope.featureIds,canvas,this);m_featureAggregate=aggregate;
                auto *pm=qobject_cast<QPropertyModel *>(m_model);pm->setData(QVariant::fromValue(aggregate));m_treeView->expandAll();
                aggregate->finished=[this](const QString &error){showAggregateEditResult(error);};
                connect(aggregate->source(),&QAbstractItemModel::dataChanged,this,[this,aggregate,pm]{if(m_featureAggregate==aggregate)pm->refreshValues();});
                m_aggregateSummary->setText(tr("%1 features · Edits require an active feature edit session; each field is one Undo.").arg(scope.featureIds.size()));m_aggregateBar->show();
#endif
                setWindowTitle(tr("%1 — %2 selected").arg(scope.title).arg(scope.refs.size()));
            } else if (scope.dataCategory >= 0) {
                clear(); QStringList names; for (const auto &ref : scope.refs) names << ref.name;
                bindDataAggregate(m_swmmLayer, scope.dataCategory, names, canvas ? canvas->undoStack() : nullptr);
                setWindowTitle(tr("%1 — %2 selected").arg(scope.title).arg(scope.refs.size()));
            } else if (!scope.targets.isEmpty()) {
                clear();
#ifdef HAVE_QPROPERTYMODEL
                auto *aggregate = new SWMMAggregateSelection(m_swmmLayer, scope.targets,
                    canvas ? canvas->undoStack() : nullptr, this);
                m_swmmAggregate = aggregate;
                connect(aggregate, &SWMMAggregateSelection::editFinished, this,
                        &PropertiesPanel::showAggregateEditResult);
                setWindowTitle(tr("%1 — %2 selected").arg(scope.title).arg(scope.refs.size()));
                auto *pm = qobject_cast<QPropertyModel *>(m_model);
                pm->setData(QVariant::fromValue(aggregate));
                m_treeView->expandAll();
                connect(aggregate, &SWMMAggregateSelection::stagedChanged, this, [this, pm, aggregate] {
                    if (m_swmmAggregate != aggregate) return;
                    pm->refreshValues();
                    updateAggregateBar();
                });
                connect(m_swmmLayer, &SWMMModelLayer::engineAboutToClose, aggregate, [this, aggregate] {
                    if (m_swmmAggregate == aggregate) clear();
                });
                updateAggregateBar();
#endif
            } else {
                clear();
                setWindowTitle(tr("%1 objects selected").arg(scope.refs.size()));
                m_aggregateSummary->setText(scope.refs.first().objectType==SWMMObjectRef::ObservedSeries
                    ?tr("Observed samples and source metadata are immutable here. Use the collection's existing relationship editor for project/model mappings.")
                    :tr("This source has no editable attribute session. Imported attributes and computed results are read-only."));
                m_aggregateBar->show();
            }
        }
        // Binding a scope clears the previous adapters. Remember the full
        // selection after binding so type changes can regroup it.
        m_selectionCanvas = canvas;
        m_displayedSelection = refs;
        m_selectionScope->setVisible(choices.size() > 1);
    };
    connect(m_selectionScope, qOverload<int>(&QComboBox::currentIndexChanged), this, bind);
    const bool wholeFamily = !commonScopes.isEmpty() && commonScopes.first().refs.size() == refs.size();
    if (choices.size() == 1 || scopes.size() == 1 || wholeFamily)
        m_selectionScope->setCurrentIndex(1);
    else
        bind(0);
}

void PropertiesPanel::showMeshSelection(MapCanvas *canvas, const QList<SWMMObjectRef> &refs)
{
    clear();
    using Kind = MeshAttributeTableModel::Kind;
    // One aggregate needs one kind on one mesh; anything else is summarized.
    QMap<QPair<QString, int>, QVector<SWMMObjectRef>> groups;
    int counts[3] = {0, 0, 0};
    int other = 0;
    for (const auto &ref : refs) {
        QString key;
        int element = -1, local = -1;
        Kind kind;
        if (mesh::MeshObjectRef::parseVertex(ref, &key, &element))
            kind = Kind::Vertex;
        else if (mesh::MeshObjectRef::parseEdge(ref, &key, &element, &local))
            kind = Kind::Edge;
        else if (mesh::MeshObjectRef::parseCell(ref, &key, &element))
            kind = Kind::Cell;
        else {
            ++other;
            continue;
        }
        groups[{key, int(kind)}].append(ref);
        ++counts[int(kind)];
    }
    if (groups.isEmpty() || !canvas)
        return;
    if (groups.size() != 1 || other > 0) {
        QStringList parts;
        for (Kind kind : {Kind::Cell, Kind::Edge, Kind::Vertex})
            if (counts[int(kind)])
                parts << meshCountText(kind, counts[int(kind)]);
        if (other)
            parts << (other == 1 ? tr("1 other object") : tr("%1 other objects").arg(other));
        QSet<QString> meshes;
        for (const auto &group : groups.keys())
            meshes.insert(group.first);
        if (meshes.size() > 1)
            parts << tr("on %1 meshes").arg(meshes.size());
        setWindowTitle(tr("Mixed selection"));
        m_aggregateSummary->setText(
            tr("%1. Select elements of one kind on one mesh to edit them together.")
                .arg(parts.join(QStringLiteral(", "))));
        m_aggregateBar->show();
        return;
    }
    const QString key = groups.firstKey().first;
    const auto kind = static_cast<Kind>(groups.firstKey().second);
    SWMM2DMeshLayer *layer = nullptr;
    for (auto *candidate : canvas->layers()) {
        auto *mesh = qobject_cast<SWMM2DMeshLayer *>(candidate);
        if (mesh && mesh::MeshObjectRef::layerKey(mesh->sourcePath()) == key) {
            if (layer) {
                setWindowTitle(tr("Mesh selection is ambiguous"));
                return;
            }
            layer = mesh;
        }
    }
#ifdef HAVE_QPROPERTYMODEL
    if (!layer)
        return;
    auto *aggregate =
        new MeshAggregateSelection(layer, kind, groups.first(), canvas, m_swmmLayer, this);
    m_meshAggregate = aggregate;
    connect(aggregate, &MeshAggregateSelection::editFinished, this,
            &PropertiesPanel::showAggregateEditResult);
    setWindowTitle(tr("2D mesh: %1").arg(meshCountText(kind, aggregate->targetCount())));
    m_treeView->setEditTriggers(QAbstractItemView::EditKeyPressed |
                                QAbstractItemView::AnyKeyPressed);
    auto *pm = qobject_cast<QPropertyModel *>(m_model);
    auto *source = aggregate->source();
    const auto bind = [this, pm, aggregate, source, layer, kind] {
        if (m_meshAggregate != aggregate)
            return;
        pm->setData(QVariant::fromValue(aggregate));
        auto *tree = m_treeView->model();
        for (int i = 0; i < tree->rowCount(); ++i) {
            const auto heading = tree->index(i, 1);
            auto *group = dynamic_cast<MeshPropertyGroup *>(
                static_cast<QPropertyItem *>(heading.internalPointer()));
            if (!group)
                continue;
            // The group editors act on every selected element, as the ribbon's do.
            QPushButton *button = nullptr;
            const QList<int> rows = aggregate->rows();
            if (kind == Kind::Cell && group->name() == tr("Coupled 1D nodes")) {
                button = new QPushButton(tr("Inspect coupled 1D nodes…"), m_treeView);
                button->setObjectName("editMeshCoupledNodes");
                connect(button, &QPushButton::clicked, source, [this, source, aggregate] {
                    source->editCoupledNodes(aggregate->rows(), this);
                });
            } else if (kind == Kind::Edge && group->name() == tr("Aquifer boundary")) {
                button = aquiferBoundaryButton(this, m_treeView, source, layer, rows, m_swmmLayer);
            } else if (kind == Kind::Edge && group->name() == tr("Boundary inflow quality")) {
                button = boundaryInflowButton(this, m_treeView, source, layer, rows, m_swmmLayer);
            } else if (kind == Kind::Edge && group->name() == tr("Boundary condition")) {
                int boundary = 0;
                for (int row : rows) {
                    int cell = -1, local = -1;
                    const auto ref = source->refForRow(row);
                    QString ignored;
                    if (mesh::MeshObjectRef::parseEdge(ref, &ignored, &cell, &local) &&
                        layer->isBoundaryEdge(cell, local))
                        ++boundary;
                }
                button = new QPushButton(tr("Edit surface boundary condition…"), m_treeView);
                button->setObjectName("editMeshBoundary");
                button->setEnabled(boundary > 0);
                button->setToolTip(tr("Opens the boundary editor on the %1 selected boundary "
                                      "edges.")
                                       .arg(boundary));
                connect(button, &QPushButton::clicked, source,
                        [this, aggregate, source] {
                            source->editBoundaryCondition(aggregate->rows(), this);
                        });
            } else if (group->name() == tr("Infiltration")) {
                int editable = 0;
                for (int row : rows)
                    if (layer->surfaceInfiltrationEditable(row))
                        ++editable;
                button = new QPushButton(tr("Edit infiltration…"), m_treeView);
                button->setObjectName("editMeshInfiltration");
                button->setEnabled(editable > 0);
                button->setToolTip(tr("Opens the infiltration form on the %1 selected cells "
                                      "with editable surface infiltration.")
                                       .arg(editable));
                connect(button, &QPushButton::clicked, source, [this, aggregate, source] {
                    source->editInfiltration(aggregate->rows(), this);
                });
            } else if (group->name() == tr("Groundwater")) {
                button = new QPushButton(tr("Edit groundwater parameters…"), m_treeView);
                button->setObjectName("editMeshGroundwater");
                gateGroundwaterButton(button, layer, m_swmmLayer);
                connect(button, &QPushButton::clicked, source, [this, layer, aggregate] {
                    emit groundwaterCellsRequested(layer, aggregate->rows());
                });
            }
            if (button) {
                button->setAccessibleName(button->text());
                button->setText(tr("Edit…"));
                m_treeView->setIndexWidget(heading, button);
            }
            if (kind != Kind::Cell || group->name() == tr("General") ||
                group->name() == tr("Hydraulics"))
                m_treeView->expand(heading.siblingAtColumn(0));
        }
        updateAggregateBar();
    };
    bind();
    const auto refresh = [this, pm, aggregate] {
        if (m_meshAggregate != aggregate)
            return;
        pm->refreshValues();
        updateAggregateBar();
    };
    connect(aggregate, &MeshAggregateSelection::stagedChanged, this, refresh);
    connect(source, &QAbstractItemModel::dataChanged, this, refresh);
    connect(source, &QAbstractItemModel::modelReset, this, refresh);
    connect(source, &QAbstractItemModel::headerDataChanged, this, bind);
    const auto invalidate = [this, aggregate] {
        if (m_meshAggregate == aggregate)
            clear();
    };
    connect(layer, &QObject::destroyed, aggregate, invalidate);
    connect(layer, &SWMM2DMeshLayer::selectionInvalidated, aggregate, invalidate);
    connect(canvas, &MapCanvas::layerRemoved, aggregate,
            [invalidate, layer](OpenSWMMVisLayer *removed) {
                if (removed == layer)
                    invalidate();
            });
    if (m_swmmLayer)
        connect(m_swmmLayer, &SWMMModelLayer::engineAboutToClose, aggregate, invalidate);
#endif
}

void PropertiesPanel::showMeshObject(MapCanvas *canvas, const SWMMObjectRef &ref, const QString &)
{
    clear();
    using Kind = MeshAttributeTableModel::Kind;
    Kind kind;
    QString key, title;
    int element = -1, local = -1;
    if (mesh::MeshObjectRef::parseVertex(ref, &key, &element)) {
        kind = Kind::Vertex;
        title = tr("2D Vertex %1").arg(element);
    } else if (mesh::MeshObjectRef::parseEdge(ref, &key, &element, &local)) {
        kind = Kind::Edge;
        title = tr("2D Edge %1:%2").arg(element).arg(local);
    } else if (mesh::MeshObjectRef::parseCell(ref, &key, &element)) {
        kind = Kind::Cell;
        title = tr("2D Cell %1").arg(element);
    } else {
        return;
    }
    if (!canvas)
        return;
    SWMM2DMeshLayer *layer = nullptr;
    for (auto *candidate : canvas->layers()) {
        auto *mesh = qobject_cast<SWMM2DMeshLayer *>(candidate);
        if (mesh && mesh::MeshObjectRef::layerKey(mesh->sourcePath()) == key) {
            if (layer) {
                setWindowTitle(tr("Mesh selection is ambiguous"));
                return;
            }
            layer = mesh;
        }
    }
#ifdef HAVE_QPROPERTYMODEL
    if (!layer)
        return;
    auto *source = new MeshAttributeTableModel(this);
    source->setCanvas(canvas);
    source->setModelLayer(m_swmmLayer);
    source->setSource(layer, kind, ref);
    const int row = source->focusedRow();
    if (row < 0) {
        delete source;
        return;
    }
    if (kind == Kind::Edge)
        title = tr("2D Edge %1").arg(source->index(row, 0).data().toString());
    setWindowTitle(title);
    m_meshFields = source;
    // CurrentChanged would create an editor on mouse press, swallowing the
    // release that opens the choice list. Mesh mouse edits start on clicked.
    m_treeView->setEditTriggers(QAbstractItemView::EditKeyPressed |
                                QAbstractItemView::AnyKeyPressed);
    auto *pm = qobject_cast<QPropertyModel *>(m_model);
    const auto bind = [this, pm, source, layer, kind, element, local, row] {
        if (m_meshFields != source)
            return;
        if (source->focusedRow() != row) {
            clear();
            return;
        }
        pm->setData(QVariant::fromValue(source));
        auto *tree = m_treeView->model();
        for (int i = 0; i < tree->rowCount(); ++i) {
            const auto heading = tree->index(i, 1);
            auto *group = dynamic_cast<MeshPropertyGroup *>(
                static_cast<QPropertyItem *>(heading.internalPointer()));
            if (!group)
                continue;
            QPushButton *button = nullptr;
            if (kind == Kind::Cell && group->name() == tr("Coupled 1D nodes")) {
                button = new QPushButton(tr("Inspect coupled 1D nodes…"), m_treeView);
                button->setObjectName("editMeshCoupledNodes");
                connect(button, &QPushButton::clicked, source,
                        [this, source, row] { source->editCoupledNodes({row}, this); });
            } else if (kind == Kind::Edge && group->name() == tr("Aquifer boundary")) {
                button = aquiferBoundaryButton(this, m_treeView, source, layer, {row}, m_swmmLayer);
            } else if (kind == Kind::Edge && group->name() == tr("Boundary inflow quality")) {
                button = boundaryInflowButton(this, m_treeView, source, layer, {row}, m_swmmLayer);
            } else if (kind == Kind::Edge && group->name() == tr("Boundary condition") &&
                       layer->isBoundaryEdge(element, local)) {
                button = new QPushButton(tr("Edit surface boundary condition…"), m_treeView);
                button->setObjectName("editMeshBoundary");
                connect(button, &QPushButton::clicked, source,
                        [this, source, row] { source->editBoundaryCondition({row}, this); });
            } else if (group->name() == tr("Infiltration")) {
                button = new QPushButton(tr("Edit infiltration…"), m_treeView);
                button->setObjectName("editMeshInfiltration");
                button->setEnabled(layer->surfaceInfiltrationEditable(row));
                button->setToolTip(tr("Available for cells with editable surface infiltration."));
                connect(source, &QAbstractItemModel::dataChanged, button, [layer, row, button] {
                    button->setEnabled(layer->surfaceInfiltrationEditable(row));
                });
                connect(button, &QPushButton::clicked, source,
                        [this, source, row] { source->editInfiltration({row}, this); });
            } else if (group->name() == tr("Groundwater")) {
                button = new QPushButton(tr("Edit groundwater parameters…"), m_treeView);
                button->setObjectName("editMeshGroundwater");
                gateGroundwaterButton(button, layer, m_swmmLayer);
                connect(button, &QPushButton::clicked, source,
                        [this, layer, row] { emit groundwaterCellsRequested(layer, {row}); });
            }
            if (button) {
                button->setAccessibleName(button->text());
                button->setText(tr("Edit…"));
                m_treeView->setIndexWidget(heading, button);
            }
            if (kind != Kind::Cell || group->name() == tr("General") ||
                group->name() == tr("Hydraulics"))
                m_treeView->expand(heading.siblingAtColumn(0));
        }
    };
    bind();
    connect(source, &QAbstractItemModel::modelReset, this, bind);
    const auto refresh = [this, pm, source] {
        if (m_meshFields == source)
            pm->refreshValues();
    };
    connect(source, &QAbstractItemModel::dataChanged, this,
            [refresh, row](const QModelIndex &first, const QModelIndex &last) {
                if (first.row() <= row && row <= last.row())
                    refresh();
            });
    connect(source, &QAbstractItemModel::headerDataChanged, this, bind);
    const auto invalidate = [this, source] {
        if (m_meshFields == source)
            clear();
    };
    connect(layer, &QObject::destroyed, source, invalidate);
    connect(layer, &SWMM2DMeshLayer::selectionInvalidated, source, invalidate);
    connect(canvas, &MapCanvas::layerRemoved, source,
            [invalidate, layer](OpenSWMMVisLayer *removed) {
                if (removed == layer)
                    invalidate();
            });
    if (m_swmmLayer)
        connect(m_swmmLayer, &SWMMModelLayer::engineAboutToClose, source, invalidate);
#endif
}

void PropertiesPanel::showIdentifyResults(const QList<IdentifyResult> &results)
{
    releaseMeshProperties();
    m_lastResults = results;
    // Populate once, then bind explicitly below. addItem() otherwise emits
    // currentIndexChanged for the first item and creates a duplicate adapter.
    const QSignalBlocker blocker(m_layerCombo);
    m_layerCombo->clear();
    // 2026-05-29 — identify results route to spatial / SWMM-feature
    // adapters, never to the non-spatial data-object surface; hide the
    // header "Open in <Editor>…" button so it doesn't linger from a
    // prior data-object view.
    m_dataObjectCategory = SWMMModelLayer::NumDataCategories;
    if (m_openEditorButton)
        m_openEditorButton->hide();
    if (m_plotRainGageButton)
        m_plotRainGageButton->hide();

    for (const auto &r : results)
        m_layerCombo->addItem(r.layerName);

    if (!results.isEmpty())
        onLayerComboIndexChanged(0);
#ifdef HAVE_QPROPERTYMODEL
    else if (auto *pm = qobject_cast<QPropertyModel *>(m_model))
        pm->clear();
#endif
}

void PropertiesPanel::showLayerProperties(OpenSWMMVisLayer *layer)
{
    showObject(layer, tr("Layer Properties — %1").arg(layer ? layer->name() : QString()));
}

void PropertiesPanel::clear()
{
    m_displayedSelection.clear();
    m_selectionCanvas = nullptr;
    releaseMeshProperties();
#ifdef HAVE_QPROPERTYMODEL
    if (auto *pm = qobject_cast<QPropertyModel *>(m_model))
        pm->clear();
#endif
    m_layerCombo->clear();
    m_lastResults.clear();
    // 2026-05-29 — Panel is empty; no data-object to open.
    m_dataObjectCategory = SWMMModelLayer::NumDataCategories;
    if (m_openEditorButton)
        m_openEditorButton->hide();
    if (m_plotRainGageButton)
        m_plotRainGageButton->hide();
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void PropertiesPanel::onIdentifyResult(const QList<IdentifyResult> &results)
{
    showIdentifyResults(results);
}

void PropertiesPanel::onObjectEditedExternally(const QString &name)
{
    // Mirror an external attribute change into the Property Browser.
    // QPropertyModel doesn't subscribe to NOTIFY signals, so we cannot
    // rely on adapter->refresh() / emit changed() to trigger a view
    // repaint.  Instead, call QPropertyModel::refreshValues() which emits
    // dataChanged for every value cell; QVariantPropertyItem::data() then
    // re-reads the current value from the adapter live via
    // QMetaProperty::read().  Guard against the resulting dataChanged
    // bouncing an objectEdited back to the attribute table.
    if (name.isEmpty())
        return;
    const QScopedValueRollback<bool> guard(m_suppressEditForward, true);
#ifdef HAVE_QPROPERTYMODEL
    auto *pm = qobject_cast<QPropertyModel *>(m_model);

    // A right-click "Convert To" can change the bound object's SWMM type.
    // The adapter subclass is type-specific (Junction vs Storage, Conduit
    // vs Pump, …), so a bare refreshValues() would leave the wrong fields
    // exposed. Detect the kind change and replay the bind, which reads the
    // type live from the engine and constructs the matching subclass.
    if (pm && m_swmmLayer && m_swmmLayer->engine()) {
        if (m_nodeAdapter && m_nodeAdapter->name() == name) {
            const int idx = swmm_node_index(m_swmmLayer->engine(), name.toUtf8().constData());
            int kind = m_nodeAdapterKind;
            if (idx >= 0)
                swmm_node_get_type(m_swmmLayer->engine(), idx, &kind);
            if (kind != m_nodeAdapterKind) {
                onLayerComboIndexChanged(m_layerCombo->currentIndex());
                return;
            }
        } else if (m_linkAdapter && m_linkAdapter->name() == name) {
            const int idx = swmm_link_index(m_swmmLayer->engine(), name.toUtf8().constData());
            int kind = m_linkAdapterKind;
            if (idx >= 0)
                swmm_link_get_type(m_swmmLayer->engine(), idx, &kind);
            if (kind != m_linkAdapterKind) {
                onLayerComboIndexChanged(m_layerCombo->currentIndex());
                return;
            }
        }
    }

    if (pm && ((m_nodeAdapter && m_nodeAdapter->name() == name) ||
               (m_linkAdapter && m_linkAdapter->name() == name) ||
               (m_subcatchAdapter && m_subcatchAdapter->name() == name) ||
               (m_dataAdapter && m_dataAdapter->name() == name))) {
        pm->refreshValues();
    }
#endif
}

void PropertiesPanel::onOffsetModeChanged()
{
    if (!m_linkAdapter || !m_layerCombo)
        return;
    onLayerComboIndexChanged(m_layerCombo->currentIndex());
}

void PropertiesPanel::setProject(SWMMModelLayer *layer)
{
    if (m_swmmLayer == layer)
        return;

    // Disconnect from the old layer before replacing it.
    if (m_swmmLayer) {
        QObject::disconnect(m_swmmLayer, &SWMMModelLayer::attributeChanged, this,
                            &PropertiesPanel::onObjectEditedExternally);
        disconnect(m_conversionConnection);
    }

    if (m_meshFields || m_meshAggregate || m_swmmAggregate || m_dataAggregate)
        clear();
    m_swmmLayer = layer;

    if (!layer) {
        // Releasing the project — drop the adapters so a stale engine
        // handle doesn't get used on the next identify.
        if (m_nodeAdapter) {
            m_nodeAdapter->deleteLater();
            m_nodeAdapter = nullptr;
        }
        if (m_linkAdapter) {
            m_linkAdapter->deleteLater();
            m_linkAdapter = nullptr;
        }
        if (m_subcatchAdapter) {
            m_subcatchAdapter->deleteLater();
            m_subcatchAdapter = nullptr;
        }
        return;
    }

    // Refresh the property browser whenever an attribute changes (e.g. conduit
    // length or subcatchment area recalculated from an edited geometry).
    connect(layer, &SWMMModelLayer::attributeChanged, this,
            &PropertiesPanel::onObjectEditedExternally, Qt::UniqueConnection);
    m_conversionConnection = connect(
        layer, &SWMMModelLayer::typesChanged, this, [this](const QSet<SWMMObjectRef> &changed) {
            if (m_selectionCanvas && !m_displayedSelection.isEmpty()) {
                for (const auto &ref : std::as_const(m_displayedSelection)) {
                    if (changed.contains(ref)) {
                        const auto refs = m_displayedSelection;
                        showSelection(m_selectionCanvas, refs);
                        return;
                    }
                }
            }
            if ((m_nodeAdapter && changed.contains({SWMMObjectRef::Node, m_nodeAdapter->name()})) ||
                (m_linkAdapter && changed.contains({SWMMObjectRef::Link, m_linkAdapter->name()})))
                onLayerComboIndexChanged(m_layerCombo->currentIndex());
        });
    connect(layer, &SWMMModelLayer::objectRenamed, this, &PropertiesPanel::onObjectRenamed,
            Qt::UniqueConnection);
}

void PropertiesPanel::onObjectRenamed(const QString &oldName, const QString &newName, int kind)
{
    // A rename from ANY view (this panel, the Attribute Table, the map) must
    // re-key the bound adapter and the cached identify result, or every later
    // edit here addresses the old name: X/Y edits were silently dropped and
    // a re-bind found nothing. Only re-key and repaint — never emit
    // objectEdited from here (the panels are cross-wired; that loop froze the
    // GUI in an earlier attempt).
    bool touched = false;
    if (m_nodeAdapter && m_nodeAdapter->name() == oldName) {
        m_nodeAdapter->updateStoredName(newName);
        touched = true;
    }
    if ((kind & SWMMModelLayer::kKindLink) && m_linkAdapter && m_linkAdapter->name() == oldName) {
        m_linkAdapter->updateStoredName(newName);
        touched = true;
    }
    if (m_subcatchAdapter && m_subcatchAdapter->name() == oldName) {
        m_subcatchAdapter->updateStoredName(newName);
        touched = true;
    }
    if (!m_dataAggregate && m_dataAdapter && m_dataAdapter->name() == oldName) {
        m_dataAdapter->updateStoredName(newName);
        touched = true;
    }
    for (auto &result : m_lastResults)
        for (auto &feature : result.features)
            if (feature.value(QStringLiteral("Name")).toString() == oldName &&
                (feature.value(QStringLiteral("Type")).toString() != "Link" ||
                 (kind & SWMMModelLayer::kKindLink)))
                feature.insert(QStringLiteral("Name"), newName);
#ifdef HAVE_QPROPERTYMODEL
    if (touched) {
        const QScopedValueRollback<bool> guard(m_suppressEditForward, true);
        if (auto *pm = qobject_cast<QPropertyModel *>(m_model))
            pm->refreshValues();
    }
#else
    Q_UNUSED(touched);
#endif
}

void PropertiesPanel::onLayerComboIndexChanged(int index)
{
    if (index < 0 || index >= m_lastResults.size())
        return;

    const IdentifyResult &result = m_lastResults.at(index);

    if (result.features.isEmpty()) {
#ifdef HAVE_QPROPERTYMODEL
        if (auto *pm = qobject_cast<QPropertyModel *>(m_model))
            pm->clear();
#endif
        return;
    }

    const QVariantMap feature = result.features.first();

    // Slice Z.5.3 / AG.3 — if the identified feature is a SWMM
    // object, route through a typed `*PropertyAdapter` so
    // QPropertyModel renders editable spinboxes via its built-in
    // auto-delegate.  Other feature kinds (GIS layers, non-SWMM
    // entities) fall through to the read-only QVariantMap.
    bool routedThroughAdapter = false;
#ifdef HAVE_QPROPERTYMODEL
    const QString typeStr = feature.value(QStringLiteral("Type")).toString();
    const QString name = feature.value(QStringLiteral("Name")).toString();
    if (m_swmmLayer && !name.isEmpty() && m_swmmLayer->engine()) {
        auto *pm = qobject_cast<QPropertyModel *>(m_model);
        if (typeStr == QStringLiteral("Node")) {
            if (m_nodeAdapter)
                m_nodeAdapter->deleteLater();
            // Round-4 follow-up 2026-05-12 — pick the SWMM-type-aware
            // subclass so the Property Browser only exposes attributes
            // applicable to this node's kind (Junction / Outfall /
            // Storage / Divider), matching the [JUNCTIONS]/[OUTFALLS]/
            // [STORAGE]/[DIVIDERS] .inp sections.
            const int nodeIdx = swmm_node_index(m_swmmLayer->engine(), name.toUtf8().constData());
            int nodeKind = 0;
            swmm_node_get_type(m_swmmLayer->engine(), nodeIdx, &nodeKind);
            switch (nodeKind) {
            case SWMM_NODE_OUTFALL:
                m_nodeAdapter = new SWMMOutfallPropertyAdapter(m_swmmLayer->engine(), name, this);
                break;
            case SWMM_NODE_STORAGE:
                m_nodeAdapter = new SWMMStoragePropertyAdapter(m_swmmLayer->engine(), name, this);
                break;
            case SWMM_NODE_DIVIDER:
                m_nodeAdapter = new SWMMDividerPropertyAdapter(m_swmmLayer->engine(), name, this);
                break;
            case SWMM_NODE_JUNCTION:
            default: {
                // Virtual junctions are JUNCTION-typed; key off the flag
                // BEFORE the kind so they get the trimmed adapter (no depth/
                // ponding fields; the inflow compound rows are kept — point
                // laterals are allowed at a virtual junction).
                //
                // An inlet junction carries BOTH flags (it IS a virtual
                // junction plus an [INLET_USAGE] row), so its probe must come
                // first or it would never get the inlet rows.
                int isInlet = 0;
                swmm_node_is_inlet(m_swmmLayer->engine(), nodeIdx, &isInlet);
                int isVirtual = 0;
                swmm_node_is_virtual(m_swmmLayer->engine(), nodeIdx, &isVirtual);
                if (isInlet)
                    m_nodeAdapter =
                        new SWMMInletJunctionPropertyAdapter(m_swmmLayer->engine(), name, this);
                else if (isVirtual)
                    m_nodeAdapter =
                        new SWMMVirtualJunctionPropertyAdapter(m_swmmLayer->engine(), name, this);
                else
                    m_nodeAdapter =
                        new SWMMJunctionPropertyAdapter(m_swmmLayer->engine(), name, this);
                break;
            }
            }
            m_nodeAdapterKind = nodeKind;
            // DB.4c — thread the layer pointer so the compound-edit
            // pickers (Inflows TS / Pattern, DWF patterns, RDII UH)
            // can call back into `layer->createDataObject(...)`.
            m_nodeAdapter->setModelLayer(m_swmmLayer);
            // Review C-01: writes go through the same checked, undoable
            // path as the Attribute Table.
            m_nodeAdapter->setFieldWriter(spatialFieldWriter(
                nodeKind == SWMM_NODE_OUTFALL   ? SWMMModelLayer::CatOutfalls
                : nodeKind == SWMM_NODE_STORAGE ? SWMMModelLayer::CatStorage
                : nodeKind == SWMM_NODE_DIVIDER ? SWMMModelLayer::CatDividers
                                                : SWMMModelLayer::CatJunctions));
            // Slice QA.2 — thread the per-project stats registry into
            // the adapter so its statMax* getters can dispatch through
            // a loaded SWMMResultsLayer. Refresh the combo immediately
            // so the new adapter sees a populated selection.
            m_nodeAdapter->setStatsRegistry(m_statsRegistry);
            refreshStatsSourceCombo();
            if (pm)
                pm->setData(QVariant::fromValue<QObject *>(m_nodeAdapter));

            // Slice DA.4.3 — for outfalls only, drive editability of the
            // three stage-data rows from the live `outfallType`. Reuses
            // the adapter's existing `changed()` signal as the trigger
            // (every setter — including setOutfallType — emits it). A
            // separate signal isn't needed and would just duplicate the
            // existing notification chain.
            if (auto *outfallAdapter = qobject_cast<SWMMOutfallPropertyAdapter *>(m_nodeAdapter)) {
                auto applyOutfallRowFlags = [pm, outfallAdapter]() {
                    if (!pm)
                        return;
                    const auto t = outfallAdapter->outfallType();
                    setRowEditable(pm, outfallAdapter, QStringLiteral("outfallStage"),
                                   t == SWMMNodePropertyAdapter::FIXED);
                    setRowEditable(pm, outfallAdapter, QStringLiteral("outfallTidalCurve"),
                                   t == SWMMNodePropertyAdapter::TIDAL);
                    setRowEditable(pm, outfallAdapter, QStringLiteral("outfallTimeseries"),
                                   t == SWMMNodePropertyAdapter::TIMESERIES);
                };
                connect(outfallAdapter, &SWMMNodePropertyAdapter::changed, pm,
                        applyOutfallRowFlags);
                // Initial pass — runs after setData() above has populated
                // the property tree so findPropertyIndexByDisplayLabel can
                // locate the rows.
                applyOutfallRowFlags();
            }

            // Slice AG.4 — storage units: drive editability of the geometry
            // rows from the live Storage Shape. Three mutually exclusive groups:
            //   TABULAR     → the curve picker
            //   FUNCTIONAL  → the three A/B/C coefficient rows
            //   CYLINDRICAL / CONICAL / PARABOLIC / PYRAMIDAL
            //               → the raw dimension rows that apply to that shape
            //                 (a cylinder has no side slope, so Param 3 stays grey)
            // Same trigger/contract as the outfall block above. The dimension rows keep
            // generic labels ("Shape Param 1"): QPropertyModel builds its rows from
            // metaObject() at setData() time and setRowEditable finds a row by matching
            // that label, so a label that changed with the shape would break its own
            // lookup the moment the user switched shapes. The shape-specific name
            // ("Base Length", …) is surfaced in the attribute table's tooltips — the
            // same split the link xsect geom1..geom4 rows already use.
            if (auto *storageAdapter = qobject_cast<SWMMStoragePropertyAdapter *>(m_nodeAdapter)) {
                auto applyStorageRowFlags = [pm, storageAdapter]() {
                    if (!pm)
                        return;
                    const int shape = static_cast<int>(storageAdapter->storageShape());
                    const bool tabular =
                        shape == static_cast<int>(SWMMNodePropertyAdapter::Tabular);
                    const bool functional =
                        shape == static_cast<int>(SWMMNodePropertyAdapter::Functional);

                    setRowEditable(pm, storageAdapter, QStringLiteral("storageCurve"), tabular);
                    setRowEditable(pm, storageAdapter, QStringLiteral("storageCoeffA"), functional);
                    setRowEditable(pm, storageAdapter, QStringLiteral("storageExpB"), functional);
                    setRowEditable(pm, storageAdapter, QStringLiteral("storageConstC"), functional);
                    for (int k = 1; k <= 3; ++k) {
                        setRowEditable(pm, storageAdapter, QStringLiteral("storageParam%1").arg(k),
                                       openswmmvis::storageGeomApplies(shape, k));
                    }
                };
                connect(storageAdapter, &SWMMNodePropertyAdapter::changed, pm,
                        applyStorageRowFlags);
                applyStorageRowFlags();
            }

            // 1D↔2D coupling rows: only the parameters the chosen structure
            // uses are editable (a manhole its diameter, an inlet or open
            // storage its opening area and perimeter), none when the node is
            // not coupleable. Same trigger/contract as the blocks above.
            if (m_nodeAdapter->metaObject()->indexOfProperty("coupleable") >= 0) {
                auto *a = m_nodeAdapter;
                auto applyCouplingRowFlags = [pm, a]() {
                    if (!pm)
                        return;
                    using A = SWMMNodePropertyAdapter;
                    const auto t = a->couplingType();
                    const bool structure = t != A::NOT_COUPLEABLE && t != A::OUTFALL_2D;
                    const bool opening = t == A::GRATE || t == A::CURB || t == A::OPEN_STORAGE;
                    const QList<QPair<const char *, bool>> rows = {
                        {"couplingDiameter", t == A::MANHOLE},
                        {"couplingPerimeter", opening},
                        {"couplingStructureArea", opening},
                        {"couplingChamberArea", structure},
                        {"couplingLidOpenFraction", structure},
                        {"couplingCrest", structure},
                        {"couplingLaw", structure},
                        {"couplingCoeffs", structure},
                        {"couplingCw", structure},
                        {"couplingCsw", structure},
                        {"couplingCoi", structure},
                        {"couplingCoo", structure},
                        {"couplingSpread", t == A::OUTFALL_2D},
                        {"couplingTailwaterRelax", t == A::OUTFALL_2D && a->outfallType() != A::FREE}};
                    for (const auto &r : rows)
                        if (a->metaObject()->indexOfProperty(r.first) >= 0)
                            setRowEditable(pm, a, QString::fromLatin1(r.first), r.second);
                };
                connect(a, &SWMMNodePropertyAdapter::changed, pm, applyCouplingRowFlags);
                connect(m_swmmLayer, &SWMMModelLayer::attributeChanged, a,
                        [this, a, applyCouplingRowFlags](const QString &name) {
                            if (m_nodeAdapter == a && a->name() == name)
                                applyCouplingRowFlags();
                        });
                applyCouplingRowFlags();
            }

            auto *structureAdapter = m_nodeAdapter;
            connect(structureAdapter, &SWMMNodePropertyAdapter::structureRefreshRequested, this,
                    [this, structureAdapter] {
                        const QScopedValueRollback<bool> guard(m_suppressEditForward, true);
                        emit structureAdapter->changed();
                        onObjectEditedExternally(structureAdapter->name());
                    });
            if (m_nodeAdapter->metaObject()->indexOfProperty("lidControl") >= 0) {
                const auto flags = [pm, structureAdapter] {
                    const bool assigned = !structureAdapter->lidControlRef().currentName.isEmpty();
                    setRowEditable(pm, structureAdapter, "lidInitialSaturation", assigned);
                    setRowEditable(pm, structureAdapter, "lidBottomOwnership", assigned);
                };
                connect(structureAdapter, &SWMMNodePropertyAdapter::changed, pm, flags);
                connect(m_swmmLayer, &SWMMModelLayer::attributeChanged, structureAdapter,
                        [this, structureAdapter, flags](const QString &name) {
                            if (m_nodeAdapter == structureAdapter && structureAdapter->name() == name)
                                flags();
                        });
                flags();
            }

            // Read the adapter's CURRENT name: a rename after binding
            // re-keys the adapter, and a captured name would keep pointing
            // the other views at an object that no longer exists.
            connect(m_nodeAdapter, &SWMMNodePropertyAdapter::changed, this, [this]() {
                if (!m_suppressEditForward && m_nodeAdapter)
                    emit objectEdited(m_nodeAdapter->name());
            });
            connect(m_nodeAdapter, &SWMMNodePropertyAdapter::renameRequested, this,
                    [this](const QString &oldN, const QString &newN) {
                        if (m_swmmLayer &&
                            m_swmmLayer->applyRename(oldN, newN, SWMMModelLayer::kKindNode)) {
                            if (m_nodeAdapter)
                                m_nodeAdapter->updateStoredName(newN);
                            emit objectEdited(newN);
                        }
                    });
            // Slice DB — route X/Y edits through applyNodeMove so the
            // scene cache + attached-link bboxes refresh atomically with
            // the engine write (a bare swmm_spatial_set_node_coord would
            // leave the canvas stale until the next geometry rebuild).
            connect(m_nodeAdapter, &SWMMNodePropertyAdapter::coordChangeRequested, this,
                    [this](double newX, double newY) {
                        if (!m_swmmLayer || !m_nodeAdapter)
                            return;
                        const QString current = m_nodeAdapter->name();
                        const int idx =
                            swmm_node_index(m_swmmLayer->engine(), current.toUtf8().constData());
                        if (idx >= 0 && m_swmmLayer->applyNodeMove(idx, newX, newY))
                            emit objectEdited(current);
                    });
            routedThroughAdapter = true;
        } else if (typeStr == QStringLiteral("Link")) {
            if (m_linkAdapter)
                m_linkAdapter->deleteLater();
            // Round-4 follow-up 2026-05-12 — pick the SWMM-link-type
            // aware subclass so the Property Browser only exposes
            // attributes applicable to this link's kind (Conduit /
            // Pump / Orifice / Weir / Outlet), matching the [CONDUITS]
            // / [PUMPS] / [ORIFICES] / [WEIRS] / [OUTLETS] sections.
            const int linkIdx = swmm_link_index(m_swmmLayer->engine(), name.toUtf8().constData());
            int linkKind = 0;
            swmm_link_get_type(m_swmmLayer->engine(), linkIdx, &linkKind);
            switch (linkKind) {
            case 1: // Pump
                m_linkAdapter = new SWMMPumpPropertyAdapter(m_swmmLayer->engine(), name, this);
                break;
            case 2: // Orifice
                m_linkAdapter = new SWMMOrificePropertyAdapter(m_swmmLayer->engine(), name, this);
                break;
            case 3: // Weir
                m_linkAdapter = new SWMMWeirPropertyAdapter(m_swmmLayer->engine(), name, this);
                break;
            case 4: // Outlet
                m_linkAdapter = new SWMMOutletPropertyAdapter(m_swmmLayer->engine(), name, this);
                break;
            case 0: // Conduit
            default:
                m_linkAdapter = new SWMMConduitPropertyAdapter(m_swmmLayer->engine(), name, this);
                break;
            }
            m_linkAdapterKind = linkKind;
            // Slice BM.0-Browse-Edit (2026-05-25) — thread the layer
            // pointer so DataObjectRef-typed Q_PROPERTYs (pumpCurve) can
            // construct refs with the right layer for the picker editor.
            m_linkAdapter->setModelLayer(m_swmmLayer);
            // Review C-01: writes go through the same checked, undoable
            // path as the Attribute Table.
            m_linkAdapter->setFieldWriter(spatialFieldWriter(
                linkKind == 1   ? SWMMModelLayer::CatPumps
                : linkKind == 2 ? SWMMModelLayer::CatOrifices
                : linkKind == 3 ? SWMMModelLayer::CatWeirs
                : linkKind == 4 ? SWMMModelLayer::CatOutlets
                                : SWMMModelLayer::CatConduits));
            // Thread the per-project stats registry so the statMax* rows
            // can dispatch through the bound run (mirror of the node
            // adapter wiring below).
            m_linkAdapter->setStatsRegistry(m_statsRegistry);
            refreshStatsSourceCombo();
            if (pm)
                pm->setData(QVariant::fromValue<QObject *>(m_linkAdapter));

            // Inline cross-section geoms (Conduit / Orifice / Weir) stay
            // editable for every shape — the stored geom is a real number
            // whatever the live shape consumes, and blanking the rest read
            // as broken editors. Only the picker-owned index slots
            // (IRREGULAR / STREET geom1, CUSTOM geom2) are locked; those
            // are set by the compound dialog's name picker. Re-run on every
            // changed() so editing the shape via the dialog re-evaluates.
            // No-op on Pump / Outlet (no geom rows). Mirrors the Attribute
            // Table's flags() so the two surfaces never disagree.
            {
                auto applyGeomRowFlags = [pm, this]() {
                    if (!pm || !m_linkAdapter)
                        return;
                    for (const auto &ref :
                         {m_linkAdapter->fromNodeRef(), m_linkAdapter->toNodeRef()}) {
                        const int link =
                            swmm_link_index(ref.engine, ref.linkName.toUtf8().constData());
                        const int node =
                            swmm_node_index(ref.engine, ref.nodeName.toUtf8().constData());
                        int isVirtual = 0;
                        const bool editable =
                            linkendpoint::allowed(ref.engine, link, ref.upstream, node) &&
                            swmm_node_is_virtual(ref.engine, node, &isVirtual) == SWMM_OK &&
                            !isVirtual;
                        setRowEditable(pm, m_linkAdapter,
                                       ref.upstream ? QStringLiteral("fromNode")
                                                    : QStringLiteral("toNode"),
                                       editable);
                    }
                    const bool functional = int(m_linkAdapter->outletRatingType()) < 2;
                    setRowEditable(pm, m_linkAdapter, "coefficient", functional);
                    setRowEditable(pm, m_linkAdapter, "expon", functional);
                    const int shape = m_linkAdapter->xsectShapeId();
                    for (int k = 1; k <= 4; ++k)
                        setRowEditable(pm, m_linkAdapter, QStringLiteral("geom%1").arg(k),
                                       !openswmmvis::xsectGeomIsPickerIndex(shape, k));
                };
                connect(m_linkAdapter, &SWMMLinkPropertyAdapter::changed, pm, applyGeomRowFlags);
                auto *adapter = m_linkAdapter;
                connect(m_swmmLayer, &SWMMModelLayer::attributeChanged, adapter,
                        [this, adapter, applyGeomRowFlags](const QString &name) {
                            if (m_linkAdapter == adapter && adapter->name() == name) applyGeomRowFlags();
                        });
                applyGeomRowFlags();
            }

            // Read the adapter's CURRENT name: a rename after binding
            // re-keys the adapter, and a captured name would keep pointing
            // the other views at an object that no longer exists.
            auto *structureAdapter = m_linkAdapter;
            connect(structureAdapter, &SWMMLinkPropertyAdapter::structureRefreshRequested, this,
                    [this, structureAdapter] {
                        const QScopedValueRollback<bool> guard(m_suppressEditForward, true);
                        emit structureAdapter->changed();
                        onObjectEditedExternally(structureAdapter->name());
                    });
            connect(m_linkAdapter, &SWMMLinkPropertyAdapter::changed, this, [this]() {
                if (!m_suppressEditForward && m_linkAdapter)
                    emit objectEdited(m_linkAdapter->name());
            });
            connect(m_linkAdapter, &SWMMLinkPropertyAdapter::renameRequested, this,
                    [this](const QString &oldN, const QString &newN) {
                        if (m_swmmLayer &&
                            m_swmmLayer->applyRename(oldN, newN, SWMMModelLayer::kKindLink)) {
                            if (m_linkAdapter)
                                m_linkAdapter->updateStoredName(newN);
                            emit objectEdited(newN);
                        }
                    });
            routedThroughAdapter = true;
        } else if (typeStr == QStringLiteral("Subcatchment")) {
            if (m_subcatchAdapter)
                m_subcatchAdapter->deleteLater();
            m_subcatchAdapter = new SWMMSubcatchPropertyAdapter(m_swmmLayer->engine(), name, this);
            // USER_FLAGS Phase 4 — layer binding so the User Flags row
            // reaches the shared UserFlagsModel.
            m_subcatchAdapter->setModelLayer(m_swmmLayer);
            // Review C-01: writes go through the same checked, undoable
            // path as the Attribute Table.
            m_subcatchAdapter->setFieldWriter(spatialFieldWriter(SWMMModelLayer::CatSubcatchments));
            // Stats registry so the stat* rows follow the bound run.
            m_subcatchAdapter->setStatsRegistry(m_statsRegistry);
            refreshStatsSourceCombo();
            if (pm)
                pm->setData(QVariant::fromValue<QObject *>(m_subcatchAdapter));
            // Read the adapter's CURRENT name: a rename after binding
            // re-keys the adapter, and a captured name would keep pointing
            // the other views at an object that no longer exists.
            connect(m_subcatchAdapter, &SWMMSubcatchPropertyAdapter::changed, this, [this]() {
                if (!m_suppressEditForward && m_subcatchAdapter)
                    emit objectEdited(m_subcatchAdapter->name());
            });
            connect(m_subcatchAdapter, &SWMMSubcatchPropertyAdapter::renameRequested, this,
                    [this](const QString &oldN, const QString &newN) {
                        if (m_swmmLayer &&
                            m_swmmLayer->applyRename(oldN, newN, SWMMModelLayer::kKindCatch)) {
                            if (m_subcatchAdapter)
                                m_subcatchAdapter->updateStoredName(newN);
                            emit objectEdited(newN);
                        }
                    });
            // Phase 3 — grey the infiltration parameter rows by the live model
            // (Horton/Mod-Horton → Horton rows; Green-Ampt/Mod-GA → GA rows;
            // Curve-Number → CN row). Same trigger/contract as the storage
            // Functional/Tabular greying.
            {
                auto *sub = m_subcatchAdapter;
                auto applyInfilRowFlags = [pm, sub]() {
                    if (!pm)
                        return;
                    const auto m = sub->infilModel();
                    const bool horton = (m == SWMMSubcatchPropertyAdapter::Horton ||
                                         m == SWMMSubcatchPropertyAdapter::ModHorton);
                    const bool ga = (m == SWMMSubcatchPropertyAdapter::GreenAmpt ||
                                     m == SWMMSubcatchPropertyAdapter::ModGreenAmpt);
                    const bool cn = (m == SWMMSubcatchPropertyAdapter::CurveNumber);
                    setRowEditable(pm, sub, QStringLiteral("hortonF0"), horton);
                    setRowEditable(pm, sub, QStringLiteral("hortonFmin"), horton);
                    setRowEditable(pm, sub, QStringLiteral("hortonDecay"), horton);
                    setRowEditable(pm, sub, QStringLiteral("hortonDryTime"), horton);
                    setRowEditable(pm, sub, QStringLiteral("gaSuction"), ga);
                    setRowEditable(pm, sub, QStringLiteral("gaConductivity"), ga);
                    setRowEditable(pm, sub, QStringLiteral("gaInitDeficit"), ga);
                    setRowEditable(pm, sub, QStringLiteral("cnNumber"), cn);
                    setRowEditable(pm, sub, QStringLiteral("cnDryTime"), cn);
                };
                connect(sub, &SWMMSubcatchPropertyAdapter::changed, pm, applyInfilRowFlags);
                applyInfilRowFlags();
            }
            routedThroughAdapter = true;
        }
    }
    if (!routedThroughAdapter) {
        if (auto *pm = qobject_cast<QPropertyModel *>(m_model))
            pm->setData(QVariant(feature));
    }
#else
    Q_UNUSED(feature);
    Q_UNUSED(routedThroughAdapter);
#endif
    m_treeView->expandAll();

    setWindowTitle(tr("Properties — %1 (%2 feature%3)")
                       .arg(result.layerName)
                       .arg(result.features.size())
                       .arg(result.features.size() == 1 ? QString() : QStringLiteral("s")));
}

// ---------------------------------------------------------------------------
// Slice DA.2 — Non-spatial Data Object dispatch
// ---------------------------------------------------------------------------

void PropertiesPanel::showDataObject(SWMMModelLayer *layer, int objectKind, const QString &name)
{
    releaseMeshProperties();
#ifdef HAVE_QPROPERTYMODEL
    if (!layer || !layer->engine() || name.isEmpty()) {
        if (auto *pm = qobject_cast<QPropertyModel *>(m_model))
            pm->clear();
        m_dataObjectCategory = SWMMModelLayer::NumDataCategories;
        if (m_openEditorButton)
            m_openEditorButton->hide();
        if (m_plotRainGageButton)
            m_plotRainGageButton->hide();
        return;
    }
    if (m_dataAdapter) {
        m_dataAdapter->deleteLater();
        m_dataAdapter = nullptr;
    }

    using K = SWMMObjectRef::ObjectType;
    SWMM_Engine eng = layer->engine();

    // 2026-05-29 — Track the DataCategory in parallel with the adapter
    // construction so the header "Open in <Editor>…" button knows which
    // editor to launch without having to qobject_cast the adapter.
    m_dataObjectCategory = SWMMModelLayer::NumDataCategories;

    switch (static_cast<K>(objectKind)) {
    case K::Pollutant:
        m_dataAdapter = new SWMMPollutantPropertyAdapter(eng, name, this);
        m_dataObjectCategory = SWMMModelLayer::DataPollutants;
        break;
    case K::LandUse:
        m_dataAdapter = new SWMMLandUsePropertyAdapter(eng, name, this);
        m_dataObjectCategory = SWMMModelLayer::DataLandUses;
        break;
    case K::Curve:
        m_dataAdapter = new SWMMCurvePropertyAdapter(eng, name, this);
        m_dataObjectCategory = SWMMModelLayer::DataCurves;
        break;
    case K::TimeSeries:
        m_dataAdapter = new SWMMTimeSeriesPropertyAdapter(eng, name, this);
        m_dataObjectCategory = SWMMModelLayer::DataTimeSeries;
        break;
    case K::TimePattern:
        m_dataAdapter = new SWMMPatternPropertyAdapter(eng, name, this);
        m_dataObjectCategory = SWMMModelLayer::DataPatterns;
        break;
    case K::LIDControl:
        m_dataAdapter = new SWMMLIDControlPropertyAdapter(eng, name, this);
        m_dataObjectCategory = SWMMModelLayer::DataLIDControls;
        break;
    case K::Aquifer:
        m_dataAdapter = new SWMMAquiferPropertyAdapter(eng, name, this);
        m_dataObjectCategory = SWMMModelLayer::DataAquifers;
        break;
    case K::Snowpack:
        m_dataAdapter = new SWMMSnowpackPropertyAdapter(eng, name, this);
        m_dataObjectCategory = SWMMModelLayer::DataSnowpacks;
        break;
    case K::Transect:
        m_dataAdapter = new SWMMTransectPropertyAdapter(eng, name, this);
        m_dataObjectCategory = SWMMModelLayer::DataTransects;
        // Slice BQ Phase 6.7.4 — keep the Property Browser live as the user
        // edits this transect from any other UI (TransectEditorDialog,
        // interactive chart drags). The transectChanged(name) signal carries
        // the affected transect name (or empty for rename / bulk operations).
        // The receiver lifetime is bound to the adapter so the connection
        // self-disconnects when the panel switches to a different object.
        if (layer) {
            connect(layer, &SWMMModelLayer::transectChanged, m_dataAdapter,
                    [this](const QString &txName) {
#ifdef HAVE_QPROPERTYMODEL
                        if (!m_dataAdapter)
                            return;
                        if (!txName.isEmpty() && txName != m_dataAdapter->name())
                            return;
                        if (auto *pm = qobject_cast<QPropertyModel *>(m_model))
                            pm->refreshValues();
#else
                        Q_UNUSED(txName);
#endif
                    });
        }
        break;
    case K::Hydrograph:
        m_dataAdapter = new SWMMHydrographPropertyAdapter(eng, name, this);
        m_dataObjectCategory = SWMMModelLayer::DataHydrographs;
        // Slice BS Phase 6.9.2 — keep the Property Browser live as the user
        // edits this group from any other UI (HydrographGroupEditor, the
        // NodeCompoundEditDialog RDII page, NewDataObjectDialog). The
        // hydrographChanged(name) signal carries the affected group name
        // (or empty for rename / bulk operations). The receiver lifetime
        // is bound to the adapter so the connection self-disconnects when
        // the panel switches to a different object.
        if (layer) {
            connect(layer, &SWMMModelLayer::hydrographChanged, m_dataAdapter,
                    [this](const QString &uhName) {
#ifdef HAVE_QPROPERTYMODEL
                        if (!m_dataAdapter)
                            return;
                        if (!uhName.isEmpty() && uhName != m_dataAdapter->name())
                            return;
                        if (auto *pm = qobject_cast<QPropertyModel *>(m_model))
                            pm->refreshValues();
#else
                        Q_UNUSED(uhName);
#endif
                    });
        }
        break;
    case K::Street:
        m_dataAdapter = new SWMMStreetPropertyAdapter(eng, name, this);
        m_dataObjectCategory = SWMMModelLayer::DataStreets;
        break;
    case K::Inlet:
        m_dataAdapter = new SWMMInletPropertyAdapter(eng, name, this);
        m_dataObjectCategory = SWMMModelLayer::DataInlets;
        break;
    case K::Control:
        m_dataAdapter = new SWMMControlRulePropertyAdapter(eng, name, this);
        m_dataObjectCategory = SWMMModelLayer::DataControls;
        // Slice BR Phase 6.8.1 — keep the Property Browser live as the user
        // edits this rule from any other UI (RulesEditorDialog, future
        // scenario-comparison views). The controlRulesChanged(name) signal
        // carries the affected rule name (or empty for rename / bulk
        // operations). The receiver lifetime is bound to the adapter so the
        // connection self-disconnects when the panel switches kinds.
        if (layer) {
            connect(layer, &SWMMModelLayer::controlRulesChanged, m_dataAdapter,
                    [this](const QString &ruleName) {
#ifdef HAVE_QPROPERTYMODEL
                        if (!m_dataAdapter)
                            return;
                        if (!ruleName.isEmpty() && ruleName != m_dataAdapter->name())
                            return;
                        if (auto *pm = qobject_cast<QPropertyModel *>(m_model))
                            pm->refreshValues();
#else
                        Q_UNUSED(ruleName);
#endif
                    });
        }
        break;
    case K::RainGage:
        // Rain gages have no DataCategory entry / no comprehensive editor;
        // the in-place property tree is the only edit surface.
        m_dataAdapter = new SWMMRainGagePropertyAdapter(eng, name, this);
        // Review C-01: a gage is a spatial object; its writes go through the
        // same checked, undoable path as the Attribute Table.
        static_cast<SWMMRainGagePropertyAdapter *>(m_dataAdapter)
            ->setFieldWriter(spatialFieldWriter(SWMMModelLayer::CatRainGages));
        break;
    default:
        // Caller passed a spatial kind (Node/Link/Subcatchment) or
        // Unknown; not our job — let the identify-result path handle it.
        if (auto *pm = qobject_cast<QPropertyModel *>(m_model))
            pm->clear();
        if (m_openEditorButton)
            m_openEditorButton->hide();
        if (m_plotRainGageButton)
            m_plotRainGageButton->hide();
        return;
    }

    // Slice BM.0-Browse-Edit (2026-05-25) — thread the layer pointer so
    // DataObjectRef-typed Q_PROPERTYs (Pollutant::coPollutant,
    // Hydrograph::gageName) construct refs with the right layer for the
    // picker editor + right-click menu.
    if (m_dataAdapter)
        m_dataAdapter->setModelLayer(layer);

    if (auto *pm = qobject_cast<QPropertyModel *>(m_model))
        pm->setData(QVariant::fromValue<QObject *>(m_dataAdapter));
    if (DataObjectAttributeTableModel::supportsCategory(m_dataObjectCategory)) {
        bindDataAggregate(layer, m_dataObjectCategory, {name}, layer->editCanvas() ? layer->editCanvas()->undoStack() : nullptr);
        m_dataAdapter->setCommitHandler([selection = QPointer<DataObjectAggregateSelection>(m_dataAggregate)](const QString &key, const QVariant &value) {
            if (!selection || !selection->active()) return false;
            const auto specs = selection->source()->columnSpecs();
            for (int column = 0; column < specs.size(); ++column)
                if (specs[column].key == key) return selection->setValue(column, value);
            return false;
        });
    }

    connect(m_dataAdapter, &SWMMDataObjectPropertyAdapter::changed, this, [this]() {
        if (!m_suppressEditForward && m_dataAdapter)
            emit objectEdited(m_dataAdapter->name());
    });
    // Of the DataCategory kinds handled by this switch, only RainGage
    // shares applyRename()'s node/link/gage/catchment namespace — hint it
    // explicitly so a rain-gage rename can't collide with a same-named
    // subcatchment. The other kinds (curves, patterns, pollutants, ...)
    // aren't network elements, so kKindAll (applyRename()'s legacy
    // best-effort scan) is unaffected either way.
    const quint8 renameKindHint = (static_cast<K>(objectKind) == K::RainGage)
                                      ? SWMMModelLayer::kKindGage
                                      : SWMMModelLayer::kKindAll;
    connect(m_dataAdapter, &SWMMDataObjectPropertyAdapter::renameRequested, this,
            [this, layer, renameKindHint](const QString &oldN, const QString &newN) {
                if (layer && layer->applyRename(oldN, newN, renameKindHint)) {
                    if (m_dataAdapter)
                        m_dataAdapter->updateStoredName(newN);
                    emit objectEdited(newN);
                }
            });

    // DA.2 parity — rain gage: drive editability of the source-specific rows
    // from the live dataSource. TIMESERIES enables the Series Name picker and
    // greys the file rows; FILE does the reverse. Recording interval and snow
    // catch factor apply to both sources and stay editable. Same trigger /
    // contract as the outfall + storage blocks in the node path above.
    if (auto *gageAdapter = qobject_cast<SWMMRainGagePropertyAdapter *>(m_dataAdapter)) {
        if (m_plotRainGageButton)
            m_plotRainGageButton->show();
        if (auto *pm = qobject_cast<QPropertyModel *>(m_model)) {
            auto applyGageRowFlags = [pm, gageAdapter]() {
                if (!pm)
                    return;
                const bool isFile = gageAdapter->dataSource() == 1; // FILE
                // Multi-column rain files (spec §4 task 4): a USER_CSV (6)
                // gage selects a column by name, not a station id — gate
                // Station ID off for that format. UNKNOWN (-1, fresh gage)
                // and STAN_PRCP (5) keep Station ID editable so a standard
                // rain file can still be configured from scratch.
                const int fmt = gageAdapter->fileFormat();
                setRowEditable(pm, gageAdapter, QStringLiteral("seriesName"), !isFile);
                setRowEditable(pm, gageAdapter, QStringLiteral("filePath"), isFile);
                setRowEditable(pm, gageAdapter, QStringLiteral("fileColumn"), isFile);
                // The grammar row itself — the only way back out of USER_CSV,
                // and therefore the only way Station ID below can be un-greyed
                // once a column has been picked.
                setRowEditable(pm, gageAdapter, QStringLiteral("fileFormat"), isFile);
                setRowEditable(pm, gageAdapter, QStringLiteral("stationId"),
                               isFile && fmt != 6 /* USER_CSV */);
                setRowEditable(pm, gageAdapter, QStringLiteral("rainUnits"), isFile);
            };
            connect(gageAdapter, &SWMMDataObjectPropertyAdapter::changed, pm, applyGageRowFlags);
            applyGageRowFlags();
        }

        // A rain gage is a placed object: route its X/Y edits through
        // applyGageMove so the cached scene point and the model extent move
        // with the engine value, exactly as the node path does. (A bare
        // swmm_spatial_set_gage_coord would leave the canvas stale until the
        // next geometry rebuild.)
        connect(gageAdapter, &SWMMRainGagePropertyAdapter::coordChangeRequested, this,
                [this, layer](double newX, double newY) {
                    if (!layer || !layer->engine() || !m_dataAdapter)
                        return;
                    const QString current = m_dataAdapter->name();
                    const int gi = swmm_gage_index(layer->engine(), current.toUtf8().constData());
                    if (gi >= 0 && layer->applyGageMove(gi, newX, newY))
                        emit objectEdited(current);
                });
    }

    m_treeView->expandAll();
    setWindowTitle(tr("Properties — %1").arg(name));

    // 2026-05-29 — surface the header "Open in <Editor>…" button when the
    // current data category has a shipped CRUD editor. The button text
    // carries the editor's user-visible title plus the object name so
    // the user can tell which object they're about to open.
    if (m_openEditorButton) {
        const auto &reg = ComprehensiveEditorRegistry::instance();
        const bool shipped = m_dataObjectCategory != SWMMModelLayer::NumDataCategories &&
                             reg.hasEditor(m_dataObjectCategory);
        if (shipped) {
            const QString title = reg.editorTitle(m_dataObjectCategory);
            m_openEditorButton->setText(
                tr("Open \"%1\" in %2…").arg(name, title.isEmpty() ? tr("Editor") : title));
            m_openEditorButton->show();
        } else {
            m_openEditorButton->hide();
        }
    }
#else
    Q_UNUSED(layer);
    Q_UNUSED(objectKind);
    Q_UNUSED(name);
#endif
}

// ---------------------------------------------------------------------------
// 2026-05-29 — Header "Open in <Editor>…" button click
// ---------------------------------------------------------------------------
//
// Dispatches through `ComprehensiveEditorRegistry::openForObject`, the same
// table used by the Object Browser and Attribute Table.

void PropertiesPanel::onOpenInEditorClicked()
{
    if (!m_swmmLayer || !m_dataAdapter || m_dataObjectCategory == SWMMModelLayer::NumDataCategories)
        return;
    if (!ComprehensiveEditorRegistry::instance().hasEditor(m_dataObjectCategory))
        return;

    auto *canvas = m_swmmLayer->editCanvas();
    ComprehensiveEditorRegistry::instance().openForObject(m_dataObjectCategory, m_swmmLayer,
                                                          canvas ? canvas->undoStack() : nullptr,
                                                          m_dataAdapter->name(), this);
}

void PropertiesPanel::onTreeContextMenu(const QPoint &pos)
{
    if (!m_treeView || !m_model)
        return;
    const QModelIndex idx = m_treeView->indexAt(pos);
    if (!idx.isValid())
        return;

    // Custom editors live on the value column (1); column 0 is the label.
    const QModelIndex valueIdx = (idx.column() == 1) ? idx : idx.sibling(idx.row(), 1);
    if (!valueIdx.isValid())
        return;

    const QVariant value = valueIdx.data(Qt::EditRole);
    if (!value.isValid())
        return;

    QMenu menu(m_treeView);
    const int metaId = value.metaType().id();

    if (metaId == qMetaTypeId<DataRecordsRef>()) {
        openDataRecords(value.value<DataRecordsRef>(), m_treeView);
        return;
    }

    if (metaId == qMetaTypeId<DataObjectRef>()) {
        if (addDataObjectReferenceAction(menu, m_treeView, valueIdx))
            menu.exec(m_treeView->viewport()->mapToGlobal(pos));
        return;
    }

    if (m_dataAdapter && (m_dataObjectCategory == SWMMModelLayer::DataControls ||
                          m_dataObjectCategory == SWMMModelLayer::DataTransects)) {
        const auto category = m_dataObjectCategory;
        const auto name = m_dataAdapter->name();
        QPointer<SWMMModelLayer> owner(m_dataAdapter->modelLayer());
        const auto engine = owner ? owner->engine() : nullptr;
        const auto &registry = ComprehensiveEditorRegistry::instance();
        auto *edit =
            menu.addAction(tr("Open \"%1\" in %2…").arg(name, registry.editorTitle(category)));
        if (menu.exec(m_treeView->viewport()->mapToGlobal(pos)) == edit && owner &&
            owner == m_swmmLayer && owner->engine() == engine) {
            auto *canvas = owner->editCanvas();
            registry.openForObject(category, owner, canvas ? canvas->undoStack() : nullptr, name,
                                   this);
        }
        return;
    }

    if (metaId == qMetaTypeId<UsageEditRef>()) {
        auto ref = value.value<UsageEditRef>();
        QPointer<SWMMModelLayer> owner = ref.layer;
        auto *edit = menu.addAction(tr("Edit assignments…"));
        if (menu.exec(m_treeView->viewport()->mapToGlobal(pos)) == edit && owner &&
            owner->engine() == ref.engine)
            openUsageEditor(ref, window());
        return;
    }
    if (metaId == qMetaTypeId<NodeCompoundEditRef>()) {
        const NodeCompoundEditRef ref = value.value<NodeCompoundEditRef>();
        if (!ref.engine || ref.nodeName.isEmpty())
            return;
        // Groundwater Sources is a navigational row (opens the owning
        // subcatchment's GroundwaterExchangeDialog from its cell button);
        // NodeCompoundEditDialog has no page for it.
        if (ref.kind == NodeCompoundEditRef::GroundwaterSources)
            return;

        QAction *actEdit = menu.addAction(tr("Edit…"));
        QAction *picked = menu.exec(m_treeView->viewport()->mapToGlobal(pos));
        if (!picked || picked != actEdit)
            return;

        if (ref.layer && ref.kind <= NodeCompoundEditRef::Treatment) {
            auto owned = ref;
            if (!owned.undoStack)
                for (auto *canvas : window()->findChildren<MapCanvas *>())
                    if (canvas->layers().contains(ref.layer)) { owned.undoStack = canvas->undoStack(); break; }
            auto *dialog = new NodeCompoundEditDialog(owned, window());
            dialog->setAttribute(Qt::WA_DeleteOnClose); dialog->show();
            return;
        }
        NodeCompoundEditDialog dlg(ref, m_treeView);
        dlg.exec();
        // Even on Cancel: per NodeCompoundEditButton::onClicked the dialog
        // commits some pages immediately, so pull the updated summary back.
        NodeCompoundEditRef updated = ref;
        updated.summary = dlg.updatedSummary();
        m_treeView->model()->setData(valueIdx, QVariant::fromValue(updated), Qt::EditRole);
        return;
    }

    // Slice SC.1 — same right-click dispatch for link-side compound cells.
    if (metaId == qMetaTypeId<LinkCompoundEditRef>()) {
        const LinkCompoundEditRef ref = value.value<LinkCompoundEditRef>();
        QPointer<SWMMModelLayer> owner = ref.layer;
        if (!ref.engine || ref.linkName.isEmpty())
            return;

        QAction *actEdit = menu.addAction(tr("Edit…"));
        QAction *picked = menu.exec(m_treeView->viewport()->mapToGlobal(pos));
        if (!picked || picked != actEdit)
            return;

        if (ref.layer && (!owner || owner->engine() != ref.engine)) return;
        int type = -1;
        swmm_link_get_type(ref.engine, swmm_link_index(ref.engine, ref.linkName.toUtf8().constData()), &type);
        if (ref.layer && (ref.kind == LinkCompoundEditRef::InletUsage ||
            type == SWMM_LINK_ORIFICE || type == SWMM_LINK_WEIR)) {
            if (!owner || owner->engine() != ref.engine)
                return;
            auto *dialog = new LinkCompoundEditDialog(ref, window());
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->show();
            return;
        }
        LinkCompoundEditDialog dlg(ref, m_treeView);
        dlg.exec();
        LinkCompoundEditRef updated = ref;
        updated.summary = dlg.updatedSummary();
        m_treeView->model()->setData(valueIdx, QVariant::fromValue(updated), Qt::EditRole);
        return;
    }

    if (metaId == qMetaTypeId<SubcatchCompoundEditRef>()) {
        auto ref = value.value<SubcatchCompoundEditRef>();
        if (!ref.layer || (ref.kind != SubcatchCompoundEditRef::LandUse &&
                           ref.kind != SubcatchCompoundEditRef::Loadings &&
                           ref.kind != SubcatchCompoundEditRef::Groundwater &&
                           ref.kind != SubcatchCompoundEditRef::LidUsage))
            return;
        QPointer<SWMMModelLayer> owner = ref.layer;
        auto *action = menu.addAction(tr("Edit…"));
        if (menu.exec(m_treeView->viewport()->mapToGlobal(pos)) != action)
            return;
        if (!owner || owner->engine() != ref.engine)
            return;
        if (!ref.undoStack)
            for (auto *canvas : window()->findChildren<MapCanvas *>())
                if (canvas->layers().contains(ref.layer)) {
                    ref.undoStack = canvas->undoStack();
                    break;
                }
        QDialog *dialog = nullptr;
        if (ref.kind == SubcatchCompoundEditRef::Groundwater)
            dialog = new GroundwaterExchangeDialog(ref, window());
        else
            dialog = new SubcatchCompoundEditDialog(ref, window());
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
        return;
    }
    // Plain row — no menu.
}

// ---------------------------------------------------------------------------
// Slice QA.2 — stats-source combo
// ---------------------------------------------------------------------------

void PropertiesPanel::setStatsRegistry(openswmmvis::OutputStatsRegistry *registry)
{
    if (m_statsRegistry == registry)
        return;

    // Disconnect previous registry's identitiesChanged so we don't leave
    // a dangling signal connection pointing at a destroyed registry.
    if (m_statsRegistry) {
        disconnect(m_statsRegistry, nullptr, this, nullptr);
    }
    m_statsRegistry = registry;
    if (m_statsRegistry) {
        connect(m_statsRegistry, &openswmmvis::OutputStatsRegistry::identitiesChanged, this,
                &PropertiesPanel::refreshStatsSourceCombo);
    }
    refreshStatsSourceCombo();

    // Push the registry pointer into every bound typed adapter so their
    // getters can dispatch through it. Done on every bind so an adapter
    // never holds a stale pointer.
    if (m_nodeAdapter)
        m_nodeAdapter->setStatsRegistry(m_statsRegistry);
    if (m_linkAdapter)
        m_linkAdapter->setStatsRegistry(m_statsRegistry);
    if (m_subcatchAdapter)
        m_subcatchAdapter->setStatsRegistry(m_statsRegistry);
}

void PropertiesPanel::setActiveResultsLayer(SWMMResultsLayer *layer)
{
    if (m_activeStatsLayer != layer) {
        if (m_activeStatsLayer)
            disconnect(m_activeStatsLayer, nullptr, this, nullptr);
        m_activeStatsLayer = layer;
        if (m_activeStatsLayer) {
            // A re-run into the same .out re-opens the layer's results in
            // place (no activeResultsLayerChanged fires), and a live-tailed
            // run finalizes without swapping layers. Both mean fresh
            // statistics for the same bound source — re-read the rows.
            connect(m_activeStatsLayer, &SWMMResultsLayer::resultsOpened, this,
                    &PropertiesPanel::refreshAdapterStatValues);
            connect(m_activeStatsLayer, &SWMMResultsLayer::resultsFinalized, this,
                    &PropertiesPanel::refreshAdapterStatValues);
        }
    }

    // Follow the active run in the Stats-source combo (the layer is
    // registered on canvas layerAdded, so its identity is present by the
    // time it becomes active). Null layer → the editing-engine sentinel
    // at row 0. Selection changes fire currentIndexChanged →
    // applyStatsSourceToAdapter; an unchanged selection still needs the
    // explicit refresh below so a re-run's new numbers repaint.
    if (m_statsSourceCombo) {
        QUuid targetId;
        if (layer && m_statsRegistry) {
            const auto ids = m_statsRegistry->identities();
            for (const auto &id : ids) {
                if (id.layer == layer) {
                    targetId = id.stableId;
                    break;
                }
            }
        }
        int targetRow = 0;
        for (int i = 0; i < m_statsSourceCombo->count(); ++i) {
            const QVariant payload = m_statsSourceCombo->itemData(i);
            if (payload.canConvert<QUuid>() && payload.toUuid() == targetId) {
                targetRow = i;
                break;
            }
        }
        if (m_statsSourceCombo->currentIndex() != targetRow)
            m_statsSourceCombo->setCurrentIndex(targetRow);
    }
    applyStatsSourceToAdapter();
}

void PropertiesPanel::refreshAdapterStatValues()
{
    // The values changed because a run produced new statistics — not a
    // model edit — so don't forward objectEdited (it would mark the
    // project dirty). QPropertyModel doesn't subscribe to NOTIFY signals;
    // refreshValues() re-reads every row live (see onObjectEditedExternally).
    m_suppressEditForward = true;
#ifdef HAVE_QPROPERTYMODEL
    if (auto *pm = qobject_cast<QPropertyModel *>(m_model))
        pm->refreshValues();
#endif
    m_suppressEditForward = false;
}

void PropertiesPanel::refreshStatsSourceCombo()
{
    if (!m_statsSourceCombo)
        return;

    // Preserve the current selection by stableId so an unrelated
    // mutation (e.g. another layer added or removed) doesn't yank the
    // user's pick out from under them. Falls through to the editing-
    // engine sentinel when the previously-selected source was removed.
    const QVariant prev = m_statsSourceCombo->currentData();
    const QUuid prevId = prev.canConvert<QUuid>() ? prev.toUuid() : QUuid();

    // Block signals while rebuilding so we don't fire a spurious
    // applyStatsSourceToAdapter mid-rebuild.
    QSignalBlocker blocker(m_statsSourceCombo);
    m_statsSourceCombo->clear();

    // The "(editing engine)" sentinel is always first and maps to the
    // null UUID — picking it restores today's behaviour.
    m_statsSourceCombo->addItem(tr("(editing engine)"), QVariant::fromValue(QUuid()));

    int restoreIdx = 0;
    if (m_statsRegistry) {
        const auto ids = m_statsRegistry->identities();
        for (const auto &id : ids) {
            const int row = m_statsSourceCombo->count();
            m_statsSourceCombo->addItem(id.shortLabel, QVariant::fromValue(id.stableId));
            m_statsSourceCombo->setItemData(row, id.tooltipPath, Qt::ToolTipRole);
            if (id.stableId == prevId)
                restoreIdx = row;
        }
    }

    m_statsSourceCombo->setCurrentIndex(restoreIdx);

    // Visibility: combo is meaningful only when (a) an adapter with
    // post-run stat rows is currently displayed AND (b) at least one real
    // output is loaded (count > 1 because the sentinel is always present).
    const bool show =
        (m_nodeAdapter || m_linkAdapter || m_subcatchAdapter) && (m_statsSourceCombo->count() > 1);
    if (m_statsSourceLabel)
        m_statsSourceLabel->setVisible(show);
    m_statsSourceCombo->setVisible(show);

    // Apply the (possibly re-resolved) selection to the adapter so its
    // value-getters dispatch through the right source. Unblocked here
    // because we want the slot to run.
    blocker.unblock();
    applyStatsSourceToAdapter();
}

void PropertiesPanel::applyStatsSourceToAdapter()
{
    if (!m_statsSourceCombo)
        return;
    if (!m_nodeAdapter && !m_linkAdapter && !m_subcatchAdapter)
        return;
    const QVariant payload = m_statsSourceCombo->currentData();
    const QUuid id = payload.canConvert<QUuid>() ? payload.toUuid() : QUuid();
    // setStatsSource emits the adapter's changed() — a view re-target,
    // not a model edit, so keep it out of the objectEdited fan-out
    // (which would mark the project dirty).
    m_suppressEditForward = true;
    if (m_nodeAdapter)
        m_nodeAdapter->setStatsSource(id);
    if (m_linkAdapter)
        m_linkAdapter->setStatsSource(id);
    if (m_subcatchAdapter)
        m_subcatchAdapter->setStatsSource(id);
    m_suppressEditForward = false;
    refreshAdapterStatValues();
}

#ifdef HAVE_QPROPERTYMODEL
#include "propertiespanel.moc"
#endif
