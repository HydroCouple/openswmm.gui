/*!
 * \file   attributepanel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/panels/attributepanel.h"
#include "layers/openswmmvislayer.h"
#include "layers/swmmmodellayer.h"
#include "map/tools/maptoolidentify.h"   // IdentifyResult
#include "output/outputstatsregistry.h"  // Slice QA.2 (full type for QPointer / connect)
#include "selection/selectionmanager.h"  // SWMMObjectRef::ObjectType
#include "ui/properties/dataobjectpickereditor.h"
#include "ui/properties/dataobjectref.h"
#include "ui/properties/nodecompoundeditbutton.h"
#include "ui/properties/nodecompoundeditref.h"
// Phase 3 — subcatchment-side compound-edit ref + cell editor.
#include "ui/properties/subcatchcompoundeditbutton.h"
#include "ui/properties/subcatchcompoundeditref.h"
// Slice SC.1 — link-side compound-edit ref + cell editor.
#include "ui/dialogs/linkcompoundeditdialog.h"
#include "ui/properties/linkcompoundeditbutton.h"
#include "ui/properties/linkcompoundeditref.h"
// ATTRIBUTE_EDITOR_WIRING Phase 0 — culvert code inline combobox.
#include "ui/properties/culvertcodecombobox.h"
#include "ui/properties/culvertcoderef.h"
// DA.2 parity — rain gage recording-interval H:MM combo.
#include "ui/properties/rainintervalcombobox.h"
#include "ui/properties/rainintervalref.h"
// USER_FLAGS Phase 4 — per-object "User Flags" row ref + cell editor.
#include "ui/properties/userflagseditbutton.h"
#include "ui/properties/userflagseditref.h"
// Slice BM.0-Browse-Edit — right-click menu dispatches to editors via the registry.
#include "ui/editors/comprehensiveeditorregistry.h"
#include "ui/dialogs/curveeditordialog.h"
#include "ui/dialogs/hydrographgroupeditor.h"
#include "ui/dialogs/nodecompoundeditdialog.h"
#include "ui/dialogs/patterneditordialog.h"
#include "ui/dialogs/timeserieseditordialog.h"
#include "ui/dialogs/transecteditordialog.h"
#include "curve/curveregistry.h"
#include "pattern/patternregistry.h"
#include "timeseries/timeseriesregistry.h"
#include "transect/transectregistry.h"
#include "ui/properties/swmmlinkpropertyadapter.h"
#include "ui/properties/swmmnodepropertyadapter.h"
#include "ui/properties/swmmsubcatchpropertyadapter.h"
// Slice DA.2 — non-spatial Data Object adapters.
#include "ui/properties/swmmaquiferpropertyadapter.h"
#include "ui/dialogs/ruleseditordialog.h"
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

// 2026-05-29 — header "Open in <Editor>…" button reuses the object
// browser's open-for-edit dispatch so all three surfaces (browser
// double-click, browser right-click "Edit…", attribute-panel header
// button) share one editor instance.
#include "ui/panels/objectbrowserpanel.h"

#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
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
    m_openEditorButton->hide();
    vlay->addWidget(m_openEditorButton);
    connect(m_openEditorButton, &QPushButton::clicked,
            this,                &AttributePanel::onOpenInEditorClicked);

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
    connect(m_statsSourceCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { applyStatsSourceToAdapter(); });

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

    // Phase 3 — same dance for the subcatchment-side compound-edit ref
    // (land-use coverage / groundwater / LID usage cells).
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
#else
    m_model    = new QStandardItemModel(this);
#endif

    m_treeView->setModel(m_model);
    if (m_delegate)
        m_treeView->setItemDelegate(m_delegate);
    m_treeView->header()->setDefaultSectionSize(150);

    // Slice BM.0-Browse-Edit (2026-05-25) — right-click on a complex-typed
    // property row offers an "Edit in <editor>…" shortcut to the
    // comprehensive editor for that data category (DataObjectRef rows) or
    // a single "Edit…" for compound-attribute rows (NodeCompoundEditRef).
    m_treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_treeView, &QWidget::customContextMenuRequested,
            this, &AttributePanel::onTreeContextMenu);

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
    // 2026-05-29 — leaving the data-object surface; drop the "Open in
    // <Editor>…" header button so it doesn't leak into layer-property or
    // generic-QObject views.
    m_dataObjectCategory = SWMMModelLayer::NumDataCategories;
    if (m_openEditorButton) m_openEditorButton->hide();

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
    // 2026-05-29 — identify results route to spatial / SWMM-feature
    // adapters, never to the non-spatial data-object surface; hide the
    // header "Open in <Editor>…" button so it doesn't linger from a
    // prior data-object view.
    m_dataObjectCategory = SWMMModelLayer::NumDataCategories;
    if (m_openEditorButton) m_openEditorButton->hide();

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
    // 2026-05-29 — Panel is empty; no data-object to open.
    m_dataObjectCategory = SWMMModelLayer::NumDataCategories;
    if (m_openEditorButton) m_openEditorButton->hide();
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

    // A right-click "Convert To" can change the bound object's SWMM type.
    // The adapter subclass is type-specific (Junction vs Storage, Conduit
    // vs Pump, …), so a bare refreshValues() would leave the wrong fields
    // exposed. Detect the kind change and replay the bind, which reads the
    // type live from the engine and constructs the matching subclass.
    if (pm && m_swmmLayer && m_swmmLayer->engine()) {
        if (m_nodeAdapter && m_nodeAdapter->name() == name) {
            const int idx = swmm_node_index(
                m_swmmLayer->engine(), name.toUtf8().constData());
            int kind = m_nodeAdapterKind;
            if (idx >= 0) swmm_node_get_type(m_swmmLayer->engine(), idx, &kind);
            if (kind != m_nodeAdapterKind) {
                onLayerComboIndexChanged(m_layerCombo->currentIndex());
                m_suppressEditForward = false;
                return;
            }
        } else if (m_linkAdapter && m_linkAdapter->name() == name) {
            const int idx = swmm_link_index(
                m_swmmLayer->engine(), name.toUtf8().constData());
            int kind = m_linkAdapterKind;
            if (idx >= 0) swmm_link_get_type(m_swmmLayer->engine(), idx, &kind);
            if (kind != m_linkAdapterKind) {
                onLayerComboIndexChanged(m_layerCombo->currentIndex());
                m_suppressEditForward = false;
                return;
            }
        }
    }

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
            m_nodeAdapterKind = nodeKind;
            // DB.4c — thread the layer pointer so the compound-edit
            // pickers (Inflows TS / Pattern, DWF patterns, RDII UH)
            // can call back into `layer->createDataObject(...)`.
            m_nodeAdapter->setModelLayer(m_swmmLayer);
            // Slice QA.2 — thread the per-project stats registry into
            // the adapter so its statMax* getters can dispatch through
            // a loaded SWMMResultsLayer. Refresh the combo immediately
            // so the new adapter sees a populated selection.
            m_nodeAdapter->setStatsRegistry(m_statsRegistry);
            refreshStatsSourceCombo();
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

            // Slice AG.4 — storage units: drive editability of the geometry
            // rows from the live Storage Shape. FUNCTIONAL enables the three
            // coefficient rows and greys the curve picker; TABULAR does the
            // reverse. Same trigger/contract as the outfall block above.
            if (auto *storageAdapter =
                    qobject_cast<SWMMStoragePropertyAdapter*>(m_nodeAdapter))
            {
                auto applyStorageRowFlags = [pm, storageAdapter]() {
                    if (!pm) return;
                    const bool functional =
                        storageAdapter->storageShape()
                            == SWMMNodePropertyAdapter::Functional;
                    setRowEditable(pm, storageAdapter,
                                   QStringLiteral("storageCurve"),  !functional);
                    setRowEditable(pm, storageAdapter,
                                   QStringLiteral("storageCoeffA"),  functional);
                    setRowEditable(pm, storageAdapter,
                                   QStringLiteral("storageExpB"),    functional);
                    setRowEditable(pm, storageAdapter,
                                   QStringLiteral("storageConstC"),  functional);
                };
                connect(storageAdapter, &SWMMNodePropertyAdapter::changed,
                        pm, applyStorageRowFlags);
                applyStorageRowFlags();
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
            m_linkAdapterKind = linkKind;
            // Slice BM.0-Browse-Edit (2026-05-25) — thread the layer
            // pointer so DataObjectRef-typed Q_PROPERTYs (pumpCurve) can
            // construct refs with the right layer for the picker editor.
            m_linkAdapter->setModelLayer(m_swmmLayer);
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
            // USER_FLAGS Phase 4 — layer binding so the User Flags row
            // reaches the shared UserFlagsModel.
            m_subcatchAdapter->setModelLayer(m_swmmLayer);
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
            // Phase 3 — grey the infiltration parameter rows by the live model
            // (Horton/Mod-Horton → Horton rows; Green-Ampt/Mod-GA → GA rows;
            // Curve-Number → CN row). Same trigger/contract as the storage
            // Functional/Tabular greying.
            {
                auto *sub = m_subcatchAdapter;
                auto applyInfilRowFlags = [pm, sub]() {
                    if (!pm) return;
                    const auto m = sub->infilModel();
                    const bool horton = (m == SWMMSubcatchPropertyAdapter::Horton
                                      || m == SWMMSubcatchPropertyAdapter::ModHorton);
                    const bool ga     = (m == SWMMSubcatchPropertyAdapter::GreenAmpt
                                      || m == SWMMSubcatchPropertyAdapter::ModGreenAmpt);
                    const bool cn     = (m == SWMMSubcatchPropertyAdapter::CurveNumber);
                    setRowEditable(pm, sub, QStringLiteral("hortonF0"),      horton);
                    setRowEditable(pm, sub, QStringLiteral("hortonFmin"),    horton);
                    setRowEditable(pm, sub, QStringLiteral("hortonDecay"),   horton);
                    setRowEditable(pm, sub, QStringLiteral("hortonDryTime"), horton);
                    setRowEditable(pm, sub, QStringLiteral("gaSuction"),      ga);
                    setRowEditable(pm, sub, QStringLiteral("gaConductivity"), ga);
                    setRowEditable(pm, sub, QStringLiteral("gaInitDeficit"),  ga);
                    setRowEditable(pm, sub, QStringLiteral("cnNumber"),       cn);
                };
                connect(sub, &SWMMSubcatchPropertyAdapter::changed,
                        pm, applyInfilRowFlags);
                applyInfilRowFlags();
            }
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
        m_dataObjectCategory = SWMMModelLayer::NumDataCategories;
        if (m_openEditorButton) m_openEditorButton->hide();
        return;
    }
    if (m_dataAdapter) { m_dataAdapter->deleteLater(); m_dataAdapter = nullptr; }

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
            connect(layer, &SWMMModelLayer::transectChanged,
                    m_dataAdapter,
                    [this](const QString &txName) {
#ifdef HAVE_QPROPERTYMODEL
                        if (!m_dataAdapter) return;
                        if (!txName.isEmpty() && txName != m_dataAdapter->name())
                            return;
                        if (auto *pm = qobject_cast<QPropertyModel*>(m_model))
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
            connect(layer, &SWMMModelLayer::controlRulesChanged,
                    m_dataAdapter,
                    [this](const QString &ruleName) {
#ifdef HAVE_QPROPERTYMODEL
                        if (!m_dataAdapter) return;
                        if (!ruleName.isEmpty() && ruleName != m_dataAdapter->name())
                            return;
                        if (auto *pm = qobject_cast<QPropertyModel*>(m_model))
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
        m_dataAdapter = new SWMMRainGagePropertyAdapter(eng, name, this); break;
    default:
        // Caller passed a spatial kind (Node/Link/Subcatchment) or
        // Unknown; not our job — let the identify-result path handle it.
        if (auto *pm = qobject_cast<QPropertyModel *>(m_model)) pm->clear();
        if (m_openEditorButton) m_openEditorButton->hide();
        return;
    }

    // Slice BM.0-Browse-Edit (2026-05-25) — thread the layer pointer so
    // DataObjectRef-typed Q_PROPERTYs (Pollutant::coPollutant,
    // Hydrograph::gageName) construct refs with the right layer for the
    // picker editor + right-click menu.
    if (m_dataAdapter) m_dataAdapter->setModelLayer(layer);

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

    // DA.2 parity — rain gage: drive editability of the source-specific rows
    // from the live dataSource. TIMESERIES enables the Series Name picker and
    // greys the file rows; FILE does the reverse. Recording interval and snow
    // catch factor apply to both sources and stay editable. Same trigger /
    // contract as the outfall + storage blocks in the node path above.
    if (auto *gageAdapter =
            qobject_cast<SWMMRainGagePropertyAdapter*>(m_dataAdapter))
    {
        if (auto *pm = qobject_cast<QPropertyModel*>(m_model)) {
            auto applyGageRowFlags = [pm, gageAdapter]() {
                if (!pm) return;
                const bool isFile = gageAdapter->dataSource() == 1; // FILE
                setRowEditable(pm, gageAdapter, QStringLiteral("seriesName"), !isFile);
                setRowEditable(pm, gageAdapter, QStringLiteral("filePath"),    isFile);
                setRowEditable(pm, gageAdapter, QStringLiteral("stationId"),   isFile);
                setRowEditable(pm, gageAdapter, QStringLiteral("rainUnits"),   isFile);
            };
            connect(gageAdapter, &SWMMDataObjectPropertyAdapter::changed,
                    pm, applyGageRowFlags);
            applyGageRowFlags();
        }
    }

    m_treeView->expandAll();
    setWindowTitle(tr("Attributes — %1").arg(name));

    // 2026-05-29 — surface the header "Open in <Editor>…" button when the
    // current data category has a shipped CRUD editor. The button text
    // carries the editor's user-visible title plus the object name so
    // the user can tell which object they're about to open.
    if (m_openEditorButton) {
        const auto &reg = ComprehensiveEditorRegistry::instance();
        const bool shipped =
            m_dataObjectCategory != SWMMModelLayer::NumDataCategories
            && reg.hasEditor(m_dataObjectCategory);
        if (shipped) {
            const QString title = reg.editorTitle(m_dataObjectCategory);
            m_openEditorButton->setText(
                tr("Open \"%1\" in %2…")
                    .arg(name, title.isEmpty() ? tr("Editor") : title));
            m_openEditorButton->show();
        } else {
            m_openEditorButton->hide();
        }
    }
#else
    Q_UNUSED(layer); Q_UNUSED(objectKind); Q_UNUSED(name);
#endif
}

// ---------------------------------------------------------------------------
// 2026-05-29 — Header "Open in <Editor>…" button click
// ---------------------------------------------------------------------------
//
// Maps the active data-category back to the SWMMObjectRef::ObjectType the
// object-browser helper expects, then dispatches through the shared static
// `ObjectBrowserPanel::openComprehensiveEditorFor`. The file-scope QPointer
// statics in the helper guarantee a single editor instance is reused across
// browser double-click, browser right-click "Edit…", and this button.

void AttributePanel::onOpenInEditorClicked()
{
    if (!m_swmmLayer || !m_dataAdapter
        || m_dataObjectCategory == SWMMModelLayer::NumDataCategories)
        return;
    if (!ComprehensiveEditorRegistry::instance().hasEditor(m_dataObjectCategory))
        return;

    using DC = SWMMModelLayer;
    using K  = SWMMObjectRef::ObjectType;
    K kind = K::Unknown;
    switch (m_dataObjectCategory) {
    case DC::DataTimeSeries:  kind = K::TimeSeries;  break;
    case DC::DataCurves:      kind = K::Curve;       break;
    case DC::DataPatterns:    kind = K::TimePattern; break;
    case DC::DataHydrographs: kind = K::Hydrograph;  break;
    case DC::DataTransects:   kind = K::Transect;    break;
    case DC::DataControls:    kind = K::Control;     break;
    default: return; // Gap category — registry.hasEditor was lying; bail.
    }

    const SWMMObjectRef ref{kind, m_dataAdapter->name()};
    // Undo stack is owned by the canvas; the attribute panel doesn't see it
    // directly. Passing null is fine — the editors all accept a null stack
    // and fall back to their own undo behaviour.
    ObjectBrowserPanel::openComprehensiveEditorFor(
        m_swmmLayer, /*undoStack=*/nullptr, ref, this);
}

