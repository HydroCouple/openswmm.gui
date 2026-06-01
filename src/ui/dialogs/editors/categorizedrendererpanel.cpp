/*!
 * \file   categorizedrendererpanel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  IRendererPanel implementation for the "Categorized" renderer
 *         class.  Slice X.4 follow-up — completes the categorized leg
 *         that previously surfaced as an empty stub.
 *
 *         Layout (QGIS-aligned):
 *           - Attribute combo (picks the classifyAttribute on the renderer)
 *           - Categories table   : Value | Label | Colour
 *           - Buttons            : Sample unique values / Add / Remove / Reset
 *
 *         Every edit writes through `setKindRenderer(category, clone)` on
 *         the host so the layer's rebuildKindFeatureOverrides repaints the
 *         scene and the legend refreshes — single source of truth, MVC
 *         all the way down.
 */
#include "ui/dialogs/irendererpanel.h"

#include "layers/openswmmvislayer.h"
#include "layers/swmmmodellayer.h"
#include "layers/swmmresultslayer.h"
#include "render/ifeaturerenderer.h"
// Slice DM.4 — provider-driven attribute combo.
#include "render/iattributeprovider.h"
#include "render/renderers/categorizedrenderer.h"
// Slice B.6d — Rule-aware path.
#include "render/rule.h"
#include "render/sublayers/feature/featuresublayer.h"
#include "render/symbolstyle.h"
#include "ui/widgets/dashstylecombo.h"
#include "ui/widgets/markershapecombo.h"

#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QToolButton>
#include <QVBoxLayout>

namespace openswmmvis::ui {

namespace {

using OpenSWMM::Render::CategorizedRenderer;
using OpenSWMM::Render::IFeatureRenderer;

/*! Render the "Colour" cell as a swatch + open QColorDialog on
 *  double-click.  Duplicated from the kindrendererpanel delegate so the
 *  two stay independent — they may grow apart as categorized gains
 *  fill/stroke/marker selection. */
class CategoryColorDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *p, const QStyleOptionViewItem &opt,
               const QModelIndex &idx) const override
    {
        const QColor c = idx.data(Qt::BackgroundRole).value<QColor>();
        p->save();
        if (opt.state & QStyle::State_Selected)
            p->fillRect(opt.rect, opt.palette.highlight());
        QRect inner = opt.rect.adjusted(4, 4, -4, -4);
        if (c.isValid()) {
            p->setBrush(c);
            p->setPen(QPen(QColor(60, 60, 60), 0.8));
            p->drawRoundedRect(inner, 3, 3);
        }
        p->restore();
    }

    QWidget *createEditor(QWidget *, const QStyleOptionViewItem &,
                          const QModelIndex &) const override
    {
        return nullptr;
    }

    bool editorEvent(QEvent *e, QAbstractItemModel *m,
                     const QStyleOptionViewItem &, const QModelIndex &idx) override
    {
        if (e->type() != QEvent::MouseButtonDblClick) return false;
        QColor cur = idx.data(Qt::BackgroundRole).value<QColor>();
        const QColor picked = QColorDialog::getColor(
            cur.isValid() ? cur : QColor(Qt::white),
            nullptr, QObject::tr("Category colour"),
            QColorDialog::ShowAlphaChannel);
        if (!picked.isValid()) return true;
        m->setData(idx, picked, Qt::BackgroundRole);
        return true;
    }
};

/*! Suggested attribute list per Category.  Mirrors the columns the
 *  categorized renderer can usefully partition on without pulling in the
 *  full attribute schema editor (Slice deferred).  "tag" is always
 *  available because SWMM lets users tag every object. */
QStringList suggestedAttributesFor(OpenSWMMVis::SwmmCategory cat)
{
    using namespace OpenSWMMVis;
    QStringList common = { QStringLiteral("tag") };
    switch (cat) {
    case CatJunctions:
    case CatOutfalls:
    case CatStorage:
    case CatDividers:
        return common + QStringList{ QStringLiteral("Node type") };
    case CatConduits:
    case CatPumps:
    case CatOrifices:
    case CatWeirs:
    case CatOutlets:
        return common + QStringList{
            QStringLiteral("Link type"),
            QStringLiteral("shape"),
        };
    case CatSubcatchments:
        return common + QStringList{ QStringLiteral("Outlet") };
    default:
        return common;
    }
}

