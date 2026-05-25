/*!
 * \file   attributepanel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/panels/attributepanel.h"
#include "layers/openswmmvislayer.h"
#include "layers/swmmmodellayer.h"
#include "map/tools/maptoolidentify.h"   // IdentifyResult
#include "selection/selectionmanager.h"  // SWMMObjectRef::ObjectType
#include "ui/properties/dataobjectpickereditor.h"
#include "ui/properties/dataobjectref.h"
#include "ui/properties/nodecompoundeditbutton.h"
#include "ui/properties/nodecompoundeditref.h"
#include "ui/properties/swmmlinkpropertyadapter.h"
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

#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_nodes.h>

// QPropertyModel library
#ifdef HAVE_QPROPERTYMODEL
#include <qpropertymodel.h>
#include <qpropertyitem.h>
#include <qpropertyitemdelegate.h>
#endif

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QStandardItemModel>
#include <QTreeView>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>

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

namespace {

QModelIndex findPropertyIndexByDisplayLabel(QAbstractItemModel *model,
                                              const QString &label,
                                              const QModelIndex &parent = {})
{
    if (!model) return {};
    const int n = model->rowCount(parent);
    for (int i = 0; i < n; ++i) {
        const QModelIndex idx = model->index(i, 0, parent);
        if (model->data(idx, Qt::DisplayRole).toString() == label)
            return idx;
        if (model->hasChildren(idx)) {
            const QModelIndex found = findPropertyIndexByDisplayLabel(model, label, idx);
            if (found.isValid()) return found;
        }
    }
    return {};
}

/*! Toggle the editable/enabled flags of one property row, located by
 *  raw Q_PROPERTY name. Routes through the adapter's `displayLabelFor`
 *  to translate the name to the displayed label that QPropertyItem
 *  carries. No-op if the row isn't found (e.g. the property is not
 *  surfaced on the current node-kind subclass). */
void setRowEditable(QPropertyModel *pm, QObject *adapter,
                     const QString &rawProperty, bool editable)
{
    if (!pm || !adapter) return;

    QString label;
    QMetaObject::invokeMethod(adapter, "displayLabelFor", Qt::DirectConnection,
                              Q_RETURN_ARG(QString, label),
                              Q_ARG(QString, rawProperty));
    if (label.isEmpty()) label = rawProperty;

    const QModelIndex idx = findPropertyIndexByDisplayLabel(pm, label);
    if (!idx.isValid()) return;

    auto *item = static_cast<QPropertyItem*>(idx.internalPointer());
    if (!item) return;

    Qt::ItemFlags f = item->flags();
    if (editable)
        f |=  (Qt::ItemIsEnabled | Qt::ItemIsEditable);
    else
        f &= ~(Qt::ItemIsEnabled | Qt::ItemIsEditable);
    item->setFlags(f);

    // Nudge the view to re-query flags + repaint the greyed cell.
    QMetaObject::invokeMethod(pm, "onDataChanged", Qt::DirectConnection,
                              Q_ARG(QModelIndex, idx));
    const QModelIndex valIdx = pm->index(idx.row(), 1, idx.parent());
    QMetaObject::invokeMethod(pm, "onDataChanged", Qt::DirectConnection,
                              Q_ARG(QModelIndex, valIdx));
}

} // namespace
#endif // HAVE_QPROPERTYMODEL

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

AttributePanel::AttributePanel(QWidget *parent)
    : QDockWidget(tr("Attributes"), parent)
{
    setupUi();
}

AttributePanel::~AttributePanel() = default;

// ---------------------------------------------------------------------------
// UI setup
// ---------------------------------------------------------------------------

