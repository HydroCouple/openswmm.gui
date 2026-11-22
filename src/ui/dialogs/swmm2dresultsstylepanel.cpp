/**
 * @file      swmm2dresultsstylepanel.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     O2-1 / VS.8 — field tree and display tabs for SWMM2DResultsLayer.
 *
 *         Pattern: each control is initialized from the style bag BEFORE
 *         its change-signal is connected, so construction never fires
 *         spurious writes; afterwards every edit applies live (the same
 *         apply-on-edit model the rest of the styling UI uses), and every
 *         control follows its model, so edits made elsewhere (the layer
 *         tree, another tab, Cancel, undo) show here.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/dialogs/swmm2dresultsstylepanel.h"

#include "layers/swmm2dresultslayer.h"
#include "render/sublayers/resultscalarsublayer.h"
#include "render/sublayers/resultvectorsublayer.h"
#include "io/mesh2dvariableexport.h"
#include "ui/dialogs/resultvariableexportdialog.h"
#include "map/spatialreferencesystem.h"
#include <QApplication>
#include <QLineEdit>
#include <QPushButton>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QTableWidget>
#include <QStandardItemModel>
#include <QSignalBlocker>
#include <QFileDialog>
#include <QScopedValueRollback>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QTreeView>
#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>
#include "render/sublayers/scalarfillsublayer.h"
#include "ui/dialogs/editors/classificationbindings.h"
#include "ui/dialogs/sublayertabhelpers.h"
#include "ui/widgets/classificationeditor.h"
#include "ui/widgets/colorbutton.h"
#include "ui/widgets/dashstylecombo.h"
#include "ui/util/tableclipboard.h"

#include <QCheckBox>
#include <QSlider>
#include <QToolButton>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include <functional>

namespace openswmmvis::ui
{

namespace
{

using OpenSWMM::Render::ISublayer;
using OpenSWMM::Render::ISublayerHost;
using OpenSWMM::Render::IsolineStyle;
using OpenSWMM::Render::MeshEdgeStyle;
using OpenSWMM::Render::MeshNodeStyle;
using OpenSWMM::Render::SublayerStyle;

// Spin factories and minimum widths live in ui/dialogs/sublayertabhelpers.h,
// shared with the mesh styling panel.

constexpr int kIdRole = Qt::UserRole;       ///< item id: a sublayer id, or kWaterId
constexpr int kKindRole = Qt::UserRole + 1; ///< ItemKind
/** What a field-tree item edits. */
// Offer: a field of the run without a row yet; checking it adds the row.
enum class ItemKind { Heading, Row, Water, Pass, Offer };
const QString kOfferPrefix = QStringLiteral("offer:");
const QString kWaterId = QStringLiteral("results2d.waterVisibility");

/**
 * @brief Show @p value in a spin box without echoing it back as an edit. A value the box already
 * shows after its own clamping and rounding is skipped, so text being typed survives model
 * notifications (animation ticks invalidate every row each frame).
 */
void followValue(QDoubleSpinBox *spin, double value)
{
    value = std::clamp(value, spin->minimum(), spin->maximum());
    if (spin->value() == spin->valueFromText(spin->textFromValue(value)))
        return;
    const QSignalBlocker block(spin);
    spin->setValue(value);
}
/** @brief Integer overload of followValue(). */
void followValue(QSpinBox *spin, int value)
{
    value = std::clamp(value, spin->minimum(), spin->maximum());
    if (spin->value() == value)
        return;
    const QSignalBlocker block(spin);
    spin->setValue(value);
}
/** @brief Check or uncheck @p box without emitting toggled. */
void followCheck(QAbstractButton *box, bool on)
{
    if (box->isChecked() == on)
        return;
    const QSignalBlocker block(box);
    box->setChecked(on);
}

/**
 * @brief Bind a check box to a boolean property of a style: hydrate it, write the user's edits
 * while the style lives, and follow the style with the box's signals blocked. Widgets that depend
 * on the value read the style, not this box.
 * @param get Callable `(S *) -> bool`. @param set Callable `(S *, bool)`.
 * @return @p box.
 */
template <class S, class Get, class Set>
QCheckBox *bindCheck(QCheckBox *box, S *style, Get get, Set set)
{
    QPointer<S> p = style;
    box->setChecked(std::invoke(get, style));
    QObject::connect(box, &QCheckBox::toggled, box, [p, set](bool on) {
        if (p)
            std::invoke(set, p.data(), on);
    });
    QObject::connect(style, &SublayerStyle::styleChanged, box, [box, p, get] {
        if (p)
            followCheck(box, std::invoke(get, p.data()));
    });
    return box;
}
/**
 * @brief Bind a spin box to a numeric style property, shown multiplied by @p scale.
 * @return @p spin.
 */
template <class S, class Get, class Set>
QDoubleSpinBox *bindDouble(QDoubleSpinBox *spin, S *style, Get get, Set set, double scale = 1.0)
{
    QPointer<S> p = style;
    spin->setValue(scale * std::invoke(get, style));
    QObject::connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), spin,
                     [p, set, scale](double v) {
                         if (p)
                             std::invoke(set, p.data(), v / scale);
                     });
    QObject::connect(style, &SublayerStyle::styleChanged, spin, [spin, p, get, scale] {
        if (p)
            followValue(spin, scale * std::invoke(get, p.data()));
    });
    return spin;
}
/** @brief Bind an integer spin box to an integer style property. @return @p spin. */
template <class S, class Get, class Set>
QSpinBox *bindInt(QSpinBox *spin, S *style, Get get, Set set)
{
    QPointer<S> p = style;
    spin->setValue(std::invoke(get, style));
    QObject::connect(spin, qOverload<int>(&QSpinBox::valueChanged), spin, [p, set](int v) {
        if (p)
            std::invoke(set, p.data(), v);
    });
    QObject::connect(style, &SublayerStyle::styleChanged, spin, [spin, p, get] {
        if (p)
            followValue(spin, std::invoke(get, p.data()));
    });
    return spin;
}
/** @brief Bind an alpha-capable color button to a color style property. @return @p button. */
template <class S, class Get, class Set>
ColorButton *bindColor(ColorButton *button, S *style, Get get, Set set)
{
    QPointer<S> p = style;
    button->setShowAlpha(true);
    button->setColor(std::invoke(get, style));
    QObject::connect(button, &ColorButton::colorChanged, button, [p, set](const QColor &c) {
        if (p)
            std::invoke(set, p.data(), c);
    });
    QObject::connect(style, &SublayerStyle::styleChanged, button, [button, p, get] {
        if (!p)
            return;
        const QColor color = std::invoke(get, p.data());
        if (button->color() != color) {
            const QSignalBlocker block(button);
            button->setColor(color);
        }
    });
    return button;
}
/** @brief Bind a stroke-style combo to a pen-style property. @return @p dash. */
template <class S, class Get, class Set>
DashStyleCombo *bindDash(DashStyleCombo *dash, S *style, Get get, Set set)
{
    QPointer<S> p = style;
    dash->setPenStyle(std::invoke(get, style));
    QObject::connect(dash, &DashStyleCombo::penStyleChanged, dash, [p, set](Qt::PenStyle s) {
        if (p)
            std::invoke(set, p.data(), s);
    });
    QObject::connect(style, &SublayerStyle::styleChanged, dash, [dash, p, get] {
        if (!p)
            return;
        const Qt::PenStyle s = std::invoke(get, p.data());
        if (dash->penStyle() != s) {
            const QSignalBlocker block(dash);
            dash->setPenStyle(s);
        }
    });
    return dash;
}
/**
 * @brief Bind a combo whose item data are the property's values (enumerators as int).
 * @return @p combo.
 */
template <class S, class Get, class Set>
QComboBox *bindCombo(QComboBox *combo, S *style, Get get, Set set)
{
    using V = std::decay_t<std::invoke_result_t<Get, S *>>;
    QPointer<S> p = style;
    const auto show = [combo](V v) {
        const int index = combo->findData(int(v));
        if (index >= 0 && combo->currentIndex() != index) {
            const QSignalBlocker block(combo);
            combo->setCurrentIndex(index);
        }
    };
    show(std::invoke(get, style));
    QObject::connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), combo,
                     [p, set, combo](int i) {
                         if (p && i >= 0)
                             std::invoke(set, p.data(), static_cast<V>(combo->itemData(i).toInt()));
                     });
    QObject::connect(style, &SublayerStyle::styleChanged, combo, [p, get, show] {
        if (p)
            show(std::invoke(get, p.data()));
    });
    return combo;
}
/** @brief Bind a check box to a sublayer's visibility, following it. @return @p box. */
QCheckBox *bindVisible(QCheckBox *box, ISublayer *sub)
{
    QPointer<ISublayer> p = sub;
    box->setChecked(sub->isVisible());
    QObject::connect(box, &QCheckBox::toggled, box, [p](bool on) {
        if (p)
            p->setVisible(on);
    });
    QObject::connect(sub, &ISublayer::invalidated, box, [box, p] {
        if (p)
            followCheck(box, p->isVisible());
    });
    return box;
}
/**
 * @brief Bind a spin box to a sublayer's opacity, shown multiplied by @p scale; follows it.
 * @return @p spin.
 */
QDoubleSpinBox *bindOpacity(QDoubleSpinBox *spin, ISublayer *sub, double scale)
{
    QPointer<ISublayer> p = sub;
    spin->setValue(scale * sub->opacity());
    QObject::connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), spin,
                     [p, scale](double v) {
                         if (p)
                             p->setOpacity(v / scale);
                     });
    QObject::connect(sub, &ISublayer::invalidated, spin, [spin, p, scale] {
        if (p)
            followValue(spin, scale * p->opacity());
    });
    return spin;
}
/**
 * @brief Opacity of a pass or built-in sublayer and, with a caption, its visibility; both follow
 * the sublayer.
 * @param showLabel Show box caption, or empty for the opacity alone.
 * @return New parent-owned header.
 */
QWidget *passHeader(QWidget *parent, ISublayer *sub, const QString &showLabel)
{
    auto *row = new QWidget(parent);
    auto *form = new QFormLayout(row);
    form->setContentsMargins(0, 0, 0, 0);
    if (!showLabel.isEmpty()) {
        auto *show = new QCheckBox(showLabel, row);
        form->addRow(QString(), show);
        if (sub)
            bindVisible(show, sub);
    }
    auto *opacity = makeDSpin(row, 0.0, 1.0, 0.05, 2, sub ? double(sub->opacity()) : 1.0);
    form->addRow(QObject::tr("Opacity:"), opacity);
    if (sub)
        bindOpacity(opacity, sub, 1.0);
    return row;
}
/**
 * @brief Move @p page into a new frameless resizable scroll area, so a short dialog scrolls
 * instead of compressing the editors below their minimum sizes.
 * @return New @p owner-owned scroll area.
 */
QScrollArea *wrapScroll(QWidget *page, QWidget *owner)
{
    auto *scroll = new QScrollArea(owner);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(page);
    return scroll;
}
/**
 * @brief Add a scrolling display tab whose first control is the display's Show box.
 * The box writes the display toggle and, when turned on, also shows the row, so a display turned
 * on is never silently invisible. It follows the style, and the tab carries a check icon while the
 * toggle is on (on, not drawn: a hidden row draws nothing).
 * @param page Tab content with a box layout; the Show box goes to its top.
 * @param pageName Object name of the tab page (routing, tab restore).
 * @param get Callable `(S *) -> bool`. @param set Callable `(S *, bool)`.
 * @return The Show box.
 */
template <class S, class Get, class Set>
QCheckBox *addDisplayTab(QTabWidget *tabs, QWidget *page, const char *pageName,
                         const QString &caption, const QString &showLabel, const char *showName,
                         S *style, Get get, Set set, ISublayer *row)
{
    auto *box = new QCheckBox(showLabel, page);
    box->setObjectName(QLatin1String(showName));
    box->setAccessibleName(showLabel);
    if (auto *layout = qobject_cast<QBoxLayout *>(page->layout()))
        layout->insertWidget(0, box);
    auto *scroll = wrapScroll(page, tabs);
    scroll->setObjectName(QLatin1String(pageName));
    tabs->addTab(scroll, caption);
    QPointer<S> p = style;
    QPointer<ISublayer> r = row;
    box->setChecked(std::invoke(get, style));
    QObject::connect(box, &QCheckBox::toggled, box, [p, r, set](bool on) {
        if (!p)
            return;
        std::invoke(set, p.data(), on);
        if (on && r)
            r->setVisible(true);
    });
    const auto sync = [tabs, scroll, box, p, get] {
        if (!p)
            return;
        const bool on = std::invoke(get, p.data());
        followCheck(box, on);
        const int index = tabs->indexOf(scroll);
        if (index >= 0 && tabs->tabIcon(index).isNull() == on)
            tabs->setTabIcon(index, on ? tabs->style()->standardIcon(QStyle::SP_DialogApplyButton,
                                                                     nullptr, tabs)
                                       : QIcon());
    };
    QObject::connect(style, &SublayerStyle::styleChanged, box, sync);
    sync();
    return box;
}

/**
 * @brief Add the level controls of an isoline style: mode, then interval and base
 * (enabled in fixed interval mode). The level count and method sit on the row's classification
 * editor. All follow the style.
 */