/*! Make a colour-only SymbolStyle for a fresh category.  Layer-archetype
 *  agnostic — populateScene applies the colour wherever an override is
 *  set, regardless of point / line / polygon. */
OpenSWMM::Render::SymbolStyle makeColourSymbol(const QColor &c)
{
    OpenSWMM::Render::SymbolStyle s;
    OpenSWMM::Render::SymbolLayer layer;
    layer.props.insert(QStringLiteral("color"), c.name(QColor::HexArgb));
    s.layers.append(layer);
    return s;
}

/*! Read/write helpers for the optional shape / size / width / dash
 *  properties on the first SymbolLayer of a SymbolStyle.  Categorized
 *  rendering treats absent keys as "use the kind's archetype default"
 *  — the paint path reads them via `extractStyleSize` on the sublayer
 *  override cache.  Keeping the schema flat (no nested objects) means
 *  any consumer reading just "color" today keeps working. */
int    readSymbolShape(const OpenSWMM::Render::SymbolStyle &s, int fallback = -1)
{
    if (s.layers.isEmpty()) return fallback;
    const auto it = s.layers.first().props.constFind(QStringLiteral("shape"));
    if (it == s.layers.first().props.constEnd()) return fallback;
    bool ok = false; const int v = it.value().toInt(&ok);
    return ok ? v : fallback;
}
double readSymbolSize(const OpenSWMM::Render::SymbolStyle &s, double fallback = -1.0)
{
    if (s.layers.isEmpty()) return fallback;
    const auto it = s.layers.first().props.constFind(QStringLiteral("size"));
    if (it == s.layers.first().props.constEnd()) return fallback;
    bool ok = false; const double v = it.value().toDouble(&ok);
    return ok ? v : fallback;
}
double readSymbolWidth(const OpenSWMM::Render::SymbolStyle &s, double fallback = -1.0)
{
    if (s.layers.isEmpty()) return fallback;
    const auto it = s.layers.first().props.constFind(QStringLiteral("width"));
    if (it == s.layers.first().props.constEnd()) return fallback;
    bool ok = false; const double v = it.value().toDouble(&ok);
    return ok ? v : fallback;
}
int readSymbolDash(const OpenSWMM::Render::SymbolStyle &s, int fallback = int(Qt::SolidLine))
{
    if (s.layers.isEmpty()) return fallback;
    const auto it = s.layers.first().props.constFind(QStringLiteral("dash"));
    if (it == s.layers.first().props.constEnd()) return fallback;
    bool ok = false; const int v = it.value().toInt(&ok);
    return ok ? v : fallback;
}

void writeSymbolKnobs(OpenSWMM::Render::SymbolStyle &s,
                      int shape, double size, double width, int dash)
{
    if (s.layers.isEmpty())
        s.layers.append(OpenSWMM::Render::SymbolLayer{});
    auto &props = s.layers.first().props;
    if (shape >= 0)  props.insert(QStringLiteral("shape"), shape);
    if (size  > 0.0) props.insert(QStringLiteral("size"),  size);
    if (width > 0.0) props.insert(QStringLiteral("width"), width);
    if (dash  >= 0)  props.insert(QStringLiteral("dash"),  dash);
}

// ---------------------------------------------------------------------------

/*! Small modal dialog for editing the non-colour knobs on a single
 *  category's symbol.  Fields are hidden when the host archetype
 *  doesn't paint with them (e.g. line width is hidden for points). */
