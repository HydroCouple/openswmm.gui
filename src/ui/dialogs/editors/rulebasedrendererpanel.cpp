/**
 * @file      rulebasedrendererpanel.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     IRendererPanel implementation for the "Rule-based" renderer
 *         class.  Slice X.13 — completes the rule-based leg that
 *         previously had no registered panel and fell through to the
 *         "no editor registered" placeholder.
 *
 *         Layout (QGIS-aligned):
 *           - Rules table : Expression | Label | Colour
 *           - Buttons     : Add / Remove / Move up / Move down / Clear
 *           - Fallback    : colour swatch + label for the no-match case
 *
 *         First-match order matters; explicit Move up/down controls preserve order.
 *         The expression evaluator is active. Empty expressions match always,
 *         so an unconditional rule usually belongs after more specific rules.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/dialogs/irendererpanel.h"

#include "layers/openswmmvislayer.h"
#include "layers/swmmmodellayer.h"
#include "layers/swmmresultslayer.h"
#include "render/ifeaturerenderer.h"
#include "render/renderers/rulebasedrenderer.h"
// Slice B.6e — Rule-aware path.
#include "render/rule.h"
#include "render/symbolstyle.h"
#include "ui/theme/iconfactory.h"

#include <QColor>
#include <QColorDialog>
#include <QEvent>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
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

using OpenSWMM::Render::IFeatureRenderer;
using OpenSWMM::Render::RuleBasedRenderer;

/*! Same delegate the categorized panel uses — swatch + double-click
 *  opens QColorDialog. */
class RuleColorDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    /**
     * @brief Paint a BackgroundRole color swatch over optional selection background without item
     * text.
     * @param p Borrowed non-null active painter, saved/restored around drawing.
     * @param opt Borrowed row geometry/palette/selection state; four-pixel inset applied.
     * @param idx Borrowed valid color-cell index; invalid/noncolor data yields no swatch.
     * Does not modify model data or draw standard focus/text decorations.
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
     * @brief Disable standard inline color-cell editor creation.
     * @param parent Borrowed ignored widget owner candidate.
     * @param option Borrowed ignored style option.
     * @param index Borrowed ignored index.
     * @return Null unconditionally; color selection instead uses editorEvent's modal picker.
     */
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override
    {
        return nullptr;
    }

    /**
     * @brief Handle a double-click with an alpha-enabled parentless color picker and
     * BackgroundRole write.
     * @param e Borrowed non-null event, consumed only for MouseButtonDblClick.
     * @param m Borrowed non-null writable model, retained by caller through modal reentry.
     * @param option Borrowed ignored style option.
     * @param idx Borrowed logical cell index; model/row identity must survive nested dialog
     * execution because no persistent-index or post-dialog lifetime validation is used.
     * @return False for other events, true for handled double-click including Cancel or
     * refused setData. Invalid current color seeds white; setter result is ignored.
     */
    bool editorEvent(QEvent *e, QAbstractItemModel *m, const QStyleOptionViewItem &option,
                     const QModelIndex &idx) override
    {
        if (e->type() != QEvent::MouseButtonDblClick)
            return false;
        QColor cur = idx.data(Qt::BackgroundRole).value<QColor>();
        const QColor picked =
            QColorDialog::getColor(cur.isValid() ? cur : QColor(Qt::white), nullptr,
                                   QObject::tr("Rule colour"), QColorDialog::ShowAlphaChannel);
        if (!picked.isValid())
            return true;
        m->setData(idx, picked, Qt::BackgroundRole);
        return true;
    }
};

/**
 * @brief Build a detached style containing one default SymbolLayer and its canonical color
 * QVariant.
 * @param c Borrowed color copied through SymbolProps::writeColor, without validity check.
 * @return Owned style; no target mutation, geometry inference or retained reference.
 */
OpenSWMM::Render::SymbolStyle makeColourSymbol(const QColor &c)
{
    OpenSWMM::Render::SymbolStyle s;
    OpenSWMM::Render::SymbolLayer layer;
    // Gap A1.2 — canonical QColor variant (hex strings are unreadable
    // through the typed spec readers' value<QColor>()).
    OpenSWMM::Render::SymbolProps::writeColor(layer.props, QStringLiteral("color"), c);
    s.layers.append(layer);
    return s;
}