void addIsolineLevelRows(QFormLayout *form, QWidget *owner, IsolineStyle *st)
{
    using LM = IsolineStyle::LevelMode;
    auto *mode = new QComboBox(owner);
    mode->addItem(QObject::tr("Level count"), int(LM::Count));
    mode->addItem(QObject::tr("Fixed interval + base"), int(LM::FixedInterval));
    mode->setMinimumWidth(kComboMinWidthPx);
    mode->setAccessibleName(QObject::tr("Contour levels"));
    bindCombo(mode, st, &IsolineStyle::levelMode, &IsolineStyle::setLevelMode);
    form->addRow(QObject::tr("M&ode:"), mode);
    auto *interval = bindDouble(makeDSpin(owner, 1e-6, 1e12, 0.1, 6, st->levelInterval(), QString()),
                                st, &IsolineStyle::levelInterval, &IsolineStyle::setLevelInterval);
    interval->setAccessibleName(QObject::tr("Contour interval"));
    form->addRow(QObject::tr("Interval (native units):"), interval);
    auto *base = bindDouble(makeDSpin(owner, -1e12, 1e12, 0.1, 6, st->baseLevel(), QString()), st,
                            &IsolineStyle::baseLevel, &IsolineStyle::setBaseLevel);
    base->setAccessibleName(QObject::tr("Contour base level"));
    base->setToolTip(QObject::tr("Contours fall at base + k × interval"));
    form->addRow(QObject::tr("Base level (native units):"), base);
    const auto apply = [st, interval, base] {
        const bool fixed = st->levelMode() == LM::FixedInterval;
        interval->setEnabled(fixed);
        base->setEnabled(fixed);
    };
    apply();
    QObject::connect(st, &SublayerStyle::styleChanged, mode, apply);
}
void addIsolineLineRows(QFormLayout *form, QWidget *owner, IsolineStyle *st)
{
    auto *color =
        bindColor(new ColorButton(owner), st, &IsolineStyle::color, &IsolineStyle::setColor);
    color->setAccessibleName(QObject::tr("Contour and label color"));
    form->addRow(QObject::tr("Line and label color (shared):"), color);
    auto *width = bindDouble(makeDSpin(owner, 0.1, 20.0, 0.25, 2, st->lineWidthPx(), QObject::tr(" px")),
                             st, &IsolineStyle::lineWidthPx, &IsolineStyle::setLineWidthPx);
    width->setAccessibleName(QObject::tr("Line width"));
    form->addRow(QObject::tr("&Width:"), width);
    auto *dash = bindDash(new DashStyleCombo(owner), st, &IsolineStyle::dashPattern,
                          &IsolineStyle::setDashPattern);
    dash->setMinimumWidth(kComboMinWidthPx);
    dash->setAccessibleName(QObject::tr("Contour stroke style"));
    form->addRow(QObject::tr("St&roke style:"), dash);
    auto *every = bindInt(makeSpin(owner, 0, 256, st->indexEvery()), st, &IsolineStyle::indexEvery,
                          &IsolineStyle::setIndexEvery);
    every->setSpecialValueText(QObject::tr("Off"));
    every->setToolTip(QObject::tr("Emphasize every Nth contour (topographic "
                                  "index contours); 0 disables"));
    form->addRow(QObject::tr("I&ndex contour every:"), every);
    auto *indexWidth = bindDouble(makeDSpin(owner, 0.1, 20.0, 0.25, 2, st->indexWidthPx(),
                                            QObject::tr(" px")),
                                  st, &IsolineStyle::indexWidthPx, &IsolineStyle::setIndexWidthPx);
    form->addRow(QObject::tr("Inde&x width:"), indexWidth);
    const auto apply = [st, indexWidth] { indexWidth->setEnabled(st->indexEvery() >= 1); };
    apply();
    QObject::connect(st, &SublayerStyle::styleChanged, indexWidth, apply);
}
void addIsolineLabelRows(QFormLayout *form, QWidget *owner, IsolineStyle *st,
                         const QString &toggle, const QString &colorCaption)
{
    if (!toggle.isEmpty()) {
        auto *labels = bindCheck(new QCheckBox(toggle, owner), st, &IsolineStyle::labels,
                                 &IsolineStyle::setLabels);
        labels->setAccessibleName(toggle);
        form->addRow(QString(), labels);
    }
    if (!colorCaption.isEmpty()) {
        auto *color =
            bindColor(new ColorButton(owner), st, &IsolineStyle::color, &IsolineStyle::setColor);
        color->setAccessibleName(QObject::tr("Contour and label color"));
        form->addRow(colorCaption, color);
    }
    auto *decimals = bindInt(makeSpin(owner, 0, 9, st->labelDecimals()), st,
                             &IsolineStyle::labelDecimals, &IsolineStyle::setLabelDecimals);
    decimals->setAccessibleName(QObject::tr("Label decimal places"));
    form->addRow(QObject::tr("Decimals:"), decimals);
    auto *font = bindDouble(makeDSpin(owner, 4.0, 72.0, 0.5, 1, st->labelFontPt(), QObject::tr(" pt")),
                            st, &IsolineStyle::labelFontPt, &IsolineStyle::setLabelFontPt);
    font->setAccessibleName(QObject::tr("Label font size"));
    form->addRow(QObject::tr("Font si&ze:"), font);
    auto *halo = bindCheck(new QCheckBox(QObject::tr("Label halo"), owner), st,
                           &IsolineStyle::labelHalo, &IsolineStyle::setLabelHalo);
    form->addRow(QString(), halo);
}

/**
 * @brief Format a native variable label with domain/zone and availability of units.
 * @param[in] v Descriptor whose label falls back to dataset; sigma layers include their stored
 * layer index.
 * @return Translated domain/zone-prefixed label plus bracketed units or units unknown; avoids
 * repeating an existing case-insensitive domain prefix.
 */
QString resultVariableLabel(const openswmmvis::io::Mesh2DResultVariable &v)
{
    using V = openswmmvis::io::Mesh2DResultVariable;
    QString domain =
        v.domain == V::Domain::Groundwater ? QObject::tr("Groundwater") : QObject::tr("Surface");
    if (v.zone == V::Zone::Saturated)
        domain += QObject::tr(" / saturated");
    else if (v.zone == V::Zone::Unsaturated)
        domain += QObject::tr(" / unsaturated");
    else if (v.zone == V::Zone::Sigma)
        domain += QObject::tr(" / sigma %1").arg(v.layer);
    const QString label = v.label.isEmpty() ? v.dataset : v.label;
    const QString named = label.startsWith(domain, Qt::CaseInsensitive)
                              ? label
                              : QStringLiteral("%1 — %2").arg(domain, label);
    return QStringLiteral("%1 [%2]").arg(named,
                                         v.unitsKnown ? v.units : QObject::tr("units unknown"));
}
/**
 * @param[in] v Vector result.
 * @return "Vector: <label> [units]".
 */
QString resultVectorLabel(const openswmmvis::io::Mesh2DResultVector &v)
{
    return QObject::tr("Vector: %1 [%2]")
        .arg(v.label, v.x.unitsKnown ? v.x.units : QObject::tr("units unknown"));
}
/**
 * @return Index of the tree item whose id is @p id, searching every level; invalid when absent.
 */
QModelIndex findItem(const QStandardItemModel *model, const QString &id)
{
    if (id.isEmpty() || model->rowCount() == 0)
        return {};
    const auto hits = model->match(model->index(0, 0), kIdRole, id, 1,
                                   Qt::MatchExactly | Qt::MatchRecursive);
    return hits.isEmpty() ? QModelIndex() : hits.first();
}

} // namespace

Swmm2DResultsStylePanel::Swmm2DResultsStylePanel(SWMM2DResultsLayer *layer, QWidget *parent)
    : QWidget(parent), m_layer(layer)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);

    if (!m_layer)
        return;

    // The field tree (left) beside the selected item's page (right), in the
    // KindTreeSymbologyPanel idiom.
    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);
    root->addWidget(splitter, 1);
    auto *left = new QWidget(splitter);
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    m_resultModel = new QStandardItemModel(this);
    m_resultTree = new QTreeView(left);
    m_resultTree->setObjectName("additionalResultList");
    m_resultTree->setAccessibleName(tr("Result fields"));
    m_resultTree->setHeaderHidden(true);
    m_resultTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_resultTree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_resultTree->setModel(m_resultModel);
    m_resultTree->setMinimumWidth(180);
    // Every field of the run is listed with a check box: checking one draws it beside the
    // others (its row is created then); unchecking hides it and keeps its style.
    m_fieldFilter = new QLineEdit(left);
    m_fieldFilter->setObjectName("additionalResultFilter");
    m_fieldFilter->setPlaceholderText(tr("Filter fields"));
    m_fieldFilter->setAccessibleName(tr("Filter result fields"));
    m_fieldFilter->setClearButtonEnabled(true);
    leftLayout->addWidget(m_fieldFilter);
    leftLayout->addWidget(m_resultTree, 1);
    connect(m_fieldFilter, &QLineEdit::textChanged, this, [this] { applyFieldFilter(); });
    auto *buttons = new QHBoxLayout;
    m_removeResult = new QPushButton(tr("&Remove"), left);
    m_removeResult->setObjectName("additionalResultRemove");
    m_removeResult->setToolTip(tr("Delete this field's row and its style; the field stays listed "
                                  "unchecked."));
    buttons->addWidget(m_removeResult);
    buttons->addStretch();
    leftLayout->addLayout(buttons);
    m_resultDetailHost = new QWidget(splitter);
    new QVBoxLayout(m_resultDetailHost);
    m_resultDetailHost->layout()->setContentsMargins(0, 0, 0, 0);
    splitter->addWidget(left);
    splitter->addWidget(m_resultDetailHost);
    splitter->setStretchFactor(1, 1);

    connect(m_resultTree->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex &current, const QModelIndex &previous) {
                if (m_suppressEdits)
                    return;
                // A heading edits nothing: step onto the field next to it, up when the
                // arrow key came from below, otherwise down (a click selects the group's
                // first field). Queued, so the view finishes its click or key first.
                if (current.data(kKindRole).toInt() == int(ItemKind::Heading)) {
                    const bool up = previous.isValid() && m_resultTree->indexBelow(current) == previous;
                    // Step past the nested headings (kind, then how the fields vary).
                    const auto step = [this, up](const QModelIndex &from) {
                        return up ? m_resultTree->indexAbove(from) : m_resultTree->indexBelow(from);
                    };
                    QModelIndex target = step(current);
                    while (target.isValid() &&
                           target.data(kKindRole).toInt() == int(ItemKind::Heading))
                        target = step(target);
                    if (!target.isValid())
                        target = previous;
                    QMetaObject::invokeMethod(
                        this,
                        [this, target = QPersistentModelIndex(target)] {
                            if (target.isValid() && m_resultTree)
                                m_resultTree->setCurrentIndex(target);
                        },
                        Qt::QueuedConnection);
                    return;
                }
                rebuildPageIfStale();
            });
    connect(m_resultModel, &QStandardItemModel::itemChanged, this, [this](QStandardItem *item) {
        if (m_suppressEdits || !m_layer || !item->isCheckable())
            return;
        if (item->data(kKindRole).toInt() == int(ItemKind::Offer)) {
            if (item->checkState() != Qt::Checked)
                return;
            // Adding the row rebuilds this model: not from inside its own signal.
            const QString key = item->data(kIdRole).toString().mid(kOfferPrefix.size());
            QMetaObject::invokeMethod(this, [this, key] { addField(key); }, Qt::QueuedConnection);
            return;
        }
        auto *sub = ISublayerHost::findSublayer(*m_layer, item->data(kIdRole).toString());
        const bool on = item->checkState() == Qt::Checked;
        if (sub && sub->isVisible() != on)
            sub->setVisible(on);
    });
    connect(m_removeResult, &QPushButton::clicked, this, [this] {
        const QModelIndex current = m_resultTree->currentIndex();
        if (!m_layer || current.data(kKindRole).toInt() != int(ItemKind::Row))
            return;
        // The neighbor takes the selection: the next row of the group, else the previous one.
        QModelIndex next = current.siblingAtRow(current.row() + 1);
        if (!next.isValid())
            next = current.siblingAtRow(current.row() - 1);
        m_pendingId = next.data(kIdRole).toString();
        m_layer->removeResultSublayer(current.data(kIdRole).toString());
        m_pendingId.clear();
    });
    connect(m_layer, &SWMM2DResultsLayer::resultSublayersChanged, this,
            &Swmm2DResultsStylePanel::refreshAdditionalResults);
    connect(m_layer, &SWMM2DResultsLayer::timeRangeChanged, this,
            &Swmm2DResultsStylePanel::refreshAdditionalResults);
    connect(m_layer, &SWMM2DResultsLayer::childrenChanged, this,
            &Swmm2DResultsStylePanel::refreshAdditionalResults);
    connect(m_layer, &SWMM2DResultsLayer::currentTimeChanged, this,
            &Swmm2DResultsStylePanel::refreshPageData);
    connect(m_layer, &SWMM2DResultsLayer::highlightedCellsChanged, this,
            &Swmm2DResultsStylePanel::refreshPageData);
    refreshAdditionalResults();

    connect(m_layer, &QObject::destroyed, this, [this] {
        m_layer.clear();
        m_suppressEdits = true;
        setEnabled(false);
        disconnect(m_resultTree->selectionModel(), nullptr, this, nullptr);
        disconnect(m_resultModel, nullptr, this, nullptr);
        delete m_rowBindings;
        m_rowBindings = nullptr;
        // Silence pending editor callbacks without disconnecting Qt's
        // internal destroyed/model bookkeeping. destroyed() is emitted
        // even while signals are blocked, so deferred deletion stays safe.
        detachPage(true);
        m_resultTree->setEnabled(false);
        m_fieldFilter->setEnabled(false);
        m_removeResult->setEnabled(false);
    });
}