class SymbolDetailsDialog : public QDialog
{
public:
    SymbolDetailsDialog(OpenSWMM::Render::FeatureSublayer::Archetype arch,
                        const OpenSWMM::Render::SymbolStyle &initial,
                        QWidget *parent)
        : QDialog(parent), m_archetype(arch)
    {
        using Arch = OpenSWMM::Render::FeatureSublayer::Archetype;
        setWindowTitle(tr("Edit symbol"));
        auto *form = new QFormLayout(this);

        if (arch == Arch::Point) {
            m_shape = new MarkerShapeCombo(this);
            m_shape->populateCanonical();   // category symbols use canonical MarkerShape
            const int initShape = readSymbolShape(initial, MarkerShapeCombo::Circle);
            m_shape->setShapeValue(initShape);
            form->addRow(tr("Shape:"), m_shape);

            m_size = new QDoubleSpinBox(this);
            m_size->setRange(1.0, 96.0);
            m_size->setDecimals(1);
            m_size->setSuffix(QStringLiteral(" px"));
            m_size->setValue(readSymbolSize(initial, 6.0));
            form->addRow(tr("Size:"), m_size);
        }
        if (arch == Arch::Line || arch == Arch::Polygon) {
            m_width = new QDoubleSpinBox(this);
            m_width->setRange(0.1, 32.0);
            m_width->setDecimals(2);
            m_width->setSuffix(QStringLiteral(" px"));
            m_width->setValue(readSymbolWidth(initial, arch == Arch::Line ? 1.5 : 0.5));
            form->addRow(arch == Arch::Line ? tr("Line width:") : tr("Outline width:"),
                          m_width);

            m_dash = new DashStyleCombo(this);
            m_dash->setCurrentIndex(m_dash->findData(readSymbolDash(initial, int(Qt::SolidLine))));
            form->addRow(tr("Dash:"), m_dash);
        }

        auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                        Qt::Horizontal, this);
        form->addRow(bb);
        connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }

    /*! Apply the dialog's choices to \p s, preserving any property the
     *  dialog doesn't surface for this archetype. */
    void applyTo(OpenSWMM::Render::SymbolStyle &s) const
    {
        const int    shape = m_shape ? m_shape->shapeValue() : -1;
        const double size  = m_size  ? m_size ->value()       : -1.0;
        const double width = m_width ? m_width->value()       : -1.0;
        const int    dash  = m_dash  ? m_dash ->currentData().toInt() : -1;
        writeSymbolKnobs(s, shape, size, width, dash);
    }

private:
    OpenSWMM::Render::FeatureSublayer::Archetype m_archetype;
    MarkerShapeCombo *m_shape = nullptr;
    QDoubleSpinBox   *m_size  = nullptr;
    QDoubleSpinBox   *m_width = nullptr;
    DashStyleCombo   *m_dash  = nullptr;
};

// ---------------------------------------------------------------------------

class CategorizedPanel : public IRendererPanel
{
public:
    explicit CategorizedPanel(const RendererPanelContext &ctx, QWidget *parent)
        : IRendererPanel(parent), m_ctx(ctx)
    {
        m_modelLayer   = qobject_cast<SWMMModelLayer *>(ctx.hostLayer);
        m_resultsLayer = qobject_cast<SWMMResultsLayer *>(ctx.hostLayer);

        auto *outer = new QVBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);

        auto *box = new QGroupBox(tr("Categorized rendering"), this);
        outer->addWidget(box);

        auto *vlay = new QVBoxLayout(box);

        auto *form = new QFormLayout;
        m_attrCombo = new QComboBox(box);
        m_attrCombo->setEditable(true);     // user can type a custom key
        form->addRow(tr("Attribute:"), m_attrCombo);
        vlay->addLayout(form);

        m_model = new QStandardItemModel(0, 3, box);
        m_model->setHorizontalHeaderLabels({
            tr("Value"), tr("Label"), tr("Colour")
        });
        m_table = new QTableView(box);
        m_table->setModel(m_model);
        m_table->verticalHeader()->setVisible(false);
        m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        m_table->horizontalHeader()->setStretchLastSection(true);
        m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_table->setEditTriggers(QAbstractItemView::DoubleClicked
                                  | QAbstractItemView::SelectedClicked);
        m_table->setAlternatingRowColors(true);
        m_table->setItemDelegateForColumn(2, new CategoryColorDelegate(m_table));
        m_table->setMinimumHeight(180);
        vlay->addWidget(m_table, 1);

        // Button row
        auto *btnRow = new QHBoxLayout;
        m_sampleBtn = new QToolButton(box);
        m_sampleBtn->setText(tr("Sample unique values"));
        m_sampleBtn->setToolTip(tr(
            "Scan the layer for distinct values of the selected attribute "
            "and seed one category per value."));
        btnRow->addWidget(m_sampleBtn);

        m_addBtn = new QToolButton(box);
        m_addBtn->setText(tr("Add"));
        btnRow->addWidget(m_addBtn);

        m_removeBtn = new QToolButton(box);
        m_removeBtn->setText(tr("Remove"));
        btnRow->addWidget(m_removeBtn);

        m_clearBtn = new QToolButton(box);
        m_clearBtn->setText(tr("Clear"));
        btnRow->addWidget(m_clearBtn);