/**
 * @brief Read the first supported color encoding from a symbol through shared tolerant lookup.
 * @param sty Borrowed symbol, not mutated.
 * @return Owned QColor, possibly invalid if no readable color; no synthesized fallback.
 */
QColor swatchFromSymbol(const OpenSWMM::Render::SymbolStyle &sty)
{
    // Gap A1.2 — tolerant read over both encodings and grammar keys.
    return OpenSWMM::Render::SymbolProps::firstColor(sty);
}

// ---------------------------------------------------------------------------

class RuleBasedPanel : public IRendererPanel
{
public:
    /**
     * @brief Build a live ordered expression/label/color table and fallback swatch for the copied
     * target context.
     * @param ctx Borrowed context copied; raw rule/host targets must outlive panel and all
     * callbacks, including modal dialogs. Rule takes renderer priority; legacy paths require
     * model/results plus category. No layer-level fallback.
     * @param parent Optional Qt owner; caller owns parentless panel.
     * Owns table/model/delegate/buttons, connects actions and immediately refreshes. Every
     * edit mutates live renderer then clone-installs through normal target notifications;
     * no expression compilation, undo, Cancel snapshot or external-change subscription here.
     */
    explicit RuleBasedPanel(const RendererPanelContext &ctx, QWidget *parent)
        : IRendererPanel(parent), m_ctx(ctx)
    {
        m_modelLayer = qobject_cast<SWMMModelLayer *>(ctx.hostLayer);
        m_resultsLayer = qobject_cast<SWMMResultsLayer *>(ctx.hostLayer);

        auto *outer = new QVBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);

        auto *box = new QGroupBox(tr("Rule-based rendering"), this);
        outer->addWidget(box);

        auto *vlay = new QVBoxLayout(box);

        // Slice X.25 — DSL evaluator is live.  Banner now documents the
        // accepted syntax rather than warning users it's deferred.
        auto *banner =
            new QLabel(tr("<i>Expression syntax:  <b>field = 'value'</b>, "
                          "<b>field &gt; 100</b>, <b>NOT</b>, <b>AND</b>, <b>OR</b>, "
                          "<b>IS NULL</b>, <b>LIKE 'pat%'</b>.  Empty expression matches "
                          "always (use as a default rule at the bottom).</i>"),
                       box);
        banner->setWordWrap(true);
        vlay->addWidget(banner);

        m_model = new QStandardItemModel(0, 3, box);
        m_model->setHorizontalHeaderLabels({tr("Expression"), tr("Label"), tr("Colour")});
        m_table = new QTableView(box);
        m_table->setModel(m_model);
        m_table->verticalHeader()->setVisible(false);
        m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
        m_table->horizontalHeader()->setStretchLastSection(false);
        m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_table->setEditTriggers(QAbstractItemView::DoubleClicked |
                                 QAbstractItemView::SelectedClicked);
        m_table->setAlternatingRowColors(true);
        m_table->setItemDelegateForColumn(2, new RuleColorDelegate(m_table));
        m_table->setMinimumHeight(180);
        vlay->addWidget(m_table, 1);

        auto *btnRow = new QHBoxLayout;
        m_addBtn = new QToolButton(box);
        m_addBtn->setText(tr("Add"));
        m_addBtn->setIcon(openswmmvis::ui::IconFactory::icon(QStringLiteral("Add")));
        m_removeBtn = new QToolButton(box);
        m_removeBtn->setText(tr("Remove"));
        m_removeBtn->setIcon(openswmmvis::ui::IconFactory::icon(QStringLiteral("Delete")));
        m_upBtn = new QToolButton(box);
        m_upBtn->setText(tr("Move up"));
        m_upBtn->setIcon(openswmmvis::ui::IconFactory::icon(QStringLiteral("MoveUp")));
        m_downBtn = new QToolButton(box);
        m_downBtn->setText(tr("Move down"));
        m_downBtn->setIcon(openswmmvis::ui::IconFactory::icon(QStringLiteral("MoveDown")));
        m_clearBtn = new QToolButton(box);
        m_clearBtn->setText(tr("Clear"));
        m_clearBtn->setIcon(openswmmvis::ui::IconFactory::icon(QStringLiteral("Clear")));
        for (QToolButton *b : {m_addBtn, m_removeBtn, m_upBtn, m_downBtn, m_clearBtn})
            b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        btnRow->addWidget(m_addBtn);
        btnRow->addWidget(m_removeBtn);
        btnRow->addWidget(m_upBtn);
        btnRow->addWidget(m_downBtn);
        btnRow->addWidget(m_clearBtn);
        btnRow->addStretch();
        vlay->addLayout(btnRow);

