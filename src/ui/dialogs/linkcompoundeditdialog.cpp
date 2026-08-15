/*!
 * \file   linkcompoundeditdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/dialogs/linkcompoundeditdialog.h"
#include "ui/theme/themehelpers.h"

#include "layers/swmmmodellayer.h"
#include "transect/transectprovider.h"
#include "transect/transectregistry.h"
#include "ui/dialogs/transecteditordialog.h"
#include "street/streetprovider.h"
#include "street/streetregistry.h"
#include "ui/dialogs/streeteditordialog.h"
#include "ui/properties/xsectshapegeom.h"   // shared shape/geom metadata
#include "ui/sectionview/sectionmodelbuilders.h"
#include "ui/sectionview/sectionpreviewwidget.h"
#include "ui/sectionview/xsecticonrenderer.h"
#include "ui/sectionview/xsectsampler.h"
#include "ui/widgets/labeledcontrols.h"
#include "ui/uiscrollhelpers.h"
#include "core/unitsystem.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_infrastructure.h>
#include <openswmm/engine/openswmm_links.h>

#include <cmath>
#include <utility>

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFontMetrics>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPalette>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {

// SWMM_XSectShape labels + geom applicability now live in the shared
// header ui/properties/xsectshapegeom.h (single source of truth, also
// consumed by the Property Browser + Attribute Table). Thin local aliases
// keep the existing dialog code below unchanged.
using ShapeRow = openswmmvis::XsectShapeRow;
constexpr const auto &kShapes = openswmmvis::kXsectShapes;

inline const ShapeRow *findShapeRow(int engineId)
{
    return openswmmvis::findXsectShapeRow(engineId);
}

// Slice SP.3 — palette thumbnails are now rendered from engine geometry by
// `sectionview::xsectShapeIcon`, replacing the 26 hand-drawn
// `:/swmmvis/xsects/*.svg` assets. That keeps a tile and the live preview
// beside it from ever disagreeing, themes the artwork with the palette, and
// gives the five shapes that never had art (BASKETHANDLE / SEMICIRCULAR /
// CUSTOM / FORCE_MAIN / DUMMY) correct tiles for free.
QIcon makeShapeIcon(int engineId, const QSize &size, const QPalette &palette)
{
    return openswmmvis::sectionview::xsectShapeIcon(engineId, size, palette);
}

//! Unit context for the preview, from the active project's UnitSystem.
openswmmvis::sectionview::DiagramUnits previewUnits()
{
    openswmmvis::sectionview::DiagramUnits u;
    if (auto *us = UnitSystem::instance()) {
        u.si          = us->isSI();
        u.lengthLabel = us->lengthLabel();
    }
    return u;
}

} // namespace

LinkCompoundEditDialog::LinkCompoundEditDialog(LinkCompoundEditRef ref,
                                                   QWidget *parent)
    : QDialog(parent), m_ref(std::move(ref))
{
    // Iteration 2 (D2) — naming wires the app-wide layout persistence.
    setObjectName(QStringLiteral("LinkCompoundEditDialog"));
    setWindowTitle(tr("Edit Link Attribute — %1").arg(m_ref.linkName));

    m_stack = new QStackedWidget(this);
    buildXSectionPage();
    buildInletUsagePage();

    switch (m_ref.kind) {
    case LinkCompoundEditRef::XSection:    m_stack->setCurrentIndex(0); break;
    case LinkCompoundEditRef::InletUsage:  m_stack->setCurrentIndex(1); break;
    }

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);

    auto *root = new QVBoxLayout(this);
    root->addWidget(m_stack);
    root->addWidget(m_buttons);
}

int LinkCompoundEditDialog::linkIdx() const
{
    if (!m_ref.engine || m_ref.linkName.isEmpty()) return -1;
    return swmm_link_index(m_ref.engine, m_ref.linkName.toUtf8().constData());
}

// ---------------------------------------------------------------------------
// XSection page
// ---------------------------------------------------------------------------

void LinkCompoundEditDialog::buildXSectionPage()
{
    auto *page = new QWidget(m_stack);

    // Slice SC.1 extended — filter shape list by the link's engine type
    // so orifice cells only offer CIRCULAR / RECT_CLOSED and weirs only
    // offer the four legal weir cross-sections. Conduits get the full
    // 20-shape list. Look up the link type once and gate via lambdas.
    int linkType = 0;   // 0 = Conduit, 2 = Orifice, 3 = Weir
    {
        const int idx = linkIdx();
        if (idx >= 0) swmm_link_get_type(m_ref.engine, idx, &linkType);
    }
    // Slice SP.3 — bare integers replaced with the SWMM_XSECT_* constants.
    // The old literals predated the 6.0 renumbering and were the same class of
    // drift XsectShapeRow warns about (see the IRREGULAR fix below).
    auto shapeAllowed = [linkType](int engineId) {
        switch (linkType) {
        case SWMM_LINK_ORIFICE: // legacy objprops.txt:866 — CIRCULAR / RECT_CLOSED.
            return engineId == SWMM_XSECT_CIRCULAR
                || engineId == SWMM_XSECT_RECT_CLOSED;
        case SWMM_LINK_WEIR: // RECT_OPEN, TRAPEZOIDAL, TRIANGULAR cover the legacy
                // TRANSVERSE / SIDEFLOW / TRAPEZOIDAL / V-NOTCH / ROADWAY set
                // (ROADWAY uses RECT_OPEN + the roadway sub-form which lands
                // in Slice SD once the engine accessors do).
            return engineId == SWMM_XSECT_RECT_OPEN
                || engineId == SWMM_XSECT_TRAPEZOIDAL
                || engineId == SWMM_XSECT_TRIANGULAR;
        default: // Conduit / Pump (unreached) / Outlet (unreached) — pumps and
                 // outlets have no xsection cell wired up in §S.SC, so this
                 // branch is conduit-only in practice.
            // CUSTOM is withheld: its geom2 is an index into the shape-curve
            // list and there is no curve picker yet, so offering it would let
            // the user select a shape they cannot finish configuring. Surface
            // it together with that picker.
            return engineId != SWMM_XSECT_CUSTOM;
        }
    };

    // §S.SC.1.a — horizontal QSplitter replaces the single QComboBox.
    // Left pane: QListWidget(IconMode) shape palette. Right pane: the
    // existing per-shape params form.
    m_xsSplitter = new QSplitter(Qt::Horizontal, page);
    m_xsSplitter->setObjectName(QStringLiteral("xsectionShapeSplitter"));
    m_xsSplitter->setChildrenCollapsible(false);

    // ---- Left: thumbnail palette ---------------------------------------
    m_xsShapeList = new QListWidget(m_xsSplitter);
    m_xsShapeList->setObjectName(QStringLiteral("xsectionShapeList"));
    m_xsShapeList->setViewMode(QListView::IconMode);
    m_xsShapeList->setFlow(QListView::LeftToRight);
    m_xsShapeList->setWrapping(true);
    m_xsShapeList->setResizeMode(QListView::Adjust);
    m_xsShapeList->setMovement(QListView::Static);
    m_xsShapeList->setSelectionMode(QAbstractItemView::SingleSelection);
    // Tile sized so the widest engine shape names fit on a single line
    // without truncation. The names contain underscores (e.g.
    // MOD_BASKETHANDLE, FILLED_CIRCULAR) which are not word-wrap break
    // points, so the cell must be as wide as the widest label rather than
    // relying on wrapping. Derive the width from the actual list font.
    const QSize kIconSize(112, 84);
    m_xsShapeList->setIconSize(kIconSize);
    const QFontMetrics fm(m_xsShapeList->font());
    int maxTextW = 0;
    for (const auto &row : kShapes)
        maxTextW = qMax(maxTextW,
                        fm.horizontalAdvance(QString::fromLatin1(row.name)));
    const int kCellPad = 16;
    const int gridW = qMax(kIconSize.width(), maxTextW) + kCellPad;
    const int gridH = kIconSize.height() + fm.height() + kCellPad;
    m_xsShapeList->setGridSize(QSize(gridW, gridH));
    m_xsShapeList->setSpacing(6);
    m_xsShapeList->setUniformItemSizes(true);
    m_xsShapeList->setWordWrap(true);
    m_xsShapeList->setMinimumWidth(2 * (gridW + 12) + 24);

    for (const auto &row : kShapes) {
        if (!shapeAllowed(row.engineId)) continue;
        auto *item = new QListWidgetItem(
            makeShapeIcon(row.engineId, kIconSize, m_xsShapeList->palette()),
            QString::fromLatin1(row.name), m_xsShapeList);
        item->setData(Qt::UserRole, row.engineId);
        item->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
    }

    // ---- Middle: per-shape params form ---------------------------------
    auto *paramsPane = new QWidget(m_xsSplitter);
    auto *form = new QFormLayout(paramsPane);

    m_xsGeom1Spin = new QDoubleSpinBox(paramsPane);
    m_xsGeom2Spin = new QDoubleSpinBox(paramsPane);
    m_xsGeom3Spin = new QDoubleSpinBox(paramsPane);
    m_xsGeom4Spin = new QDoubleSpinBox(paramsPane);
    for (QDoubleSpinBox *s : { m_xsGeom1Spin, m_xsGeom2Spin,
                                m_xsGeom3Spin, m_xsGeom4Spin }) {
        s->setRange(0.0, 1.0e9);
        s->setDecimals(4);
        s->setSingleStep(0.1);
    }
    m_xsBarrelsSpin = new QSpinBox(paramsPane);
    m_xsBarrelsSpin->setRange(1, 100);

    m_xsGeom1Label = new QLabel(tr("Geom 1"), paramsPane);
    m_xsGeom2Label = new QLabel(tr("Geom 2"), paramsPane);
    m_xsGeom3Label = new QLabel(tr("Geom 3"), paramsPane);
    m_xsGeom4Label = new QLabel(tr("Geom 4"), paramsPane);

    form->addRow(m_xsGeom1Label,  m_xsGeom1Spin);
    form->addRow(m_xsGeom2Label,  m_xsGeom2Spin);
    form->addRow(m_xsGeom3Label,  m_xsGeom3Spin);
    form->addRow(m_xsGeom4Label,  m_xsGeom4Spin);

    // §S.SC.1.b — Transect picker row (visible only for IRREGULAR).
    // Empty label arg → LabeledPickerCombo uses the form's left column
    // for its name, matching the rest of the rows.
    m_xsTransectLabel  = new QLabel(tr("Transect"), paramsPane);
    m_xsTransectPicker = new LabeledPickerCombo(QString(), paramsPane);
    form->addRow(m_xsTransectLabel, m_xsTransectPicker);

    // Street picker row (visible only for STREET). Same pattern as transect.
    m_xsStreetLabel  = new QLabel(tr("Street"), paramsPane);
    m_xsStreetPicker = new LabeledPickerCombo(QString(), paramsPane);
    form->addRow(m_xsStreetLabel, m_xsStreetPicker);

    form->addRow(tr("B&arrels"),   m_xsBarrelsSpin);

    m_xsSummaryLabel = new QLabel(paramsPane);
    m_xsSummaryLabel->setStyleSheet(openswmmvis::ui::theme::hintStyle());
    form->addRow(QString{}, m_xsSummaryLabel);

    // ---- Right: live preview -------------------------------------------
    // Slice SP.3 — the section is drawn from the same engine geometry the
    // solver uses (sampled via swmm_xsect_width_of_depth), so what the user
    // sees while typing IS the section they are about to store.
    m_xsPreview = new openswmmvis::sectionview::SectionPreviewWidget(m_xsSplitter);
    m_xsPreview->setObjectName(QStringLiteral("xsectionPreview"));
    m_xsPreview->setPlaceholderText(tr("Pick a shape to preview its section."));

    m_xsSplitter->addWidget(m_xsShapeList);
    m_xsSplitter->addWidget(paramsPane);
    m_xsSplitter->addWidget(m_xsPreview);
    m_xsSplitter->setStretchFactor(0, 1);
    m_xsSplitter->setStretchFactor(1, 1);
    m_xsSplitter->setStretchFactor(2, 2);
    m_xsSplitter->setSizes({ 320, 300, 360 });

    auto *pageLay = new QVBoxLayout(page);
    pageLay->setContentsMargins(0, 0, 0, 0);
    pageLay->addWidget(m_xsSplitter);

    // Populate from engine state.
    const int idx = linkIdx();
    if (idx >= 0) {
        int shape = 0;
        double g1 = 0, g2 = 0, g3 = 0, g4 = 0;
        swmm_link_get_xsect(m_ref.engine, idx, &shape, &g1, &g2, &g3, &g4);
        int barrels = 1;
        swmm_link_get_barrels(m_ref.engine, idx, &barrels);

        m_xsSuppressApply = true;
        int listRow = 0;
        for (int i = 0; i < m_xsShapeList->count(); ++i) {
            if (m_xsShapeList->item(i)->data(Qt::UserRole).toInt() == shape) {
                listRow = i;
                break;
            }
        }
        m_xsShapeList->setCurrentRow(listRow);
        m_xsGeom1Spin->setValue(g1);
        m_xsGeom2Spin->setValue(g2);
        m_xsGeom3Spin->setValue(g3);
        m_xsGeom4Spin->setValue(g4);
        m_xsBarrelsSpin->setValue(std::max(1, barrels));

        // §S.SC.1.b — Populate the transect picker. For IRREGULAR shape
        // geom1 carries the transect index; translate that to the name
        // via the engine API. For other shapes we still seed the items
        // so the picker is ready the moment the user switches shape.
        QString currentTransectName;
        // Slice SP.3 BUGFIX — this compared against a stale literal 19, which
        // is VERT_ELLIPSE under the 6.0 numbering; SWMM_XSECT_IRREGULAR is 21.
        // The effect was that opening the dialog on an irregular conduit never
        // populated (nor showed) the transect picker. Exactly the drift the
        // XsectShapeRow comment warns about — hence the named constants here
        // and at every other former literal in this file.
        //
        // The picker is deliberately left UNSELECTED for IRREGULAR rather than
        // seeded from geom1. Activating this branch exposed that geom1 is only
        // a transect index for links whose xsection was set through the API in
        // this session; for a link parsed from an .inp the [XSECTIONS] handler
        // never populates geom1..4 for irregular sections, so swmm_link_get_xsect
        // falls through to reporting DERIVED geometry — g1 = full depth. (The
        // engine has a name→index branch for STREET but none for IRREGULAR.)
        // Measured with tests/scratch/sp_geom1_probe.inp: three conduits on
        // three different transects reported g1 = 5 / 9 / 3, their depths.
        //
        // Since the two cases are indistinguishable from here, seeding the
        // combo would preselect the WRONG transect whenever a depth happened to
        // land inside [0, transectCount). Showing nothing and making the user
        // pick is the only honest option until the engine grows a getter.
        refreshTransectPickerItems(currentTransectName);

        // Populate the street picker. For STREET shape geom1 carries the
        // street index; translate to the name via the engine API.
        QString currentStreetName;
        if (shape == openswmmvis::kXsectStreetId) {
            const int sIdx = static_cast<int>(std::lround(g1));
            if (m_ref.engine && sIdx >= 0 && sIdx < swmm_street_count(m_ref.engine)) {
                if (const char *id = swmm_street_id(m_ref.engine, sIdx))
                    currentStreetName = QString::fromUtf8(id);
            }
        }
        refreshStreetPickerItems(currentStreetName);

        m_xsSuppressApply = false;
        updateXsectFieldVisibility();
    }

    // Apply-as-you-go: every change triggers an engine write.
    connect(m_xsShapeList, &QListWidget::currentItemChanged,
            this, [this](QListWidgetItem *cur, QListWidgetItem *) {
                if (m_xsSuppressApply || !cur) return;
                updateXsectFieldVisibility();
                applyXsect();
                refreshXsectPreview();
                // A different shape is a different drawing; geom edits below
                // deliberately keep whatever zoom the user has set.
                if (m_xsPreview) m_xsPreview->zoomToExtents();
            });
    // Slice SP.3 — the preview redraws on every keystroke, ahead of (and
    // independently of) the engine write, so an intermediate value that the
    // engine rejects still shows the user what they are typing.
    auto applyOnChange = [this](int ordinal) {
        return [this, ordinal](double) {
            if (m_xsSuppressApply) return;
            applyXsect();
            refreshXsectPreview(ordinal);
        };
    };
    connect(m_xsGeom1Spin, &QDoubleSpinBox::valueChanged, this, applyOnChange(1));
    connect(m_xsGeom2Spin, &QDoubleSpinBox::valueChanged, this, applyOnChange(2));
    connect(m_xsGeom3Spin, &QDoubleSpinBox::valueChanged, this, applyOnChange(3));
    connect(m_xsGeom4Spin, &QDoubleSpinBox::valueChanged, this, applyOnChange(4));
    connect(m_xsBarrelsSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int v) {
                if (m_xsSuppressApply) return;
                const int idx = linkIdx();
                if (idx < 0 || v < 1) return;
                if (m_ref.layer) {
                    m_ref.layer->applyLinkBarrels(idx, v);
                } else {
                    swmm_link_set_barrels(m_ref.engine, idx, v);
                }
            });

    // §S.SC.1.b — Transect picker wiring. The combo's name selection
    // round-trips back to the engine via applyXsect() (which maps the
    // name → index for IRREGULAR shape). The "..." button opens the
    // TransectEditorDialog CRUD modal so the user can create / edit /
    // delete transects without leaving the cross-section dialog.
    connect(m_xsTransectPicker, &LabeledPickerCombo::currentTextChanged,
            this, [this](const QString &) {
                if (m_xsSuppressApply) return;
                applyXsect();
                refreshXsectPreview();
            });
    connect(m_xsTransectPicker, &LabeledPickerCombo::pickerClicked,
            this, &LinkCompoundEditDialog::onTransectPickerClicked);

    // Street picker wiring — same contract as the transect picker, mapping
    // name → street index for STREET shape via applyXsect().
    connect(m_xsStreetPicker, &LabeledPickerCombo::currentTextChanged,
            this, [this](const QString &) {
                if (m_xsSuppressApply) return;
                applyXsect();
                refreshXsectPreview();
            });
    connect(m_xsStreetPicker, &LabeledPickerCombo::pickerClicked,
            this, &LinkCompoundEditDialog::onStreetPickerClicked);

    m_xsSummaryLabel->setText(computeXsectSummary());
    refreshXsectPreview();
    m_stack->addWidget(OpenSWMM::Ui::wrapInScrollArea(page, m_stack));
}

// Slice SP.3 — live preview, built from the widgets rather than the engine.
void LinkCompoundEditDialog::refreshXsectPreview(int highlightOrdinal)
{
    if (!m_xsPreview) return;

    namespace sv = openswmmvis::sectionview;

    QListWidgetItem *cur = m_xsShapeList ? m_xsShapeList->currentItem() : nullptr;
    if (!cur) {
        m_xsPreview->setModel(sv::SectionDiagramModel{});
        return;
    }
    const int shape = cur->data(Qt::UserRole).toInt();
    const sv::DiagramUnits units = previewUnits();
    const QString title = m_ref.linkName;

    // IRREGULAR / STREET geometry comes from the picked provider, not from
    // geom1 (an index).
    //
    // NB this deliberately differs from the Section View dock, which resolves
    // the same shapes from the ENGINE's transect / street tables. Here the
    // registry is the right source precisely because it is the uncommitted
    // one: the user may have just edited station-elevation pairs in the
    // transect editor and not yet flushed them, and the preview beside the
    // picker has to show what they just drew, not what the engine still holds.
    if (shape == openswmmvis::kXsectIrregularId) {
        const QString name = m_xsTransectPicker ? m_xsTransectPicker->currentText()
                                                : QString();
        openswmmvis::transect::TransectProvider *prov = nullptr;
        if (m_ref.layer && !name.isEmpty()) {
            if (auto *reg = qobject_cast<openswmmvis::transect::TransectRegistry *>(
                    m_ref.layer->ensureTransectRegistry())) {
                for (auto *p : reg->providers())
                    if (p && p->name() == name) { prov = p; break; }
            }
        }
        if (!prov) {
            sv::SectionDiagramModel m;
            m.title     = title;
            m.subtitle  = QStringLiteral("IRREGULAR");
            m.emptyText = tr("Pick a transect to preview its section.");
            m_xsPreview->setModel(m);
            return;
        }

        QVector<double> stations, elevations;
        const auto pts = prov->allPoints();
        stations.reserve(pts.size());
        elevations.reserve(pts.size());
        for (const auto &pt : pts) { stations << pt.first; elevations << pt.second; }

        sv::XsectSampler s = sv::XsectSampler::fromTransect(
            stations, elevations, prov->xLeftBank(), prov->xRightBank(),
            prov->nLeftBank(), prov->nChannel(), prov->nRightBank(),
            prov->meanderFactor(), units.si);
        m_xsPreview->setModel(sv::buildSamplerPreview(
            s, title, tr("IRREGULAR — %1").arg(name), units));
        return;
    }

    if (shape == openswmmvis::kXsectStreetId) {
        const QString name = m_xsStreetPicker ? m_xsStreetPicker->currentText()
                                              : QString();
        openswmmvis::street::StreetProvider *prov = nullptr;
        if (m_ref.layer && !name.isEmpty()) {
            if (auto *reg = qobject_cast<openswmmvis::street::StreetRegistry *>(
                    m_ref.layer->ensureStreetRegistry())) {
                for (auto *p : reg->providers())
                    if (p && p->name() == name) { prov = p; break; }
            }
        }
        if (!prov) {
            sv::SectionDiagramModel m;
            m.title     = title;
            m.subtitle  = QStringLiteral("STREET");
            m.emptyText = tr("Pick a street to preview its section.");
            m_xsPreview->setModel(m);
            return;
        }

        sv::XsectSampler s = sv::XsectSampler::fromStreet(
            prov->crownWidth(), prov->curbHeight(), prov->crossSlope(),
            prov->roadRoughness(), prov->gutterDepression(), prov->gutterWidth(),
            prov->sides(), prov->backingWidth(), prov->backingSlope(),
            prov->backingRoughness(), units.si);
        m_xsPreview->setModel(sv::buildSamplerPreview(
            s, title, tr("STREET — %1").arg(name), units));
        return;
    }

    sv::SectionDiagramModel m = sv::buildXsectEditorPreview(
        shape, m_xsGeom1Spin->value(), m_xsGeom2Spin->value(),
        m_xsGeom3Spin->value(), m_xsGeom4Spin->value(), units,
        highlightOrdinal);
    m.title = title;
    m_xsPreview->setModel(std::move(m));
}

// §S.SC.1.b — Refresh the picker's items from the layer's registry. We
// always feed off the layer's TransectRegistry rather than the engine's
// transect list because the registry is the editable source of truth
// and `pickTransect` mutates it inline; the engine flush happens on
// dialog close.
void LinkCompoundEditDialog::refreshTransectPickerItems(const QString &selected)
{
    if (!m_xsTransectPicker) return;

    QStringList names;
    if (m_ref.layer) {
        if (auto *reg = qobject_cast<openswmmvis::transect::TransectRegistry *>(
                m_ref.layer->ensureTransectRegistry())) {
            for (auto *p : reg->providers())
                if (p) names << p->name();
        }
    }

    // Suppress the resulting currentTextChanged → applyXsect bounce
    // while we repopulate; applyXsect runs on user action only.
    const bool prevSuppress = m_xsSuppressApply;
    m_xsSuppressApply = true;
    m_xsTransectPicker->setItems(names, selected);
    m_xsSuppressApply = prevSuppress;
}

void LinkCompoundEditDialog::onTransectPickerClicked()
{
    if (!m_ref.layer) return;
    auto *reg = qobject_cast<openswmmvis::transect::TransectRegistry *>(
        m_ref.layer->ensureTransectRegistry());
    if (!reg) return;

    const QString initial = m_xsTransectPicker
        ? m_xsTransectPicker->currentText() : QString();
    const QString chosen = openswmmvis::ui::TransectEditorDialog::pickTransect(
        reg, m_ref.layer, /*undoStack=*/nullptr, initial, this);
    if (chosen.isEmpty()) {
        // User cancelled or deleted everything — still refresh the list
        // so newly-created-but-not-chosen entries appear in the combo.
        refreshTransectPickerItems(initial);
        return;
    }

    refreshTransectPickerItems(chosen);
    // refreshTransectPickerItems suppressed the applyXsect bounce; commit
    // the picked name explicitly so the engine receives the new index.
    applyXsect();
    refreshXsectPreview();
}

