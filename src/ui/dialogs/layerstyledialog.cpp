/*!
 * \file   layerstyledialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/layerstyledialog.h"

#include "ui/dialogs/ilayerstylesubject.h"
#include "ui/dialogs/istyleeditorwidget.h"
#include "ui/dialogs/irendererpanel.h"
#include "ui/dialogs/kindtreesymbologypanel.h"
#include "ui/dialogs/labelstab.h"
// VS.5 — Temporal / Mask / AuxStorage / Joins / Diagrams tabs removed.
#include "ui/dialogs/symbologytab.h"
// Slice B.2 — Rule Model entry point. RuleList ownership lives on the
// layer; LayerStyleDialog detects it via OpenSWMMVisLayer::ruleList().
#include "render/rulelist.h"
// RuleSymbologyTab retired from the dialog — kind tree is the symbology nav.
#include "render/stylefileio.h"
#include "ui/dialogs/crsselectiondialog.h"
#include "layers/openswmmvislayer.h"
#include "layers/gisvectorlayer.h"
#include "layers/gisvectorsymboladapter.h"   // G-1/G-2 — tabbed point/line/polygon editor
#include "layers/swmmmodellayer.h"
#include "layers/swmmresultslayer.h"
#include "layers/swmm2dresultslayer.h"
#include "ui/dialogs/swmm2dresultsstylepanel.h"
#include "layers/wcslayer.h"
#include "layers/wmslayer.h"
#include "layers/wmtslayer.h"
#include "layers/xyztilelayer.h"
#include "render/basemaprenderparams.h"
#include "map/spatialreferencesystem.h"

#include <qpropertymodel.h>

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QUndoCommand>
#include <QUndoStack>
#include <QFileDialog>
#include <QFormLayout>
#include <QMessageBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QMetaProperty>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QSlider>
#include <QSortFilterProxyModel>
#include <QSpinBox>
#include <QStyle>
#include <QTabWidget>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

#include <cmath>

namespace openswmmvis::ui {

namespace {

// #36 — app-level undo for symbology edits. Captures the layer's style-subject
// snapshots before/after a dialog session and restores them on undo/redo by
// re-acquiring styleSubjects() (the subjects write through to the layer). This
// covers exactly what the dialog's snapshot/Cancel mechanism covers.
void applyStyleSnapshots(OpenSWMMVisLayer *layer, const std::vector<QJsonObject> &snaps)
{
    if (!layer) return;
    auto subs = layer->styleSubjects();
    const size_t n = std::min(subs.size(), snaps.size());
    for (size_t i = 0; i < n; ++i)
        if (subs[i]) subs[i]->restore(snaps[i]);
}

class EditLayerStyleCommand : public QUndoCommand
{
public:
    EditLayerStyleCommand(OpenSWMMVisLayer *layer,
                          std::vector<QJsonObject> before,
                          std::vector<QJsonObject> after)
        : m_layer(layer), m_before(std::move(before)), m_after(std::move(after))
    {
        setText(QCoreApplication::translate("LayerStyleDialog", "Edit layer style")
                + (layer ? QStringLiteral(" — %1").arg(layer->objectName()) : QString()));
    }
    void undo() override { applyStyleSnapshots(m_layer.data(), m_before); }
    void redo() override
    {
        // QUndoStack::push fires redo() immediately; the layer is already in
        // the "after" state (edits applied live), so the first redo is a
        // harmless re-apply.
        applyStyleSnapshots(m_layer.data(), m_after);
    }
private:
    QPointer<OpenSWMMVisLayer> m_layer;
    std::vector<QJsonObject>   m_before;
    std::vector<QJsonObject>   m_after;
};

// ---------------------------------------------------------------------------
// QPropertyModel proxy that filters property rows by Q_CLASSINFO group.
// Slice X.1 keeps this as the placeholder Symbology content while X.2+ build
// out the proper SymbologyTab. Mirrors the recursive-filter fix from U-1.
// ---------------------------------------------------------------------------

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
        if (!parent.isValid()) return true;
        const QModelIndex idx = sourceModel()->index(row, 0, parent);
        return m_allowed.contains(idx.data(Qt::DisplayRole).toString());
    }
private:
    QSet<QString> m_allowed;
};

QHash<QString, QStringList> groupPropertiesByClassInfo(const QObject *obj)
{
    QHash<QString, QStringList> out;
    if (!obj) return out;
    const QMetaObject *mo = obj->metaObject();
    QHash<QString, QString> propToGroup;
    for (int i = 0; i < mo->classInfoCount(); ++i) {
        const QMetaClassInfo ci = mo->classInfo(i);
        const QString name = QString::fromLatin1(ci.name());
        if (!name.startsWith(QStringLiteral("group:"))) continue;
        propToGroup.insert(name.mid(6), QString::fromLatin1(ci.value()));
    }
    const int firstOwn = mo->propertyOffset();
    const int total    = mo->propertyCount();
    for (int p = firstOwn; p < total; ++p) {
        const QMetaProperty mp = mo->property(p);
        const QString name = QString::fromLatin1(mp.name());
        out[propToGroup.value(name, QStringLiteral("General"))].append(name);
    }
    return out;
}

QWidget *makeGroupView(QPropertyModel *pm, const QStringList &propNames, QWidget *parent)
{
    auto *view = new QTreeView(parent);
    view->setAlternatingRowColors(true);
    view->setEditTriggers(QAbstractItemView::AllEditTriggers);
    view->setSelectionMode(QAbstractItemView::SingleSelection);
    view->setRootIsDecorated(false);
    view->setItemsExpandable(false);
    auto *proxy = new GroupFilterProxy(view);
    proxy->setRecursiveFilteringEnabled(true);
    proxy->setSourceModel(pm);
    proxy->setAllowedProperties(propNames);
    view->setModel(proxy);
    view->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    view->header()->setStretchLastSection(true);
    view->expandAll();
    return view;
}

/*! Build the inner editor for a single subject — registry first, then a
 *  grouped QPropertyModel tree fallback (with Q_CLASSINFO sub-tabs when
 *  the bag carries them). Slice X.2 will pull this through SymbologyTab. */
