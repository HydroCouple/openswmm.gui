/*!
 * \file   linkcompoundeditdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/dialogs/linkcompoundeditdialog.h"

#include "layers/swmmmodellayer.h"
#include "transect/transectprovider.h"
#include "transect/transectregistry.h"
#include "ui/dialogs/transecteditordialog.h"
#include "ui/widgets/labeledcontrols.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_infrastructure.h>
#include <openswmm/engine/openswmm_links.h>

#include <cmath>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {

// SWMM_XSectShape labels (matches openswmm_links.h:47 enum). Order is
// the engine's integer value; legacy SWMM-GUI Dxsect.pas uses the same
// names. Slice BN Phase 6.4.4 swaps this for the rich icon palette.
struct ShapeRow {
    const char *name;
    int         engineId;          // SWMM_XSECT_* numeric
    const char *geom1Label;        // "Diameter", "Max Depth", etc.
    const char *geom2Label;        // empty if not used
    const char *geom3Label;
    const char *geom4Label;
};

static const ShapeRow kShapes[] = {
    { "CIRCULAR",         0, "Diameter",  "",                "",               ""  },
    { "FILLED_CIRCULAR",  1, "Diameter",  "Filled Depth",    "",               ""  },
    { "RECT_CLOSED",      2, "Max Depth", "Width",           "",               ""  },
    { "RECT_OPEN",        3, "Max Depth", "Width",           "",               ""  },
    { "TRAPEZOIDAL",      4, "Max Depth", "Bottom Width",    "Left Slope",     "Right Slope" },
    { "TRIANGULAR",       5, "Max Depth", "Top Width",       "",               ""  },
    { "PARABOLIC",        6, "Max Depth", "Top Width",       "",               ""  },
    { "POWER",            7, "Max Depth", "Top Width",       "Exponent",       ""  },
    { "RECT_TRIANGULAR",  8, "Max Depth", "Top Width",       "Triangle Height","" },
    { "RECT_ROUND",       9, "Max Depth", "Top Width",       "Bottom Radius",  ""  },
    { "MOD_BASKETHANDLE",10, "Max Depth", "Bottom Width",    "Top Radius",     ""  },
    { "HORIZ_ELLIPSE",   11, "Max Height","Max Width",       "",               ""  },
    { "VERT_ELLIPSE",    12, "Max Height","Max Width",       "",               ""  },
    { "ARCH",            13, "Max Height","Max Width",       "",               ""  },
    { "EGGSHAPED",       14, "Max Depth", "",                "",               ""  },
    { "HORSESHOE",       15, "Max Depth", "",                "",               ""  },
    { "GOTHIC",          16, "Max Depth", "",                "",               ""  },
    { "CATENARY",        17, "Max Depth", "",                "",               ""  },
    { "SEMIELLIPTICAL",  18, "Max Depth", "",                "",               ""  },
    { "IRREGULAR",       19, "Transect (index)", "",         "",               ""  },
};

const ShapeRow *findShapeRow(int engineId)
{
    for (const auto &r : kShapes) {
        if (r.engineId == engineId) return &r;
    }
    return &kShapes[0];   // fall back to CIRCULAR for unknown
}

// §S.SC.1.a (2026-05-25) — Cross-section thumbnail loader.
//
// Each engine shape id maps to a user-supplied SVG asset under the Qt
// resource prefix `:/swmmvis/xsects/` (see resources/swmmvis.qrc). When
// the resource isn't present (e.g. tests that don't link swmmvis.qrc;
// or future engine shape ids that don't yet have art), we fall back to
// a deterministic procedural placeholder so the QListWidget always has
// *something* to draw.
//
// The shape-id→basename table is the single source of truth — adding a
// shape (e.g. BASKETHANDLE / CUSTOM / DUMMY / FORCE_MAIN / STREET when
// Slice BN.6.4.4 surfaces them in kShapes) is one new switch case.
const char *xsectSvgBasenameFor(int engineId)
{
    switch (engineId) {
    case  0: return "circular_xsect.svg";                 // CIRCULAR
    case  1: return "filled_circular_xsect.svg";          // FILLED_CIRCULAR
    case  2: return "rectangular_xsect.svg";              // RECT_CLOSED
    case  3: return "open_rectangular_xsect.svg";         // RECT_OPEN
    case  4: return "trapezoidal_xsect.svg";              // TRAPEZOIDAL
    case  5: return "triangular_xsect.svg";               // TRIANGULAR
    case  6: return "parabolic_xsect.svg";                // PARABOLIC
    case  7: return "power_xsect.svg";                    // POWER
    case  8: return "rectangular_triangular_xsect.svg";   // RECT_TRIANGULAR
    case  9: return "rectangular_round_xsect.svg";        // RECT_ROUND
    case 10: return "modified_baskethandle_xsect.svg";    // MOD_BASKETHANDLE
    case 11: return "horizontal_ellipse_xsect.svg";       // HORIZ_ELLIPSE
    case 12: return "vertical_ellipse_xsect.svg";         // VERT_ELLIPSE
    case 13: return "arch_xsect.svg";                     // ARCH
    case 14: return "egg_xsect.svg";                      // EGGSHAPED
    case 15: return "horseshoe_xsect.svg";                // HORSESHOE
    case 16: return "gothic_xsect.svg";                   // GOTHIC
    case 17: return "catenary_xsect.svg";                 // CATENARY
    case 18: return "semi-elliptical_xsect.svg";          // SEMIELLIPTICAL
    case 19: return "irregular_xsect.svg";                // IRREGULAR
    // BN.6.4.4 will extend kShapes with BASKETHANDLE / CUSTOM / DUMMY /
    // FORCE_MAIN / STREET; the matching SVGs are already registered in
    // resources/swmmvis.qrc (baskethandle_xsect.svg etc.).
    default: return nullptr;
    }
}

QIcon makePlaceholderShapeIcon(int engineId)
{
    constexpr int kW = 64;
    constexpr int kH = 48;
    QPixmap pm(kW, kH);
    pm.fill(Qt::transparent);

    // Deterministic hue per engine id — golden-ratio walk across the
    // colour wheel keeps adjacent shapes visually distinct.
    const qreal hue = std::fmod(engineId * 0.6180339887, 1.0);
    const QColor fill = QColor::fromHsvF(hue, 0.45, 0.92);
    const QColor edge = QColor::fromHsvF(hue, 0.65, 0.55);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath path;
    path.addRoundedRect(QRectF(4, 4, kW - 8, kH - 8), 6.0, 6.0);
    p.fillPath(path, fill);
    p.setPen(QPen(edge, 1.5));
    p.drawPath(path);
    return QIcon(pm);
}

// SVG-first, placeholder fallback. QIcon::isNull() returns true when the
// resource path doesn't exist (e.g. running inside a test binary that
// doesn't compile swmmvis.qrc), so the fallback is automatic.
QIcon makeShapeIcon(int engineId)
{
    if (const char *base = xsectSvgBasenameFor(engineId)) {
        QIcon ico(QStringLiteral(":/swmmvis/xsects/%1")
                      .arg(QString::fromLatin1(base)));
        if (!ico.isNull()) return ico;
    }
    return makePlaceholderShapeIcon(engineId);
}

// FHWA culvert chart codes (1..6 in legacy SWMM-GUI; 0 = none).
struct CulvertCode {
    int         code;
    const char *label;
};
static const CulvertCode kCulvertCodes[] = {
    { 0, "(none)" },
    { 1, "Code 1 — Circular Concrete (square-edge headwall)" },
    { 2, "Code 2 — Circular Concrete (grooved end with headwall)" },
    { 3, "Code 3 — Circular Concrete (grooved end projecting)" },
    { 4, "Code 4 — Circular CMP (headwall)" },
    { 5, "Code 5 — Circular CMP (mitered to slope)" },
    { 6, "Code 6 — Circular CMP (projecting)" },
};

} // namespace

LinkCompoundEditDialog::LinkCompoundEditDialog(LinkCompoundEditRef ref,
                                                   QWidget *parent)
    : QDialog(parent), m_ref(std::move(ref))
{
    setWindowTitle(tr("Edit Link Attribute — %1").arg(m_ref.linkName));

    m_stack = new QStackedWidget(this);
    buildXSectionPage();
    buildCulvertCodePage();
    buildInletUsagePage();

    switch (m_ref.kind) {
    case LinkCompoundEditRef::XSection:    m_stack->setCurrentIndex(0); break;
    case LinkCompoundEditRef::CulvertCode: m_stack->setCurrentIndex(1); break;
    case LinkCompoundEditRef::InletUsage:  m_stack->setCurrentIndex(2); break;
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
    auto shapeAllowed = [linkType](int engineId) {
        switch (linkType) {
        case 2: // ORIFICE — legacy objprops.txt:866 lists CIRCULAR / RECT_CLOSED.
            return engineId == /*CIRCULAR*/ 0 || engineId == /*RECT_CLOSED*/ 2;
        case 3: // WEIR — RECT_OPEN, TRAPEZOIDAL, TRIANGULAR cover the legacy
                // TRANSVERSE / SIDEFLOW / TRAPEZOIDAL / V-NOTCH / ROADWAY set
                // (ROADWAY uses RECT_OPEN + the roadway sub-form which lands
                // in Slice SD once the engine accessors do).
            return engineId == /*RECT_OPEN*/   3
                || engineId == /*TRAPEZOIDAL*/ 4
                || engineId == /*TRIANGULAR*/  5;
        default: // Conduit (0) / Pump (1, unreached) / Outlet (4, unreached) —
                 // pumps + outlets have no xsection cell wired up in §S.SC, so
                 // this branch is conduit-only in practice. Full 20-shape list.
            return true;
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
    // Tile sized so the widest engine shape names (MOD_BASKETHANDLE,
    // SEMIELLIPTICAL, FILLED_CIRCULAR, RECT_TRIANGULAR) fit on two lines
    // without truncation at the default font.
    m_xsShapeList->setIconSize(QSize(80, 60));
    m_xsShapeList->setGridSize(QSize(124, 116));
    m_xsShapeList->setSpacing(6);
    m_xsShapeList->setUniformItemSizes(true);
    m_xsShapeList->setWordWrap(true);
    m_xsShapeList->setMinimumWidth(280);

    for (const auto &row : kShapes) {
        if (!shapeAllowed(row.engineId)) continue;
        auto *item = new QListWidgetItem(makeShapeIcon(row.engineId),
                                          QString::fromLatin1(row.name),
                                          m_xsShapeList);
        item->setData(Qt::UserRole, row.engineId);
        item->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
    }

    // ---- Right: per-shape params form ----------------------------------
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

    form->addRow(tr("Barrels"),   m_xsBarrelsSpin);

    m_xsSummaryLabel = new QLabel(paramsPane);
    m_xsSummaryLabel->setStyleSheet("QLabel { color: gray; }");
    form->addRow(QString{}, m_xsSummaryLabel);

    m_xsSplitter->addWidget(m_xsShapeList);
    m_xsSplitter->addWidget(paramsPane);
    m_xsSplitter->setStretchFactor(0, 1);
    m_xsSplitter->setStretchFactor(1, 2);
    m_xsSplitter->setSizes({ 320, 420 });

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
        if (shape == /*IRREGULAR*/ 19) {
            const int tIdx = static_cast<int>(std::lround(g1));
            if (m_ref.engine && tIdx >= 0 && tIdx < swmm_transect_count(m_ref.engine)) {
                if (const char *id = swmm_transect_id(m_ref.engine, tIdx))
                    currentTransectName = QString::fromUtf8(id);
            }
        }
        refreshTransectPickerItems(currentTransectName);

        m_xsSuppressApply = false;
        updateXsectFieldVisibility();
    }

    // Apply-as-you-go: every change triggers an engine write.
    connect(m_xsShapeList, &QListWidget::currentItemChanged,
            this, [this](QListWidgetItem *cur, QListWidgetItem *) {
                if (m_xsSuppressApply || !cur) return;
                updateXsectFieldVisibility();
                applyXsect();
            });
    auto applyOnChange = [this](double){ if (!m_xsSuppressApply) applyXsect(); };
    connect(m_xsGeom1Spin, &QDoubleSpinBox::valueChanged, this, applyOnChange);
    connect(m_xsGeom2Spin, &QDoubleSpinBox::valueChanged, this, applyOnChange);
    connect(m_xsGeom3Spin, &QDoubleSpinBox::valueChanged, this, applyOnChange);
    connect(m_xsGeom4Spin, &QDoubleSpinBox::valueChanged, this, applyOnChange);
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
            });
    connect(m_xsTransectPicker, &LabeledPickerCombo::pickerClicked,
            this, &LinkCompoundEditDialog::onTransectPickerClicked);

    m_xsSummaryLabel->setText(computeXsectSummary());
    m_stack->addWidget(page);
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
}

