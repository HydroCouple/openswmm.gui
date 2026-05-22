/*!
 * \file   attributepanel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/panels/attributepanel.h"
#include "layers/openswmmvislayer.h"
#include "layers/swmmmodellayer.h"
#include "map/tools/maptoolidentify.h"   // IdentifyResult
#include "ui/properties/swmmlinkpropertyadapter.h"
#include "ui/properties/swmmnodepropertyadapter.h"
#include "ui/properties/swmmsubcatchpropertyadapter.h"

#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_nodes.h>

// QPropertyModel library
#ifdef HAVE_QPROPERTYMODEL
#include <qpropertymodel.h>
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
    m_delegate = new QPropertyItemDelegate(this);
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
            if (pm) pm->setData(QVariant::fromValue<QObject *>(m_nodeAdapter));
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