QWidget *buildSubjectEditor(QObject *propertyObject, QWidget *parent)
{
    if (auto *custom = StyleEditorRegistry::instance().createEditorFor(
            propertyObject, parent)) {
        return custom;
    }
    auto *pm = new QPropertyModel(propertyObject, parent);
    const auto groups = groupPropertiesByClassInfo(propertyObject);
    if (groups.size() <= 1) {
        auto *view = new QTreeView(parent);
        view->setAlternatingRowColors(true);
        view->setEditTriggers(QAbstractItemView::AllEditTriggers);
        view->setRootIsDecorated(false);
        view->setItemsExpandable(false);
        view->setModel(pm);
        view->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        view->header()->setStretchLastSection(true);
        view->expandAll();
        return view;
    }
    auto *innerTabs = new QTabWidget(parent);
    QStringList names = groups.keys();
    names.sort(Qt::CaseInsensitive);
    names.removeAll(QStringLiteral("General"));
    if (groups.contains(QStringLiteral("General")))
        names.append(QStringLiteral("General"));
    for (const QString &g : names)
        innerTabs->addTab(makeGroupView(pm, groups.value(g), innerTabs), g);
    return innerTabs;
}

const char *layerTypeLabel(int t)
{
    using L = OpenSWMMVisLayer;
    switch (t) {
        case L::SWMMModelLayer:               return "SWMM Model";
        case L::SWMMResultsLayer:             return "SWMM Results";
        case L::SWMMVectorLayer:
        case L::SWMMGISLayer:                 return "Vector";
        case L::SWMMRasterLayer:              return "Raster / DEM";
        case L::SWMMImageryLayer:             return "Imagery (basemap)";
        case L::SWMMWMSLayer:                 return "WMS";
        case L::SWMMWMTSLayer:                return "WMTS / XYZ";
        case L::SWMMTabularDataLayer:         return "Tabular";
        case L::SWMMTabularyTimeSeriesLayer:  return "Tabular time-series";
        case L::SWMMSubProjectLayer:          return "Sub-project";
        case L::SWMM2DMeshLayer:              return "2D Mesh / TIN";
        case L::SWMM2DResultsLayer:           return "2D Results";
        case L::SWMMDefaultLayer:
        default:                              return "Unknown";
    }
}

} // namespace

// ---------------------------------------------------------------------------
// layerCapabilities — table from §X.3.1
// ---------------------------------------------------------------------------

LayerCapabilities layerCapabilities(OpenSWMMVisLayer *layer)
{
    using L = OpenSWMMVisLayer;
    LayerCapabilities caps;
    // Universal tabs.
    caps |= LayerCapability::Information;
    caps |= LayerCapability::Rendering;
    caps |= LayerCapability::Metadata;
    if (!layer) return caps;

    const int t = layer->layerType();

    // Source tab — only when the layer has an editable CRS / filter / path.
    switch (t) {
        case L::SWMMModelLayer:
        case L::SWMMResultsLayer:
        case L::SWMMVectorLayer:
        case L::SWMMGISLayer:
        case L::SWMMRasterLayer:
        case L::SWMM2DMeshLayer:
        case L::SWMM2DResultsLayer:
        case L::SWMMWMSLayer:
        case L::SWMMWMTSLayer:
        case L::SWMMImageryLayer:
            caps |= LayerCapability::Source;
            break;
        default: break;
    }

    // Symbology — every layer kind except basemap-only types (WMTS/WMS/XYZ
    // imagery have no client-side symbology; styling lives on the server).
    switch (t) {
        case L::SWMMModelLayer:
        case L::SWMMResultsLayer:
        case L::SWMMVectorLayer:
        case L::SWMMGISLayer:
        case L::SWMMRasterLayer:
        case L::SWMM2DMeshLayer:
        case L::SWMM2DResultsLayer:
            caps |= LayerCapability::Symbology;
            break;
        default: break;
    }

    // Labels — VS.10: now uniform across all feature-bearing layers. The
    // base OpenSWMMVisLayer owns the LabelConfig, so results / 2D / mesh
    // layers expose the Labels tab too (label painting for those kinds is
    // wired in their paint paths).
    switch (t) {
        case L::SWMMModelLayer:
        case L::SWMMVectorLayer:
        case L::SWMMGISLayer:
        case L::SWMMResultsLayer:
        case L::SWMM2DResultsLayer:
        case L::SWMM2DMeshLayer:
            caps |= LayerCapability::Labels;
            break;
        default: break;
    }

    // VS.5 — the Temporal / Mask / Auxiliary Storage / Joins / Diagrams
    // capability blocks were removed; those deferred-feature tabs no longer
    // appear in the dialog. The dialog now shows only the canonical QGIS
    // set: Information / Source / Symbology / Labels / Rendering / Metadata.

    return caps;
}

