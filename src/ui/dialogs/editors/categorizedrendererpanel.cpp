/**
 * @file      categorizedrendererpanel.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     IRendererPanel implementation for the "Categorized" renderer
 *         class.  Slice X.4 follow-up — completes the categorized leg
 *         that previously surfaced as an empty stub.
 *
 *         Layout (QGIS-aligned):
 *           - Attribute combo (picks the classifyAttribute on the renderer)
 *           - Categories table   : Value | Label | Colour
 *           - Buttons            : Sample unique values / Add / Remove / Clear / Edit symbol
 *
 *         Every edit writes through `setKindRenderer(category, clone)` on
 *         the host so the layer's rebuildKindFeatureOverrides repaints the
 *         scene and the legend refreshes — single source of truth, MVC
 *         all the way down.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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
#include "ui/dialogs/editors/basesymbolgroup.h" // feedback C2
#include "ui/widgets/dashstylecombo.h"
#include "ui/widgets/markershapecombo.h"
#include "ui/theme/iconfactory.h"

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

namespace openswmmvis::ui
{

namespace
{

using OpenSWMM::Render::CategorizedRenderer;
using OpenSWMM::Render::IFeatureRenderer;

/**
 * @brief Resolve canonical item data for an unchanged selected label, otherwise trim the editable
 * text.
 * @param combo Borrowed non-null live attribute combo.
 * @return Owned canonical key or trimmed free text; no provider/schema validation,
 * mutation or retained reference. An empty/invalid selection can return empty.
 */
QString attributeKey(const QComboBox *combo)
{
    const int row = combo->currentIndex();
    if (row >= 0 && combo->currentText() == combo->itemText(row) && combo->itemData(row).isValid())
        return combo->itemData(row).toString();
    return combo->currentText().trimmed();
}

/*! Render the "Colour" cell as a swatch + open QColorDialog on
 *  double-click.  Duplicated from the kindrendererpanel delegate so the
 *  two stay independent — they may grow apart as categorized gains
 *  fill/stroke/marker selection. */
class CategoryColorDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    /**
     * @brief Draw the BackgroundRole color swatch over optional selection highlight.
     * @param p Borrowed non-null active painter, saved/restored around drawing.
     * @param opt Borrowed cell geometry/palette/state, using a four-pixel inset.
     * @param idx Borrowed color-cell index; invalid color draws no swatch.
     * No text/focus decoration or model write.
     */
    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const override
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

    /**
     * @brief Disable the standard inline color-cell editor.
     * @param parent Borrowed ignored candidate owner.
     * @param option Borrowed ignored option.
     * @param index Borrowed ignored model index.
     * @return Null; double-click colors are handled by editorEvent's modal picker.
     */
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override
    {
        return nullptr;
    }

    /**
     * @brief Open an alpha-enabled color picker on double-click and submit BackgroundRole.
     * @param e Borrowed non-null event; other event types are not consumed.
     * @param m Borrowed non-null writable model, required to survive nested dialog execution.
     * @param option Borrowed ignored option.
     * @param idx Borrowed cell index whose model/row must remain valid and unchanged across
     * modal reentry; no persistent index or post-dialog lifetime check.
     * @return False for other events, true for double-click including Cancel/refused setData.
     * Invalid current color seeds white; no parent is given to picker and write result is
     * ignored.
     */
    bool editorEvent(QEvent *e, QAbstractItemModel *m, const QStyleOptionViewItem &option,
                     const QModelIndex &idx) override
    {
        if (e->type() != QEvent::MouseButtonDblClick)
            return false;
        QColor cur = idx.data(Qt::BackgroundRole).value<QColor>();
        const QColor picked =
            QColorDialog::getColor(cur.isValid() ? cur : QColor(Qt::white), nullptr,
                                   QObject::tr("Category colour"), QColorDialog::ShowAlphaChannel);
        if (!picked.isValid())
            return true;
        m->setData(idx, picked, Qt::BackgroundRole);
        return true;
    }
};