void LinkCompoundEditDialog::refreshStreetPickerItems(const QString &selected)
{
    if (!m_xsStreetPicker) return;

    QStringList names;
    if (m_ref.layer) {
        if (auto *reg = qobject_cast<openswmmvis::street::StreetRegistry *>(
                m_ref.layer->ensureStreetRegistry())) {
            for (auto *p : reg->providers())
                if (p) names << p->name();
        }
    }

    const bool prevSuppress = m_xsSuppressApply;
    m_xsSuppressApply = true;
    m_xsStreetPicker->setItems(names, selected);
    m_xsSuppressApply = prevSuppress;
}

void LinkCompoundEditDialog::onStreetPickerClicked()
{
    if (!m_ref.layer) return;
    auto *reg = qobject_cast<openswmmvis::street::StreetRegistry *>(
        m_ref.layer->ensureStreetRegistry());
    if (!reg) return;

    const QString initial = m_xsStreetPicker
        ? m_xsStreetPicker->currentText() : QString();
    const QString chosen = openswmmvis::ui::StreetEditorDialog::pickStreet(
        reg, m_ref.layer, initial, this);
    if (chosen.isEmpty()) {
        refreshStreetPickerItems(initial);
        return;
    }

    refreshStreetPickerItems(chosen);
    applyXsect();
    refreshXsectPreview();
}