// ---------------------------------------------------------------------------
// ctor / dtor
// ---------------------------------------------------------------------------

LayerStyleDialog::LayerStyleDialog(OpenSWMMVisLayer *layer,
                                    QString initialRoutingId,
                                    QWidget *parent,
                                    QUndoStack *undoStack)
    : QDialog(parent)
    , m_layer(layer)
    , m_initialRoutingId(std::move(initialRoutingId))
    , m_undoStack(undoStack)
{
    setWindowTitle(layer ? tr("%1 — Layer Properties").arg(layer->name())
                         : tr("Layer Properties"));
    resize(820, 600);

    m_caps = layerCapabilities(layer);

    auto *root = new QVBoxLayout(this);
    // Don't let the layout pin a minimum dialog size — the default
    // SetDefaultConstraint forces the dialog's minimumHeight to the summed
    // child minimums, preventing the user from making it shorter. Allow free
    // resize (the scroll/clip behaviour of the inner widgets handles small
    // sizes gracefully).
    root->setSizeConstraint(QLayout::SetNoConstraint);
    setMinimumSize(0, 0);
    m_tabs = new QTabWidget(this);
    m_tabs->setTabPosition(QTabWidget::North);    // Slice X.17 — horizontal tabs.
    m_tabs->setDocumentMode(true);
    root->addWidget(m_tabs, 1);

    if (m_layer)
        m_subjects = m_layer->styleSubjects();

    buildTabs();

    if (m_layer) {
        readFromLayer();
        snapshotSubjects();
        m_undoBaseline = m_subjectSnapshots;   // #36 — open-time state for undo
        focusInitialSubject();
    }

    auto *bb = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply,
        this);

    // Slice X.23 — Import / Export style buttons.  Aligned to the
    // dialog button bar so they're available regardless of which
    // tab is active.  Restricted to layer kinds whose styling we
    // can round-trip (vector + SWMM model/results).
    if (m_layer && (qobject_cast<SWMMModelLayer  *>(m_layer)
                  || qobject_cast<SWMMResultsLayer *>(m_layer)
                  || qobject_cast<GISVectorLayer *>(m_layer))) {
        auto *importBtn = bb->addButton(tr("Import style…"),
                                         QDialogButtonBox::ActionRole);
        auto *exportBtn = bb->addButton(tr("Export style…"),
                                         QDialogButtonBox::ActionRole);
        connect(importBtn, &QPushButton::clicked,
                this, &LayerStyleDialog::onImportStyle);
        connect(exportBtn, &QPushButton::clicked,
                this, &LayerStyleDialog::onExportStyle);
    }

    root->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &LayerStyleDialog::onAccept);
    connect(bb, &QDialogButtonBox::rejected, this, &LayerStyleDialog::onCancel);
    connect(bb->button(QDialogButtonBox::Apply), &QPushButton::clicked,
            this, &LayerStyleDialog::onApply);
}

LayerStyleDialog::~LayerStyleDialog() = default;

// ---------------------------------------------------------------------------
// Tab assembly — driven by m_caps
// ---------------------------------------------------------------------------

void LayerStyleDialog::buildTabs()
{
    QStyle *s = style();
    if (m_caps.testFlag(LayerCapability::Information)) {
        buildInformationTab();
    }
    if (m_caps.testFlag(LayerCapability::Source)) {
        buildSourceTab();
    }
    if (m_caps.testFlag(LayerCapability::Symbology)) {
        buildSymbologyTab();
    }
    if (m_caps.testFlag(LayerCapability::Labels)) {
        buildLabelsTab();
    }
    // VS.5 — Temporal / Mask / AuxStorage / Joins / Diagrams tabs removed.
    if (m_caps.testFlag(LayerCapability::Rendering)) {
        buildRenderingTab();
    }
    if (m_caps.testFlag(LayerCapability::Metadata)) {
        buildMetadataTab();
    }
    Q_UNUSED(s);
}

void LayerStyleDialog::buildInformationTab()
{
    // QGIS-style Information tab: editable Name + read-only metadata
    // (type, layer-id, extent, child count, source path summary).
    auto *page = new QWidget(m_tabs);
    auto *root = new QVBoxLayout(page);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    auto *idBox  = new QGroupBox(tr("Identification"), page);
    auto *idForm = new QFormLayout(idBox);
    m_nameEdit = new QLineEdit(idBox);
    idForm->addRow(tr("Name:"), m_nameEdit);
    m_typeLabel = new QLabel(idBox);
    m_typeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    idForm->addRow(tr("Type:"), m_typeLabel);
    root->addWidget(idBox);

    auto *propBox  = new QGroupBox(tr("Properties"), page);
    auto *propLay  = new QVBoxLayout(propBox);
    m_infoText = new QPlainTextEdit(propBox);
    m_infoText->setReadOnly(true);
    m_infoText->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_infoText->setMinimumHeight(160);
    propLay->addWidget(m_infoText);
    root->addWidget(propBox, 1);

    m_tabs->addTab(page,
                   style()->standardIcon(QStyle::SP_FileDialogInfoView),
                   tr("Information"));
}