/**
 * @brief Build fallback category attribute suggestions when no provider exists.
 * @param cat Category enumeration.
 * @return Owned tag plus Node type for node kinds, Link type/shape for links, Outlet for
 * subcatchments, otherwise tag alone. Suggestions are strings, not verified available keys.
 */
QStringList suggestedAttributesFor(OpenSWMMVis::SwmmCategory cat)
{
    using namespace OpenSWMMVis;
    QStringList common = {QStringLiteral("tag")};
    switch (cat) {
    case CatJunctions:
    case CatOutfalls:
    case CatStorage:
    case CatDividers:
        return common + QStringList{QStringLiteral("Node type")};
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
        return common + QStringList{QStringLiteral("Outlet")};
    default:
        return common;
    }
}

/**
 * @brief Create a detached one-default-layer symbol with a canonical color QVariant.
 * @param c Borrowed QColor copied without validity validation.
 * @return Owned style, retaining no caller reference and modifying no renderer.
 */
OpenSWMM::Render::SymbolStyle makeColourSymbol(const QColor &c)
{
    OpenSWMM::Render::SymbolStyle s;
    OpenSWMM::Render::SymbolLayer layer;
    // Gap A1.2 — canonical QColor variant (the old hex-string write was
    // unreadable through the typed spec readers' value<QColor>()).
    OpenSWMM::Render::SymbolProps::writeColor(layer.props, QStringLiteral("color"), c);
    s.layers.append(layer);
    return s;
}

/**
 * @brief Convert the first symbol layer's exact shape property to an integer.
 * @param s Borrowed style; later layers are ignored.
 * @param fallback Value returned for absent layer/key or failed QVariant conversion, default
 * minus one.
 * @return Converted raw integer or fallback, without MarkerShape enum-bound validation.
 */
int readSymbolShape(const OpenSWMM::Render::SymbolStyle &s, int fallback = -1)
{
    if (s.layers.isEmpty())
        return fallback;
    const auto it = s.layers.first().props.constFind(QStringLiteral("shape"));
    if (it == s.layers.first().props.constEnd())
        return fallback;
    bool ok = false;
    const int v = it.value().toInt(&ok);
    return ok ? v : fallback;
}
/**
 * @brief Convert the first symbol layer's exact size property to a scalar.
 * @param s Borrowed style; later layers are ignored.
 * @param fallback Missing/unconvertible value, default minus one.
 * @return Converted double in stored pixel units or fallback; no positive/finite/range
 * validation.
 */
double readSymbolSize(const OpenSWMM::Render::SymbolStyle &s, double fallback = -1.0)
{
    if (s.layers.isEmpty())
        return fallback;
    const auto it = s.layers.first().props.constFind(QStringLiteral("size"));
    if (it == s.layers.first().props.constEnd())
        return fallback;
    bool ok = false;
    const double v = it.value().toDouble(&ok);
    return ok ? v : fallback;
}
/**
 * @brief Convert the first symbol layer's exact width property to a scalar.
 * @param s Borrowed style; later layers are ignored.
 * @param fallback Missing/unconvertible value, default minus one.
 * @return Converted double in stored pixel units or fallback, without domain/finite validation.
 */
double readSymbolWidth(const OpenSWMM::Render::SymbolStyle &s, double fallback = -1.0)
{
    if (s.layers.isEmpty())
        return fallback;
    const auto it = s.layers.first().props.constFind(QStringLiteral("width"));
    if (it == s.layers.first().props.constEnd())
        return fallback;
    bool ok = false;
    const double v = it.value().toDouble(&ok);
    return ok ? v : fallback;
}
/**
 * @brief Convert the first symbol layer's exact dash property to an integer.
 * @param s Borrowed style; later layers are ignored.
 * @param fallback Missing/unconvertible value, default SolidLine integer.
 * @return Raw integer or fallback; no Qt pen-style enum validation.
 */