void AttributePanel::setupUi()
{
    auto *central = new QWidget(this);
    auto *vlay    = new QVBoxLayout(central);
    vlay->setContentsMargins(2, 2, 2, 2);
    vlay->setSpacing(4);

    // Layer selector row
    auto *hlay = new QHBoxLayout;
    hlay->addWidget(new QLabel(tr("Layer:"), central));
    m_layerCombo = new QComboBox(central);
    hlay->addWidget(m_layerCombo, 1);
    vlay->addLayout(hlay);

    // Tree view
    m_treeView = new QTreeView(central);
    m_treeView->setAlternatingRowColors(true);
    m_treeView->setEditTriggers(QAbstractItemView::AllEditTriggers);
    vlay->addWidget(m_treeView, 1);

    setWidget(central);

    // Property model / delegate
#ifdef HAVE_QPROPERTYMODEL
    m_model    = new QPropertyModel(this);
    auto *delegate = new QPropertyItemDelegate(this);
    m_delegate = delegate;

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

    // Slice DA.4.3 — same dance for `DataObjectRef`, used by the new
    // outfall stage-data picker rows (Tidal Curve, Stage Time Series).
    // Display-side converter renders the picked object name in the cell;
    // edit-side creator gives the user a filtered combobox + "…" button.
    qRegisterMetaType<DataObjectRef>("DataObjectRef");
    registerDataObjectRefConverter();
    delegate->registerCustomTypeEditorCreator(
        QMetaType::Type(qMetaTypeId<DataObjectRef>()),
        new QStandardItemEditorCreator<DataObjectPickerEditor>());
#else
    m_model    = new QStandardItemModel(this);
#endif

    m_treeView->setModel(m_model);
    if (m_delegate)
        m_treeView->setItemDelegate(m_delegate);
    m_treeView->header()->setDefaultSectionSize(150);

    connect(m_layerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AttributePanel::onLayerComboIndexChanged);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void AttributePanel::showObject(QObject *object, const QString &title)
{
    setWindowTitle(title.isEmpty() ? tr("Attributes") : title);
    m_layerCombo->clear();
    m_lastResults.clear();

#ifdef HAVE_QPROPERTYMODEL
    auto *pm = qobject_cast<QPropertyModel*>(m_model);
    if (!object)
    {
        if (pm) pm->clear();
        return;
    }
    if (pm) pm->setData(QVariant::fromValue(object));
#else
    Q_UNUSED(object);
#endif
    m_treeView->expandAll();
}

void AttributePanel::showIdentifyResults(const QList<IdentifyResult> &results)
{
    m_lastResults = results;
    m_layerCombo->clear();

    for (const auto &r : results)
        m_layerCombo->addItem(r.layerName);

    if (!results.isEmpty())
        onLayerComboIndexChanged(0);
#ifdef HAVE_QPROPERTYMODEL
    else if (auto *pm = qobject_cast<QPropertyModel*>(m_model))
        pm->clear();
#endif
}

void AttributePanel::showLayerProperties(OpenSWMMVisLayer *layer)
{
    showObject(layer, tr("Layer Properties — %1").arg(layer ? layer->name() : QString()));
}

void AttributePanel::clear()
{
#ifdef HAVE_QPROPERTYMODEL
    if (auto *pm = qobject_cast<QPropertyModel*>(m_model))
        pm->clear();
#endif
    m_layerCombo->clear();
    m_lastResults.clear();
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void AttributePanel::onIdentifyResult(const QList<IdentifyResult> &results)
{
    showIdentifyResults(results);
}

void AttributePanel::onObjectEditedExternally(const QString &name)
{
    // Mirror an external attribute change into the Property Browser.
    // QPropertyModel doesn't subscribe to NOTIFY signals, so we cannot
    // rely on adapter->refresh() / emit changed() to trigger a view
    // repaint.  Instead, call QPropertyModel::refreshValues() which emits
    // dataChanged for every value cell; QVariantPropertyItem::data() then
    // re-reads the current value from the adapter live via
    // QMetaProperty::read().  Guard against the resulting dataChanged
    // bouncing an objectEdited back to the attribute table.
    if (name.isEmpty()) return;
    m_suppressEditForward = true;
#ifdef HAVE_QPROPERTYMODEL
    auto *pm = qobject_cast<QPropertyModel*>(m_model);
    if (pm && (
            (m_nodeAdapter     && m_nodeAdapter->name()     == name) ||
            (m_linkAdapter     && m_linkAdapter->name()     == name) ||
            (m_subcatchAdapter && m_subcatchAdapter->name() == name)))
    {
        pm->refreshValues();
    }
#endif
    m_suppressEditForward = false;
}

void AttributePanel::setProject(SWMMModelLayer *layer)
{
    if (m_swmmLayer == layer) return;

    // Disconnect from the old layer before replacing it.
    if (m_swmmLayer)
        QObject::disconnect(m_swmmLayer, &SWMMModelLayer::attributeChanged,
                            this, &AttributePanel::onObjectEditedExternally);

    m_swmmLayer = layer;

    if (!layer) {
        // Releasing the project — drop the adapters so a stale engine
        // handle doesn't get used on the next identify.
        if (m_nodeAdapter)     { m_nodeAdapter->deleteLater();     m_nodeAdapter     = nullptr; }
        if (m_linkAdapter)     { m_linkAdapter->deleteLater();     m_linkAdapter     = nullptr; }
        if (m_subcatchAdapter) { m_subcatchAdapter->deleteLater(); m_subcatchAdapter = nullptr; }
        return;
    }

    // Refresh the property browser whenever an attribute changes (e.g. conduit
    // length or subcatchment area recalculated from an edited geometry).
    connect(layer, &SWMMModelLayer::attributeChanged,
            this,  &AttributePanel::onObjectEditedExternally,
            Qt::UniqueConnection);
}

void AttributePanel::onLayerComboIndexChanged(int index)
{
    if (index < 0 || index >= m_lastResults.size())
        return;

    const IdentifyResult &result = m_lastResults.at(index);

    if (result.features.isEmpty())
    {
#ifdef HAVE_QPROPERTYMODEL
        if (auto *pm = qobject_cast<QPropertyModel*>(m_model))
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
    const QString name    = feature.value(QStringLiteral("Name")).toString();
    if (m_swmmLayer && !name.isEmpty() && m_swmmLayer->engine()) {
        auto *pm = qobject_cast<QPropertyModel*>(m_model);
        if (typeStr == QStringLiteral("Node")) {
            if (m_nodeAdapter) m_nodeAdapter->deleteLater();
            // Round-4 follow-up 2026-05-12 — pick the SWMM-type-aware
            // subclass so the Property Browser only exposes attributes
            // applicable to this node's kind (Junction / Outfall /
            // Storage / Divider), matching the [JUNCTIONS]/[OUTFALLS]/
            // [STORAGE]/[DIVIDERS] .inp sections.
            const int nodeIdx = swmm_node_index(
                m_swmmLayer->engine(), name.toUtf8().constData());
            int nodeKind = 0;
            swmm_node_get_type(m_swmmLayer->engine(), nodeIdx, &nodeKind);
            switch (nodeKind) {
            case SWMM_NODE_OUTFALL:
                m_nodeAdapter = new SWMMOutfallPropertyAdapter(
                    m_swmmLayer->engine(), name, this);
                break;
            case SWMM_NODE_STORAGE:
                m_nodeAdapter = new SWMMStoragePropertyAdapter(
                    m_swmmLayer->engine(), name, this);
                break;
            case SWMM_NODE_DIVIDER:
                m_nodeAdapter = new SWMMDividerPropertyAdapter(
                    m_swmmLayer->engine(), name, this);
                break;
            case SWMM_NODE_JUNCTION:
            default:
                m_nodeAdapter = new SWMMJunctionPropertyAdapter(
                    m_swmmLayer->engine(), name, this);
                break;
            }
            // DB.4c — thread the layer pointer so the compound-edit
            // pickers (Inflows TS / Pattern, DWF patterns, RDII UH)
            // can call back into `layer->createDataObject(...)`.
            m_nodeAdapter->setModelLayer(m_swmmLayer);
            if (pm) pm->setData(QVariant::fromValue<QObject *>(m_nodeAdapter));

            // Slice DA.4.3 — for outfalls only, drive editability of the
            // three stage-data rows from the live `outfallType`. Reuses
            // the adapter's existing `changed()` signal as the trigger
            // (every setter — including setOutfallType — emits it). A
            // separate signal isn't needed and would just duplicate the
            // existing notification chain.
            if (auto *outfallAdapter =
                    qobject_cast<SWMMOutfallPropertyAdapter*>(m_nodeAdapter))
            {
                auto applyOutfallRowFlags = [pm, outfallAdapter]() {
                    if (!pm) return;
                    const auto t = outfallAdapter->outfallType();
                    setRowEditable(pm, outfallAdapter, QStringLiteral("outfallStage"),
                                   t == SWMMNodePropertyAdapter::FIXED);
                    setRowEditable(pm, outfallAdapter, QStringLiteral("outfallTidalCurve"),
                                   t == SWMMNodePropertyAdapter::TIDAL);
                    setRowEditable(pm, outfallAdapter, QStringLiteral("outfallTimeseries"),
                                   t == SWMMNodePropertyAdapter::TIMESERIES);
                };
                connect(outfallAdapter, &SWMMNodePropertyAdapter::changed,
                        pm, applyOutfallRowFlags);
                // Initial pass — runs after setData() above has populated
                // the property tree so findPropertyIndexByDisplayLabel can
                // locate the rows.
                applyOutfallRowFlags();
            }

            connect(m_nodeAdapter, &SWMMNodePropertyAdapter::changed,
                    this, [this, name]() {
                        if (!m_suppressEditForward) emit objectEdited(name);
                    });
            connect(m_nodeAdapter, &SWMMNodePropertyAdapter::renameRequested,
                    this, [this](const QString &oldN, const QString &newN) {
                        if (m_swmmLayer && m_swmmLayer->applyRename(oldN, newN)) {
                            if (m_nodeAdapter) m_nodeAdapter->updateStoredName(newN);
                            emit objectEdited(newN);
                        }
                    });
            // Slice DB — route X/Y edits through applyNodeMove so the
            // scene cache + attached-link bboxes refresh atomically with
            // the engine write (a bare swmm_spatial_set_node_coord would
            // leave the canvas stale until the next geometry rebuild).
            connect(m_nodeAdapter, &SWMMNodePropertyAdapter::coordChangeRequested,
                    this, [this, name](double newX, double newY) {
                        if (!m_swmmLayer) return;
                        const int idx = swmm_node_index(
                            m_swmmLayer->engine(), name.toUtf8().constData());
                        if (idx >= 0 && m_swmmLayer->applyNodeMove(idx, newX, newY))
                            emit objectEdited(name);
                    });
            routedThroughAdapter = true;
        } else if (typeStr == QStringLiteral("Link")) {
            if (m_linkAdapter) m_linkAdapter->deleteLater();
            // Round-4 follow-up 2026-05-12 — pick the SWMM-link-type
            // aware subclass so the Property Browser only exposes
            // attributes applicable to this link's kind (Conduit /
            // Pump / Orifice / Weir / Outlet), matching the [CONDUITS]
            // / [PUMPS] / [ORIFICES] / [WEIRS] / [OUTLETS] sections.
            const int linkIdx = swmm_link_index(
                m_swmmLayer->engine(), name.toUtf8().constData());
            int linkKind = 0;
            swmm_link_get_type(m_swmmLayer->engine(), linkIdx, &linkKind);
            switch (linkKind) {
            case 1: // Pump
                m_linkAdapter = new SWMMPumpPropertyAdapter(
                    m_swmmLayer->engine(), name, this);
                break;
            case 2: // Orifice
                m_linkAdapter = new SWMMOrificePropertyAdapter(
                    m_swmmLayer->engine(), name, this);
                break;
            case 3: // Weir
                m_linkAdapter = new SWMMWeirPropertyAdapter(
                    m_swmmLayer->engine(), name, this);
                break;
            case 4: // Outlet
                m_linkAdapter = new SWMMOutletPropertyAdapter(
                    m_swmmLayer->engine(), name, this);
                break;
            case 0: // Conduit
            default:
                m_linkAdapter = new SWMMConduitPropertyAdapter(
                    m_swmmLayer->engine(), name, this);
                break;
            }
            if (pm) pm->setData(QVariant::fromValue<QObject *>(m_linkAdapter));
            connect(m_linkAdapter, &SWMMLinkPropertyAdapter::changed,
                    this, [this, name]() {
                        if (!m_suppressEditForward) emit objectEdited(name);
                    });
            connect(m_linkAdapter, &SWMMLinkPropertyAdapter::renameRequested,
                    this, [this](const QString &oldN, const QString &newN) {
                        if (m_swmmLayer && m_swmmLayer->applyRename(oldN, newN)) {
                            if (m_linkAdapter) m_linkAdapter->updateStoredName(newN);
                            emit objectEdited(newN);
                        }
                    });
            routedThroughAdapter = true;
        } else if (typeStr == QStringLiteral("Subcatchment")) {
            if (m_subcatchAdapter) m_subcatchAdapter->deleteLater();
            m_subcatchAdapter = new SWMMSubcatchPropertyAdapter(
                m_swmmLayer->engine(), name, this);
            if (pm) pm->setData(QVariant::fromValue<QObject *>(m_subcatchAdapter));
            connect(m_subcatchAdapter, &SWMMSubcatchPropertyAdapter::changed,
                    this, [this, name]() {
                        if (!m_suppressEditForward) emit objectEdited(name);
                    });
            connect(m_subcatchAdapter, &SWMMSubcatchPropertyAdapter::renameRequested,
                    this, [this](const QString &oldN, const QString &newN) {
                        if (m_swmmLayer && m_swmmLayer->applyRename(oldN, newN)) {
                            if (m_subcatchAdapter) m_subcatchAdapter->updateStoredName(newN);
                            emit objectEdited(newN);
                        }
                    });
            routedThroughAdapter = true;
        }
    }
    if (!routedThroughAdapter) {
        if (auto *pm = qobject_cast<QPropertyModel*>(m_model))
            pm->setData(QVariant(feature));
    }
#else
    Q_UNUSED(feature);
    Q_UNUSED(routedThroughAdapter);
#endif
    m_treeView->expandAll();

    setWindowTitle(tr("Attributes — %1 (%2 feature%3)")
                   .arg(result.layerName)
                   .arg(result.features.size())
                   .arg(result.features.size() == 1 ? QString() : QStringLiteral("s")));
}

// ---------------------------------------------------------------------------
// Slice DA.2 — Non-spatial Data Object dispatch
// ---------------------------------------------------------------------------

void AttributePanel::showDataObject(SWMMModelLayer *layer, int objectKind,
                                      const QString &name)
{
#ifdef HAVE_QPROPERTYMODEL
    if (!layer || !layer->engine() || name.isEmpty()) {
        if (auto *pm = qobject_cast<QPropertyModel *>(m_model)) pm->clear();
        return;
    }
    if (m_dataAdapter) { m_dataAdapter->deleteLater(); m_dataAdapter = nullptr; }

    using K = SWMMObjectRef::ObjectType;
    SWMM_Engine eng = layer->engine();

    switch (static_cast<K>(objectKind)) {
    case K::Pollutant:
        m_dataAdapter = new SWMMPollutantPropertyAdapter(eng, name, this); break;
    case K::LandUse:
        m_dataAdapter = new SWMMLandUsePropertyAdapter(eng, name, this); break;
    case K::Curve:
        m_dataAdapter = new SWMMCurvePropertyAdapter(eng, name, this); break;
    case K::TimeSeries:
        m_dataAdapter = new SWMMTimeSeriesPropertyAdapter(eng, name, this); break;
    case K::TimePattern:
        m_dataAdapter = new SWMMPatternPropertyAdapter(eng, name, this); break;
    case K::LIDControl:
        m_dataAdapter = new SWMMLIDControlPropertyAdapter(eng, name, this); break;
    case K::Aquifer:
        m_dataAdapter = new SWMMAquiferPropertyAdapter(eng, name, this); break;
    case K::Snowpack:
        m_dataAdapter = new SWMMSnowpackPropertyAdapter(eng, name, this); break;
    case K::Transect:
        m_dataAdapter = new SWMMTransectPropertyAdapter(eng, name, this); break;
    case K::Hydrograph:
        m_dataAdapter = new SWMMHydrographPropertyAdapter(eng, name, this);
        // Slice BS Phase 6.9.2 — keep the Property Browser live as the user
        // edits this group from any other UI (HydrographGroupEditor, the
        // NodeCompoundEditDialog RDII page, NewDataObjectDialog). The
        // hydrographChanged(name) signal carries the affected group name
        // (or empty for rename / bulk operations). The receiver lifetime
        // is bound to the adapter so the connection self-disconnects when
        // the panel switches to a different object.
        if (layer) {
            connect(layer, &SWMMModelLayer::hydrographChanged,
                    m_dataAdapter,
                    [this](const QString &uhName) {
#ifdef HAVE_QPROPERTYMODEL
                        if (!m_dataAdapter) return;
                        if (!uhName.isEmpty() && uhName != m_dataAdapter->name())
                            return;
                        if (auto *pm = qobject_cast<QPropertyModel*>(m_model))
                            pm->refreshValues();
#else
                        Q_UNUSED(uhName);
#endif
                    });
        }
        break;
    case K::Street:
        m_dataAdapter = new SWMMStreetPropertyAdapter(eng, name, this); break;
    case K::Inlet:
        m_dataAdapter = new SWMMInletPropertyAdapter(eng, name, this); break;
    case K::Control:
        m_dataAdapter = new SWMMControlRulePropertyAdapter(eng, name, this); break;
    case K::RainGage:
        m_dataAdapter = new SWMMRainGagePropertyAdapter(eng, name, this); break;
    default:
        // Caller passed a spatial kind (Node/Link/Subcatchment) or
        // Unknown; not our job — let the identify-result path handle it.
        if (auto *pm = qobject_cast<QPropertyModel *>(m_model)) pm->clear();
        return;
    }

    if (auto *pm = qobject_cast<QPropertyModel *>(m_model))
        pm->setData(QVariant::fromValue<QObject *>(m_dataAdapter));

    connect(m_dataAdapter, &SWMMDataObjectPropertyAdapter::changed,
            this, [this, name]() {
                if (!m_suppressEditForward) emit objectEdited(name);
            });
    connect(m_dataAdapter, &SWMMDataObjectPropertyAdapter::renameRequested,
            this, [this, layer](const QString &oldN, const QString &newN) {
                if (layer && layer->applyRename(oldN, newN)) {
                    if (m_dataAdapter) m_dataAdapter->updateStoredName(newN);
                    emit objectEdited(newN);
                }
            });

    m_treeView->expandAll();
    setWindowTitle(tr("Attributes — %1").arg(name));
#else
    Q_UNUSED(layer); Q_UNUSED(objectKind); Q_UNUSED(name);
#endif
}