void LayerStyleDialog::buildSourceTab()
{
    // QGIS-style Source tab: read-only source path label + editable CRS
    // picker.  Encoding / filter expression land here once the underlying
    // layer APIs expose them uniformly (today only GISVectorLayer carries
    // a filterExpression Q_PROPERTY; the rest are CRS-only).
    auto *page = new QWidget(m_tabs);
    auto *root = new QVBoxLayout(page);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    auto *origBox  = new QGroupBox(tr("Layer Source"), page);
    auto *origForm = new QFormLayout(origBox);

    auto *pathLabel = new QLabel(origBox);
    pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    pathLabel->setWordWrap(true);
    QString sourcePath = tr("(none)");
    if (m_layer) {
        // Try the Q_PROPERTY chain — most layer kinds expose a filePath.
        const QVariant v = m_layer->property("filePath");
        if (v.isValid() && !v.toString().isEmpty())
            sourcePath = v.toString();
    }
    pathLabel->setText(sourcePath);
    origForm->addRow(tr("File:"), pathLabel);

    root->addWidget(origBox);

    auto *crsBox  = new QGroupBox(tr("Coordinate Reference System"), page);
    auto *crsForm = new QFormLayout(crsBox);

    auto *crsRow = new QWidget(crsBox);
    auto *crsLay = new QHBoxLayout(crsRow);
    crsLay->setContentsMargins(0, 0, 0, 0);
    m_crsLabel  = new QLabel(crsRow);
    m_crsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_crsButton = new QToolButton(crsRow);
    m_crsButton->setText(tr("Change…"));
    crsLay->addWidget(m_crsLabel, 1);
    crsLay->addWidget(m_crsButton);
    crsForm->addRow(tr("Authority:"), crsRow);

    root->addWidget(crsBox);
    root->addStretch();

    connect(m_crsButton, &QToolButton::clicked,
            this, &LayerStyleDialog::onPickCRS);

    m_tabs->addTab(page,
                   style()->standardIcon(QStyle::SP_DriveHDIcon),
                   tr("Source"));
}

void LayerStyleDialog::buildSymbologyTab()
{
    // Slice X.2 + X.5 — the Symbology tab content depends on the layer:
    //
    //   * Multi-kind SWMM layers (SWMMModelLayer, SWMMResultsLayer) →
    //     KindTreeSymbologyPanel (tree on left, SymbologyTab on right).
    //   * Everything else (vector, raster/DEM, 2D mesh, 2D results) →
    //     a single SymbologyTab editing the layer-level renderer.
    //
    // Slice B.2 (RENDERING_RULE_MODEL_PLAN.md) — when a layer overrides
    // OpenSWMMVisLayer::ruleList() to return non-null, the dialog
    // mounts RuleSymbologyTab instead. The Active Rule combo + Rule
    // List is the user-facing surface; the legacy paths above fall back
    // for layers that haven't migrated yet (every shipped layer kind at
    // the time of B.2 — opt-in is per-layer in Slices B.3 → B.5).
    //
    // SWMM2DResultsLayer is multi-sublayer (mesh fill / depth ramp /
    // contour band / isoline / velocity vector) but each sublayer is
    // really a rendering mode toggle, not a kind — so it gets the
    // single-SymbologyTab layout for now and the per-sublayer style
    // adapters surface as choices inside the renderer panel.
    auto *page = new QWidget(m_tabs);
    auto *root = new QVBoxLayout(page);
    root->setContentsMargins(8, 8, 8, 8);

    // The QGIS-style "rules list" editor (RuleSymbologyTab) was retired — for
    // a SWMM layer the rules are a fixed per-kind taxonomy, so the kind /
    // sub-layer tree (KindTreeSymbologyPanel) is the navigation. The RuleList
    // remains the underlying single-source model; rule-based (filter)
    // rendering survives as a per-kind renderer *type* in the renderer combo.
    // See docs/SYMBOLOGY_MVC_ARCHITECTURE_AND_GAPS.md §4a.
    const bool isMultiKind =
        qobject_cast<SWMMModelLayer  *>(m_layer.data()) ||
        qobject_cast<SWMMResultsLayer *>(m_layer.data());

    if (isMultiKind) {
        auto *panel = new KindTreeSymbologyPanel(m_layer.data(), page);
        if (!m_initialRoutingId.isEmpty())
            panel->focusKind(m_initialRoutingId);
        root->addWidget(panel, 1);
    } else if (auto *r2d = qobject_cast<SWMM2DResultsLayer *>(m_layer.data())) {
        // O2-1 — 2D results layer-level display controls (depth range / ramp
        // mode / classes, filled contours, isolines, velocity). Per-sublayer
        // colour/shape styling stays on the layer-tree sub-rows.
        auto *panel = new Swmm2DResultsStylePanel(r2d, page);
        root->addWidget(panel, 1);
    } else if (qobject_cast<GISVectorLayer *>(m_layer.data())) {
        // G-1/G-2 — GIS vector uses its purpose-built tabbed editor
        // (Marker / Line / Polygon / Labels). It writes the GISVectorSymbol
        // directly via the adapter, so every geometry type (incl. polygon
        // fill/outline) edits reliably — the renderer-derive SymbologyTab
        // path only reliably surfaced point/line. Mount the editor from the
        // layer's style subject (the GisVectorSymbolAdapter).
        QObject *adapter = nullptr;
        for (const auto &subj : m_subjects)
            if (subj && qobject_cast<GisVectorSymbolAdapter *>(subj->propertyObject())) {
                adapter = subj->propertyObject();
                break;
            }
        if (adapter) {
            root->addWidget(buildSubjectEditor(adapter, page), 1);
        } else {
            RendererPanelContext ctx;
            ctx.hostLayer = m_layer.data();
            root->addWidget(new SymbologyTab(ctx, page), 1);
        }
    } else {
        // Single-renderer layers (raster/DEM, 2D mesh).
        RendererPanelContext ctx;
        ctx.hostLayer = m_layer.data();
        auto *tab = new SymbologyTab(ctx, page);
        root->addWidget(tab, 1);
    }

    m_tabs->addTab(page,
                   style()->standardIcon(QStyle::SP_FileDialogContentsView),
                   tr("Symbology"));
}