bool Swmm2DResultsStylePanel::focusResult(const QString &id)
{
    if (!m_layer || !m_resultTree)
        return false;
    // The hidden passes are edited on the tabs of the row that presents them.
    struct Route {
        const char *pass;
        bool velocity;
        const char *tab;
    };
    static constexpr Route kRoutes[] = {
        {"results2d.cellDepthFill", false, "resultFillTab"},
        {"results2d.smoothDepthFill", false, "resultFillTab"},
        {"results2d.bands", false, "resultFilledContoursTab"},
        {"results2d.isolines", false, "resultContourLinesTab"},
        {"results2d.flowStreaks", true, "resultStreaksTab"},
        {"results2d.velocity", true, "resultArrowsTab"},
    };
    QString item = id, tab;
    for (const auto &route : kRoutes) {
        if (id != QLatin1String(route.pass))
            continue;
        OpenSWMM::Render::ISublayer *row =
            route.velocity
                ? static_cast<OpenSWMM::Render::ISublayer *>(m_layer->velocityResultSublayer())
                : m_layer->depthResultSublayer();
        if (row) {
            item = row->id();
            tab = QLatin1String(route.tab);
        }
        break;
    }
    if (!selectItem(item))
        return false;
    if (!tab.isEmpty())
        selectTab(tab);
    m_resultTree->setFocus();
    return true;
}

void Swmm2DResultsStylePanel::refreshAdditionalResults()
{
    if (!m_layer || !m_resultTree || deferWhileModal())
        return;
    QStringList catalog;
    for (const auto &v : m_layer->resultVariables())
        catalog << v.key() + QLatin1Char('=') + resultVariableLabel(v);
    for (const auto &v : m_layer->resultVectors())
        catalog << v.key() + QLatin1Char('=') + resultVectorLabel(v);
    m_catalogSignature = catalog.join(QLatin1Char('\n'));
    // The tree lists the rows and every field without one: rebuilt when either changes.
    QStringList listed{m_catalogSignature};
    for (auto *sub : m_layer->sublayers())
        if (sub && m_layer->sublayerListed(sub)) {
            QString key;
            if (auto *scalar = qobject_cast<OpenSWMM::Render::ResultScalarSublayer *>(sub))
                key = scalar->variableKey();
            else if (auto *vector = qobject_cast<OpenSWMM::Render::ResultVectorSublayer *>(sub))
                key = vector->vectorKey();
            listed << sub->id() + QLatin1Char('@') + QString::number(m_layer->sublayerGroup(sub)) +
                          QLatin1Char('=') + key;
        }
    if (const QString signature = listed.join(QLatin1Char('\n')); signature != m_treeSignature) {
        m_treeSignature = signature;
        rebuildTree();
    }
    // A live run ends here on every new frame: the page keeps its widgets,
    // focus, scroll position and drafts, and only refreshes its data.
    if (!rebuildPageIfStale())
        refreshPageData();
}

bool Swmm2DResultsStylePanel::deferWhileModal()
{
    auto *modal = QApplication::activeModalWidget();
    if (!modal || modal == window())
        return false;
    // A picker opened from the page (color, ramp) runs its own event loop:
    // replacing or refreshing the page under it would delete or reset widgets
    // it still uses. Look again once it has closed.
    if (!m_retryPending) {
        m_retryPending = true;
        QTimer::singleShot(250, this, [this] {
            m_retryPending = false;
            refreshAdditionalResults();
        });
    }
    return true;
}

void Swmm2DResultsStylePanel::refreshPageData()
{
    if (m_refreshResultData && !deferWhileModal())
        m_refreshResultData();
}

void Swmm2DResultsStylePanel::addField(const QString &key)
{
    if (!m_layer || key.isEmpty())
        return;
    OpenSWMM::Render::ISublayer *sub =
        key.startsWith(QLatin1String("vector:"))
            ? static_cast<OpenSWMM::Render::ISublayer *>(m_layer->addResultVectorSublayer(key))
            : m_layer->addResultSublayer(key);
    if (!sub)
        return;
    m_lastTab.clear(); // a new field opens on its first tab
    refreshAdditionalResults();
    selectItem(sub->id());
}

void Swmm2DResultsStylePanel::applyFieldFilter()
{
    if (!m_resultTree || !m_fieldFilter)
        return;
    const QString text = m_fieldFilter->text().trimmed();
    // Hide fields that do not match and headings left without a visible field. The layer-wide
    // items are not fields: never filtered away.
    const std::function<bool(QStandardItem *)> apply = [&](QStandardItem *parent) {
        bool any = false;
        for (int r = 0; r < parent->rowCount(); ++r) {
            auto *child = parent->child(r);
            bool show;
            if (child->data(kKindRole).toInt() == int(ItemKind::Heading))
                show = child->data(kIdRole).toString() == QLatin1String("layer") || apply(child);
            else
                show = text.isEmpty() || child->text().contains(text, Qt::CaseInsensitive) ||
                       child->toolTip().contains(text, Qt::CaseInsensitive);
            m_resultTree->setRowHidden(r, parent->index(), !show);
            any |= show;
        }
        return any;
    };
    apply(m_resultModel->invisibleRootItem());
}

void Swmm2DResultsStylePanel::rebuildTree()
{
    using namespace OpenSWMM::Render;
    const QString selected = currentItemId();
    const QScopedValueRollback suppress(m_suppressEdits, true);
    delete m_rowBindings;
    m_rowBindings = new QObject(this);
    m_resultModel->clear();
    /**
     * @brief Append a heading item. Argument `text`: caption. Returns: the new top-level item.
     */
    const auto heading = [this](const QString &text) {
        auto *item = new QStandardItem(text);
        item->setData(int(ItemKind::Heading), kKindRole);
        item->setFlags(Qt::ItemIsEnabled);
        QFont font = item->font();
        font.setBold(true);
        item->setFont(font);
        m_resultModel->appendRow(item);
        return item;
    };
    /**
     * @brief Append a checkable item under `parent` for `sub` that follows its name and visibility.
     * Argument `kind`: Row for result rows, Pass for built-in sublayers.
     */
    const auto addItem = [this](QStandardItem *parent, ISublayer *sub, ItemKind kind) {
        auto *item = new QStandardItem(sub->displayName());
        item->setData(sub->id(), kIdRole);
        item->setData(int(kind), kKindRole);
        if (auto *scalar = qobject_cast<ResultScalarSublayer *>(sub))
            item->setToolTip(scalar->variableKey());
        else if (auto *vector = qobject_cast<ResultVectorSublayer *>(sub))
            item->setToolTip(vector->vectorKey());
        item->setCheckable(true);
        item->setCheckState(sub->isVisible() ? Qt::Checked : Qt::Unchecked);
        parent->appendRow(item);
        const QPersistentModelIndex index(item->index());
        connect(sub, &ISublayer::invalidated, m_rowBindings, [this, sub, index] {
            auto *it = index.isValid() ? m_resultModel->itemFromIndex(index) : nullptr;
            if (!it)
                return;
            const QScopedValueRollback guard(m_suppressEdits, true);
            if (const QString name = sub->displayName(); it->text() != name)
                it->setText(name);
            const auto state = sub->isVisible() ? Qt::Checked : Qt::Unchecked;
            if (it->checkState() != state)
                it->setCheckState(state);
            // A re-key, here or of another row, can change the shown row's
            // variable or whether it presents the depth/velocity passes.
            rebuildPageIfStale();
        });
    };
    // Fields are either vectors or scalars, per domain; under each, grouped by how they vary:
    // with time, statistical summaries of the run (saved by the engine), or fixed. Each group
    // lists its rows (top of the paint stack first), then, unchecked, every field of the run
    // without a row in catalog order: checking one adds it to what is drawn.
    using V = openswmmvis::io::Mesh2DResultVariable;
    enum Varies { TimeVarying, Statistical, Fixed, VariesCount };
    const auto varies = [](V::Temporal temporal) {
        return temporal == V::Temporal::Envelope ? Statistical
               : temporal == V::Temporal::Static ? Fixed
                                                 : TimeVarying;
    };
    const QStringList kinds = m_layer->sublayerGroups(); // 2 * domain + (scalar ? 1 : 0)
    const QStringList variesTitles{tr("Time varying"), tr("Statistical summaries"), tr("Static")};
    struct Offer {
        QString key, label;
    };
    QVector<QVector<QVector<ISublayer *>>> rows(kinds.size(),
                                                QVector<QVector<ISublayer *>>(VariesCount));
    QVector<QVector<QVector<Offer>>> offers(kinds.size(), QVector<QVector<Offer>>(VariesCount));
    QSet<QString> shown;
    const auto subs = m_layer->sublayers();
    for (int i = int(subs.size()) - 1; i >= 0; --i) {
        auto *sub = subs[i];
        const int kind = sub && m_layer->sublayerListed(sub) ? m_layer->sublayerGroup(sub) : -1;
        if (kind < 0 || kind >= kinds.size())
            continue;
        int group = TimeVarying; // an unavailable field keeps its place among the time series
        if (auto *scalar = qobject_cast<ResultScalarSublayer *>(sub)) {
            shown.insert(scalar->variableKey());
            if (scalar->descriptor().key() == scalar->variableKey())
                group = varies(scalar->descriptor().temporal);
        } else if (auto *vector = qobject_cast<ResultVectorSublayer *>(sub)) {
            shown.insert(vector->vectorKey());
            if (vector->descriptor().key() == vector->vectorKey())
                group = varies(vector->descriptor().x.temporal);
        }
        rows[kind][group].append(sub);
    }
    const auto vectors = m_layer->resultVectors();
    for (const auto &v : vectors)
        if (!shown.contains(v.key()))
            offers[2 * int(v.domain())][varies(v.x.temporal)].append(
                {v.key(), resultVectorLabel(v)});
    for (const auto &v : m_layer->resultVariables())
        if (!shown.contains(v.key()) && !openswmmvis::io::isVectorComponentKey(vectors, v.key()))
            offers[2 * int(v.domain) + 1][varies(v.temporal)].append(
                {v.key(), resultVariableLabel(v)});
    for (int kind = 0; kind < kinds.size(); ++kind) {
        QStandardItem *kindItem = nullptr;
        for (int group = 0; group < VariesCount; ++group) {
            if (rows[kind][group].isEmpty() && offers[kind][group].isEmpty())
                continue;
            if (!kindItem)
                kindItem = heading(kinds[kind]);
            auto *groupItem = new QStandardItem(variesTitles[group]);
            groupItem->setData(int(ItemKind::Heading), kKindRole);
            groupItem->setFlags(Qt::ItemIsEnabled);
            QFont font = groupItem->font();
            font.setItalic(true);
            groupItem->setFont(font);
            kindItem->appendRow(groupItem);
            for (auto *sub : rows[kind][group])
                addItem(groupItem, sub, ItemKind::Row);
            for (const auto &offer : offers[kind][group]) {
                auto *item = new QStandardItem(offer.label);
                item->setData(kOfferPrefix + offer.key, kIdRole);
                item->setData(int(ItemKind::Offer), kKindRole);
                item->setToolTip(tr("%1 — check to draw this field").arg(offer.key));
                item->setCheckable(true);
                item->setCheckState(Qt::Unchecked);
                item->setForeground(
                    m_resultTree->palette().brush(QPalette::Disabled, QPalette::Text));
                groupItem->appendRow(item);
            }
        }
    }
    // Layer-wide items. The mesh fill (results2d.mesh) has no editor.
    auto *layerGroup = heading(tr("Layer"));
    layerGroup->setData(QStringLiteral("layer"), kIdRole); // not a field: never filtered
    auto *water = new QStandardItem(tr("Water visibility"));
    water->setData(kWaterId, kIdRole);
    water->setData(int(ItemKind::Water), kKindRole);
    layerGroup->appendRow(water);
    for (int i = int(subs.size()) - 1; i >= 0; --i)
        if ((subs[i] == m_layer->meshEdgeSublayer() || subs[i] == m_layer->meshNodeSublayer() ||
             subs[i] == m_layer->flowStreakSublayer()) &&
            subs[i] && m_layer->sublayerListed(subs[i]))
            addItem(layerGroup, subs[i], ItemKind::Pass);
    m_resultTree->expandAll();
    // Keep the selection; else the pending neighbor; else the first field; else water.
    QModelIndex index = findItem(m_resultModel, selected);
    if (!index.isValid())
        index = findItem(m_resultModel, m_pendingId);
    if (!index.isValid() && m_resultModel->rowCount() > 1) { // a field group precedes Layer
        // The first drawn field, else the first listed one.
        const auto drawn = m_resultModel->match(m_resultModel->index(0, 0), kKindRole,
                                                int(ItemKind::Row), 1,
                                                Qt::MatchExactly | Qt::MatchRecursive);
        if (!drawn.isEmpty())
            index = drawn.first();
        else if (const auto offered = m_resultModel->match(
                     m_resultModel->index(0, 0), kKindRole, int(ItemKind::Offer), 1,
                     Qt::MatchExactly | Qt::MatchRecursive);
                 !offered.isEmpty())
            index = offered.first();
    }
    if (!index.isValid())
        index = water->index();
    m_resultTree->setCurrentIndex(index);
    applyFieldFilter();
}