void LinkCompoundEditDialog::updateXsectFieldVisibility()
{
    QListWidgetItem *cur = m_xsShapeList ? m_xsShapeList->currentItem() : nullptr;
    const int shapeId = cur ? cur->data(Qt::UserRole).toInt() : 0;
    const ShapeRow *row = findShapeRow(shapeId);
    const bool isIrregular = (shapeId == /*IRREGULAR*/ 19);

    auto setRow = [](QLabel *lbl, QDoubleSpinBox *spin, const char *text) {
        const bool show = (text && *text);
        lbl->setVisible(show);
        spin->setVisible(show);
        if (show) lbl->setText(QString::fromLatin1(text));
    };
    // §S.SC.1.b — For IRREGULAR the engine's geom1 is a transect index,
    // not a length-like geometry, so hide the numeric spin and surface
    // the name picker instead. All other shapes use the spin as before.
    if (isIrregular) {
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
    // §S.SC.1.b — IRREGULAR's geom1 is a transect index; resolve to the
    // transect name so the cell label reads "IRREGULAR (Creek-A)"
    // rather than the cryptic "IRREGULAR (3)".
    if (shape == /*IRREGULAR*/ 19) {
        const int tIdx = static_cast<int>(std::lround(g1));
        QString txName;
        if (tIdx >= 0 && tIdx < swmm_transect_count(m_ref.engine)) {
            if (const char *id = swmm_transect_id(m_ref.engine, tIdx))
                txName = QString::fromUtf8(id);
        }
        return tr("%1 (%2)").arg(QString::fromLatin1(row->name),
                                  txName.isEmpty() ? tr("—") : txName);
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
    if (shape == /*IRREGULAR*/ 19) {
        if (!m_xsTransectPicker || !m_ref.engine) return;
        const QString name = m_xsTransectPicker->currentText();
        if (name.isEmpty()) return;
        const int tIdx = swmm_transect_index(
            m_ref.engine, name.toUtf8().constData());
        if (tIdx < 0) return;
        g1 = static_cast<double>(tIdx);
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
// Culvert code page
// ---------------------------------------------------------------------------

void LinkCompoundEditDialog::buildCulvertCodePage()
{
    auto *page = new QWidget(m_stack);
    auto *form = new QFormLayout(page);

    m_cvCodeCombo = new QComboBox(page);
    for (const auto &c : kCulvertCodes)
        m_cvCodeCombo->addItem(QString::fromLatin1(c.label), c.code);
    form->addRow(tr("Culvert Code"), m_cvCodeCombo);

    const int idx = linkIdx();
    if (idx >= 0) {
        int code = 0;
        swmm_link_get_culvert_code(m_ref.engine, idx, &code);
        m_cvSuppressApply = true;
        const int i = m_cvCodeCombo->findData(code);
        m_cvCodeCombo->setCurrentIndex(i >= 0 ? i : 0);
        m_cvSuppressApply = false;
    }

    connect(m_cvCodeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
                if (m_cvSuppressApply) return;
                const int idx = linkIdx();
                if (idx < 0) return;
                const int code = m_cvCodeCombo->currentData().toInt();
                if (m_ref.layer)
                    m_ref.layer->applyLinkCulvertCode(idx, code);
                else
                    swmm_link_set_culvert_code(m_ref.engine, idx, code);
                m_ref.summary = m_cvCodeCombo->currentText();
            });

    m_stack->addWidget(page);
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
    m_stack->addWidget(page);
}