void LayerStyleDialog::buildLabelsTab()
{
    // Slice X.18 — full QGIS-style Labels editor.  Replaces the X.6
    // placeholder.  The shared LabelConfig value-type lets both
    // SWMMModelLayer and GISVectorLayer drive the same tab; the host
    // layer's setLabelConfig() emits labelConfigChanged so the canvas
    // + legend stay in sync via the standard MVC channel.
    m_tabs->addTab(new openswmmvis::ui::LabelsTab(m_layer, m_tabs),
                   style()->standardIcon(QStyle::SP_FileDialogDetailedView),
                   tr("Labels"));
}

void LayerStyleDialog::buildRenderingTab()
{
    auto *page = new QWidget(m_tabs);
    auto *vlay = new QVBoxLayout(page);

    m_visibleBox = new QCheckBox(tr("Visible"), page);
    vlay->addWidget(m_visibleBox);

    auto *opBox = new QGroupBox(tr("Opacity"), page);
    auto *opLay = new QHBoxLayout(opBox);
    m_opacitySlider = new QSlider(Qt::Horizontal, opBox);
    m_opacitySlider->setRange(0, 100);
    m_opacitySlider->setSingleStep(1);
    m_opacitySlider->setPageStep(10);
    m_opacitySpin = new QSpinBox(opBox);
    m_opacitySpin->setRange(0, 100);
    m_opacitySpin->setSuffix(QStringLiteral(" %"));
    opLay->addWidget(m_opacitySlider, 1);
    opLay->addWidget(m_opacitySpin);
    vlay->addWidget(opBox);

    // ── Slice X.26 — basemap render adjustments ────────────────────────
    // Only emit when the active layer is one of our four basemap kinds.
    // Helpers read/write the layer's BasemapRenderParams via dynamic_cast;
    // the layer's setter triggers paint via repaintRequested.
    using OpenSWMM::Render::BasemapRenderParams;
    auto readBmParams = [this]() -> std::optional<BasemapRenderParams> {
        if (auto *l = qobject_cast<XYZTileLayer *>(m_layer)) return l->basemapRenderParams();
        if (auto *l = qobject_cast<WMTSLayer    *>(m_layer)) return l->basemapRenderParams();
        if (auto *l = qobject_cast<WMSLayer     *>(m_layer)) return l->basemapRenderParams();
        if (auto *l = qobject_cast<WCSLayer     *>(m_layer)) return l->basemapRenderParams();
        return std::nullopt;
    };
    auto writeBmParams = [this](const BasemapRenderParams &p) {
        if (auto *l = qobject_cast<XYZTileLayer *>(m_layer)) { l->setBasemapRenderParams(p); return; }
        if (auto *l = qobject_cast<WMTSLayer    *>(m_layer)) { l->setBasemapRenderParams(p); return; }
        if (auto *l = qobject_cast<WMSLayer     *>(m_layer)) { l->setBasemapRenderParams(p); return; }
        if (auto *l = qobject_cast<WCSLayer     *>(m_layer)) { l->setBasemapRenderParams(p); return; }
    };

    if (const auto cur = readBmParams()) {
        m_basemapAdjustBox = new QGroupBox(tr("Basemap adjustments"), page);
        auto *form = new QFormLayout(m_basemapAdjustBox);

        // Sliders: brightness/saturation in [-100, +100] ↔ [-1, +1].
        // Contrast slider in [-100, +100] ↔ contrast = 1 + v/100  (so 0
        // is identity, +100 doubles, -100 collapses to grey).
        auto makeBipolarSlider = [&](QWidget *parent) {
            auto *s = new QSlider(Qt::Horizontal, parent);
            s->setRange(-100, 100);
            s->setSingleStep(1);
            s->setPageStep(10);
            s->setTickPosition(QSlider::TicksBelow);
            s->setTickInterval(50);
            return s;
        };
        m_brightnessSlider = makeBipolarSlider(m_basemapAdjustBox);
        m_brightnessSlider->setValue(int(std::round(cur->brightness * 100.0)));
        m_brightnessSlider->setToolTip(tr("Brightness — additive offset (-100 = black, +100 = max bright)."));
        form->addRow(tr("Brightness:"), m_brightnessSlider);

        m_contrastSlider = makeBipolarSlider(m_basemapAdjustBox);
        m_contrastSlider->setValue(int(std::round((cur->contrast - 1.0) * 100.0)));
        m_contrastSlider->setToolTip(tr("Contrast — pivot around 0.5 (-100 = flat grey, 0 = identity, +100 = doubled)."));
        form->addRow(tr("Contrast:"), m_contrastSlider);

        m_saturationSlider = makeBipolarSlider(m_basemapAdjustBox);
        m_saturationSlider->setValue(int(std::round(cur->saturation * 100.0)));
        m_saturationSlider->setToolTip(tr("Saturation — -100 = greyscale, 0 = unchanged, +100 = oversaturated."));
        form->addRow(tr("Saturation:"), m_saturationSlider);

        m_resamplingCombo = new QComboBox(m_basemapAdjustBox);
        m_resamplingCombo->addItem(tr("Bilinear (smooth)"), int(BasemapRenderParams::Bilinear));
        m_resamplingCombo->addItem(tr("Nearest (crisp)"),   int(BasemapRenderParams::Nearest));
        m_resamplingCombo->setCurrentIndex(m_resamplingCombo->findData(int(cur->resampling)));
        m_resamplingCombo->setToolTip(tr(
            "How the painter scales tiles to screen pixels.  Bilinear "
            "is the default; switch to Nearest when you want crisp "
            "pixel-art-style edges on raster basemaps."));
        form->addRow(tr("Resampling:"), m_resamplingCombo);

        // Reset button — handy because dragging multiple sliders away
        // from default doesn't immediately make it obvious what the
        // canonical zero is.
        auto *resetBtn = new QToolButton(m_basemapAdjustBox);
        resetBtn->setText(tr("Reset"));
        resetBtn->setToolTip(tr("Restore brightness / contrast / saturation to identity."));
        form->addRow(QString(), resetBtn);

        vlay->addWidget(m_basemapAdjustBox);

        // Push slider edits back through writeBmParams; the layer's
        // setter handles repaintRequested + signal fan-out.
        auto push = [this, writeBmParams]() {
            BasemapRenderParams p;
            p.brightness = m_brightnessSlider->value() / 100.0;
            p.contrast   = 1.0 + (m_contrastSlider->value() / 100.0);
            p.saturation = m_saturationSlider->value() / 100.0;
            p.resampling = static_cast<BasemapRenderParams::Resampling>(
                m_resamplingCombo->currentData().toInt());
            writeBmParams(p);
        };
        connect(m_brightnessSlider, &QSlider::valueChanged, this, [push](int){ push(); });
        connect(m_contrastSlider,   &QSlider::valueChanged, this, [push](int){ push(); });
        connect(m_saturationSlider, &QSlider::valueChanged, this, [push](int){ push(); });
        connect(m_resamplingCombo,  qOverload<int>(&QComboBox::currentIndexChanged),
                this, [push](int){ push(); });
        connect(resetBtn, &QToolButton::clicked, this, [this, writeBmParams]() {
            writeBmParams(BasemapRenderParams{});
            onBasemapRenderParamsChanged();
        });

        // MVC sync — re-pull from the layer when something else mutates
        // it (style file import, programmatic change).  Each basemap
        // type has its own signal; connect whichever applies.
        if (auto *l = qobject_cast<XYZTileLayer *>(m_layer))
            connect(l, &XYZTileLayer::basemapRenderParamsChanged,
                    this, &LayerStyleDialog::onBasemapRenderParamsChanged);
        else if (auto *l = qobject_cast<WMTSLayer *>(m_layer))
            connect(l, &WMTSLayer::basemapRenderParamsChanged,
                    this, &LayerStyleDialog::onBasemapRenderParamsChanged);
        else if (auto *l = qobject_cast<WMSLayer *>(m_layer))
            connect(l, &WMSLayer::basemapRenderParamsChanged,
                    this, &LayerStyleDialog::onBasemapRenderParamsChanged);
        else if (auto *l = qobject_cast<WCSLayer *>(m_layer))
            connect(l, &WCSLayer::basemapRenderParamsChanged,
                    this, &LayerStyleDialog::onBasemapRenderParamsChanged);
    }

    vlay->addStretch();

    connect(m_opacitySlider, &QSlider::valueChanged,
            this, &LayerStyleDialog::onOpacitySliderChanged);
    connect(m_opacitySpin, qOverload<int>(&QSpinBox::valueChanged),
            this, &LayerStyleDialog::onOpacitySpinChanged);

    m_tabs->addTab(page,
                   style()->standardIcon(QStyle::SP_ComputerIcon),
                   tr("Rendering"));
}