QString Swmm2DResultsStylePanel::pageSignature() const
{
    using namespace OpenSWMM::Render;
    const QString id = currentItemId();
    QString signature = id;
    if (!m_layer)
        return signature;
    auto *sub = ISublayerHost::findSublayer(*m_layer, id);
    if (auto *scalar = qobject_cast<ResultScalarSublayer *>(sub))
        signature += QLatin1Char('|') + scalar->variableKey() +
                     (scalar == m_layer->depthResultSublayer() ? QLatin1String("|depth")
                                                               : QLatin1String("|")) +
                     (scalar->descriptor().key() == scalar->variableKey() ? QLatin1String("|ok")
                                                                          : QLatin1String("|"));
    else if (auto *vector = qobject_cast<ResultVectorSublayer *>(sub))
        signature += QLatin1Char('|') + vector->vectorKey() +
                     (vector == m_layer->velocityResultSublayer() ? QLatin1String("|velocity")
                                                                  : QLatin1String("|")) +
                     (vector->descriptor().key() == vector->vectorKey() ? QLatin1String("|ok")
                                                                        : QLatin1String("|"));
    else
        return signature; // water and built-in pages do not list the catalog
    return signature + QLatin1Char('|') + m_catalogSignature;
}

bool Swmm2DResultsStylePanel::rebuildPageIfStale()
{
    if (deferWhileModal())
        return false;
    const QString signature = pageSignature();
    if (signature == m_pageSignature)
        return false;
    m_pageSignature = signature;
    rebuildDetails();
    return true;
}

QString Swmm2DResultsStylePanel::currentItemId() const
{
    return m_resultTree ? m_resultTree->currentIndex().data(kIdRole).toString() : QString();
}

bool Swmm2DResultsStylePanel::selectItem(const QString &id)
{
    const QModelIndex index = findItem(m_resultModel, id);
    if (!index.isValid())
        return false;
    m_resultTree->setCurrentIndex(index);
    rebuildPageIfStale();
    return true;
}

void Swmm2DResultsStylePanel::selectTab(const QString &page)
{
    auto *tabs = m_resultDetailHost->findChild<QTabWidget *>(QStringLiteral("resultDisplayTabs"));
    if (!tabs)
        return;
    for (int i = 0; i < tabs->count(); ++i)
        if (tabs->widget(i)->objectName() == page)
            tabs->setCurrentIndex(i);
}

void Swmm2DResultsStylePanel::detachPage(bool silence)
{
    m_refreshResultData = {};
    if (!m_resultDetailHost)
        return;
    auto *layout = m_resultDetailHost->layout();
    while (auto *item = layout->takeAt(0)) {
        if (auto *w = item->widget()) {
            w->setEnabled(false);
            if (silence)
                for (auto *object : w->findChildren<QObject *>())
                    object->blockSignals(true);
            w->hide();
            w->setParent(nullptr);
            w->deleteLater();
        }
        delete item;
    }
}

void Swmm2DResultsStylePanel::rebuildDetails()
{
    using namespace OpenSWMM::Render;
    // A change can arrive inside a child signal callback. Remove old controls
    // from discovery/focus immediately, but defer their destruction safely.
    detachPage(false);
    if (!m_layer || !m_resultDetailHost)
        return;
    const QModelIndex current = m_resultTree->currentIndex();
    const auto kind = ItemKind(current.data(kKindRole).toInt());
    const QString id = current.data(kIdRole).toString();
    m_removeResult->setEnabled(kind == ItemKind::Row);
    auto *host = m_resultDetailHost->layout();
    if (kind == ItemKind::Water || kind == ItemKind::Pass) {
        QWidget *page = nullptr;
        if (kind == ItemKind::Water)
            page = buildWaterPage(m_resultDetailHost);
        else if (m_layer->meshEdgeSublayer() && id == m_layer->meshEdgeSublayer()->id())
            page = buildMeshEdgeTab(m_resultDetailHost);
        else if (m_layer->meshNodeSublayer() && id == m_layer->meshNodeSublayer()->id())
            page = buildMeshNodeTab(m_resultDetailHost);
        else if (m_layer->flowStreakSublayer() && id == m_layer->flowStreakSublayer()->id())
            page = buildFlowStreakTab(m_resultDetailHost, nullptr);
        if (page)
            host->addWidget(wrapScroll(page, m_resultDetailHost));
        return;
    }
    if (kind == ItemKind::Offer) {
        // A field not drawn yet: its name and how to add it; editors come with its row.
        auto *page = new QWidget(m_resultDetailHost);
        auto *form = new QVBoxLayout(page);
        auto *name = new QLabel(current.data(Qt::DisplayRole).toString(), page);
        name->setObjectName("additionalResultOfferName");
        name->setWordWrap(true);
        QFont font = name->font();
        font.setBold(true);
        name->setFont(font);
        form->addWidget(name);
        auto *hint = new QLabel(tr("Check the box to draw this field. It is drawn beside the "
                                   "fields already shown and gets its own style."),
                                page);
        hint->setObjectName("additionalResultOfferHint");
        hint->setWordWrap(true);
        form->addWidget(hint);
        form->addStretch();
        host->addWidget(page);
        return;
    }
    auto *row = ISublayerHost::findSublayer(*m_layer, id);
    if (kind != ItemKind::Row || !row)
        return;
    auto *page = new QWidget(m_resultDetailHost);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(buildRowHeader(row, page));
    auto *tabs = new QTabWidget(page);
    tabs->setObjectName("resultDisplayTabs");
    tabs->setDocumentMode(true);
    layout->addWidget(tabs, 1);
    host->addWidget(page);
    if (auto *vector = qobject_cast<ResultVectorSublayer *>(row))
        buildVectorTabs(vector, tabs);
    else if (auto *scalar = qobject_cast<ResultScalarSublayer *>(row))
        buildScalarTabs(scalar, tabs);
    // The last shown display stays in front across rows and rebuilds.
    for (int i = 0; i < tabs->count(); ++i)
        if (tabs->widget(i)->objectName() == m_lastTab)
            tabs->setCurrentIndex(i);
    connect(tabs, &QTabWidget::currentChanged, page, [this, tabs](int i) {
        if (i >= 0)
            m_lastTab = tabs->widget(i)->objectName();
    });
}

QWidget *Swmm2DResultsStylePanel::buildRowHeader(OpenSWMM::Render::ISublayer *row,
                                                 QWidget *parent)
{
    using namespace OpenSWMM::Render;
    auto *header = new QWidget(parent);
    auto *form = new QFormLayout(header);
    // The field this row draws. A row never changes field: other fields are checked in the list.
    auto *scalar = qobject_cast<ResultScalarSublayer *>(row);
    auto *vector = qobject_cast<ResultVectorSublayer *>(row);
    auto domain = openswmmvis::io::Mesh2DResultVariable::Domain::Surface;
    QString name;
    if (scalar) {
        const QString key = scalar->variableKey();
        name = tr("Unavailable — %1").arg(key);
        for (const auto &v : m_layer->resultVariables())
            if (v.key() == key) {
                name = resultVariableLabel(v);
                break;
            }
    } else {
        const QString key = vector->vectorKey();
        name = tr("Unavailable — %1").arg(key);
        for (const auto &v : m_layer->resultVectors())
            if (v.key() == key) {
                name = resultVectorLabel(v);
                domain = v.domain();
                break;
            }
    }
    auto *field = new QLabel(name, header);
    field->setObjectName("additionalResultField");
    field->setWordWrap(true);
    field->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFont bold = field->font();
    bold.setBold(true);
    field->setFont(bold);
    form->addRow(field);
    auto *line = new QHBoxLayout;
    auto *show = bindVisible(new QCheckBox(tr("Show"), header), row);
    show->setObjectName("additionalResultVisible");
    show->setAccessibleName(tr("Show result layer"));
    line->addWidget(show);
    auto *opacity = new QDoubleSpinBox(header);
    opacity->setObjectName("additionalResultOpacity");
    opacity->setRange(0, 100);
    opacity->setSuffix(tr(" %"));
    bindOpacity(opacity, row, 100);
    auto *opacityLabel = new QLabel(tr("&Opacity:"), header);
    opacityLabel->setBuddy(opacity);
    line->addSpacing(12);
    line->addWidget(opacityLabel);
    line->addWidget(opacity);
    if (vector && domain == openswmmvis::io::Mesh2DResultVariable::Domain::Surface) {
        auto *cutoff = bindDouble(makeDSpin(header, 0, 1000, 0.005, 3, 0, tr(" m")),
                                  vector->vectorStyle(), &ResultVectorStyle::dryDepthCutoff,
                                  &ResultVectorStyle::setDryDepthCutoff);
        cutoff->setObjectName("additionalResultDryCutoff");
        cutoff->setAccessibleName(tr("Dry depth cutoff"));
        cutoff->setToolTip(row == m_layer->velocityResultSublayer()
                               ? tr("No arrows, and no color range, where the water is shallower "
                                    "than this. The animated streaks are not masked.")
                               : tr("No arrows or streaks, and no color range, where the water is "
                                    "shallower than this."));
        auto *cutoffLabel = new QLabel(tr("&Dry cutoff:"), header);
        cutoffLabel->setBuddy(cutoff);
        line->addSpacing(12);
        line->addWidget(cutoffLabel);
        line->addWidget(cutoff);
    }
    line->addStretch();
    form->addRow(line);
    return header;
}