void LinkCompoundEditDialog::updateXsectFieldVisibility()
{
    QListWidgetItem *cur = m_xsShapeList ? m_xsShapeList->currentItem() : nullptr;
    const int shapeId = cur ? cur->data(Qt::UserRole).toInt() : 0;
    const ShapeRow *row = findShapeRow(shapeId);
    const bool isIrregular = (shapeId == openswmmvis::kXsectIrregularId);
    const bool isStreet    = (shapeId == openswmmvis::kXsectStreetId);

    auto setRow = [](QLabel *lbl, QDoubleSpinBox *spin, const char *text) {
        const bool show = (text && *text);
        lbl->setVisible(show);
        spin->setVisible(show);
        if (show) lbl->setText(QString::fromLatin1(text));
    };
    // §S.SC.1.b — For IRREGULAR (and STREET) the engine's geom1 is an
    // index into the transect / street list, not a length-like geometry,
    // so hide the numeric spin and surface the name picker instead. All
    // other shapes use the spin as before.
    if (isIrregular || isStreet) {
        m_xsGeom1Label->setVisible(false);
        m_xsGeom1Spin->setVisible(false);
    } else {
        setRow(m_xsGeom1Label, m_xsGeom1Spin, row->geom1Label);
    }
    setRow(m_xsGeom2Label, m_xsGeom2Spin, row->geom2Label);
    setRow(m_xsGeom3Label, m_xsGeom3Spin, row->geom3Label);
    setRow(m_xsGeom4Label, m_xsGeom4Spin, row->geom4Label);

    if (m_xsTransectLabel)  m_xsTransectLabel->setVisible(isIrregular);
    if (m_xsTransectPicker) m_xsTransectPicker->setVisible(isIrregular);
    if (m_xsStreetLabel)    m_xsStreetLabel->setVisible(isStreet);
    if (m_xsStreetPicker)   m_xsStreetPicker->setVisible(isStreet);
}

