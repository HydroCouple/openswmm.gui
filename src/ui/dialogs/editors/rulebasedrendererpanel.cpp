/*!
 * \file   rulebasedrendererpanel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  IRendererPanel implementation for the "Rule-based" renderer
 *         class.  Slice X.13 — completes the rule-based leg that
 *         previously had no registered panel and fell through to the
 *         "no editor registered" placeholder.
 *
 *         Layout (QGIS-aligned):
 *           - Rules table : Expression | Label | Colour
 *           - Buttons     : Add / Remove / Move up / Move down / Clear
 *           - Fallback    : colour swatch + label for the no-match case
 *
 *         First-match order matters, hence the explicit Move up / Move
 *         down buttons.  Expression eval is still gated behind Slice
 *         BI.2 (LabelExpression DSL) so a banner at the top of the
 *         panel calls that out — empty-expression rules already work
 *         today (they "match always") and are useful for static
 *         "default rule first" setups.
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

namespace openswmmvis::ui {

namespace {

using OpenSWMM::Render::IFeatureRenderer;
using OpenSWMM::Render::RuleBasedRenderer;

/*! Same delegate the categorized panel uses — swatch + double-click
 *  opens QColorDialog. */
class RuleColorDelegate : public QStyledItemDelegate
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
            nullptr, QObject::tr("Rule colour"),
            QColorDialog::ShowAlphaChannel);
        if (!picked.isValid()) return true;
        m->setData(idx, picked, Qt::BackgroundRole);
        return true;
    }
};

OpenSWMM::Render::SymbolStyle makeColourSymbol(const QColor &c)
{
    OpenSWMM::Render::SymbolStyle s;
    OpenSWMM::Render::SymbolLayer layer;
    // Gap A1.2 — canonical QColor variant (hex strings are unreadable
    // through the typed spec readers' value<QColor>()).
    OpenSWMM::Render::SymbolProps::writeColor(
        layer.props, QStringLiteral("color"), c);
    s.layers.append(layer);
    return s;
}

QColor swatchFromSymbol(const OpenSWMM::Render::SymbolStyle &sty)
{
    // Gap A1.2 — tolerant read over both encodings and grammar keys.
    return OpenSWMM::Render::SymbolProps::firstColor(sty);
}

// ---------------------------------------------------------------------------

class RuleBasedPanel : public IRendererPanel
{
public:
    explicit RuleBasedPanel(const RendererPanelContext &ctx, QWidget *parent)
        : IRendererPanel(parent), m_ctx(ctx)
    {
        m_modelLayer   = qobject_cast<SWMMModelLayer *>(ctx.hostLayer);
        m_resultsLayer = qobject_cast<SWMMResultsLayer *>(ctx.hostLayer);

        auto *outer = new QVBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);

        auto *box = new QGroupBox(tr("Rule-based rendering"), this);
        outer->addWidget(box);

        auto *vlay = new QVBoxLayout(box);

        // Slice X.25 — DSL evaluator is live.  Banner now documents the
        // accepted syntax rather than warning users it's deferred.
        auto *banner = new QLabel(tr(
            "<i>Expression syntax:  <b>field = 'value'</b>, "
            "<b>field &gt; 100</b>, <b>NOT</b>, <b>AND</b>, <b>OR</b>, "
            "<b>IS NULL</b>, <b>LIKE 'pat%'</b>.  Empty expression matches "
            "always (use as a default rule at the bottom).</i>"),
            box);
        banner->setWordWrap(true);
        vlay->addWidget(banner);

        m_model = new QStandardItemModel(0, 3, box);
        m_model->setHorizontalHeaderLabels({
            tr("Expression"), tr("Label"), tr("Colour")
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
        m_table->setItemDelegateForColumn(2, new RuleColorDelegate(m_table));
        m_table->setMinimumHeight(180);
        vlay->addWidget(m_table, 1);

        auto *btnRow = new QHBoxLayout;
        m_addBtn    = new QToolButton(box); m_addBtn   ->setText(tr("Add"));
        m_removeBtn = new QToolButton(box); m_removeBtn->setText(tr("Remove"));
        m_upBtn     = new QToolButton(box); m_upBtn    ->setText(tr("Move up"));
        m_downBtn   = new QToolButton(box); m_downBtn  ->setText(tr("Move down"));
        m_clearBtn  = new QToolButton(box); m_clearBtn ->setText(tr("Clear"));
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

        connect(m_addBtn,    &QToolButton::clicked, this, &RuleBasedPanel::onAddRule);
        connect(m_removeBtn, &QToolButton::clicked, this, &RuleBasedPanel::onRemoveSelected);
        connect(m_upBtn,     &QToolButton::clicked, this, [this] { moveSelected(-1); });
        connect(m_downBtn,   &QToolButton::clicked, this, [this] { moveSelected(+1); });
        connect(m_clearBtn,  &QToolButton::clicked, this, &RuleBasedPanel::onClearAll);
        connect(m_model, &QStandardItemModel::itemChanged,
                this, &RuleBasedPanel::onRowEdited);

        refreshFromModel();
    }

    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (o == m_fallbackSwatch && e->type() == QEvent::MouseButtonDblClick) {
            auto *r = currentRenderer();
            if (!r) return true;
            const QColor cur = swatchFromSymbol(r->fallbackSymbol());
            const QColor picked = QColorDialog::getColor(
                cur.isValid() ? cur : QColor(Qt::gray),
                this, tr("Fallback colour"),
                QColorDialog::ShowAlphaChannel);
            if (picked.isValid()) {
                r->setFallbackSymbol(makeColourSymbol(picked));
                pushBack(r);
                refreshFallbackSwatch();
            }
            return true;
        }
        return IRendererPanel::eventFilter(o, e);
    }