void Swmm2DResultsStylePanel::buildScalarTabs(OpenSWMM::Render::ResultScalarSublayer *row,
                                              QTabWidget *tabs)
{
    using namespace OpenSWMM::Render;
    using namespace openswmmvis::io;
    using RS = ResultScalarStyle;
    using BM = BinMethod;
    QPointer<SWMM2DResultsLayer> layer = m_layer;
    QPointer<ResultScalarSublayer> sub = row;
    auto *style = row->fillStyle();
    QPointer<RS> guarded = style;
    auto *line = style->contourStyle();
    QPointer<IsolineStyle> guardedLine = line;
    const bool depthRow = row == m_layer->depthResultSublayer();
    /**
     * @brief Resolve the frame a classification of this row previews: the whole run only when the
     * scheme classifies over it (automatic FixedOverRun) and the source has its history, else the
     * current frame. Argument `scheme`: the edited scheme. Returns: shared immutable frame or
     * empty when the layer/row was destroyed.
     */
    const auto frameFor = [layer, sub](const ClassificationScheme &scheme) {
        if (!layer || !sub)
            return std::shared_ptr<const Mesh2DScalarFrame>();
        const bool wholeRun =
            !scheme.useCustomRange() && scheme.rangeMode() == RangeMode::FixedOverRun;
        auto frame = layer->resultFrame(sub->variableKey(), layer->currentTimeIndex(), wholeRun);
        // Without a whole-run history (live runs) preview the current frame, so
        // the user can still choose a per-frame or custom range.
        if (wholeRun && frame && !frame->error.isEmpty())
            frame = layer->resultFrame(sub->variableKey(), layer->currentTimeIndex());
        return frame;
    };
    /**
     * @brief Create a binding that edits one scheme of this row, previewing its own frame.
     * Arguments `get`, `set`: scheme getter/setter. `continuous`: offer continuous mode.
     */
    const auto binding = [frameFor, layer, depthRow](
                             std::function<ClassificationScheme()> get,
                             std::function<void(const ClassificationScheme &)> set,
                             bool continuous) {
        // Depth is drawn by passes that classify from dry to the run's maximum depth: its
        // editors preview that range, so the classes shown are the classes drawn.
        if (depthRow)
            return new SublayerSchemeBinding(
                get, set, [] { return QVector<double>{}; },
                [layer] { return qMakePair(0.0, layer ? layer->maxDepth() : 0.0); }, continuous,
                true);
        return new SublayerSchemeBinding(
            get, set,
            [frameFor, get] {
                auto f = frameFor(get());
                return f ? f->samples : QVector<double>();
            },
            [frameFor, get] {
                auto f = frameFor(get());
                const double nan = std::numeric_limits<double>::quiet_NaN();
                return f ? qMakePair(f->minimum, f->maximum) : qMakePair(nan, nan);
            },
            continuous, true);
    };

    // ── Fill: the flat or smooth cell fill ───────────────────────────────────
    auto *fill = new QWidget(tabs);
    auto *fillLayout = new QVBoxLayout(fill);
    auto *fillForm = new QFormLayout;
    fillLayout->addLayout(fillForm);
    auto *smooth = bindCheck(new QCheckBox(tr("Smooth (interpolated)"), fill), style,
                             &RS::smoothFill, &RS::setSmoothFill);
    smooth->setObjectName("additionalResultSmoothFill");
    smooth->setAccessibleName(smooth->text());
    fillForm->addRow(smooth);
    {
        const auto smoothEnabled = [style, smooth] { smooth->setEnabled(style->cellFill()); };
        smoothEnabled();
        connect(style, &SublayerStyle::styleChanged, smooth, smoothEnabled);
    }
    // The editor of the cell fill's classes (filled contours have their own tab).
    auto *fillEditor = new ClassificationEditor(
        binding([guarded] { return guarded ? guarded->scheme() : ClassificationScheme(); },
                [guarded](const ClassificationScheme &s) {
                    if (guarded)
                        guarded->setScheme(s);
                },
                true),
        true, fill);
    fillEditor->setObjectName("additionalResultClassification");
    fillEditor->setOutsideRangeControlsVisible(true); // the fill draws them
    fillLayout->addWidget(fillEditor);
    {
        auto *colors = new QGroupBox(tr("Cells without a value"), fill);
        colors->setToolTip(tr("Used by the flat fill, by every cell of an invalid classification, "
                              "and by the legend while the fill or filled contours show."));
        auto *colorForm = new QFormLayout(colors);
        /**
         * @brief Add a live availability-color control. Argument `label`: caption/accessibility
         * name. Argument `name`: object name. Arguments `get`, `set`: style color members.
         */
        const auto addColor = [&](const QString &label, const char *name, auto get, auto set) {
            auto *button = bindColor(new ColorButton(colors), style, get, set);
            button->setObjectName(name);
            button->setAccessibleName(label);
            colorForm->addRow(label, button);
        };
        addColor(tr("Missing data:"), "additionalResultMissing", &RS::missingColor,
                 &RS::setMissingColor);
        addColor(tr("Waterless cell:"), "additionalResultWaterless", &RS::waterlessColor,
                 &RS::setWaterlessColor);
        addColor(tr("Not applicable:"), "additionalResultNotApplicable", &RS::notApplicableColor,
                 &RS::setNotApplicableColor);
        fillLayout->addWidget(colors);
    }
    fillLayout->addStretch();
    addDisplayTab(tabs, fill, "resultFillTab", tr("Fill"), tr("Show fill"),
                  "additionalResultCellFill", style, &RS::cellFill, &RS::setCellFill, row);

    // ── Filled contours ──────────────────────────────────────────────────────
    auto *bands = new QWidget(tabs);
    auto *bandsLayout = new QVBoxLayout(bands);
    // Filled contours classify the interpolated field into bands of their own; the Fill tab's
    // classes color each cell by its value. Bands are always classes.
    auto *bandEditor = new ClassificationEditor(
        binding(
            [guarded] {
                auto scheme = guarded ? guarded->bandScheme() : ClassificationScheme();
                scheme.setMode(ClassificationScheme::ClassMode::Classified);
                return scheme;
            },
            [guarded](const ClassificationScheme &s) {
                if (guarded)
                    guarded->setBandScheme(s);
            },
            false),
        true, bands);
    bandEditor->setObjectName("additionalResultBandClassification");
    bandEditor->setOutsideRangeControlsVisible(true); // bands draw the outside colors
    bandsLayout->addWidget(bandEditor);
    bandsLayout->addStretch();
    addDisplayTab(tabs, bands, "resultFilledContoursTab", tr("Filled contours"),
                  tr("Show filled contours"), "additionalResultFilledContours", style,
                  &RS::filledContours, &RS::setFilledContours, row);

    // ── Contour lines ────────────────────────────────────────────────────────
    auto *lines = new QWidget(tabs);
    auto *linesLayout = new QVBoxLayout(lines);
    auto *levels = new QFormLayout;
    linesLayout->addLayout(levels);
    addIsolineLevelRows(levels, lines, line);
    auto *hint = new QLabel(tr("Level count: the classes below set the lines (lines = classes "
                               "− 1). Fixed interval: lines at base + k × interval. The range "
                               "applies to both; the line color is set below."),
                            lines);
    hint->setWordWrap(true);
    linesLayout->addWidget(hint);
    // The only editor of the contour scheme: method, classes, range and breaks. Lines use the
    // classes, not a continuous ramp: always classified, so a scheme saved continuous keeps
    // its count, method and breaks editable.
    auto *contourEditor = new ClassificationEditor(
        binding(
            [guardedLine] {
                auto scheme = guardedLine ? guardedLine->scheme() : ClassificationScheme();
                scheme.setMode(ClassificationScheme::ClassMode::Classified);
                return scheme;
            },
            [guardedLine](const ClassificationScheme &s) {
                if (!guardedLine)
                    return;
                auto scheme = s;
                scheme.setMode(ClassificationScheme::ClassMode::Classified);
                guardedLine->setScheme(scheme);
            },
            false),
        true, lines);
    contourEditor->setMaximumClassCount(256);
    contourEditor->setObjectName("additionalResultContourClassification");
    linesLayout->addWidget(contourEditor);
    auto *stroke = new QFormLayout;
    linesLayout->addLayout(stroke);
    addIsolineLineRows(stroke, lines, line);
    addIsolineLabelRows(stroke, lines, line, tr("Label contours"), QString());
    linesLayout->addStretch();
    addDisplayTab(tabs, lines, "resultContourLinesTab", tr("Contour lines"),
                  tr("Show contour lines"), "additionalResultIsolines", style, &RS::isolines,
                  &RS::setIsolines, row);

    // ── Labels: native cell values ───────────────────────────────────────────
    auto *labels = new QWidget(tabs);
    auto *labelsLayout = new QVBoxLayout(labels);
    auto *labelForm = new QFormLayout;
    labelsLayout->addLayout(labelForm);
    addIsolineLabelRows(labelForm, labels, line, QString(), tr("Line and label color (shared):"));
    auto *shared = new QLabel(tr("Decimals, font and halo are shared with the contour-line "
                                 "labels."),
                              labels);
    shared->setWordWrap(true);
    labelsLayout->addWidget(shared);
    labelsLayout->addStretch();
    addDisplayTab(tabs, labels, "resultLabelsTab", tr("Labels"), tr("Show cell value labels"),
                  "additionalResultCellLabels", style, &RS::cellLabels, &RS::setCellLabels, row);

    /**
     * @brief Refresh this row's classification editors (valid drafts only) and enable each only
     * while its own previewed range is finite, so an editor without one never blocks OK/Apply.
     * An editor being disabled drops its draft, so it shows no stale bounds. Runs on every
     * frame: refreshFromData() leaves an editor the user is typing in alone.
     */
    QPointer<ClassificationEditor> fillGuard = fillEditor, contourGuard = contourEditor,
                                   bandGuard = bandEditor;
    const auto refreshEditors = [frameFor, guarded, guardedLine, fillGuard, contourGuard,
                                 bandGuard] {
        const auto ranged = [&frameFor](const ClassificationScheme &scheme) {
            const auto f = frameFor(scheme);
            return f && f->error.isEmpty() && std::isfinite(f->minimum) &&
                   std::isfinite(f->maximum);
        };
        if (fillGuard && guarded) {
            const bool on = ranged(guarded->scheme());
            if (!on || fillGuard->hasValidDraft())
                fillGuard->refreshFromData();
            fillGuard->setEnabled(on);
        }
        if (contourGuard && guardedLine) {
            const bool on = ranged(guardedLine->scheme());
            if (!on || contourGuard->hasValidDraft())
                contourGuard->refreshFromData();
            contourGuard->setEnabled(on);
        }
        if (bandGuard && guarded) {
            const bool on = ranged(guarded->bandScheme());
            if (!on || bandGuard->hasValidDraft())
                bandGuard->refreshFromData();
            bandGuard->setEnabled(on);
        }
    };
    buildScalarDataTab(row, tabs, refreshEditors);
}

void Swmm2DResultsStylePanel::buildScalarDataTab(OpenSWMM::Render::ResultScalarSublayer *row,
                                                 QTabWidget *tabs,
                                                 std::function<void()> refreshEditors)
{
    using namespace OpenSWMM::Render;
    using namespace openswmmvis::io;
    QPointer<SWMM2DResultsLayer> layer = m_layer;
    QPointer<ResultScalarSublayer> sub = row;
    const bool depthRow = row == m_layer->depthResultSublayer();
    QStringList warnings;
    (void)m_layer->resultVariables(&warnings);
    auto *page = new QWidget(tabs);
    auto *form = new QFormLayout(page);
    form->setFormAlignment(Qt::AlignTop);
    auto *status = new QLabel(page);
    status->setObjectName("additionalResultStatus");
    status->setTextFormat(Qt::PlainText);
    status->setWordWrap(true);
    status->setAccessibleName(tr("Result availability"));
    form->addRow(status);
    auto *table = new QTableWidget(0, 4, page);
    openswmmvis::ui::TableClipboard::install(table, {.copyOnly = true});
    table->setObjectName("additionalResultInspection");
    table->setMaximumHeight(90);
    table->setAccessibleName(tr("Selected cell result"));
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setHorizontalHeaderLabels({tr("Cell"), tr("Status"), tr("Raw value"), tr("Units")});
    table->setToolTip(tr(
        "Select a mesh cell on the map to inspect this variable. Missing and waterless values are not physical zeros."));
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    form->addRow(table);
    auto *exportButton = new QPushButton(tr("Export current variable to CSV…"), page);
    exportButton->setObjectName("additionalResultExport");
    exportButton->setToolTip(tr(
        "Exports all cells for this variable at the current report frame, with native units and availability status."));
    form->addRow(exportButton);
    auto *gisButton = new QPushButton(tr("Export current variable to GIS…"), page);
    gisButton->setObjectName("additionalResultGisExport");
    gisButton->setToolTip(tr(
        "Captures this frame for asynchronous GeoPackage or GeoTIFF export with native units and explicit availability."));
    form->addRow(gisButton);
    auto *scroll = wrapScroll(page, tabs);
    scroll->setObjectName("resultDataTab");
    tabs->addTab(scroll, tr("Data"));
    connect(gisButton, &QPushButton::clicked, page, [layer, sub, status, page] {
        if (!layer || !sub || !layer->source())
            return;
        QString error;
        auto snapshot = openswmmvis::io::captureMesh2DVariableGis(
            *layer->source(), sub->variableKey(), layer->currentTimeIndex(),
            layer->depthToMeshUnits(), layer->srs() ? layer->srs()->toWkt() : QString(), &error);
        if (!snapshot) {
            status->setText(error);
            return;
        }
        auto *dialog = new ResultVariableExportDialog(std::move(snapshot), page);
        dialog->show();
    });
    /**
     * @brief Refresh availability diagnostics, the row's classification editors (through
     * `refreshEditors`) and selected native-cell inspection. Takes no arguments and returns no
     * value; ignores destroyed layer/sublayer. Displays the lowest selected mapped cell with
     * one-based numbering, and enables CSV/GIS only for available known nonempty units. Captured
     * widgets belong to the active page; the closure is cleared before page teardown.
     */
    m_refreshResultData = [layer, sub, refreshEditors, status, table, exportButton, gisButton,
                           warnings, depthRow] {
        if (!layer || !sub)
            return;
        const auto frame = layer->resultFrame(sub->variableKey(), layer->currentTimeIndex());
        const bool available = frame && frame->error.isEmpty();
        // Keep the editors and any invalid visible draft intact during animation
        // and selection changes.
        refreshEditors();
        table->setRowCount(0);
        QStringList messages = warnings;
        if (!available)
            messages << (frame ? frame->error : tr("Result data unavailable."));
        else {
            const auto &v = frame->descriptor;
            messages
                << (v.unitsKnown
                        ? tr("Native units: %1.").arg(v.units)
                        : tr("Units are unknown. Raw inspection is available; scientific export is disabled."));
            switch (v.temporal) {
            case Mesh2DResultVariable::Temporal::Held:
                messages << tr(
                    "Held result sampled at report times; this is not the solver firing time.");
                break;
            case Mesh2DResultVariable::Temporal::Static:
                messages << tr("Static result; independent of report time.");
                break;
            case Mesh2DResultVariable::Temporal::Envelope:
                messages << tr("Run statistic saved by the engine over its time steps; values "
                               "need not occur simultaneously. Cells the run never wetted are "
                               "waterless.");
                break;
            default:
                messages << tr("Result at the current report frame.");
                break;
            }
        }
        const auto *scalarStyle = sub->fillStyle();
        if (available && (scalarStyle->isolines() || scalarStyle->filledContours() ||
                          (scalarStyle->cellFill() && scalarStyle->smoothFill())))
            messages << tr("Contours and the smooth fill reconstruct nodal display values from "
                           "valid cell values; they are not native cell measurements.");
        if (available && scalarStyle->cellLabels())
            messages << tr(
                "Labels display native valid cell values; screen collisions reduce visible labels.");
        // A whole-run classification draws nothing until the run has a history.
        if (available && !depthRow && scalarStyle->needsWholeRunRange()) {
            const auto whole =
                layer->resultFrame(sub->variableKey(), layer->currentTimeIndex(), true);
            if (whole && !whole->error.isEmpty())
                messages << whole->error;
        }
        // The depth row's lines come from its isoline pass, not its own levels.
        if (available && !depthRow && scalarStyle->isolines()) {
            const auto levels =
                scalarStyle->contourLevels(frame->minimum, frame->maximum, frame->samples);
            const auto geometry = layer->resultScalarGeometry(frame, levels, false);
            if (!geometry->error.isEmpty())
                messages << geometry->error;
        }
        status->setText(messages.join('\n'));
        QSet<int> selectedCells;
        const auto &triCell = layer->triCellMap();
        for (int triangle : layer->highlightedCells())
            if (triangle >= 0 && size_t(triangle) < triCell.size())
                selectedCells.insert(triCell[size_t(triangle)]);
        if (available && !selectedCells.isEmpty()) {
            const int cell = *std::min_element(selectedCells.begin(), selectedCells.end());
            if (cell >= 0 && cell < int(frame->values.size()) && cell < int(frame->status.size())) {
                QString state;
                switch (frame->status[size_t(cell)]) {
                case Mesh2DValueStatus::Valid:
                    state = tr("Valid");
                    break;
                case Mesh2DValueStatus::Missing:
                    state = tr("Missing");
                    break;
                case Mesh2DValueStatus::Waterless:
                    state = tr("Waterless");
                    break;
                case Mesh2DValueStatus::NotApplicable:
                    state = tr("Not applicable");
                    break;
                }
                table->setRowCount(1);
                const double raw = frame->values[size_t(cell)];
                const QStringList cells = {
                    tr("%1 (first of %2 selected)").arg(cell + 1).arg(selectedCells.size()), state,
                    std::isfinite(raw) ? QString::number(raw, 'g', 9) : tr("Unavailable"),
                    frame->descriptor.unitsKnown ? frame->descriptor.units : tr("Unknown")};
                for (int i = 0; i < cells.size(); ++i)
                    table->setItem(0, i, new QTableWidgetItem(cells[i]));
            }
        }

        exportButton->setEnabled(available && frame->descriptor.unitsKnown &&
                                 !frame->descriptor.units.isEmpty());
        gisButton->setEnabled(exportButton->isEnabled());
    };
    m_refreshResultData();
    connect(row->fillStyle(), &SublayerStyle::styleChanged, page,
            [this] { refreshPageData(); });

    connect(exportButton, &QPushButton::clicked, page, [this, layer, sub, status] {
        if (!layer || !sub || !layer->source())
            return;
        const QString key = sub->variableKey();
        const int time = layer->currentTimeIndex();
        const quint64 revision = layer->sourceRevision();
        auto *source = layer->source();
        QPointer<QLabel> safeStatus = status;
        const QString path = QFileDialog::getSaveFileName(this, tr("Export result variable"),
                                                          QString(), tr("CSV files (*.csv)"));
        if (path.isEmpty() || !safeStatus)
            return;
        if (!layer || !sub || layer->source() != source || layer->sourceRevision() != revision ||
            layer->currentTimeIndex() != time || sub->variableKey() != key) {
            safeStatus->setText(tr(
                "The result source or selection changed. Reopen export for the current result."));
            return;
        }
        QString error;
        if (!exportMesh2DVariableCsv(*source, key, time, path, &error))
            safeStatus->setText(error);
        else
            safeStatus->setText(tr("Exported %1").arg(path));
    });
}