int readSymbolDash(const OpenSWMM::Render::SymbolStyle &s, int fallback = int(Qt::SolidLine))
{
    if (s.layers.isEmpty())
        return fallback;
    const auto it = s.layers.first().props.constFind(QStringLiteral("dash"));
    if (it == s.layers.first().props.constEnd())
        return fallback;
    bool ok = false;
    const int v = it.value().toInt(&ok);
    return ok ? v : fallback;
}

/**
 * @brief Upsert accepted shape/size/width/dash values on the first layer, creating a default
 * layer when absent.
 * @param s Borrowed mutable symbol; unrelated keys/later layers remain intact.
 * @param shape Nonnegative raw shape integer to store; negative preserves any old key.
 * @param size Positive pixel size to store; nonpositive/NaN preserves the old key.
 * @param width Positive pixel width to store; nonpositive/NaN preserves the old key.
 * @param dash Nonnegative raw dash integer to store; negative preserves the old key.
 * Does not remove keys for sentinels, validate enums or reject positive infinity. Even
 * when all values are skipped an initially empty style gains a default layer.
 */
void writeSymbolKnobs(OpenSWMM::Render::SymbolStyle &s, int shape, double size, double width,
                      int dash)
{
    if (s.layers.isEmpty())
        s.layers.append(OpenSWMM::Render::SymbolLayer{});
    auto &props = s.layers.first().props;
    if (shape >= 0)
        props.insert(QStringLiteral("shape"), shape);
    if (size > 0.0)
        props.insert(QStringLiteral("size"), size);
    if (width > 0.0)
        props.insert(QStringLiteral("width"), width);
    if (dash >= 0)
        props.insert(QStringLiteral("dash"), dash);
}

// ---------------------------------------------------------------------------

/*! Small modal dialog for editing the non-colour knobs on a single
 *  category's symbol.  Fields are hidden when the host archetype
 *  doesn't paint with them (e.g. line width is hidden for points). */
class SymbolDetailsDialog : public QDialog
{
public:
    /**
     * @brief Stage noncolor symbol controls applicable to one geometry archetype.
     * @param arch Point builds shape/size; Line/Polygon build width/dash; other values build
     * only buttons. Stored archetype is not used to validate applyTo later.
     * @param initial Borrowed style read synchronously; no reference retained. Missing keys
     * seed defaults that a later apply materializes.
     * @param parent Optional Qt owner of this dialog; caller controls modal execution/lifetime.
     * Owns controls, with point size1..96pixels/one decimal or width0.1..32/two decimals.
     * Constructor does not mutate a renderer, execute modally, snapshot or create undo.
     */
    SymbolDetailsDialog(OpenSWMM::Render::FeatureSublayer::Archetype arch,
                        const OpenSWMM::Render::SymbolStyle &initial, QWidget *parent)
        : QDialog(parent), m_archetype(arch)
    {
        using Arch = OpenSWMM::Render::FeatureSublayer::Archetype;
        setWindowTitle(tr("Edit symbol"));
        auto *form = new QFormLayout(this);

        if (arch == Arch::Point) {
            m_shape = new MarkerShapeCombo(this);
            m_shape->populateCanonical(); // category symbols use canonical MarkerShape
            const int initShape = readSymbolShape(initial, MarkerShapeCombo::Circle);
            m_shape->setShapeValue(initShape);
            form->addRow(tr("S&hape:"), m_shape);

            m_size = new QDoubleSpinBox(this);
            m_size->setRange(1.0, 96.0);
            m_size->setDecimals(1);
            m_size->setSuffix(QStringLiteral(" px"));
            m_size->setValue(readSymbolSize(initial, 6.0));
            form->addRow(tr("Si&ze:"), m_size);
        }
        if (arch == Arch::Line || arch == Arch::Polygon) {
            m_width = new QDoubleSpinBox(this);
            m_width->setRange(0.1, 32.0);
            m_width->setDecimals(2);
            m_width->setSuffix(QStringLiteral(" px"));
            m_width->setValue(readSymbolWidth(initial, arch == Arch::Line ? 1.5 : 0.5));
            form->addRow(arch == Arch::Line ? tr("Line width:") : tr("Outline width:"), m_width);

            m_dash = new DashStyleCombo(this);
            m_dash->setCurrentIndex(m_dash->findData(readSymbolDash(initial, int(Qt::SolidLine))));
            form->addRow(tr("&Dash:"), m_dash);
        }

        auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                        Qt::Horizontal, this);
        form->addRow(bb);
        connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }

    /**
     * @brief Write displayed archetype controls into the supplied detached/live symbol without
     * checking dialog result.
     * @param s Borrowed mutable target, preserving keys not represented by controls. Creates
     * first layer if empty through writeSymbolKnobs, even for unsupported archetype.
     * Caller checks Accepted before use and issues renderer notifications/undo policy.
     * Display defaults/clamps can materialize or change previously absent/precise properties.
     */
    void applyTo(OpenSWMM::Render::SymbolStyle &s) const
    {
        const int shape = m_shape ? m_shape->shapeValue() : -1;
        const double size = m_size ? m_size->value() : -1.0;
        const double width = m_width ? m_width->value() : -1.0;
        const int dash = m_dash ? m_dash->currentData().toInt() : -1;
        writeSymbolKnobs(s, shape, size, width, dash);
    }