QString LinkCompoundEditDialog::computeXsectSummary() const
{
    const int idx = linkIdx();
    if (idx < 0) return tr("(no link)");
    int shape = 0;
    double g1 = 0, g2 = 0, g3 = 0, g4 = 0;
    if (swmm_link_get_xsect(m_ref.engine, idx, &shape, &g1, &g2, &g3, &g4) != SWMM_OK)
        return tr("(unknown)");
    const ShapeRow *row = findShapeRow(shape);
    // §S.SC.1.b — the cell label used to resolve IRREGULAR's geom1 to a
    // transect name. It is not reliably an index (see the note in
    // buildXSectionPage), so name the picked transect when the user has just
    // chosen one in this dialog and fall back to an em-dash otherwise, rather
    // than printing a name that may belong to a different transect.
    if (shape == openswmmvis::kXsectIrregularId) {
        const QString picked = m_xsTransectPicker ? m_xsTransectPicker->currentText()
                                                  : QString();
        return tr("%1 (%2)").arg(QString::fromLatin1(row->name),
                                  picked.isEmpty() ? tr("—") : picked);
    }
    // STREET's geom1 is a street index; resolve to the street name.
    if (shape == openswmmvis::kXsectStreetId) {
        const int sIdx = static_cast<int>(std::lround(g1));
        QString stName;
        if (sIdx >= 0 && sIdx < swmm_street_count(m_ref.engine)) {
            if (const char *id = swmm_street_id(m_ref.engine, sIdx))
                stName = QString::fromUtf8(id);
        }
        return tr("%1 (%2)").arg(QString::fromLatin1(row->name),
                                  stName.isEmpty() ? tr("—") : stName);
    }
    if (g2 > 0.0)
        return tr("%1 (%2 × %3)").arg(QString::fromLatin1(row->name))
                                 .arg(g1, 0, 'g', 4).arg(g2, 0, 'g', 4);
    return tr("%1 (%2)").arg(QString::fromLatin1(row->name))
                        .arg(g1, 0, 'g', 4);
}