void Swmm2DResultsStylePanel::buildVectorTabs(OpenSWMM::Render::ResultVectorSublayer *row,
                                              QTabWidget *tabs)
{
    using namespace OpenSWMM::Render;
    using Style = ResultVectorStyle;
    QPointer<ResultVectorSublayer> sub = row;
    QPointer<SWMM2DResultsLayer> layer = m_layer;
    auto *style = row->vectorStyle();
    QPointer<Style> guarded = style;
    auto domain = openswmmvis::io::Mesh2DResultVariable::Domain::Surface;
    for (const auto &v : m_layer->resultVectors())
        if (v.key() == style->vectorKey())
            domain = v.domain();

    /**
     * @brief Add a spin bound to a style setter. Arguments: owner, form, caption, bounds,
     * displayed decimals, style getter and setter. Returns: the spin.
     */
    const auto number = [style](QWidget *owner, QFormLayout *into, const QString &label, double lo,
                                double hi, int decimals, auto get, auto set) {
        auto *spin = new QDoubleSpinBox(owner);
        spin->setRange(lo, hi);
        spin->setDecimals(decimals);
        spin->setAccessibleName(label);
        bindDouble(spin, style, get, set);
        into->addRow(label, spin);
        return spin;
    };
    // Same-domain scalar variables a size or color can bind to.
    const auto scalarCombo = [this, domain](QWidget *owner, const QString &key) {
        auto *combo = new QComboBox(owner);
        for (const auto &v : m_layer->resultVariables())
            if (v.domain == domain)
                combo->addItem(resultVariableLabel(v), v.key());
        int at = combo->findData(key);
        if (at < 0 && !key.isEmpty()) {
            at = combo->count();
            combo->addItem(tr("Unavailable — %1").arg(key), key);
        }
        // Unbound: show the first variable, the one "Scalar variable" binds.
        combo->setCurrentIndex(std::max(at, 0));
        return combo;
    };
    /**
     * @brief Show `key` in a variable combo without echoing the change back.
     */
    const auto followKey = [](QComboBox *combo, const QString &key) {
        const int at = combo->findData(key);
        if (at >= 0 && at != combo->currentIndex()) {
            const QSignalBlocker block(combo);
            combo->setCurrentIndex(at);
        }
    };

    // ── Arrows ───────────────────────────────────────────────────────────────
    auto *arrows = new QWidget(tabs);
    auto *arrowsLayout = new QVBoxLayout(arrows);
    auto *sizeBox = new QGroupBox(tr("Size"), arrows);
    auto *sizeForm = new QFormLayout(sizeBox);
    arrowsLayout->addWidget(sizeBox);
    auto *sizeMode = new QComboBox(sizeBox);
    sizeMode->setObjectName("additionalResultSizeMode");
    sizeMode->addItem(tr("Vector magnitude (frame range)"), int(Style::SizeMode::Magnitude));
    sizeMode->addItem(tr("Constant"), int(Style::SizeMode::Constant));
    sizeMode->addItem(tr("Scalar variable"), int(Style::SizeMode::Scalar));
    sizeMode->addItem(tr("Vector magnitude, fixed scale"), int(Style::SizeMode::FixedScale));
    sizeForm->addRow(tr("Size by:"), sizeMode);
    auto *sizeKey = scalarCombo(sizeBox, style->sizeKey());
    sizeKey->setObjectName("additionalResultSizeVariable");
    sizeForm->addRow(tr("Size variable:"), sizeKey);
    auto *scale = number(sizeBox, sizeForm, tr("Scale (px per unit)"), 0, 1e9, 3,
                         &Style::scalePxPerUnit, &Style::setScalePxPerUnit);
    scale->setObjectName("additionalResultScale");
    scale->setToolTip(tr("Arrow length per unit of speed, the same in every frame (clamped to "
                         "the shortest and longest arrow)."));
    const auto syncSize = [style, sizeMode, sizeKey, scale, followKey] {
        const auto mode = style->sizeMode();
        if (const int at = sizeMode->findData(int(mode)); at != sizeMode->currentIndex()) {
            const QSignalBlocker block(sizeMode);
            sizeMode->setCurrentIndex(at);
        }
        followKey(sizeKey, style->sizeKey());
        sizeKey->setEnabled(mode == Style::SizeMode::Scalar);
        scale->setEnabled(mode == Style::SizeMode::FixedScale);
    };
    syncSize();
    connect(style, &SublayerStyle::styleChanged, sizeMode, syncSize);
    connect(sizeMode, qOverload<int>(&QComboBox::currentIndexChanged), sizeMode,
            [guarded, sizeMode, sizeKey](int i) {
                if (!guarded || i < 0)
                    return;
                const auto mode = Style::SizeMode(sizeMode->itemData(i).toInt());
                if (mode == Style::SizeMode::Scalar && guarded->sizeKey().isEmpty())
                    guarded->setSizeKey(sizeKey->currentData().toString());
                guarded->setSizeMode(mode);
            });
    connect(sizeKey, qOverload<int>(&QComboBox::currentIndexChanged), sizeKey,
            [guarded, sizeKey](int i) {
                if (guarded && i >= 0)
                    guarded->setSizeKey(sizeKey->itemData(i).toString());
            });
    // Item order follows Style::LengthScaling.
    auto *scaling = new QComboBox(sizeBox);
    scaling->addItem(tr("Linear"), int(Style::LengthScaling::Linear));
    scaling->addItem(tr("Square root"), int(Style::LengthScaling::SquareRoot));
    scaling->addItem(tr("Logarithmic"), int(Style::LengthScaling::Log));
    bindCombo(scaling, style, &Style::lengthScaling, &Style::setLengthScaling);
    scaling->setObjectName("additionalResultSizeScaling");
    sizeForm->addRow(tr("Scaling:"), scaling);
    auto *shortest = number(sizeBox, sizeForm, tr("Shortest arrow (px)"), 0, 1000, 1,
                            &Style::minLengthPx, &Style::setMinLengthPx);
    shortest->setObjectName("additionalResultShortestArrow");
    number(sizeBox, sizeForm, tr("Longest arrow (px)"), 1, 1000, 1, &Style::maxLengthPx,
           &Style::setMaxLengthPx);
    auto *customRange = bindCheck(new QCheckBox(tr("Fixed size range"), sizeBox), style,
                                  &Style::sizeUseCustomRange, &Style::setSizeUseCustomRange);
    customRange->setObjectName("additionalResultSizeCustomRange");
    sizeForm->addRow(customRange);
    auto *rangeMin = number(sizeBox, sizeForm, tr("Range minimum"), -1e12, 1e12, 6,
                            &Style::sizeRangeMin, &Style::setSizeRangeMin);
    rangeMin->setObjectName("additionalResultSizeRangeMin");
    auto *rangeMax = number(sizeBox, sizeForm, tr("Range maximum"), -1e12, 1e12, 6,
                            &Style::sizeRangeMax, &Style::setSizeRangeMax);
    rangeMax->setObjectName("additionalResultSizeRangeMax");
    // Gray what the size mode ignores (ResultVectorStyle::lengthPxFor): a constant arrow is
    // the longest; a fixed scale maps speed directly; only a ranged mode reads the range.
    const auto syncSizeUse = [style, scaling, shortest, customRange, rangeMin, rangeMax] {
        const auto mode = style->sizeMode();
        const bool ranged = mode == Style::SizeMode::Magnitude || mode == Style::SizeMode::Scalar;
        const auto use = [](QWidget *w, bool used) {
            w->setEnabled(used);
            w->setToolTip(used ? QString() : tr("Not used by the current \"Size by\" mode."));
        };
        use(scaling, mode != Style::SizeMode::Constant);
        use(shortest, mode != Style::SizeMode::Constant);
        use(customRange, ranged);
        use(rangeMin, ranged && style->sizeUseCustomRange());
        use(rangeMax, ranged && style->sizeUseCustomRange());
        if (ranged && !style->sizeUseCustomRange()) {
            const QString off = tr("Used when \"Fixed size range\" is checked.");
            rangeMin->setToolTip(off);
            rangeMax->setToolTip(off);
        }
    };
    syncSizeUse();
    connect(style, &SublayerStyle::styleChanged, sizeBox, syncSizeUse);
    auto *shapeBox = new QGroupBox(tr("Shape"), arrows);
    auto *shapeForm = new QFormLayout(shapeBox);
    arrowsLayout->addWidget(shapeBox);
    number(shapeBox, shapeForm, tr("Spacing (px)"), 1, 1000, 0, &Style::spacingPx,
           &Style::setSpacingPx);
    number(shapeBox, shapeForm, tr("Head size (px)"), 0, 200, 1, &Style::headSizePx,
           &Style::setHeadSizePx);
    number(shapeBox, shapeForm, tr("Shaft width (px)"), .1, 50, 1, &Style::shaftWidthPx,
           &Style::setShaftWidthPx);
    arrowsLayout->addStretch();
    addDisplayTab(tabs, arrows, "resultArrowsTab", tr("Arrows"), tr("Show arrows"),
                  "additionalResultArrows", style, &Style::arrows, &Style::setArrows, row);

    // ── Streaks: the same animated flow streaks and settings for every vector ─────────────
    auto *streaks = new QWidget(tabs);
    auto *streaksLayout = new QVBoxLayout(streaks);
    streaksLayout->addWidget(buildFlowStreakTab(streaks, row));
    addDisplayTab(tabs, streaks, "resultStreaksTab", tr("Streaks"), tr("Show streaks"),
                  "additionalResultStreaks", style, &Style::streaks, &Style::setStreaks, row);

    // ── Color of the arrows (streaks are colored on their own tab) ─────────────────────────
    auto *colour = new QWidget(tabs);
    auto *colourLayout = new QVBoxLayout(colour);
    {
        auto *note = new QLabel(tr("Arrows only; streaks are colored on the Streaks tab."), colour);
        note->setWordWrap(true);
        colourLayout->addWidget(note);
    }
    auto *colorForm = new QFormLayout;
    colourLayout->addLayout(colorForm);
    auto *colorMode = new QComboBox(colour);
    colorMode->setObjectName("additionalResultColorMode");
    colorMode->addItem(tr("Constant"), int(Style::ColorMode::Constant));
    colorMode->addItem(tr("Vector magnitude"), int(Style::ColorMode::Magnitude));
    colorMode->addItem(tr("Scalar variable"), int(Style::ColorMode::Scalar));
    colorForm->addRow(tr("Color by:"), colorMode);
    auto *colorKey = scalarCombo(colour, style->colorKey());
    colorKey->setObjectName("additionalResultColorVariable");
    colorForm->addRow(tr("Color variable:"), colorKey);
    const auto syncColor = [style, colorMode, colorKey, followKey] {
        const auto mode = style->colorMode();
        if (const int at = colorMode->findData(int(mode)); at != colorMode->currentIndex()) {
            const QSignalBlocker block(colorMode);
            colorMode->setCurrentIndex(at);
        }
        followKey(colorKey, style->colorKey());
        colorKey->setEnabled(mode == Style::ColorMode::Scalar);
    };
    syncColor();
    connect(style, &SublayerStyle::styleChanged, colorMode, syncColor);
    connect(colorMode, qOverload<int>(&QComboBox::currentIndexChanged), colorMode,
            [guarded, colorMode, colorKey](int i) {
                if (!guarded || i < 0)
                    return;
                const auto mode = Style::ColorMode(colorMode->itemData(i).toInt());
                if (mode == Style::ColorMode::Scalar && guarded->colorKey().isEmpty())
                    guarded->setColorKey(colorKey->currentData().toString());
                guarded->setColorMode(mode);
            });
    connect(colorKey, qOverload<int>(&QComboBox::currentIndexChanged), colorKey,
            [guarded, colorKey](int i) {
                if (guarded && i >= 0)
                    guarded->setColorKey(colorKey->itemData(i).toString());
            });
    auto *constant = bindColor(new ColorButton(colour), style, &Style::color, &Style::setColor);
    constant->setObjectName("additionalResultConstantColor");
    constant->setAccessibleName(tr("Constant color"));
    colorForm->addRow(tr("Constant color:"), constant);
    // ResultVectorSublayer::colorFor reads the constant color only in Constant mode.
    // (The classification editor's enabled state is set in m_refreshResultData alone.)
    const auto syncConstant = [style, constant] {
        const bool used = style->colorMode() == Style::ColorMode::Constant;
        constant->setEnabled(used);
        constant->setToolTip(used ? QString()
                                  : tr("Used when \"Color by\" is Constant; other modes take "
                                       "colors from the classification below."));
    };
    syncConstant();
    connect(style, &SublayerStyle::styleChanged, constant, syncConstant);
    auto colorRange = [layer, sub] {
        double lo = std::numeric_limits<double>::quiet_NaN(), hi = lo;
        if (layer && sub)
            (void)layer->resultVectorGeometry(sub, QRectF(), 1.0, &lo, &hi, nullptr,
                                              layer->displayedVectorTime(sub));
        return qMakePair(lo, hi);
    };
    auto *binding = new SublayerSchemeBinding(
        [guarded] { return guarded ? guarded->colorScheme() : ClassificationScheme(); },
        [guarded](const ClassificationScheme &scheme) {
            if (guarded)
                guarded->setColorScheme(scheme);
        },
        [] { return QVector<double>{}; }, colorRange, true, false);
    auto *classification = new ClassificationEditor(binding, true, colour);
    classification->setObjectName("additionalResultVectorClassification");
    colourLayout->addWidget(classification);
    colourLayout->addStretch();
    auto *colourScroll = wrapScroll(colour, tabs);
    colourScroll->setObjectName("resultColourTab");
    tabs->addTab(colourScroll, tr("Color"));

    // ── Data ─────────────────────────────────────────────────────────────────
    auto *data = new QWidget(tabs);
    auto *dataLayout = new QVBoxLayout(data);
    auto *status = new QLabel(data);
    status->setObjectName("additionalResultStatus");
    status->setTextFormat(Qt::PlainText);
    status->setWordWrap(true);
    dataLayout->addWidget(status);
    dataLayout->addStretch();
    auto *dataScroll = wrapScroll(data, tabs);
    dataScroll->setObjectName("resultDataTab");
    tabs->addTab(dataScroll, tr("Data"));
    m_refreshResultData = [layer, sub, status, classification] {
        if (!layer || !sub)
            return;
        double lo = std::numeric_limits<double>::quiet_NaN(), hi = lo;
        const auto geometry = layer->resultVectorGeometry(sub, QRectF(), 1.0, &lo, &hi, nullptr,
                                                          layer->displayedVectorTime(sub));
        status->setText(geometry.error.isEmpty()
                            ? tr("Arrows sample cell values on a screen grid; streaks follow the "
                                 "field as moving particles.")
                            : geometry.error);
        // As on the scalar page: without a color range (constant color,
        // nothing moving) an enabled editor would hold an invalid draft and
        // block OK/Apply. Disabled, it drops its draft and shows no bounds.
        const bool ranged = std::isfinite(lo) && std::isfinite(hi);
        if (!ranged || classification->hasValidDraft())
            classification->refreshFromData();
        classification->setEnabled(ranged);
    };
    m_refreshResultData();
    // Style and visibility changes both change the color range.
    connect(row, &ISublayer::invalidated, data, [this] { refreshPageData(); });
}