void LayerStyleDialog::onBasemapRenderParamsChanged()
{
    if (!m_brightnessSlider) return;
    using OpenSWMM::Render::BasemapRenderParams;
    BasemapRenderParams cur;
    if      (auto *l = qobject_cast<XYZTileLayer *>(m_layer)) cur = l->basemapRenderParams();
    else if (auto *l = qobject_cast<WMTSLayer    *>(m_layer)) cur = l->basemapRenderParams();
    else if (auto *l = qobject_cast<WMSLayer     *>(m_layer)) cur = l->basemapRenderParams();
    else if (auto *l = qobject_cast<WCSLayer     *>(m_layer)) cur = l->basemapRenderParams();
    else return;
    QSignalBlocker b1(m_brightnessSlider), b2(m_contrastSlider),
        b3(m_saturationSlider), b4(m_resamplingCombo);
    m_brightnessSlider->setValue(int(std::round(cur.brightness * 100.0)));
    m_contrastSlider  ->setValue(int(std::round((cur.contrast - 1.0) * 100.0)));
    m_saturationSlider->setValue(int(std::round(cur.saturation * 100.0)));
    const int idx = m_resamplingCombo->findData(int(cur.resampling));
    if (idx >= 0) m_resamplingCombo->setCurrentIndex(idx);
}