        m_editSymBtn = new QToolButton(box);
        m_editSymBtn->setText(tr("Edit symbol…"));
        m_editSymBtn->setToolTip(tr(
            "Edit the shape / size / line width / dash for the selected "
            "category.  Colour is editable inline."));
        btnRow->addWidget(m_editSymBtn);

        btnRow->addStretch();
        vlay->addLayout(btnRow);

        connect(m_attrCombo, &QComboBox::editTextChanged,
                this, &CategorizedPanel::onAttributeChanged);
        connect(m_attrCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                this, [this](int) { onAttributeChanged(m_attrCombo->currentText()); });

        connect(m_sampleBtn, &QToolButton::clicked,
                this, &CategorizedPanel::onSampleUniqueValues);
        connect(m_addBtn, &QToolButton::clicked,
                this, &CategorizedPanel::onAddCategory);
        connect(m_removeBtn, &QToolButton::clicked,
                this, &CategorizedPanel::onRemoveSelected);
        connect(m_clearBtn, &QToolButton::clicked,
                this, &CategorizedPanel::onClearAll);
        connect(m_editSymBtn, &QToolButton::clicked,
                this, &CategorizedPanel::onEditSelectedSymbol);
        connect(m_model, &QStandardItemModel::itemChanged,
                this, &CategorizedPanel::onRowEdited);

        refreshFromModel();
    }

    void refreshFromModel() override
    {
        auto *cz = currentRenderer();

        // Populate the attribute combo from the host's IAttributeProvider
        // when available (Slice DM.4); fall back to the hardcoded
        // suggestion list otherwise (preserves the existing behaviour
        // for layers that haven't been ported to the provider yet).
        // The combo is editable, so a user can still type a custom
        // field name when neither path lists it.
        {
            QSignalBlocker b(m_attrCombo);
            m_attrCombo->clear();
            if (m_ctx.category.has_value()) {
                auto *provider = m_ctx.hostLayer
                    ? qobject_cast<OpenSWMM::Render::IAttributeProvider *>(
                          m_ctx.hostLayer)
                    : nullptr;
                if (provider) {
                    const auto fields = provider->availableAttributes(*m_ctx.category);
                    for (const auto &f : fields)
                        m_attrCombo->addItem(f.displayName, f.name);
                } else {
                    m_attrCombo->addItems(suggestedAttributesFor(*m_ctx.category));
                }
            }
            const QString current = cz
                ? cz->classifyAttribute()
                : (m_attrCombo->count() > 0 ? m_attrCombo->itemText(0) : QString());
            m_attrCombo->setCurrentText(current);
        }

        rebuildTable();
    }

    QString displayName() const override { return tr("Categorized"); }