// ─── Water visibility ────────────────────────────────────────────────────────

QWidget *Swmm2DResultsStylePanel::buildWaterPage(QWidget *parent)
{
    auto *water = new QGroupBox(tr("Water visibility"), parent);
    auto *waterForm = new QFormLayout(water);
    auto *modelDepth = new QCheckBox(tr("Use model dry depth"), water);
    modelDepth->setObjectName(QStringLiteral("useModelThinFilmDepth"));
    auto *filmDepth = new QDoubleSpinBox(water);
    filmDepth->setObjectName(QStringLiteral("thinFilmDepth"));
    filmDepth->setDecimals(6);
    filmDepth->setRange(0.0, 1000.0);
    filmDepth->setSingleStep(0.001);
    filmDepth->setSuffix(tr(" m"));
    auto *showFilms = new QCheckBox(tr("Show thin films"), water);
    showFilms->setObjectName(QStringLiteral("showThinFilms"));
    auto *hint =
        new QLabel(tr("Hide water in cells that are shallow everywhere. "
                      "Partially wet cells retain their shoreline. Applies to maps and profiles; "
                      "simulation results are unchanged."),
                   water);
    hint->setWordWrap(true);
    waterForm->addRow(modelDepth);
    waterForm->addRow(tr("Thin-film depth:"), filmDepth);
    waterForm->addRow(showFilms);
    waterForm->addRow(hint);
    /**
     * @brief Read water display policy from a surviving layer under three signal blockers. Takes no
     * arguments and returns no value; updates model-depth toggle, depth spin/enabled state and
     * thin-film toggle without writes. Null layer is ignored.
     */
    const auto syncWater = [this, modelDepth, filmDepth, showFilms] {
        if (!m_layer)
            return;
        const QSignalBlocker a(modelDepth), b(filmDepth), c(showFilms);
        modelDepth->setChecked(m_layer->usesModelThinFilmDepth());
        filmDepth->setValue(m_layer->thinFilmDepth());
        filmDepth->setEnabled(!modelDepth->isChecked());
        showFilms->setChecked(m_layer->showThinFilms());
    };
    syncWater();
    connect(modelDepth, &QCheckBox::toggled, water, [this, filmDepth](bool on) {
        if (m_layer)
            m_layer->setThinFilmDepth(on ? -1.0 : filmDepth->value());
    });
    connect(filmDepth, qOverload<double>(&QDoubleSpinBox::valueChanged), water,
            [this](double depth) {
                if (m_layer)
                    m_layer->setThinFilmDepth(depth);
            });
    connect(showFilms, &QCheckBox::toggled, water, [this](bool show) {
        if (m_layer)
            m_layer->setShowThinFilms(show);
    });
    // The page, not the panel, is the context: the page is deleted when
    // another item is selected, while the layer lives on.
    connect(m_layer, &SWMM2DResultsLayer::waterDisplayPolicyChanged, water, syncWater);
    return water;
}

// ─── Scalar depth fills (cell / smooth) ─────────────────────────────────────