private:
    OpenSWMM::Render::FeatureSublayer::Archetype m_archetype;
    MarkerShapeCombo *m_shape = nullptr;
    QDoubleSpinBox *m_size = nullptr;
    QDoubleSpinBox *m_width = nullptr;
    DashStyleCombo *m_dash = nullptr;
};

// ---------------------------------------------------------------------------

class CategorizedPanel : public IRendererPanel
{
public:
    /**
     * @brief Build a live category table, editable attribute picker and optional shared
     * base-symbol group.
     * @param ctx Borrowed context copied into panel storage; raw host/rule/style targets must
     * outlive panel and modal/edit callbacks. Rule renderer takes priority; category/host
     * metadata still drive field hints, sampling and geometry-specific symbol controls.
     * @param parent Optional Qt owner; caller owns parentless panel.
     * Owns model/table/delegate/actions and immediately refreshes, which can store the first
     * provider field on a renderer without an attribute. Edits mutate the live renderer
     * then clone-install for notifications. No undo, snapshot, external-change subscription
     * or provider membership enforcement here; GUI thread only.
     */
    explicit CategorizedPanel(const RendererPanelContext &ctx, QWidget *parent)
        : IRendererPanel(parent), m_ctx(ctx)
    {
        m_modelLayer = qobject_cast<SWMMModelLayer *>(ctx.hostLayer);
        m_resultsLayer = qobject_cast<SWMMResultsLayer *>(ctx.hostLayer);

        auto *outer = new QVBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);

        // Feedback C2 — size / outline / labels stay editable under a
        // classified renderer (colour still comes from the categories).
        if (QWidget *base = createBaseSymbolGroup(ctx, this))
            outer->addWidget(base);

        auto *box = new QGroupBox(tr("Categorized rendering"), this);
        outer->addWidget(box);

        auto *vlay = new QVBoxLayout(box);

        auto *form = new QFormLayout;
        m_attrCombo = new QComboBox(box);
        m_attrCombo->setEditable(true); // user can type a custom key
        form->addRow(tr("A&ttribute:"), m_attrCombo);
        vlay->addLayout(form);

        m_model = new QStandardItemModel(0, 3, box);
        m_model->setHorizontalHeaderLabels({tr("Value"), tr("Label"), tr("Colour")});
        m_table = new QTableView(box);
        m_table->setModel(m_model);
        m_table->verticalHeader()->setVisible(false);
        m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
        m_table->horizontalHeader()->setStretchLastSection(false);
        m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_table->setEditTriggers(QAbstractItemView::DoubleClicked |
                                 QAbstractItemView::SelectedClicked);
        m_table->setAlternatingRowColors(true);
        m_table->setItemDelegateForColumn(2, new CategoryColorDelegate(m_table));
        m_table->setMinimumHeight(180);
        vlay->addWidget(m_table, 1);