        // Fallback group — colour the renderer returns when no rule matches.
        auto *fbBox = new QGroupBox(tr("Fallback (no-match)"), box);
        auto *fbLay = new QHBoxLayout(fbBox);
        fbLay->addWidget(new QLabel(tr("Colour:"), fbBox));
        m_fallbackSwatch = new QLabel(fbBox);
        m_fallbackSwatch->setMinimumSize(QSize(48, 18));
        m_fallbackSwatch->setFrameShape(QFrame::Panel);
        m_fallbackSwatch->setFrameShadow(QFrame::Sunken);
        m_fallbackSwatch->setAutoFillBackground(true);
        m_fallbackSwatch->setToolTip(tr("Click to change the fallback colour"));
        m_fallbackSwatch->installEventFilter(this);
        fbLay->addWidget(m_fallbackSwatch);
        fbLay->addStretch();
        vlay->addWidget(fbBox);

        connect(m_addBtn, &QToolButton::clicked, this, &RuleBasedPanel::onAddRule);
        connect(m_removeBtn, &QToolButton::clicked, this, &RuleBasedPanel::onRemoveSelected);
        connect(m_upBtn, &QToolButton::clicked, this, [this] { moveSelected(-1); });
        connect(m_downBtn, &QToolButton::clicked, this, [this] { moveSelected(+1); });
        connect(m_clearBtn, &QToolButton::clicked, this, &RuleBasedPanel::onClearAll);
        connect(m_model, &QStandardItemModel::itemChanged, this, &RuleBasedPanel::onRowEdited);

        refreshFromModel();
    }

    /**
     * @brief Open a fallback color picker on swatch double-click and submit an accepted
     * replacement symbol.
     * @param o Borrowed event receiver compared by identity to the owned fallback swatch.
     * @param e Borrowed non-null event.
     * @return True for that double-click even without a renderer or on Cancel; otherwise
     * base event-filter result. Live renderer/targets must survive modal reentry unchanged,
     * since the pre-dialog raw renderer pointer is reused afterward. Accepted color replaces
     * the complete fallback symbol with a color-only style, then clone-installs/notifies.
     */
    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (o == m_fallbackSwatch && e->type() == QEvent::MouseButtonDblClick) {
            auto *r = currentRenderer();
            if (!r)
                return true;
            const QColor cur = swatchFromSymbol(r->fallbackSymbol());
            const QColor picked =
                QColorDialog::getColor(cur.isValid() ? cur : QColor(Qt::gray), this,
                                       tr("Fallback colour"), QColorDialog::ShowAlphaChannel);
            if (picked.isValid()) {
                r->setFallbackSymbol(makeColourSymbol(picked));
                pushBack(r);
                refreshFallbackSwatch();
            }
            return true;
        }
        return IRendererPanel::eventFilter(o, e);
    }

    /**
     * @brief Rebuild the rule table and refresh the fallback swatch from the live target.
     * Does not install a renderer, subscribe to changes or preserve current selection/drafts;
     * table-generated itemChanged callbacks are suppressed while rebuilding.
     */
    void refreshFromModel() override
    {
        rebuildTable();
        refreshFallbackSwatch();
    }

    /**
     * @brief Return the translated panel display label.
     * @return Owned Rule-based string, without inspecting current renderer class.
     */
    QString displayName() const override { return tr("Rule-based"); }