QWidget *Swmm2DResultsStylePanel::buildFlowStreakTab(QWidget *parent,
                                                     OpenSWMM::Render::ResultVectorSublayer *row)
{
    auto *page = new QWidget(parent);
    page->setObjectName("flowStreakTab");
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, 0, 0, 0);
    // A vector row's own streak settings (the same controls for every vector); without a row,
    // the native streak pass listed on its own (a run with edge fluxes but no cell velocity).
    OpenSWMM::Render::ISublayer *sub =
        row ? static_cast<OpenSWMM::Render::ISublayer *>(row) : m_layer->flowStreakSublayer();
    auto *st = row ? row->vectorStyle()->streakStyle() : m_layer->flowStreakSublayer()->streakStyle();
    if (!row)
        lay->addWidget(passHeader(page, sub, tr("Show flow streaks")));
    auto *help = new QLabel(tr("Moving streaks follow the field's direction, with slower "
                               "movement boosted for visibility. Colors by speed use the "
                               "field's magnitude."),
                            page);
    help->setWordWrap(true);
    lay->addWidget(help);
    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    lay->addLayout(form);
    auto *colourRow = new QWidget(page);
    auto *colourLayout = new QHBoxLayout(colourRow);
    colourLayout->setContentsMargins(0, 0, 0, 0);
    auto *colour = new ColorButton(colourRow);
    colour->setShowAlpha(true);
    colour->setColor(st->color());
    colour->setObjectName("flowStreakColor");
    auto *bySpeed = new QCheckBox(tr("By speed"), colourRow);
    bySpeed->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    bySpeed->setObjectName("colorByMagnitude");
    bySpeed->setChecked(st->colorByMagnitude());
    colourLayout->addWidget(colour);
    colourLayout->addWidget(bySpeed);
    colourLayout->addStretch();
    form->addRow(tr("Color:"), colourRow);
    connect(colour, &ColorButton::colorChanged, st, &OpenSWMM::Render::FlowStreakStyle::setColor);
    connect(bySpeed, &QCheckBox::toggled, st,
            &OpenSWMM::Render::FlowStreakStyle::setColorByMagnitude);
    connect(st, &OpenSWMM::Render::SublayerStyle::styleChanged, colour, [st, colour, bySpeed] {
        QSignalBlocker a(colour), b(bySpeed);
        colour->setColor(st->color());
        bySpeed->setChecked(st->colorByMagnitude());
        colour->setEnabled(!st->colorByMagnitude());
    });
    colour->setEnabled(!st->colorByMagnitude());
    /**
     * @brief Add a slider row for a streak setting, with its value beside it; the exact value is
     * also under Advanced. Argument `name`: object name of the slider (its value label is
     * name + "Value"). Arguments `lo`, `hi`: slider range in steps. Arguments `toValue`,
     * `toSteps`: map slider steps to the style property and back. Argument `text`: value caption.
     */
    const auto slider = [&](const char *name, const QString &label, int lo, int hi,
                            const char *property, std::function<double(int)> toValue,
                            std::function<int(double)> toSteps,
                            std::function<QString(double)> text) {
        auto *rowWidget = new QWidget(page);
        auto *rowLayout = new QHBoxLayout(rowWidget);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        auto *control = new QSlider(Qt::Horizontal, rowWidget);
        control->setObjectName(name);
        control->setAccessibleName(QString(label).remove(QLatin1Char(':')));
        control->setRange(lo, hi);
        auto *value = new QLabel(rowWidget);
        value->setObjectName(QString::fromLatin1(name) + QStringLiteral("Value"));
        value->setMinimumWidth(value->fontMetrics().horizontalAdvance(QStringLiteral("Very dense ")));
        rowLayout->addWidget(control, 1);
        rowLayout->addWidget(value);
        form->addRow(label, rowWidget);
        connect(control, &QSlider::valueChanged, st,
                [st, property, toValue](int steps) { st->setProperty(property, toValue(steps)); });
        const auto refresh = [st, property, control, value, toSteps, text, lo, hi] {
            const double current = st->property(property).toDouble();
            QSignalBlocker block(control);
            control->setValue(std::clamp(toSteps(current), lo, hi));
            value->setText(text(current));
        };
        connect(st, &OpenSWMM::Render::SublayerStyle::styleChanged, control, refresh);
        refresh();
    };
    // Density: streaks are seeded one per spacing; the slider runs sparse (80 px) to dense (8 px).
    slider(
        "flowDensity", tr("Density:"), 0, 72, "spacingPx", [](int steps) { return 80.0 - steps; },
        [](double spacing) { return int(std::lround(80.0 - spacing)); },
        [](double spacing) { return tr("%1 px apart").arg(spacing, 0, 'g', 3); });
    auto *motionRow = new QWidget(page);
    auto *motionLayout = new QHBoxLayout(motionRow);
    motionLayout->setContentsMargins(0, 0, 0, 0);
    auto *motion = new QSlider(Qt::Horizontal, motionRow);
    motion->setObjectName("flowMotion");
    motion->setRange(25, 400);
    motion->setTickPosition(QSlider::TicksBelow);
    motion->setTickInterval(100);
    auto *motionValue = new QLabel(motionRow);
    motionValue->setObjectName("flowMotionValue");
    motionLayout->addWidget(motion, 1);
    motionLayout->addWidget(motionValue);
    form->addRow(tr("Motion:"), motionRow);
    connect(motion, &QSlider::valueChanged, st, [st](int v) { st->setSpeedMultiplier(v / 100.); });
    /**
     * @brief Read speed multiplier into the motion slider and caption without write feedback. Takes
     * no arguments and returns no value; blocks slider signals and clamps its display to [.25,4]
     * times reference while retaining the full style value in the label.
     */
    auto refreshMotion = [st, motion, motionValue] {
        QSignalBlocker block(motion);
        motion->setValue(int(std::clamp(st->speedMultiplier() * 100, 25., 400.)));
        motionValue->setText(std::abs(st->speedMultiplier() - 1) < 1e-6
                                 ? tr("Normal")
                                 : tr("%1×").arg(st->speedMultiplier(), 0, 'g', 3));
    };
    connect(st, &OpenSWMM::Render::SublayerStyle::styleChanged, motion, refreshMotion);
    refreshMotion();
    // Trail: how long a streak's tail lasts, 0.25 s to 5 s.
    slider(
        "flowTrail", tr("Trail:"), 1, 20, "trailDurationSeconds",
        [](int steps) { return steps * 0.25; },
        [](double seconds) { return int(std::lround(seconds / 0.25)); },
        [](double seconds) { return tr("%1 s").arg(seconds, 0, 'g', 3); });
    auto *mapping = new QLabel(page);
    mapping->setObjectName("flowMotionMapping");
    mapping->setWordWrap(true);
    lay->addWidget(mapping);
    auto *binding = new SublayerSchemeBinding(
        [st] { return st->scheme(); },
        [st](const OpenSWMM::Render::ClassificationScheme &v) { st->setScheme(v); },
        [] { return QVector<double>{}; },
        [st] { return qMakePair(st->scheme().rangeMin(), st->scheme().rangeMax()); }, true, false);
    auto *classification = new ClassificationEditor(binding, true, page);
    classification->setObjectName("flowClassification");
    classification->setVisible(st->colorByMagnitude());
    classification->setEnabled(st->colorByMagnitude());
    lay->addWidget(classification);
    // A hidden editor is also disabled, and drops its draft: an invalid edit
    // the user can no longer see never blocks OK.
    connect(
        st, &OpenSWMM::Render::SublayerStyle::styleChanged, classification,
        [style = QPointer<OpenSWMM::Render::FlowStreakStyle>(st), classification,
         last = st->scheme().toJson()]() mutable {
            if (!style)
                return;
            const bool on = style->colorByMagnitude();
            classification->setVisible(on);
            classification->setEnabled(on);
            const auto current = style->scheme().toJson();
            if (current != last || !on) {
                last = current;
                classification->refresh();
            }
        },
        Qt::QueuedConnection);
    auto *advancedToggle = new QToolButton(page);
    advancedToggle->setObjectName("flowAdvancedToggle");
    advancedToggle->setText(tr("Advanced"));
    advancedToggle->setCheckable(true);
    advancedToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    advancedToggle->setArrowType(Qt::RightArrow);
    lay->addWidget(advancedToggle, 0, Qt::AlignLeft);
    auto *advanced = new QWidget(page);
    advanced->setObjectName("flowAdvanced");
    advanced->setVisible(false);
    auto *details = new QFormLayout(advanced);
    lay->addWidget(advanced);
    connect(advancedToggle, &QToolButton::toggled, advanced, [advanced, advancedToggle](bool open) {
        advanced->setVisible(open);
        advancedToggle->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
    });
    /**
     * @brief Create a live advanced streak property spin with guarded signal feedback. Argument
     * `key`: required stable Qt property-name pointer. Argument `label`: row caption. Argument
     * `minimum`: lower numeric bound in suffix units; upper bound is maximumNumericValue. Argument
     * `decimals`: display precision. Argument `suffix`: unit text. Returns: no value; advanced page
     * owns the spin, style changes refresh it with signals blocked.
     */
    auto number = [&](const char *key, const QString &label, double minimum, int decimals,
                      const QString &suffix) {
        auto *box =
            makeDSpin(advanced, minimum, OpenSWMM::Render::FlowStreakStyle::maximumNumericValue(),
                      .5, decimals, st->property(key).toDouble(), suffix);
        box->setObjectName(key);
        box->setKeyboardTracking(false);
        box->setStepType(QAbstractSpinBox::AdaptiveDecimalStepType);
        box->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        box->setMaximumWidth(220);
        details->addRow(label, box);
        connect(box, qOverload<double>(&QDoubleSpinBox::valueChanged), st,
                [st, key](double value) { st->setProperty(key, value); });
        connect(st, &OpenSWMM::Render::SublayerStyle::styleChanged, box, [st, key, box] {
            QSignalBlocker block(box);
            box->setValue(st->property(key).toDouble());
        });
    };
    number("spacingPx", tr("Exact spacing:"), 2, 1, tr(" px"));
    auto *exactMotion =
        makeDSpin(advanced, 0, 48 * OpenSWMM::Render::FlowStreakStyle::maximumNumericValue(), 1, 2,
                  48 * st->speedMultiplier(), tr(" px/s"));
    exactMotion->setObjectName("motionPixelsPerSecond");
    exactMotion->setKeyboardTracking(false);
    exactMotion->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    exactMotion->setMaximumWidth(220);
    details->addRow(tr("Reference motion:"), exactMotion);
    connect(exactMotion, qOverload<double>(&QDoubleSpinBox::valueChanged), st,
            [st](double value) { st->setSpeedMultiplier(value / 48); });
    connect(st, &OpenSWMM::Render::SublayerStyle::styleChanged, exactMotion, [st, exactMotion] {
        QSignalBlocker block(exactMotion);
        exactMotion->setValue(48 * st->speedMultiplier());
    });
    number("trailDurationSeconds", tr("Exact trail duration:"), .1, 2, tr(" s"));
    number("minimumLifetimeSeconds", tr("Minimum lifetime:"), .1, 1, tr(" s"));
    number("maximumLifetimeSeconds", tr("Maximum lifetime:"), .1, 1, tr(" s"));
    number("widthPx", tr("Trail width:"), .1, 1, tr(" px"));
    number("headWidthPx", tr("Head width:"), .1, 1, tr(" px"));
    number("lifetimeFadeSeconds", tr("Fade duration:"), 0, 1, tr(" s"));
    auto *status = new QLabel(advanced);
    status->setObjectName("flowDiagnostics");
    status->setWordWrap(true);
    details->addRow(status);
    /**
     * @brief Refresh streak motion-mapping/availability and diagnostics labels from a surviving
     * layer. Takes no arguments and returns no value; availability overrides reference-speed
     * wording. Uses captured QPointer for the layer and page-owned labels; requires its existing
     * streak style while alive.
     */
    auto updateStatus = [layer = m_layer, mapping, status,
                         vector = QPointer<OpenSWMM::Render::ResultVectorSublayer>(row),
                         bound = row != nullptr] {
        if (!layer || (bound && !vector))
            return;
        const auto d = layer->flowStreakController(vector)->diagnostics();
        const auto *style = layer->flowStreakStyle(vector);
        QString text =
            style->motionWasMigrated() ? tr("Old motion setting reset to Normal. ") : QString();
        text += d.referenceSpeedSI > 0
                    ? tr("Reference: %1 px/s at %2 m/s. Slower flow is boosted.")
                          .arg(d.referencePixelsPerSecond, 0, 'f', 0)
                          .arg(d.referenceSpeedSI, 0, 'g', 3)
                    : tr("Normal motion uses a 48 px/s reference. Slower flow is boosted.");
        const auto availability = layer->flowStreakAvailability(vector);
        if (!availability.isEmpty())
            text = availability;
        mapping->setText(text);
        status->setText(layer->flowStreakStatus(vector));
    };
    connect(m_layer, &SWMM2DResultsLayer::flowStreaksChanged, status, updateStatus);
    connect(sub, &OpenSWMM::Render::ISublayer::invalidated, status, updateStatus);
    connect(m_layer, &SWMM2DResultsLayer::repaintRequested, status, updateStatus);
    updateStatus();
    lay->addStretch();
    return page;
}

// ─── Mesh edges (Issue 6) ────────────────────────────────────────────────────

QWidget *Swmm2DResultsStylePanel::buildMeshEdgeTab(QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *lay = new QVBoxLayout(page);

    auto *sub = m_layer->meshEdgeSublayer();
    MeshEdgeStyle *st = sub ? sub->edgeStyle() : nullptr;

    lay->addWidget(passHeader(page, sub, tr("Show mesh edges")));

    if (st) {
        auto *symBox = new QGroupBox(tr("Symbology"), page);
        auto *symForm = new QFormLayout(symBox);

        auto *color =
            bindColor(new ColorButton(symBox), st, &MeshEdgeStyle::color, &MeshEdgeStyle::setColor);
        color->setToolTip(tr("Edge color. The wireframe is now drawn once per "
                             "unique edge, so a translucent color no longer "
                             "darkens where cells meet."));
        symForm->addRow(tr("Color:"), color);

        auto *width = bindDouble(makeDSpin(symBox, 0.1, 10.0, 0.05, 2, st->lineWidthPx(), tr(" px")),
                                 st, &MeshEdgeStyle::lineWidthPx, &MeshEdgeStyle::setLineWidthPx);
        symForm->addRow(tr("Width:"), width);

        auto *dash = bindDash(new DashStyleCombo(symBox), st, &MeshEdgeStyle::dashPattern,
                              &MeshEdgeStyle::setDashPattern);
        dash->setMinimumWidth(kComboMinWidthPx);
        symForm->addRow(tr("Stro&ke style:"), dash);

        lay->addWidget(symBox);

        // Slope emphasis, as on the mesh layer: steep edges of the bed draw wider.
        auto *slopeBox = new QGroupBox(tr("Slope emphasis"), page);
        auto *slopeForm = new QFormLayout(slopeBox);
        auto *useSlope = bindCheck(new QCheckBox(tr("Widen steep edges"), slopeBox), st,
                                   &MeshEdgeStyle::useSlopeDrivenWidth,
                                   &MeshEdgeStyle::setUseSlopeDrivenWidth);
        useSlope->setObjectName("resultEdgeSlopeDriven");
        slopeForm->addRow(QString(), useSlope);
        auto *slopeBreak =
            bindDouble(makeDSpin(slopeBox, 0.0, 1.0, 0.05, 2, st->slopeBreak()), st,
                       &MeshEdgeStyle::slopeBreak, &MeshEdgeStyle::setSlopeBreak);
        slopeBreak->setToolTip(tr("Fraction of the maximum slope above which "
                                  "edges use the wide width/color."));
        slopeForm->addRow(tr("Slope brea&k:"), slopeBreak);
        auto *wideWidth =
            bindDouble(makeDSpin(slopeBox, 0.1, 10.0, 0.05, 2, st->wideWidthPx(), tr(" px")), st,
                       &MeshEdgeStyle::wideWidthPx, &MeshEdgeStyle::setWideWidthPx);
        slopeForm->addRow(tr("Wide width:"), wideWidth);
        auto *wideColor = bindColor(new ColorButton(slopeBox), st, &MeshEdgeStyle::wideColor,
                                    &MeshEdgeStyle::setWideColor);
        slopeForm->addRow(tr("Wide color:"), wideColor);
        // The width and color of steep edges matter only while they are widened.
        const auto syncSlope = [st, slopeBreak, wideWidth, wideColor] {
            const bool on = st->useSlopeDrivenWidth();
            for (QWidget *w : {static_cast<QWidget *>(slopeBreak),
                               static_cast<QWidget *>(wideWidth),
                               static_cast<QWidget *>(wideColor)})
                w->setEnabled(on);
        };
        syncSlope();
        connect(st, &SublayerStyle::styleChanged, slopeBox, syncSlope);
        lay->addWidget(slopeBox);
    }
    lay->addStretch();
    return page;
}

// ─── Mesh vertices (Issue 6) ─────────────────────────────────────────────────

QWidget *Swmm2DResultsStylePanel::buildMeshNodeTab(QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *lay = new QVBoxLayout(page);

    auto *sub = m_layer->meshNodeSublayer();
    MeshNodeStyle *st = sub ? sub->nodeStyle() : nullptr;

    lay->addWidget(passHeader(page, sub, tr("Show mesh vertices")));

    if (st) {
        // Symbology + outline only — the shape and tagged-vertex groups are
        // omitted: the results renderer draws a fixed marker and carries no
        // tagged-vertex data, so those knobs would be no-ops here.
        auto *symBox = new QGroupBox(tr("Symbology"), page);
        auto *symForm = new QFormLayout(symBox);

        auto *color =
            bindColor(new ColorButton(symBox), st, &MeshNodeStyle::color, &MeshNodeStyle::setColor);
        symForm->addRow(tr("Color:"), color);

        auto *size = bindDouble(makeDSpin(symBox, 0.5, 20.0, 0.5, 1, st->markerSizePx(), tr(" px")),
                                st, &MeshNodeStyle::markerSizePx, &MeshNodeStyle::setMarkerSizePx);
        symForm->addRow(tr("Marker size:"), size);

        auto *outlineColor = bindColor(new ColorButton(symBox), st, &MeshNodeStyle::outlineColor,
                                       &MeshNodeStyle::setOutlineColor);
        symForm->addRow(tr("Outline color:"), outlineColor);

        auto *outlineWidth =
            bindDouble(makeDSpin(symBox, 0.0, 5.0, 0.25, 2, st->outlineWidthPx(), tr(" px")), st,
                       &MeshNodeStyle::outlineWidthPx, &MeshNodeStyle::setOutlineWidthPx);
        symForm->addRow(tr("Outline width:"), outlineWidth);

        lay->addWidget(symBox);
    }
    lay->addStretch();
    return page;
}

} // namespace openswmmvis::ui