void LayerStyleDialog::buildMetadataTab()
{
    auto *page = new QWidget(m_tabs);
    auto *vlay = new QVBoxLayout(page);
    m_metadataText = new QPlainTextEdit(page);
    m_metadataText->setReadOnly(true);
    m_metadataText->setLineWrapMode(QPlainTextEdit::NoWrap);
    vlay->addWidget(m_metadataText, 1);
    m_tabs->addTab(page,
                   style()->standardIcon(QStyle::SP_FileIcon),
                   tr("Metadata"));
}

// ---------------------------------------------------------------------------
// Layer ↔ widgets
// ---------------------------------------------------------------------------

void LayerStyleDialog::readFromLayer()
{
    if (!m_layer) return;

    // Information tab.
    if (m_nameEdit) {
        QSignalBlocker b(m_nameEdit);
        m_nameEdit->setText(m_layer->name());
    }
    if (m_typeLabel)
        m_typeLabel->setText(QString::fromLatin1(layerTypeLabel(m_layer->layerType())));

    // Source tab.
    if (m_crsLabel) {
        QString crsText = tr("(none)");
        if (auto *srs = m_layer->srs()) {
            const QString auth = srs->toAuthority();
            crsText = auth.isEmpty() ? tr("(local)") : auth;
        }
        m_crsLabel->setText(crsText);
        m_pendingCRSAuthority.clear();
    }

    // Rendering tab.
    if (m_visibleBox) { QSignalBlocker b(m_visibleBox); m_visibleBox->setChecked(m_layer->isVisible()); }
    const int opacity = qRound(m_layer->opacity() * 100);
    if (m_opacitySlider) { QSignalBlocker b(m_opacitySlider); m_opacitySlider->setValue(opacity); }
    if (m_opacitySpin)   { QSignalBlocker b(m_opacitySpin);   m_opacitySpin->setValue(opacity); }

    // Information + Metadata text dumps.
    QStringList lines;
    lines << QStringLiteral("ID:        %1").arg(m_layer->layerId());
    lines << QStringLiteral("Type:      %1").arg(layerTypeLabel(m_layer->layerType()));
    lines << QStringLiteral("Visible:   %1").arg(m_layer->isVisible() ? "yes" : "no");
    lines << QStringLiteral("Opacity:   %1 %").arg(qRound(m_layer->opacity() * 100));
    if (auto *srs = m_layer->srs()) {
        const QString auth = srs->toAuthority();
        lines << QStringLiteral("CRS:       %1").arg(auth.isEmpty() ? "(local)" : auth);
    } else {
        lines << QStringLiteral("CRS:       (none)");
    }
    const MapExtent ext = m_layer->extent();
    if (ext.isValid()) {
        lines << QStringLiteral("Extent:    [%1, %2] → [%3, %4]")
                     .arg(ext.xMin(), 0, 'g', 8).arg(ext.yMin(), 0, 'g', 8)
                     .arg(ext.xMax(), 0, 'g', 8).arg(ext.yMax(), 0, 'g', 8);
    } else {
        lines << QStringLiteral("Extent:    (invalid / not yet computed)");
    }
    if (!m_layer->children().isEmpty())
        lines << QStringLiteral("Children:  %1").arg(m_layer->children().size());
    const QString summary = lines.join('\n');
    if (m_infoText)     m_infoText    ->setPlainText(summary);
    if (m_metadataText) m_metadataText->setPlainText(summary);

    // Cancel-rollback snapshot.
    m_snapshotName    = m_layer->name();
    m_snapshotVisible = m_layer->isVisible();
    m_snapshotOpacity = m_layer->opacity();
}

void LayerStyleDialog::writeGeneralRenderingToLayer()
{
    if (!m_layer) return;
    if (m_nameEdit && m_nameEdit->text() != m_layer->name())
        m_layer->setName(m_nameEdit->text());
    if (m_visibleBox && m_visibleBox->isChecked() != m_layer->isVisible())
        m_layer->setVisible(m_visibleBox->isChecked());
    if (m_opacitySpin) {
        const double newOpacity = m_opacitySpin->value() / 100.0;
        if (!qFuzzyCompare(newOpacity, m_layer->opacity()))
            m_layer->setOpacity(newOpacity);
    }
}

void LayerStyleDialog::snapshotSubjects()
{
    m_subjectSnapshots.clear();
    m_subjectSnapshots.reserve(m_subjects.size());
    for (auto &up : m_subjects)
        m_subjectSnapshots.push_back(up->snapshot());
}

void LayerStyleDialog::restoreSubjectsFromSnapshot()
{
    const size_t n = std::min(m_subjects.size(), m_subjectSnapshots.size());
    for (size_t i = 0; i < n; ++i)
        m_subjects[i]->restore(m_subjectSnapshots[i]);
}