private:
    /**
     * @brief Resolve/cast the live rule-first or legacy per-kind renderer.
     * @return Borrowed RuleBasedRenderer or null for missing/wrong target. Expires on target
     * replacement/destruction; no ownership transfer, clone or layer-level read.
     */
    RuleBasedRenderer *currentRenderer() const
    {
        IFeatureRenderer *r = nullptr;
        // Slice B.6e — Rule path takes priority.
        if (m_ctx.rule) {
            r = m_ctx.rule->renderer();
        } else if (m_ctx.category.has_value()) {
            if (m_modelLayer)
                r = m_modelLayer->kindRenderer(*m_ctx.category);
            if (m_resultsLayer)
                r = m_resultsLayer->kindRenderer(*m_ctx.category);
        }
        return dynamic_cast<RuleBasedRenderer *>(r);
    }

    /**
     * @brief Clone an already-mutated renderer and transfer ownership through the
     * rule/model/results setter.
     * @param r Optional borrowed renderer valid throughout clone; null is a no-op.
     * No target/category means the clone is discarded. A successful install invalidates old
     * renderer pointers; callers must reacquire before later use. No mutation rollback,
     * undo, expression validation or status result.
     */
    void pushBack(RuleBasedRenderer *r)
    {
        if (!r)
            return;
        auto fresh = r->clone();
        // Slice B.6e — Rule path takes priority.
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
     * @brief Read the live fallback color and set the swatch palette, using gray when
     * unavailable/invalid.
     * No-op without swatch; does not modify target symbol or emit an edit notification.
     */
    void refreshFallbackSwatch()
    {
        if (!m_fallbackSwatch)
            return;
        auto *r = currentRenderer();
        const QColor c = r ? swatchFromSymbol(r->fallbackSymbol()) : QColor();
        QPalette p = m_fallbackSwatch->palette();
        p.setColor(QPalette::Window, c.isValid() ? c : QColor(Qt::gray));
        m_fallbackSwatch->setPalette(p);
    }

    /**
     * @brief Replace table rows from a copy of the live ordered rules under the item-edit
     * suppression flag.
     * Uses expression as display label when saved label is empty and deterministic HSV color
     * for unreadable symbol color. These substitutes are display-only until a subsequent
     * row edit serializes the entire table. Missing target clears rows; selection/drafts
     * are lost. Does not parse expressions or clone-install the renderer.
     */
    void rebuildTable()
    {
        m_suppress = true;
        m_model->setRowCount(0);

        auto *r = currentRenderer();
        if (!r) {
            m_suppress = false;
            return;
        }

        const auto rules = r->rules();
        int idx = 0;
        for (const auto &ru : rules) {
            auto *exprItem = new QStandardItem(ru.expression);
            auto *labelItem = new QStandardItem(ru.label.isEmpty() ? ru.expression : ru.label);
            auto *colorItem = new QStandardItem;
            QColor sw = swatchFromSymbol(ru.symbol);
            if (!sw.isValid()) {
                const double hue = std::fmod(idx * 0.137, 1.0);
                sw = QColor::fromHsvF(hue, 0.55, 0.85);
            }
            colorItem->setData(sw, Qt::BackgroundRole);
            colorItem->setEditable(true);
            m_model->appendRow({exprItem, labelItem, colorItem});
            ++idx;
        }
        m_suppress = false;
    }

    /**
     * @brief Append an unconditional New rule with a deterministic color to a live rule renderer.
     * Empty expression matches always and earlier matches can shadow it. Mutates live rules,
     * clone-installs and rebuilds table; no-op without target. No expression editor, undo
     * or confirmation dialog.
     */
    void onAddRule()
    {
        auto *r = currentRenderer();
        if (!r)
            return;
        RuleBasedRenderer::Rule ru;
        ru.expression = QString(); // empty → matches always (engine default)
        ru.label = tr("New rule");
        const int idx = r->rules().size();
        const double hue = std::fmod(idx * 0.137, 1.0);
        ru.symbol = makeColourSymbol(QColor::fromHsvF(hue, 0.55, 0.85));
        r->addRule(ru);
        pushBack(r);
        rebuildTable();
    }

    /**
     * @brief Remove selected table-row rules in descending index order and clone-install the
     * result.
     * No-op without target/selection; invalid copied row indices are skipped. Rebuilds rows
     * and loses selection/drafts, without confirmation, expression validation or undo.
     */
    void onRemoveSelected()
    {
        auto *r = currentRenderer();
        if (!r)
            return;
        const auto sel = m_table->selectionModel() ? m_table->selectionModel()->selectedRows()
                                                   : QModelIndexList{};
        if (sel.isEmpty())
            return;

        QList<int> rows;
        for (const QModelIndex &i : sel)
            rows.append(i.row());
        std::sort(rows.begin(), rows.end(), std::greater<int>());
        auto rules = r->rules();
        for (int row : rows)
            if (row >= 0 && row < rules.size())
                rules.removeAt(row);
        r->setRules(rules);
        pushBack(r);
        rebuildTable();
    }

    /**
     * @brief Move exactly one selected rule by a signed row offset, install and reselect it.
     * @param delta Signed offset, normally minus/plus one; zero is accepted and can still
     * reinstall. Requires valid current selected row; out-of-range destination is a no-op.
     * Preserves rule payload/order aside from move; no confirmation, undo or status.
     */
    void moveSelected(int delta)
    {
        auto *r = currentRenderer();
        if (!r)
            return;
        if (!m_table->selectionModel())
            return;
        const QModelIndexList sel = m_table->selectionModel()->selectedRows();
        if (sel.size() != 1)
            return;
        const int from = sel.first().row();
        const int to = from + delta;
        auto rules = r->rules();
        if (to < 0 || to >= rules.size())
            return;
        rules.move(from, to);
        r->setRules(rules);
        pushBack(r);
        rebuildTable();
        m_table->selectRow(to);
    }

    /**
     * @brief Clear every live rule, clone-install and rebuild the empty table.
     * No-op without target; retains fallback symbol. No confirmation, undo or dirty-state
     * comparison, even when rules were already empty.
     */
    void onClearAll()
    {
        auto *r = currentRenderer();
        if (!r)
            return;
        r->setRules({});
        pushBack(r);
        rebuildTable();
    }

    /**
     * @brief Serialize every complete table row into ordered rules and clone-install after any
     * cell change.
     * @param item Borrowed notification item, deliberately ignored; all three cells of every
     * row must exist. No-op under rebuild suppression or without target.
     * Retains existing symbols/scale ranges by row position, updates first-layer color when
     * valid and synthesizes a color-only style for new/empty symbols. Thus editing another
     * field can materialize displayed fallback label/color. Does not validate expressions,
     * deduplicate rows or match identity by expression; no undo/transaction status.
     */
    void onRowEdited(QStandardItem *item)
    {
        if (m_suppress)
            return;
        auto *r = currentRenderer();
        if (!r)
            return;

        QList<RuleBasedRenderer::Rule> rules;
        const int rows = m_model->rowCount();
        const auto existing = r->rules();
        rules.reserve(rows);
        for (int row = 0; row < rows; ++row) {
            RuleBasedRenderer::Rule ru;
            ru.expression = m_model->item(row, 0)->text();
            ru.label = m_model->item(row, 1)->text();
            const QColor sw = m_model->item(row, 2)->data(Qt::BackgroundRole).value<QColor>();
            if (row < existing.size()) {
                ru.symbol = existing.at(row).symbol;
                ru.scaleRange = existing.at(row).scaleRange;
                if (sw.isValid()) {
                    if (ru.symbol.layers.isEmpty())
                        ru.symbol = makeColourSymbol(sw);
                    else
                        OpenSWMM::Render::SymbolProps::writeColor(ru.symbol.layers.first().props,
                                                                  QStringLiteral("color"), sw);
                }
            } else {
                ru.symbol = makeColourSymbol(sw.isValid() ? sw : QColor(Qt::gray));
            }
            rules.append(ru);
        }
        r->setRules(rules);
        pushBack(r);
    }

    RendererPanelContext m_ctx;
    SWMMModelLayer *m_modelLayer = nullptr;
    SWMMResultsLayer *m_resultsLayer = nullptr;

    QTableView *m_table = nullptr;
    QStandardItemModel *m_model = nullptr;
    QToolButton *m_addBtn = nullptr;
    QToolButton *m_removeBtn = nullptr;
    QToolButton *m_upBtn = nullptr;
    QToolButton *m_downBtn = nullptr;
    QToolButton *m_clearBtn = nullptr;
    QLabel *m_fallbackSwatch = nullptr;

    bool m_suppress = false;
};

} // namespace

// Gap A4.2 — rule expressions reference attributes; grey out when the
// provider exposes none.
REGISTER_RENDERER_PANEL_GATED(
    "rule", "Rule-based",
    ([](const RendererPanelContext &ctx, QWidget *parent) -> IRendererPanel * {
        return new RuleBasedPanel(ctx, parent);
    }),
    ([](const RendererPanelContext &ctx) { return !ctx.fields.isEmpty(); }),
    "No attributes available for this kind")

} // namespace openswmmvis::ui