    void refreshFromModel() override
    {
        rebuildTable();
        refreshFallbackSwatch();
    }

    QString displayName() const override { return tr("Rule-based"); }

private:
    RuleBasedRenderer *currentRenderer() const
    {
        IFeatureRenderer *r = nullptr;
        // Slice B.6e — Rule path takes priority.
        if (m_ctx.rule) {
            r = m_ctx.rule->renderer();
        } else if (m_ctx.category.has_value()) {
            if (m_modelLayer)   r = m_modelLayer  ->kindRenderer(*m_ctx.category);
            if (m_resultsLayer) r = m_resultsLayer->kindRenderer(*m_ctx.category);
        }
        return dynamic_cast<RuleBasedRenderer *>(r);
    }

    void pushBack(RuleBasedRenderer *r)
    {
        if (!r) return;
        auto fresh = r->clone();
        // Slice B.6e — Rule path takes priority.
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

    void refreshFallbackSwatch()
    {
        if (!m_fallbackSwatch) return;
        auto *r = currentRenderer();
        const QColor c = r ? swatchFromSymbol(r->fallbackSymbol()) : QColor();
        QPalette p = m_fallbackSwatch->palette();
        p.setColor(QPalette::Window, c.isValid() ? c : QColor(Qt::gray));
        m_fallbackSwatch->setPalette(p);
    }

    void rebuildTable()
    {
        m_suppress = true;
        m_model->setRowCount(0);

        auto *r = currentRenderer();
        if (!r) { m_suppress = false; return; }

        const auto rules = r->rules();
        int idx = 0;
        for (const auto &ru : rules) {
            auto *exprItem  = new QStandardItem(ru.expression);
            auto *labelItem = new QStandardItem(ru.label.isEmpty() ? ru.expression : ru.label);
            auto *colorItem = new QStandardItem;
            QColor sw = swatchFromSymbol(ru.symbol);
            if (!sw.isValid()) {
                const double hue = std::fmod(idx * 0.137, 1.0);
                sw = QColor::fromHsvF(hue, 0.55, 0.85);
            }
            colorItem->setData(sw, Qt::BackgroundRole);
            colorItem->setEditable(true);
            m_model->appendRow({ exprItem, labelItem, colorItem });
            ++idx;
        }
        m_suppress = false;
    }

    void onAddRule()
    {
        auto *r = currentRenderer();
        if (!r) return;
        RuleBasedRenderer::Rule ru;
        ru.expression = QString();      // empty → matches always (engine default)
        ru.label      = tr("New rule");
        const int idx = r->rules().size();
        const double hue = std::fmod(idx * 0.137, 1.0);
        ru.symbol = makeColourSymbol(QColor::fromHsvF(hue, 0.55, 0.85));
        r->addRule(ru);
        pushBack(r);
        rebuildTable();
    }

    void onRemoveSelected()
    {
        auto *r = currentRenderer();
        if (!r) return;
        const auto sel = m_table->selectionModel()
            ? m_table->selectionModel()->selectedRows()
            : QModelIndexList{};
        if (sel.isEmpty()) return;

        QList<int> rows;
        for (const QModelIndex &i : sel) rows.append(i.row());
        std::sort(rows.begin(), rows.end(), std::greater<int>());
        auto rules = r->rules();
        for (int row : rows)
            if (row >= 0 && row < rules.size()) rules.removeAt(row);
        r->setRules(rules);
        pushBack(r);
        rebuildTable();
    }

    void moveSelected(int delta)
    {
        auto *r = currentRenderer();
        if (!r) return;
        if (!m_table->selectionModel()) return;
        const QModelIndexList sel = m_table->selectionModel()->selectedRows();
        if (sel.size() != 1) return;
        const int from = sel.first().row();
        const int to   = from + delta;
        auto rules = r->rules();
        if (to < 0 || to >= rules.size()) return;
        rules.move(from, to);
        r->setRules(rules);
        pushBack(r);
        rebuildTable();
        m_table->selectRow(to);
    }

    void onClearAll()
    {
        auto *r = currentRenderer();
        if (!r) return;
        r->setRules({});
        pushBack(r);
        rebuildTable();
    }

    void onRowEdited(QStandardItem *)
    {
        if (m_suppress) return;
        auto *r = currentRenderer();
        if (!r) return;

        QList<RuleBasedRenderer::Rule> rules;
        const int rows = m_model->rowCount();
        const auto existing = r->rules();
        rules.reserve(rows);
        for (int row = 0; row < rows; ++row) {
            RuleBasedRenderer::Rule ru;
            ru.expression = m_model->item(row, 0)->text();
            ru.label      = m_model->item(row, 1)->text();
            const QColor sw = m_model->item(row, 2)->data(Qt::BackgroundRole).value<QColor>();
            if (row < existing.size()) {
                ru.symbol     = existing.at(row).symbol;
                ru.scaleRange = existing.at(row).scaleRange;
                if (sw.isValid()) {
                    if (ru.symbol.layers.isEmpty())
                        ru.symbol = makeColourSymbol(sw);
                    else
                        OpenSWMM::Render::SymbolProps::writeColor(
                            ru.symbol.layers.first().props,
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

    RendererPanelContext   m_ctx;
    SWMMModelLayer        *m_modelLayer   = nullptr;
    SWMMResultsLayer      *m_resultsLayer = nullptr;

    QTableView            *m_table          = nullptr;
    QStandardItemModel    *m_model          = nullptr;
    QToolButton           *m_addBtn         = nullptr;
    QToolButton           *m_removeBtn      = nullptr;
    QToolButton           *m_upBtn          = nullptr;
    QToolButton           *m_downBtn        = nullptr;
    QToolButton           *m_clearBtn       = nullptr;
    QLabel                *m_fallbackSwatch = nullptr;

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