// ---------------------------------------------------------------------------
// Slice BM.0-Browse-Edit (2026-05-25) — right-click on complex-typed rows
// ---------------------------------------------------------------------------
//
// DataObjectRef rows: "Edit \"<name>\" in <editor>…" routed through the
// matching modal-pick factory (HydrographGroupEditor::pickGroup, ...).
// Curve rows fall back to the picker's non-modal flow (no pickCurve yet).
// NodeCompoundEditRef rows: single "Edit…" that spawns NodeCompoundEditDialog.
// Other row types pop no menu so we don't add UI noise on numeric / enum cells.
//
// Dispatch mirrors `DataObjectPickerEditor::onPickerClicked` — kept in sync
// here intentionally rather than extracted, since the picker's body was last
// edited by the user and we want to avoid clobber-on-revert. A shared
// helper is a small follow-up once both surfaces stabilise.

void AttributePanel::onTreeContextMenu(const QPoint &pos)
{
    if (!m_treeView || !m_model) return;
    const QModelIndex idx = m_treeView->indexAt(pos);
    if (!idx.isValid()) return;

    // Custom editors live on the value column (1); column 0 is the label.
    const QModelIndex valueIdx = (idx.column() == 1)
        ? idx : idx.sibling(idx.row(), 1);
    if (!valueIdx.isValid()) return;

    const QVariant value = m_model->data(valueIdx, Qt::EditRole);
    if (!value.isValid()) return;

    QMenu menu(m_treeView);
    const int metaId = value.metaType().id();

    // Slice BR Phase 6.8.4 — when the panel is showing a control rule,
    // offer a top-of-menu "Open in Rules Editor…" entry on any row so
    // the user can jump to the dedicated CRUD editor regardless of which
    // cell they happened to right-click. The inline MultiLineCellDelegate
    // text editor stays in place for direct edits.
    if (auto *crAdapter = qobject_cast<SWMMControlRulePropertyAdapter *>(m_dataAdapter)) {
        const QString ruleName = crAdapter->name();
        if (!ruleName.isEmpty()) {
            QAction *actOpenInEditor = menu.addAction(
                tr("Open \"%1\" in Rules Editor…").arg(ruleName));
            QAction *picked = menu.exec(m_treeView->viewport()->mapToGlobal(pos));
            if (picked == actOpenInEditor) {
                auto *layer = crAdapter->modelLayer();
                if (layer) {
                    // Reuse the ComprehensiveEditorRegistry-singleton dialog by
                    // first triggering createNew (which raises/reuses) then
                    // pivoting to the named rule.
                    using openswmmvis::ui::RulesEditorDialog;
                    auto *dlg = new RulesEditorDialog(layer, /*undoStack=*/nullptr, m_treeView);
                    dlg->setAttribute(Qt::WA_DeleteOnClose);
                    dlg->openForRule(ruleName);
                }
            }
            return;
        }
    }

    // Slice BQ Phase 6.7.4 — Transect parity with the control-rule path
    // above: any row in the attribute panel for a transect offers a
    // top-of-menu "Open in Transect Editor…" jump to the dedicated CRUD
    // editor. The transect's roughness / station tables are edited there.
    if (auto *txAdapter = qobject_cast<SWMMTransectPropertyAdapter *>(m_dataAdapter)) {
        const QString txName = txAdapter->name();
        if (!txName.isEmpty()) {
            QAction *actOpenInEditor = menu.addAction(
                tr("Open \"%1\" in Transect Editor…").arg(txName));
            QAction *picked = menu.exec(m_treeView->viewport()->mapToGlobal(pos));
            if (picked == actOpenInEditor) {
                auto *layer = txAdapter->modelLayer();
                if (layer) {
                    using openswmmvis::transect::TransectRegistry;
                    using openswmmvis::ui::TransectEditorDialog;
                    auto *reg = qobject_cast<TransectRegistry *>(layer->ensureTransectRegistry());
                    if (!reg) return;
                    auto *dlg = new TransectEditorDialog(reg, layer, /*undoStack=*/nullptr, m_treeView);
                    dlg->setAttribute(Qt::WA_DeleteOnClose);
                    dlg->openForTransect(txName);
                }
            }
            return;
        }
    }

    if (metaId == qMetaTypeId<DataObjectRef>()) {
        const DataObjectRef ref = value.value<DataObjectRef>();
        if (!ref.layer) return;

        // Rain gages and the subcatch outlet picker have no comprehensive
        // editor — skip the menu entirely (combo selection on the cell is
        // sufficient; right-click adds no value).
        if (ref.kind == DataObjectRef::RainGage
            || ref.kind == DataObjectRef::SubcatchOutlet) return;

        // Kind → category (mirrors dataobjectpickereditor.cpp).
        SWMMModelLayer::DataCategory dc = SWMMModelLayer::DataTimeSeries;
        switch (ref.kind) {
        case DataObjectRef::TidalCurve:
        case DataObjectRef::AnyCurve:
        case DataObjectRef::StorageCurve:   dc = SWMMModelLayer::DataCurves;      break;
        case DataObjectRef::TimeSeries:     dc = SWMMModelLayer::DataTimeSeries;  break;
        case DataObjectRef::Pattern:        dc = SWMMModelLayer::DataPatterns;    break;
        case DataObjectRef::UnitHydrograph: dc = SWMMModelLayer::DataHydrographs; break;
        case DataObjectRef::Pollutant:      dc = SWMMModelLayer::DataPollutants;  break;
        case DataObjectRef::RainGage:       /* unreachable, handled above */      break;
        case DataObjectRef::SubcatchOutlet: /* unreachable, handled above */      break;
        }

        const auto &reg = ComprehensiveEditorRegistry::instance();
        const QString title  = reg.editorTitle(dc);
        const bool   shipped = reg.hasEditor(dc);

        QAction *actEdit = menu.addAction(
            ref.currentName.isEmpty()
                ? tr("Open %1…").arg(title.isEmpty() ? tr("Editor") : title)
                : tr("Edit \"%1\" in %2…")
                      .arg(ref.currentName,
                           title.isEmpty() ? tr("Editor") : title));
        actEdit->setEnabled(shipped);
        if (!shipped) actEdit->setToolTip(reg.gapTooltip(dc));
        menu.setToolTipsVisible(true);

        QAction *picked = menu.exec(m_treeView->viewport()->mapToGlobal(pos));
        if (!picked || picked != actEdit || !shipped) return;

        QString chosen;
        switch (dc) {
        case SWMMModelLayer::DataHydrographs:
            chosen = HydrographGroupEditor::pickGroup(
                ref.layer, ref.currentName, m_treeView);
            break;
        case SWMMModelLayer::DataPatterns: {
            using openswmmvis::pattern::PatternRegistry;
            using openswmmvis::ui::PatternEditorDialog;
            auto *r = qobject_cast<PatternRegistry *>(ref.layer->ensurePatternRegistry());
            if (!r) return;
            chosen = PatternEditorDialog::pickPattern(
                r, /*undoStack=*/nullptr, ref.currentName, m_treeView);
            break;
        }
        case SWMMModelLayer::DataTimeSeries: {
            using openswmmvis::timeseries::TimeseriesRegistry;
            using openswmmvis::ui::TimeseriesEditorDialog;
            auto *r = qobject_cast<TimeseriesRegistry *>(ref.layer->ensureTimeseriesRegistry());
            if (!r) return;
            chosen = TimeseriesEditorDialog::pickTimeseries(
                r, /*undoStack=*/nullptr, ref.currentName, m_treeView);
            if (!chosen.isEmpty()) r->saveToEngine();
            break;
        }
        case SWMMModelLayer::DataCurves: {
            // CurveEditorDialog has no pickCurve modal yet — match the
            // picker's non-modal fallback: open the dialog and rely on
            // the user to re-pick from the combo once they're done.
            using openswmmvis::curve::CurveRegistry;
            using openswmmvis::ui::CurveEditorDialog;
            auto *r = qobject_cast<CurveRegistry *>(ref.layer->ensureCurveRegistry());
            if (!r) return;
            QPointer<CurveEditorDialog> dlg = ref.currentName.isEmpty()
                ? CurveEditorDialog::createNew(r, /*undoStack=*/nullptr, m_treeView)
                : nullptr;
            if (!dlg) {
                dlg = new CurveEditorDialog(r, /*undoStack=*/nullptr, m_treeView);
                dlg->openForCurve(ref.currentName);
            }
            if (dlg) {
                dlg->setAttribute(Qt::WA_DeleteOnClose);
                dlg->show();
            }
            return;
        }
        default:
            return;
        }
        if (chosen.isEmpty()) return;

        DataObjectRef updated = ref;
        updated.currentName = chosen;
        m_model->setData(valueIdx, QVariant::fromValue(updated), Qt::EditRole);
        return;
    }

    if (metaId == qMetaTypeId<NodeCompoundEditRef>()) {
        const NodeCompoundEditRef ref = value.value<NodeCompoundEditRef>();
        if (!ref.engine || ref.nodeName.isEmpty()) return;

        QAction *actEdit = menu.addAction(tr("Edit…"));
        QAction *picked  = menu.exec(m_treeView->viewport()->mapToGlobal(pos));
        if (!picked || picked != actEdit) return;

        NodeCompoundEditDialog dlg(ref, m_treeView);
        dlg.exec();
        // Even on Cancel: per NodeCompoundEditButton::onClicked the dialog
        // commits some pages immediately, so pull the updated summary back.
        NodeCompoundEditRef updated = ref;
        updated.summary = dlg.updatedSummary();
        m_model->setData(valueIdx, QVariant::fromValue(updated), Qt::EditRole);
        return;
    }

    // Slice SC.1 — same right-click dispatch for link-side compound cells.
    if (metaId == qMetaTypeId<LinkCompoundEditRef>()) {
        const LinkCompoundEditRef ref = value.value<LinkCompoundEditRef>();
        if (!ref.engine || ref.linkName.isEmpty()) return;

        QAction *actEdit = menu.addAction(tr("Edit…"));
        QAction *picked  = menu.exec(m_treeView->viewport()->mapToGlobal(pos));
        if (!picked || picked != actEdit) return;

        LinkCompoundEditDialog dlg(ref, m_treeView);
        dlg.exec();
        LinkCompoundEditRef updated = ref;
        updated.summary = dlg.updatedSummary();
        m_model->setData(valueIdx, QVariant::fromValue(updated), Qt::EditRole);
        return;
    }

    // Plain row — no menu.
}