        // Button row
        auto *btnRow = new QHBoxLayout;
        m_sampleBtn = new QToolButton(box);
        m_sampleBtn->setText(tr("Sample unique values"));
        m_sampleBtn->setIcon(openswmmvis::ui::IconFactory::icon(QStringLiteral("Search")));
        m_sampleBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        m_sampleBtn->setToolTip(tr("Scan the layer for distinct values of the selected attribute "
                                   "and seed one category per value."));
        btnRow->addWidget(m_sampleBtn);

        m_addBtn = new QToolButton(box);
        m_addBtn->setText(tr("Add"));
        m_addBtn->setIcon(openswmmvis::ui::IconFactory::icon(QStringLiteral("Add")));
        m_addBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        btnRow->addWidget(m_addBtn);

        m_removeBtn = new QToolButton(box);
        m_removeBtn->setText(tr("Remove"));
        m_removeBtn->setIcon(openswmmvis::ui::IconFactory::icon(QStringLiteral("Delete")));
        m_removeBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        btnRow->addWidget(m_removeBtn);

        m_clearBtn = new QToolButton(box);
        m_clearBtn->setText(tr("Clear"));
        m_clearBtn->setIcon(openswmmvis::ui::IconFactory::icon(QStringLiteral("Clear")));
        m_clearBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        btnRow->addWidget(m_clearBtn);

        m_editSymBtn = new QToolButton(box);
        m_editSymBtn->setText(tr("Edit symbol…"));
        m_editSymBtn->setIcon(openswmmvis::ui::IconFactory::icon(QStringLiteral("SelectEdit")));
        m_editSymBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        m_editSymBtn->setToolTip(tr("Edit the shape / size / line width / dash for the selected "
                                    "category.  Colour is editable inline."));
        btnRow->addWidget(m_editSymBtn);

        btnRow->addStretch();
        vlay->addLayout(btnRow);

        connect(m_attrCombo, &QComboBox::editTextChanged, this,
                &CategorizedPanel::onAttributeChanged);
        connect(m_attrCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [this](int) { onAttributeChanged(attributeKey(m_attrCombo)); });

        connect(m_sampleBtn, &QToolButton::clicked, this, &CategorizedPanel::onSampleUniqueValues);
        connect(m_addBtn, &QToolButton::clicked, this, &CategorizedPanel::onAddCategory);
        connect(m_removeBtn, &QToolButton::clicked, this, &CategorizedPanel::onRemoveSelected);
        connect(m_clearBtn, &QToolButton::clicked, this, &CategorizedPanel::onClearAll);
        connect(m_editSymBtn, &QToolButton::clicked, this, &CategorizedPanel::onEditSelectedSymbol);
        connect(m_model, &QStandardItemModel::itemChanged, this, &CategorizedPanel::onRowEdited);

