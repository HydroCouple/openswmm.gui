/*!
 * \file   annotationstyledialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/annotationstyledialog.h"

#include "layers/annotationtextitem.h"

#include <qpropertymodel.h>
#include <qpropertyitemdelegate.h>

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLineEdit>
#include "layers/annotationfeatures.h"
#include "layers/annotationlayer.h"
#include "layers/gisvectorlayer.h"
#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include <QPushButton>
#include <QSpinBox>
#include <QTreeView>
#include <QVBoxLayout>

namespace {
class AnnotationPropertyDelegate : public QPropertyItemDelegate
{
public:
    using QPropertyItemDelegate::QPropertyItemDelegate;

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override
    {
        auto *editor = QPropertyItemDelegate::createEditor(parent, option, index);
        if (!editor) return nullptr;
        // The property delegate wraps spin boxes in another widget. Focus
        // leaving the child does not reliably commit that wrapper before
        // Apply, so keep the draft in sync with each numeric value change.
        auto *delegate = const_cast<AnnotationPropertyDelegate *>(this);
        if (auto *spin = editor->findChild<QSpinBox *>())
            connect(spin, &QSpinBox::valueChanged, editor,
                    [delegate, editor] { emit delegate->commitData(editor); });
        if (auto *spin = editor->findChild<QDoubleSpinBox *>())
            connect(spin, &QDoubleSpinBox::valueChanged, editor,
                    [delegate, editor] { emit delegate->commitData(editor); });
        return editor;
    }
};
}

AnnotationStyleDialog::AnnotationStyleDialog(AnnotationTextItem *item,
                                             QWidget *parent, MapCanvas *canvas)
    : QDialog(parent)
    , m_target(item)
    , m_canvas(canvas)
{
    m_item = new AnnotationTextItem(this);
    if (item) m_item->fromJson(item->toJson());
    setWindowTitle(tr("Add Text Annotation"));
    // Iteration 2 (D3) — naming wires the app-wide layout persistence.
    setObjectName(QStringLiteral("AnnotationStyleDialog"));
    resize(560, 680);
    buildUi();
}

AnnotationStyleDialog::~AnnotationStyleDialog() = default;

void AnnotationStyleDialog::setEditMode(bool edit)
{
    setWindowTitle(edit ? tr("Edit Text Annotation")
                        : tr("Add Text Annotation"));
    if (auto *ok = m_btns ? m_btns->button(QDialogButtonBox::Ok) : nullptr)
        ok->setText(edit ? tr("Apply") : tr("Add"));
}

void AnnotationStyleDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);

    // Quick text field — the single most-edited attribute lives outside the
    // tree so the user can type the label without expanding any group.
    auto *textRow = new QHBoxLayout;
    textRow->addWidget(new QLabel(tr("Text:"), this));
    m_text = new QPlainTextEdit(m_item ? m_item->text() : QString(), this);
    m_text->setObjectName(QStringLiteral("annotationText"));
    m_text->setMaximumHeight(85);
    m_text->setPlaceholderText(tr("Annotation text"));
    textRow->addWidget(m_text, 1);
    root->addLayout(textRow);

    auto *form = new QFormLayout;
    auto *halo = new QCheckBox(tr("Draw halo"), this);
    halo->setObjectName(QStringLiteral("annotationHalo"));
    halo->setChecked(m_item->haloEnabled());
    connect(halo, &QCheckBox::toggled, m_item, &AnnotationTextItem::setHaloEnabled);
    connect(m_item, &AnnotationTextItem::haloEnabledChanged, halo, &QCheckBox::setChecked);
    form->addRow(halo);
    auto *pinned = new QCheckBox(tr("Keep this position during automatic layout"), this);
    pinned->setChecked(m_item->pinned());
    connect(pinned, &QCheckBox::toggled, m_item, &AnnotationTextItem::setPinned);
    form->addRow(pinned);

    auto *source = new QComboBox(this);
    source->setObjectName(QStringLiteral("annotationSource"));
    source->addItem(tr("Free text"));
    if (m_canvas) for (auto *layer : m_canvas->layers())
        if (qobject_cast<SWMMModelLayer *>(layer) || qobject_cast<GISVectorLayer *>(layer))
            source->addItem(layer->name(), QVariant::fromValue(layer));
    auto *feature = new QComboBox(this);
    feature->setObjectName(QStringLiteral("annotationFeature"));
    feature->setEditable(true);
    feature->setInsertPolicy(QComboBox::NoInsert);
    auto *expression = new QLineEdit(m_item->expression(), this);
    expression->setObjectName(QStringLiteral("annotationExpression"));
    expression->setPlaceholderText(tr("Leave blank for custom text; e.g. {name}: {invertElev}"));
    auto *callout = new QCheckBox(tr("Connect callout line to feature"), this);
    callout->setObjectName(QStringLiteral("annotationCallout"));
    callout->setChecked(m_item->calloutEnabled());
    connect(expression, &QLineEdit::textChanged, m_item, &AnnotationTextItem::setExpression);
    connect(callout, &QCheckBox::toggled, m_item, &AnnotationTextItem::setCalloutEnabled);
    connect(feature, &QComboBox::currentIndexChanged, this, [this, feature](int index) {
        if (index >= 0) m_item->setFeatureReference(feature->itemData(index).toJsonObject());
    });
    auto populate = [this, source, feature, expression, callout] {
        QSignalBlocker block(feature);
        feature->clear();
        if (source->currentData(Qt::UserRole+1).toBool()) {
            feature->addItem(tr("Missing feature source"), m_item->featureReference());
            feature->setEnabled(false); callout->setEnabled(false);
            return;
        }
        auto *layer = source->currentData().value<OpenSWMMVisLayer *>();
        const auto oldRef = m_item->featureReference();
        int selected = -1;
        if (layer) for (const auto &f : AnnotationFeatures::enumerate(layer)) {
            QString label = f.name;
            if (f.reference.contains("category")) {
                const QStringList kinds = {tr("Junction"),tr("Outfall"),tr("Storage"),tr("Divider"),tr("Conduit"),tr("Pump"),tr("Orifice"),tr("Weir"),tr("Outlet"),tr("Subcatchment"),tr("Rain gage")};
                label += " (" + kinds.value(f.reference.value("category").toInt()) + ")";
            }
            feature->addItem(label, f.reference);
            if (f.reference.value("name") == oldRef.value("name") &&
                f.reference.value("category") == oldRef.value("category") &&
                f.reference.value("fid") == oldRef.value("fid")) selected = feature->count()-1;
        }
        if (layer && selected < 0 && oldRef.value("layerId").toString() == layer->layerId()) {
            feature->addItem(tr("Missing feature: %1").arg(oldRef.value("name").toString(oldRef.value("fid").toString())), oldRef);
            selected = feature->count()-1;
        }
        if (selected >= 0) feature->setCurrentIndex(selected);
        feature->setEnabled(layer);
        expression->setEnabled(layer);
        callout->setEnabled(layer);
        m_item->setFeatureReference(layer ? feature->currentData().toJsonObject() : QJsonObject());
        if (!layer) { expression->clear(); callout->setChecked(false); }
    };
    if (m_canvas) {
        auto *linkedSource = AnnotationFeatures::source(m_canvas->layers(), m_item->featureReference());
        for (int i = 1; i < source->count(); ++i)
            if (source->itemData(i).value<OpenSWMMVisLayer *>() == linkedSource) source->setCurrentIndex(i);
        if (!linkedSource && !m_item->featureReference().isEmpty()) {
            source->addItem(tr("Missing source: %1").arg(m_item->featureReference().value("layerName").toString()));
            source->setItemData(source->count()-1, true, Qt::UserRole+1);
            source->setCurrentIndex(source->count()-1);
        }
    }
    // Keep unresolved saved references intact until the user explicitly changes the source.
    if (source->currentIndex() > 0 || m_item->featureReference().isEmpty()) populate();
    connect(source, &QComboBox::currentIndexChanged, this, populate);
    form->addRow(tr("Feature layer:"), source);
    form->addRow(tr("Feature:"), feature);
    form->addRow(tr("Live label:"), expression);
    form->addRow(callout);
    root->addLayout(form);

    // Property tree — every style attribute is exposed here. QPropertyModel
    // auto-resolves an editor for QColor (colour picker), QFont (font
    // dialog), bool (checkbox), and numeric types (spin boxes); no
    // per-field delegate wiring is needed.
    m_tree = new QTreeView(this);
    m_tree->setAlternatingRowColors(true);
    m_tree->setEditTriggers(QAbstractItemView::AllEditTriggers);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setRootIsDecorated(true);
    m_tree->setUniformRowHeights(false);
    if (m_item) {
        m_model = new QPropertyModel(m_item, this);
        m_tree->setModel(m_model);
        // QPropertyItemDelegate is what binds QColor → QColorDialog,
        // QFont → QFontDialog, bool → checkbox editor, etc. Without it the
        // tree falls back to plain text editors and the user can't drive a
        // colour picker from the row.
        m_tree->setItemDelegate(new AnnotationPropertyDelegate(this));
        m_tree->header()->setSectionResizeMode(0, QHeaderView::Interactive);
        m_tree->header()->setStretchLastSection(true);
        m_tree->header()->resizeSection(0, 250);
        m_tree->header()->setMinimumSectionSize(90);  // stop columns collapsing on a narrow dialog
        m_tree->expandAll();
    }
    root->addWidget(m_tree, 1);

    // Keep the quick text field in sync with the tree's text row — both
    // ultimately write back to the same Q_PROPERTY, so a typed change in
    // the line edit needs to round-trip into the model, and a change made
    // via the tree needs to refresh the line edit.
    if (m_item) {
        connect(m_text, &QPlainTextEdit::textChanged, this, [this]() {
            const QString t = m_text->toPlainText();
            if (m_item->text() == t) return;
            m_item->setText(t);
            if (m_model) m_model->refreshValues();
        });
        connect(m_item, &AnnotationTextItem::textChanged,
                m_text, [this](const QString &t) {
            if (m_text->toPlainText() == t) return;
            QSignalBlocker block(m_text);
            m_text->setPlainText(t);
        });
    }

    m_btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    if (auto *ok = m_btns->button(QDialogButtonBox::Ok))
        ok->setText(tr("Add"));
    connect(m_btns, &QDialogButtonBox::accepted, this, &AnnotationStyleDialog::accept);
    connect(m_btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(m_btns);
}

void AnnotationStyleDialog::accept()
{
    // Typed spin-box text is parsed on focus loss by default. Apply may run
    // without moving focus (for example the default-button shortcut).
    for (auto *spin : m_tree->findChildren<QAbstractSpinBox *>())
        if (spin->isVisibleTo(m_tree)) spin->interpretText();
    if (m_item->text().trimmed().isEmpty() && m_item->expression().trimmed().isEmpty()) return;
    if (m_target && m_canvas && m_target->featureReference() != m_item->featureReference()) {
        auto *layer = qobject_cast<OpenSWMMVisAnnotationLayer *>(m_target->parent());
        auto position = [this, layer](AnnotationTextItem *item, QPointF &p) {
            if (item->featureReference().isEmpty()) {
                p = {item->x(), item->y()};
                return AnnotationFeatures::transform(p, layer ? layer->srs() : m_canvas->canvasSRS(), m_canvas->canvasSRS());
            }
            auto *source = AnnotationFeatures::source(m_canvas->layers(), item->featureReference());
            AnnotationFeatures::Feature f;
            if (!source || !AnnotationFeatures::resolve(source, item->featureReference(), f)) return false;
            p = f.position;
            return AnnotationFeatures::transform(p, source->srs(), m_canvas->canvasSRS());
        };
        QPointF oldAnchor, newAnchor;
        if (position(m_target, oldAnchor) && position(m_item, newAnchor)) {
            int ox, oy, nx, ny;
            m_canvas->toPixelCoords(oldAnchor.x(), oldAnchor.y(), ox, oy);
            m_canvas->toPixelCoords(newAnchor.x(), newAnchor.y(), nx, ny);
            const QPointF oldOffset = layer && !layer->renderedBounds(m_target->id()).isEmpty()
                ? layer->renderedOffset(m_target->id()) : m_target->offset();
            m_item->setOffset(oldOffset + QPointF(ox-nx, oy-ny));
        }
    }
    if (m_target) m_target->fromJson(m_item->toJson());
    QDialog::accept();
}