// ---------------------------------------------------------------------------
// Slice QA.2 — stats-source combo
// ---------------------------------------------------------------------------

void AttributePanel::setStatsRegistry(openswmmvis::OutputStatsRegistry *registry)
{
    if (m_statsRegistry == registry) return;

    // Disconnect previous registry's identitiesChanged so we don't leave
    // a dangling signal connection pointing at a destroyed registry.
    if (m_statsRegistry) {
        disconnect(m_statsRegistry, nullptr, this, nullptr);
    }
    m_statsRegistry = registry;
    if (m_statsRegistry) {
        connect(m_statsRegistry,
                &openswmmvis::OutputStatsRegistry::identitiesChanged,
                this, &AttributePanel::refreshStatsSourceCombo);
    }
    refreshStatsSourceCombo();

    // Push the registry pointer into the bound node adapter so its
    // getters can dispatch through it. Done on every bind so the
    // adapter never holds a stale pointer.
    if (m_nodeAdapter) {
        m_nodeAdapter->setStatsRegistry(m_statsRegistry);
    }
}

void AttributePanel::refreshStatsSourceCombo()
{
    if (!m_statsSourceCombo) return;

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
    m_statsSourceCombo->addItem(tr("(editing engine)"),
                                QVariant::fromValue(QUuid()));

    int restoreIdx = 0;
    if (m_statsRegistry) {
        const auto ids = m_statsRegistry->identities();
        for (const auto &id : ids) {
            const int row = m_statsSourceCombo->count();
            m_statsSourceCombo->addItem(id.shortLabel,
                                        QVariant::fromValue(id.stableId));
            m_statsSourceCombo->setItemData(row, id.tooltipPath,
                                            Qt::ToolTipRole);
            if (id.stableId == prevId) restoreIdx = row;
        }
    }

    m_statsSourceCombo->setCurrentIndex(restoreIdx);

    // Visibility: combo is meaningful only when (a) there's a node
    // adapter currently displayed AND (b) at least one real output is
    // loaded (count > 1 because the sentinel is always present).
    const bool show = (m_nodeAdapter != nullptr)
                    && (m_statsSourceCombo->count() > 1);
    if (m_statsSourceLabel) m_statsSourceLabel->setVisible(show);
    m_statsSourceCombo->setVisible(show);

    // Apply the (possibly re-resolved) selection to the adapter so its
    // value-getters dispatch through the right source. Unblocked here
    // because we want the slot to run.
    blocker.unblock();
    applyStatsSourceToAdapter();
}

void AttributePanel::applyStatsSourceToAdapter()
{
    if (!m_nodeAdapter || !m_statsSourceCombo) return;
    const QVariant payload = m_statsSourceCombo->currentData();
    const QUuid id = payload.canConvert<QUuid>() ? payload.toUuid() : QUuid();
    m_nodeAdapter->setStatsSource(id);
}