        refreshFromModel();
    }

    /**
     * @brief Rebuild field hints and category rows, possibly store a first provider field for an
     * unset attribute.
     * Provider fields list exact QString types first then all others; without a provider
     * uses unverified suggested text. Preserves nonempty current key as editable text.
     * Blocks combo signals and suppresses table edit callbacks during rebuild. If live
     * attribute is empty and first item's data is nonempty, mutates it then clone-installs;
     * text-only fallback suggestions do not seed this way. Loses table selection/drafts.
     */
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
                auto *provider =
                    m_ctx.hostLayer
                        ? qobject_cast<OpenSWMM::Render::IAttributeProvider *>(m_ctx.hostLayer)
                        : nullptr;
                if (provider) {
                    // Gap A4.3 — string fields first (the natural
                    // categorisation targets: tag / status / group), then
                    // numerics (legal but secondary, QGIS-style).
                    const auto fields = provider->availableAttributes(*m_ctx.category);
                    for (const auto &f : fields)
                        if (f.type == QMetaType::QString)
                            m_attrCombo->addItem(f.displayName, f.name);
                    for (const auto &f : fields)
                        if (f.type != QMetaType::QString)
                            m_attrCombo->addItem(f.displayName, f.name);
                } else {
                    m_attrCombo->addItems(suggestedAttributesFor(*m_ctx.category));
                }
            }
            const QString current = cz ? cz->classifyAttribute() : attributeKey(m_attrCombo);
            const int row = m_attrCombo->findData(current);
            if (row >= 0)
                m_attrCombo->setCurrentIndex(row);
            else
                m_attrCombo->setCurrentText(current);
        }

        rebuildTable();

        // Feedback C3 (categorized leg) — a renderer with no attribute shows
        // the first field; store it so the renderer and its exports carry
        // what the user sees. pushBack re-enters refreshFromModel through
        // the host's rendererChanged, where the attribute is then set.
        if (cz && cz->classifyAttribute().isEmpty() && m_attrCombo->count() > 0) {
            const QString first = m_attrCombo->itemData(0).toString();
            if (!first.isEmpty()) {
                QSignalBlocker b(m_attrCombo);
                m_attrCombo->setCurrentIndex(0);
                cz->setClassifyAttribute(first);
                pushBack(cz);
            }
        }
    }

    /**
     * @brief Return the translated panel label.
     * @return Owned Categorized string, without target/state inspection.
     */
    QString displayName() const override { return tr("Categorized"); }