void LinkCompoundEditDialog::applyXsect()
{
    const int idx = linkIdx();
    if (idx < 0) return;
    QListWidgetItem *cur = m_xsShapeList ? m_xsShapeList->currentItem() : nullptr;
    if (!cur) return;
    const int shape = cur->data(Qt::UserRole).toInt();
    double g1 = m_xsGeom1Spin->value();
    const double g2 = m_xsGeom2Spin->value();
    const double g3 = m_xsGeom3Spin->value();
    const double g4 = m_xsGeom4Spin->value();

    // §S.SC.1.b — For IRREGULAR the engine expects geom1 = transect
    // index. The user picks transects by name in m_xsTransectPicker;
    // resolve the name → index here. Bail (without writing) when no
    // name is selected so the user doesn't accidentally apply a stale
    // numeric geom1 to an irregular shape.
    if (shape == openswmmvis::kXsectIrregularId) {
        if (!m_xsTransectPicker || !m_ref.engine) return;
        const QString name = m_xsTransectPicker->currentText();
        if (name.isEmpty()) return;
        const int tIdx = swmm_transect_index(
            m_ref.engine, name.toUtf8().constData());
        if (tIdx < 0) return;
        g1 = static_cast<double>(tIdx);
    }
    // STREET: geom1 = street index, picked by name in m_xsStreetPicker.
    if (shape == openswmmvis::kXsectStreetId) {
        if (!m_xsStreetPicker || !m_ref.engine) return;
        const QString name = m_xsStreetPicker->currentText();
        if (name.isEmpty()) return;
        const int sIdx = swmm_street_index(
            m_ref.engine, name.toUtf8().constData());
        if (sIdx < 0) return;
        g1 = static_cast<double>(sIdx);
    }

    bool ok = false;
    if (m_ref.layer) {
        // MVC path: model layer emits attributeChanged so the Property
        // Browser, Attribute Table, and Map repaint in one tick.
        ok = m_ref.layer->applyLinkXsect(idx, shape, g1, g2, g3, g4);
    } else {
        // Test / standalone path: direct engine call.
        ok = (swmm_link_set_xsect(m_ref.engine, idx, shape, g1, g2, g3, g4)
              == SWMM_OK);
    }
    if (!ok) return;

    m_ref.summary = computeXsectSummary();
    if (m_xsSummaryLabel) m_xsSummaryLabel->setText(m_ref.summary);
}

// ---------------------------------------------------------------------------
// Inlet usage page (placeholder per §S.2 — BO Phase 6.5.8 deepens)
// ---------------------------------------------------------------------------

void LinkCompoundEditDialog::buildInletUsagePage()
{
    auto *page = new QWidget(m_stack);
    auto *lay  = new QVBoxLayout(page);
    auto *info = new QLabel(
        tr("Inlet usage editing is provided by Slice BO Phase 6.5.8\n"
           "(InletUsageEditor). This row currently displays the\n"
           "engine state read-only. Use the Inlets data category in\n"
           "the Object Browser to manage inlet definitions today."),
        page);
    info->setWordWrap(true);
    lay->addWidget(info);
    lay->addStretch(1);
    m_stack->addWidget(OpenSWMM::Ui::wrapInScrollArea(page, m_stack));
}