private:
    CategorizedRenderer *currentRenderer() const
    {
        IFeatureRenderer *r = nullptr;
        // Slice B.6d — Rule path takes priority.
        if (m_ctx.rule) {
            r = m_ctx.rule->renderer();
        } else if (m_ctx.category.has_value()) {
            if (m_modelLayer)   r = m_modelLayer  ->kindRenderer(*m_ctx.category);
            if (m_resultsLayer) r = m_resultsLayer->kindRenderer(*m_ctx.category);
        }
        return dynamic_cast<CategorizedRenderer *>(r);
    }

    /*! Push the live renderer back through setKindRenderer / Rule so the
     *  host rebuilds its per-feature overrides and emits repaintRequested.
     *  Always clones — the renderer instance the panel mutated is the
     *  one the layer/Rule owns, but the setters expect a unique_ptr. */
    void pushBack(CategorizedRenderer *cz)
    {
        if (!cz) return;
        auto fresh = cz->clone();
        // Slice B.6d — Rule path takes priority.
        if (m_ctx.rule) {
            m_ctx.rule->setRenderer(std::move(fresh));
            return;
        }
        if (!m_ctx.category.has_value()) return;
        if (m_modelLayer)
            m_modelLayer->setKindRenderer(*m_ctx.category, std::move(fresh));
        else if (m_resultsLayer)
            m_resultsLayer->setKindRenderer(*m_ctx.category, std::move(fresh));
    }

    void rebuildTable()
    {
        m_suppress = true;
        m_model->setRowCount(0);

        auto *cz = currentRenderer();
        if (!cz) { m_suppress = false; return; }

        const auto cats = cz->categories();
        for (const auto &c : cats) {
            auto *valItem   = new QStandardItem(c.value);
            auto *labelItem = new QStandardItem(c.label.isEmpty() ? c.value : c.label);
            auto *colorItem = new QStandardItem;

            // Pull colour out of the symbol if it exposes one.
            QColor swatch;
            for (const auto &sl : c.symbol.layers) {
                const auto it = sl.props.constFind(QStringLiteral("color"));
                if (it != sl.props.constEnd()) {
                    const QColor cc(it.value().toString());
                    if (cc.isValid()) { swatch = cc; break; }
                }
            }
            if (!swatch.isValid()) swatch = QColor::fromHsvF(
                (m_model->rowCount() * 0.137) - std::floor(m_model->rowCount() * 0.137),
                0.55, 0.85);
            colorItem->setData(swatch, Qt::BackgroundRole);
            colorItem->setEditable(true);

            m_model->appendRow({ valItem, labelItem, colorItem });
        }
        m_suppress = false;
    }

    void onAttributeChanged(const QString &attr)
    {
        if (m_suppress) return;
        auto *cz = currentRenderer();
        if (!cz) return;
        if (cz->classifyAttribute() == attr) return;
        cz->setClassifyAttribute(attr);
        pushBack(cz);
    }

    /*! Walk every feature in the kind, look up the configured attribute
     *  via the layer's identify path, collect the distinct string
     *  values, and seed one category per unique value.  Existing
     *  categories are merged — values that already exist keep their
     *  colour/label, new ones get a HSV-spread auto colour. */
    void onSampleUniqueValues()
    {
        auto *cz = currentRenderer();
        if (!cz || (!m_modelLayer && !m_resultsLayer)) return;
        if (!m_ctx.category.has_value()) return;

        const QString attr = m_attrCombo->currentText().trimmed();
        if (attr.isEmpty()) return;

        // Always sample against the underlying model layer — even on
        // results layers, the categorical attributes ("tag", "Link
        // type", etc.) come from the network topology, not the time
        // series.  SWMMResultsLayer carries a pointer to its model.
        SWMMModelLayer *model = m_modelLayer;
        if (!model && m_resultsLayer)
            model = m_resultsLayer->modelLayer();
        if (!model) return;

        const SWMMModelLayer::Category cat =
            static_cast<SWMMModelLayer::Category>(*m_ctx.category);
        const int count = model->categoryCount(cat);
        if (count <= 0) return;

        QSet<QString> seen;
        QStringList ordered;     // preserve first-seen order
        ordered.reserve(count);
        for (int row = 0; row < count; ++row) {
            const QString name = model->objectNameAt(cat, row);
            const QVariantMap attrs = model->identifyByName(name);
            const QString v = attrs.value(attr).toString();
            // Treat empty values as "(none)" so categorized still
            // renders something — leaving them out would silently drop
            // every untagged feature back to the fallback symbol.
            const QString key = v.isEmpty() ? QStringLiteral("(none)") : v;
            if (!seen.contains(key)) {
                seen.insert(key);
                ordered.append(key);
            }
        }

        // Build a preserve-existing / append-new pass: lookup current
        // categories by value, reuse their (label, symbol) when present.
        QHash<QString, CategorizedRenderer::Category> existing;
        for (const auto &c : cz->categories())
            existing.insert(c.value, c);

        QList<CategorizedRenderer::Category> next;
        int autoIdx = 0;
        for (const QString &val : ordered) {
            CategorizedRenderer::Category c;
            if (existing.contains(val)) {
                c = existing.value(val);
            } else {
                c.value = val;
                c.label = val;
                const double hue = std::fmod(autoIdx * 0.137, 1.0);
                c.symbol = makeColourSymbol(QColor::fromHsvF(hue, 0.55, 0.85));
            }
            next.append(c);
            ++autoIdx;
        }
        cz->setCategories(next);
        cz->setClassifyAttribute(attr);
        pushBack(cz);
        rebuildTable();
    }

    void onAddCategory()
    {
        auto *cz = currentRenderer();
        if (!cz) return;
        CategorizedRenderer::Category c;
        c.value = tr("value");
        c.label = c.value;
        const int idx = cz->categories().size();
        const double hue = std::fmod(idx * 0.137, 1.0);
        c.symbol = makeColourSymbol(QColor::fromHsvF(hue, 0.55, 0.85));
        cz->addCategory(c);
        pushBack(cz);
        rebuildTable();
    }

    void onRemoveSelected()
    {
        auto *cz = currentRenderer();
        if (!cz) return;
        const auto sel = m_table->selectionModel()
            ? m_table->selectionModel()->selectedRows()
            : QModelIndexList{};
        if (sel.isEmpty()) return;

        QList<int> rows;
        for (const QModelIndex &i : sel) rows.append(i.row());
        std::sort(rows.begin(), rows.end(), std::greater<int>());
        auto cats = cz->categories();
        for (int r : rows)
            if (r >= 0 && r < cats.size()) cats.removeAt(r);
        cz->setCategories(cats);
        pushBack(cz);
        rebuildTable();
    }

    void onClearAll()
    {
        auto *cz = currentRenderer();
        if (!cz) return;
        cz->setCategories({});
        pushBack(cz);
        rebuildTable();
    }

    /*! Open SymbolDetailsDialog on the selected row.  Honours the host
     *  category's archetype so points get shape/size, lines get
     *  width/dash, polygons get outline-width/dash. */
    void onEditSelectedSymbol()
    {
        auto *cz = currentRenderer();
        if (!cz || !m_ctx.category.has_value()) return;
        if (!m_table->selectionModel()) return;
        const auto sel = m_table->selectionModel()->selectedRows();
        if (sel.size() != 1) return;
        const int row = sel.first().row();
        auto cats = cz->categories();
        if (row < 0 || row >= cats.size()) return;

        const auto arch = OpenSWMM::Render::FeatureSublayer::archetypeFor(*m_ctx.category);
        SymbolDetailsDialog dlg(arch, cats.at(row).symbol, this);
        if (dlg.exec() != QDialog::Accepted) return;

        dlg.applyTo(cats[row].symbol);
        cz->setCategories(cats);
        pushBack(cz);
        rebuildTable();
        m_table->selectRow(row);
    }

    /*! Pull row state out of the table and push it back into the
     *  renderer.  Called from QStandardItemModel::itemChanged on every
     *  cell edit including the colour delegate. */
    void onRowEdited(QStandardItem *)
    {
        if (m_suppress) return;
        auto *cz = currentRenderer();
        if (!cz) return;

        QList<CategorizedRenderer::Category> cats;
        const int rows = m_model->rowCount();
        const auto existing = cz->categories();
        cats.reserve(rows);
        for (int r = 0; r < rows; ++r) {
            CategorizedRenderer::Category c;
            c.value = m_model->item(r, 0)->text();
            c.label = m_model->item(r, 1)->text();
            const QColor sw = m_model->item(r, 2)->data(Qt::BackgroundRole).value<QColor>();
            // Carry forward any non-colour layer props the existing
            // category had so we don't accidentally strip stroke /
            // marker shape on a simple colour edit.
            if (r < existing.size()) {
                c.symbol = existing.at(r).symbol;
                if (sw.isValid()) {
                    if (c.symbol.layers.isEmpty())
                        c.symbol = makeColourSymbol(sw);
                    else
                        c.symbol.layers.first().props.insert(
                            QStringLiteral("color"), sw.name(QColor::HexArgb));
                }
            } else {
                c.symbol = makeColourSymbol(sw.isValid() ? sw : QColor(Qt::gray));
            }
            cats.append(c);
        }
        cz->setCategories(cats);
        pushBack(cz);
    }

    RendererPanelContext   m_ctx;
    SWMMModelLayer        *m_modelLayer   = nullptr;
    SWMMResultsLayer      *m_resultsLayer = nullptr;

    QComboBox             *m_attrCombo  = nullptr;
    QTableView            *m_table      = nullptr;
    QStandardItemModel    *m_model      = nullptr;
    QToolButton           *m_sampleBtn  = nullptr;
    QToolButton           *m_addBtn     = nullptr;
    QToolButton           *m_removeBtn  = nullptr;
    QToolButton           *m_clearBtn   = nullptr;
    QToolButton           *m_editSymBtn = nullptr;

    bool m_suppress = false;
};

} // namespace

REGISTER_RENDERER_PANEL(
    "categorized", "Categorized",
    [](const RendererPanelContext &ctx, QWidget *parent) -> IRendererPanel * {
        return new CategorizedPanel(ctx, parent);
    })

} // namespace openswmmvis::ui