private:
    /**
     * @brief Resolve the live rule-first or model/results per-kind renderer and require
     * categorized type.
     * @return Borrowed renderer or null; expires on target replacement/destruction. No
     * layer-level fallback, cloning, renderer creation or ownership transfer.
     */
    CategorizedRenderer *currentRenderer() const
    {
        IFeatureRenderer *r = nullptr;
        // Slice B.6d — Rule path takes priority.
        if (m_ctx.rule) {
            r = m_ctx.rule->renderer();
        } else if (m_ctx.category.has_value()) {
            if (m_modelLayer)
                r = m_modelLayer->kindRenderer(*m_ctx.category);
            if (m_resultsLayer)
                r = m_resultsLayer->kindRenderer(*m_ctx.category);
        }
        return dynamic_cast<CategorizedRenderer *>(r);
    }

    /**
     * @brief Clone an already-mutated categorized renderer and install it through
     * rule/model/results target.
     * @param cz Optional borrowed live renderer; null is a no-op. Clone ownership transfers
     * to the selected target, or is discarded without one/category. Successful installation
     * invalidates old renderer pointers. No rollback, undo, validation or success result.
     */
    void pushBack(CategorizedRenderer *cz)
    {
        if (!cz)
            return;
        auto fresh = cz->clone();
        // Slice B.6d — Rule path takes priority.
        if (m_ctx.rule) {
            m_ctx.rule->setRenderer(std::move(fresh));
            return;
        }
        if (!m_ctx.category.has_value())
            return;
        if (m_modelLayer)
            m_modelLayer->setKindRenderer(*m_ctx.category, std::move(fresh));
        else if (m_resultsLayer)
            m_resultsLayer->setKindRenderer(*m_ctx.category, std::move(fresh));
    }

    /**
     * @brief Replace table rows from a copy of current categories under the item-edit suppression
     * flag.
     * Uses value as label when saved label is empty and deterministic HSV color when symbol
     * color is unreadable. Substitutes are display-only until another row edit writes the
     * entire table. Missing renderer clears rows; selection/drafts are lost. No install.
     */
    void rebuildTable()
    {
        m_suppress = true;
        m_model->setRowCount(0);

        auto *cz = currentRenderer();
        if (!cz) {
            m_suppress = false;
            return;
        }

        const auto cats = cz->categories();
        for (const auto &c : cats) {
            auto *valItem = new QStandardItem(c.value);
            auto *labelItem = new QStandardItem(c.label.isEmpty() ? c.value : c.label);
            auto *colorItem = new QStandardItem;

            // Pull colour out of the symbol if it exposes one (gap A1.2 —
            // tolerant read over both grammar keys via SymbolProps).
            QColor swatch = OpenSWMM::Render::SymbolProps::firstColor(c.symbol);
            if (!swatch.isValid())
                swatch = QColor::fromHsvF((m_model->rowCount() * 0.137) -
                                              std::floor(m_model->rowCount() * 0.137),
                                          0.55, 0.85);
            colorItem->setData(swatch, Qt::BackgroundRole);
            colorItem->setEditable(true);

            m_model->appendRow({valItem, labelItem, colorItem});
        }
        m_suppress = false;
    }

    /**
     * @brief Read the canonical/current editable key and mutate/clone-install it when different.
     * @param text Borrowed notification text, ignored in favor of attributeKey(m_attrCombo).
     * No-op under suppression, missing renderer or unchanged key. Unknown/empty keys are
     * accepted without provider validation; no resampling, undo or retained text reference.
     */
    void onAttributeChanged(const QString &text)
    {
        if (m_suppress)
            return;
        const QString attr = attributeKey(m_attrCombo);
        auto *cz = currentRenderer();
        if (!cz)
            return;
        if (cz->classifyAttribute() == attr)
            return;
        cz->setClassifyAttribute(attr);
        pushBack(cz);
    }

    /**
     * @brief Synchronously scan underlying model identities and rebuild categories from
     * first-seen string values.
     * Requires categorized target, model/results host, category, nonempty key and model rows;
     * results use their linked model, not per-frame values. Reads identifyByName exact-key
     * values converted to strings; empty/missing maps to literal (none), conflating it with
     * that actual string. No decimation/cancellation. Retains payloads for observed matching
     * values (last old duplicate wins), appends new deterministic colors and drops old values
     * not observed; this is not a union retaining every old category. Sets attribute,
     * mutates/clone-installs and rebuilds; no undo or diagnostic result.
     */
    void onSampleUniqueValues()
    {
        auto *cz = currentRenderer();
        if (!cz || (!m_modelLayer && !m_resultsLayer))
            return;
        if (!m_ctx.category.has_value())
            return;

        const QString attr = attributeKey(m_attrCombo);
        if (attr.isEmpty())
            return;

        // Always sample against the underlying model layer — even on
        // results layers, the categorical attributes ("tag", "Link
        // type", etc.) come from the network topology, not the time
        // series.  SWMMResultsLayer carries a pointer to its model.
        SWMMModelLayer *model = m_modelLayer;
        if (!model && m_resultsLayer)
            model = m_resultsLayer->modelLayer();
        if (!model)
            return;

        const SWMMModelLayer::Category cat = static_cast<SWMMModelLayer::Category>(*m_ctx.category);
        const int count = model->categoryCount(cat);
        if (count <= 0)
            return;

        QSet<QString> seen;
        QStringList ordered; // preserve first-seen order
        ordered.reserve(count);
        for (int row = 0; row < count; ++row) {
            const QString name = model->objectNameAt(cat, row);
            const QVariantMap attrs =
                model->identifyByName(name, SWMMModelLayer::kindBitForCategory(cat));
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

    /**
     * @brief Append a default value/label and deterministic color, then clone-install/rebuild.
     * No-op without categorized renderer; duplicate default values are allowed. No editor
     * prompt, validation, undo or snapshot.
     */
    void onAddCategory()
    {
        auto *cz = currentRenderer();
        if (!cz)
            return;
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

    /**
     * @brief Remove selected category rows in descending index order, install and rebuild.
     * No-op without target/selection; skips invalid copied indices. No confirmation or undo;
     * table rebuild loses selection/drafts.
     */
    void onRemoveSelected()
    {
        auto *cz = currentRenderer();
        if (!cz)
            return;
        const auto sel = m_table->selectionModel() ? m_table->selectionModel()->selectedRows()
                                                   : QModelIndexList{};
        if (sel.isEmpty())
            return;

        QList<int> rows;
        for (const QModelIndex &i : sel)
            rows.append(i.row());
        std::sort(rows.begin(), rows.end(), std::greater<int>());
        auto cats = cz->categories();
        for (int r : rows)
            if (r >= 0 && r < cats.size())
                cats.removeAt(r);
        cz->setCategories(cats);
        pushBack(cz);
        rebuildTable();
    }

    /**
     * @brief Clear all live categories, clone-install and rebuild an empty table.
     * No-op without target; no confirmation, undo or already-empty change comparison.
     */
    void onClearAll()
    {
        auto *cz = currentRenderer();
        if (!cz)
            return;
        cz->setCategories({});
        pushBack(cz);
        rebuildTable();
    }

    /**
     * @brief Open geometry-specific symbol details for exactly one selected category and apply
     * accepted controls.
     * Requires category, valid row and categorized renderer. Copies category values before
     * modal execution but reuses the raw live renderer afterward; targets/renderer/row must
     * survive modal reentry unchanged. On Accepted writes controls, sets full category list,
     * clone-installs, rebuilds and reselects row. No post-dialog identity check or undo.
     */
    void onEditSelectedSymbol()
    {
        auto *cz = currentRenderer();
        if (!cz || !m_ctx.category.has_value())
            return;
        if (!m_table->selectionModel())
            return;
        const auto sel = m_table->selectionModel()->selectedRows();
        if (sel.size() != 1)
            return;
        const int row = sel.first().row();
        auto cats = cz->categories();
        if (row < 0 || row >= cats.size())
            return;

        const auto arch = OpenSWMM::Render::FeatureSublayer::archetypeFor(*m_ctx.category);
        SymbolDetailsDialog dlg(arch, cats.at(row).symbol, this);
        if (dlg.exec() != QDialog::Accepted)
            return;

        dlg.applyTo(cats[row].symbol);
        cz->setCategories(cats);
        pushBack(cz);
        rebuildTable();
        m_table->selectRow(row);
    }

    /**
     * @brief Serialize the full complete table after any cell edit, retaining existing symbols by
     * row position.
     * @param item Borrowed notification item, ignored; each row must have all three items.
     * No-op under suppression/missing target. Updates first-layer canonical color when valid,
     * preserves other symbol keys/layers, and synthesizes color-only symbols for new/empty
     * rows. Display fallback labels/colors can become stored even on unrelated edits.
     * Does not deduplicate values, validate keys, identity-match categories or create undo.
     */
    void onRowEdited(QStandardItem *item)
    {
        if (m_suppress)
            return;
        auto *cz = currentRenderer();
        if (!cz)
            return;

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
                        OpenSWMM::Render::SymbolProps::writeColor(c.symbol.layers.first().props,
                                                                  QStringLiteral("color"), sw);
                }
            } else {
                c.symbol = makeColourSymbol(sw.isValid() ? sw : QColor(Qt::gray));
            }
            cats.append(c);
        }
        cz->setCategories(cats);
        pushBack(cz);
    }

    RendererPanelContext m_ctx;
    SWMMModelLayer *m_modelLayer = nullptr;
    SWMMResultsLayer *m_resultsLayer = nullptr;

    QComboBox *m_attrCombo = nullptr;
    QTableView *m_table = nullptr;
    QStandardItemModel *m_model = nullptr;
    QToolButton *m_sampleBtn = nullptr;
    QToolButton *m_addBtn = nullptr;
    QToolButton *m_removeBtn = nullptr;
    QToolButton *m_clearBtn = nullptr;
    QToolButton *m_editSymBtn = nullptr;

    bool m_suppress = false;
};

} // namespace

// Gap A4.2 — Categorized needs at least one attribute (string OR numeric;
// numeric categorisation stays legal, QGIS-style).
REGISTER_RENDERER_PANEL_GATED(
    "categorized", "Categorized",
    ([](const RendererPanelContext &ctx, QWidget *parent) -> IRendererPanel * {
        return new CategorizedPanel(ctx, parent);
    }),
    ([](const RendererPanelContext &ctx) { return !ctx.fields.isEmpty(); }),
    "No attributes available for this kind")

} // namespace openswmmvis::ui