void LayerStyleDialog::focusInitialSubject()
{
    if (m_initialRoutingId.isEmpty()) return;
    // Until Slice X.5 lands the tree-based Symbology, just bring the
    // Symbology tab to the foreground when an initialRoutingId is provided.
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (m_tabs->tabText(i) == tr("Symbology")) {
            m_tabs->setCurrentIndex(i);
            // Walk the inner QTabWidget if present.
            auto *inner = m_tabs->widget(i)->findChild<QTabWidget *>();
            if (inner) {
                for (int j = 0; j < inner->count(); ++j) {
                    if (inner->tabToolTip(j) == m_initialRoutingId) {
                        inner->setCurrentIndex(j);
                        return;
                    }
                }
            }
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void LayerStyleDialog::onApply()
{
    writeGeneralRenderingToLayer();
    snapshotSubjects();
    m_snapshotName    = m_layer ? m_layer->name()     : m_snapshotName;
    m_snapshotVisible = m_layer ? m_layer->isVisible() : m_snapshotVisible;
    m_snapshotOpacity = m_layer ? m_layer->opacity()  : m_snapshotOpacity;
}

void LayerStyleDialog::onAccept()
{
    writeGeneralRenderingToLayer();

    // #36 — if an undo stack was supplied and the symbology actually changed,
    // push one command capturing the open-time vs final subject snapshots so
    // the whole dialog edit is a single undoable step after the dialog closes.
    if (m_undoStack && m_layer && !m_subjects.empty()) {
        std::vector<QJsonObject> after;
        after.reserve(m_subjects.size());
        for (const auto &s : m_subjects)
            after.push_back(s ? s->snapshot() : QJsonObject{});
        if (after != m_undoBaseline)
            m_undoStack->push(new EditLayerStyleCommand(
                m_layer.data(), m_undoBaseline, std::move(after)));
    }
    accept();
}

void LayerStyleDialog::onCancel()
{
    if (m_layer) {
        if (m_layer->name() != m_snapshotName)
            m_layer->setName(m_snapshotName);
        if (m_layer->isVisible() != m_snapshotVisible)
            m_layer->setVisible(m_snapshotVisible);
        if (!qFuzzyCompare(m_layer->opacity(), m_snapshotOpacity))
            m_layer->setOpacity(m_snapshotOpacity);
    }
    // NOTE: symbology edits are applied live to the layer's renderer/struct as
    // the user edits (and reflected on the map immediately), so they are
    // treated as committed — Cancel does NOT revert them. Reverting here
    // restored a *separate, stale* styleSubjects() snapshot (the dialog's
    // m_subjects are different adapter instances from the ones the Symbology
    // panel edits), which clobbered the user's change back to the open-time
    // colour. Only the explicit name / visibility / opacity fields above are
    // rolled back on Cancel.
    //   restoreSubjectsFromSnapshot();   // intentionally disabled — see above
    reject();
}

// ---------------------------------------------------------------------------
// Slice X.23 — Import / Export style
// ---------------------------------------------------------------------------

void LayerStyleDialog::onExportStyle()
{
    if (!m_layer) return;
    const QString defaultName = m_layer->name() + QStringLiteral(".swmm-style.json");
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export style"), defaultName,
        tr("SWMMVis style (*.swmm-style.json *.json)"));
    if (path.isEmpty()) return;

    const auto res = OpenSWMM::Render::StyleFileIO::exportStyle(m_layer, path);
    if (!res.ok) {
        QMessageBox::warning(this, tr("Export style"),
                              res.errorMessage.isEmpty()
                                  ? tr("Failed to export style.")
                                  : res.errorMessage);
        return;
    }
    QString msg = tr("Saved style to %1").arg(path);
    if (!res.warnings.isEmpty())
        msg += QStringLiteral("\n\n") + res.warnings.join(QChar('\n'));
    QMessageBox::information(this, tr("Export style"), msg);
}

void LayerStyleDialog::onImportStyle()
{
    if (!m_layer) return;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import style"), QString(),
        tr("Style files (*.swmm-style.json *.json *.qml);;All files (*)"));
    if (path.isEmpty()) return;

    const auto res = OpenSWMM::Render::StyleFileIO::importStyle(m_layer, path);
    if (!res.ok) {
        QMessageBox::warning(this, tr("Import style"),
                              res.errorMessage.isEmpty()
                                  ? tr("Failed to import style.")
                                  : res.errorMessage);
        return;
    }
    if (!res.warnings.isEmpty()) {
        QMessageBox::information(this, tr("Import style"),
                                  tr("Imported with warnings:\n\n%1")
                                      .arg(res.warnings.join(QChar('\n'))));
    }
    // Re-snapshot subjects so Cancel doesn't try to revert what we
    // just imported, and refresh tab editors against the new state.
    if (m_layer)
        m_subjects = m_layer->styleSubjects();
}

void LayerStyleDialog::onPickCRS()
{
    CRSSelectionDialog dlg(this);
    dlg.setCurrentCRS(m_layer ? m_layer->srs() : nullptr);
    if (dlg.exec() != QDialog::Accepted) return;
    SpatialReferenceSystem *srs = dlg.selectedSRS();
    if (!srs) return;
    if (m_layer)
        m_layer->setSRS(srs, true);
    if (m_crsLabel)
        m_crsLabel->setText(srs->toAuthority());
    m_pendingCRSAuthority = srs->toAuthority();
}

void LayerStyleDialog::onOpacitySliderChanged(int v)
{
    if (m_opacitySpin && m_opacitySpin->value() != v) {
        QSignalBlocker b(m_opacitySpin);
        m_opacitySpin->setValue(v);
    }
}

void LayerStyleDialog::onOpacitySpinChanged(int v)
{
    if (m_opacitySlider && m_opacitySlider->value() != v) {
        QSignalBlocker b(m_opacitySlider);
        m_opacitySlider->setValue(v);
    }
}

} // namespace openswmmvis::ui
