/*!
 * \file textlayersdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/textlayersdialog.h"
#include "ui/dialogs/annotationstyledialog.h"
#include "layers/annotationfeatures.h"
#include "layers/annotationlayer.h"
#include "layers/annotationtextitem.h"
#include "layers/gisvectorlayer.h"
#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "map/textlayercommands.h"
#include "map/spatialreferencesystem.h"
#include <QCheckBox>
#include <QSet>
#include <QTimer>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

TextLayersDialog::TextLayersDialog(MapCanvas *canvas, OpenSWMMVisAnnotationLayer *selected, QWidget *parent)
    : QDialog(parent), m_canvas(canvas)
{
    setWindowTitle(tr("Text Layers"));
    setObjectName(QStringLiteral("TextLayersDialog"));
    resize(650, 580);
    auto *root = new QVBoxLayout(this);
    auto *row = new QHBoxLayout;
    m_layers = new QComboBox(this);
    m_layers->setObjectName(QStringLiteral("textLayerSelector"));
    row->addWidget(m_layers, 1);
    auto *add = new QPushButton(tr("New layer…"), this);
    add->setObjectName(QStringLiteral("newTextLayer"));
    auto *rename = new QPushButton(tr("Rename…"), this);
    row->addWidget(add); row->addWidget(rename); root->addLayout(row);
    auto *hint = new QLabel(tr("Choose a text layer, generate feature labels or add your own text. Edit on map: click to add or edit, drag to move, Escape to finish."), this);
    hint->setWordWrap(true); root->addWidget(hint);
    m_layout = new QCheckBox(tr("Automatically avoid overlaps across visible text layers"), this);
    m_layout->setObjectName(QStringLiteral("textLayerAutoLayout"));
    m_layout->setToolTip(tr("Keeps pinned text fixed. Tries nearby positions; hides unplaced text when the view is too crowded. Pan or zoom to see more."));
    root->addWidget(m_layout);
    auto *buttons = new QHBoxLayout;
    auto *generateButton = new QPushButton(tr("Generate labels…"), this);
    generateButton->setObjectName(QStringLiteral("generateTextLabels"));
    auto *halos = new QPushButton(tr("Halos on"), this);
    auto *noHalos = new QPushButton(tr("Halos off"), this);
    buttons->addWidget(generateButton); buttons->addStretch(); buttons->addWidget(halos); buttons->addWidget(noHalos);
    root->addLayout(buttons);
    m_items = new QListWidget(this);
    m_items->setObjectName(QStringLiteral("textLayerItems"));
    m_items->setSelectionMode(QAbstractItemView::ExtendedSelection);
    root->addWidget(m_items, 1);
    auto *editRow = new QHBoxLayout;
    auto *edit = new QPushButton(tr("Edit…"), this);
    auto *remove = new QPushButton(tr("Delete"), this);
    auto *move = new QPushButton(tr("Move to layer…"), this);
    auto *unpin = new QPushButton(tr("Unpin"), this);
    editRow->addWidget(edit); editRow->addWidget(remove); editRow->addWidget(move); editRow->addWidget(unpin); root->addLayout(editRow);
    auto *box = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto *map = box->addButton(tr("Edit on map"), QDialogButtonBox::AcceptRole);
    map->setObjectName(QStringLiteral("editTextOnMap"));
    root->addWidget(box);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(map, &QPushButton::clicked, this, [this] { if (selectedLayer()) accept(); });
    connect(m_layers, &QComboBox::currentIndexChanged, this, [this] { reloadItems(); });
    connect(add, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const auto name = QInputDialog::getText(this, tr("New text layer"), tr("Name:"), QLineEdit::Normal, tr("Text"), &ok).trimmed();
        if (!ok || name.isEmpty()) return;
        auto *layer = new OpenSWMMVisAnnotationLayer(name);
        layer->setParent(m_canvas);
        if (m_canvas->canvasSRS()) layer->setSRS(new SpatialReferenceSystem(*m_canvas->canvasSRS()), true);
        m_canvas->addLayer(layer, true);
        reloadLayers(layer);
    });
    connect(rename, &QPushButton::clicked, this, [this] {
        auto *layer = selectedLayer(); if (!layer) return;
        bool ok = false;
        const auto name = QInputDialog::getText(this, tr("Rename text layer"), tr("Name:"), QLineEdit::Normal, layer->name(), &ok).trimmed();
        if (!ok || name.isEmpty()) return;
        const auto before = layer->layerJson(); layer->setName(name); commit(before, tr("Rename text layer")); reloadLayers(layer);
    });
    connect(m_layout, &QCheckBox::toggled, this, [this](bool on) {
        auto *layer = selectedLayer(); if (!layer) return;
        const auto before = layer->layerJson(); layer->setAutoLayout(on); commit(before, tr("Change text layout"));
    });
    auto haloAction = [this](bool on) {
        auto *layer = selectedLayer(); if (!layer) return;
        const auto before = layer->layerJson();
        const auto selected = m_items->selectedItems();
        if (selected.isEmpty()) for (auto *item : layer->annotations()) item->setHaloEnabled(on);
        else for (auto *row : selected) if (auto *item = layer->annotation(row->data(Qt::UserRole).toString())) item->setHaloEnabled(on);
        commit(before, tr("Change text halos"));
    };
    connect(halos, &QPushButton::clicked, this, [haloAction] { haloAction(true); });
    connect(noHalos, &QPushButton::clicked, this, [haloAction] { haloAction(false); });
    halos->setToolTip(tr("Applies to selected text, or all text when nothing is selected."));
    noHalos->setToolTip(halos->toolTip());
    connect(edit, &QPushButton::clicked, this, &TextLayersDialog::editItem);
    connect(m_items, &QListWidget::itemDoubleClicked, this, &TextLayersDialog::editItem);
    connect(generateButton, &QPushButton::clicked, this, &TextLayersDialog::generate);
    connect(remove, &QPushButton::clicked, this, [this] {
        auto *layer = selectedLayer(); if (!layer) return;
        const auto before = layer->layerJson();
        QStringList ids;
        for (auto *row : m_items->selectedItems()) ids.append(row->data(Qt::UserRole).toString());
        for (const auto &id : ids) layer->removeAnnotation(id);
        commit(before, tr("Delete text"));
    });
    connect(unpin, &QPushButton::clicked, this, [this] {
        auto *layer = selectedLayer(); if (!layer) return;
        const auto before = layer->layerJson();
        QStringList ids;
        for (auto *row : m_items->selectedItems()) ids.append(row->data(Qt::UserRole).toString());
        for (const auto &id : ids) if (auto *item = layer->annotation(id)) { item->setPinned(false); item->setOffset({}); }
        commit(before, tr("Unpin text"));
    });
    connect(move, &QPushButton::clicked, this, [this] {
        auto *from = selectedLayer(); if (!from || m_items->selectedItems().isEmpty()) return;
        QDialog dlg(this); dlg.setWindowTitle(tr("Move text to layer"));
        auto *layout = new QVBoxLayout(&dlg); auto *target = new QComboBox(&dlg);
        for (auto *base : m_canvas->layers())
            if (auto *layer = qobject_cast<OpenSWMMVisAnnotationLayer *>(base); layer && layer != from)
                target->addItem(layer->name(), QVariant::fromValue(static_cast<OpenSWMMVisLayer *>(layer)));
        if (!target->count()) { QMessageBox::information(this, tr("Move text"), tr("Create another text layer first.")); return; }
        layout->addWidget(target);
        auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg); layout->addWidget(box);
        connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept); connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        if (dlg.exec() != QDialog::Accepted) return;
        auto *to = qobject_cast<OpenSWMMVisAnnotationLayer *>(target->currentData().value<OpenSWMMVisLayer *>());
        const auto oldFrom = from->layerJson(), oldTo = to->layerJson();
        QStringList ids;
        for (auto *row : m_items->selectedItems()) ids.append(row->data(Qt::UserRole).toString());
        for (const auto &id : ids) {
            auto *item = from->annotation(id); if (!item) continue;
            QPointF position(item->x(), item->y());
            if (!AnnotationFeatures::transform(position, from->srs(), to->srs())) continue;
            item = from->takeAnnotation(id); item->setPosition(position.x(), position.y()); to->addAnnotation(item);
        }
        auto *stack = m_canvas->undoStack(); stack->beginMacro(tr("Move text between layers"));
        stack->push(new EditTextLayerCommand(from, oldFrom, from->layerJson(), tr("Move text")));
        stack->push(new EditTextLayerCommand(to, oldTo, to->layerJson(), tr("Move text")));
        stack->endMacro(); reloadItems();
    });
    reloadLayers(selected);
}
OpenSWMMVisAnnotationLayer *TextLayersDialog::selectedLayer() const
{
    auto *base = m_layers->currentData().value<OpenSWMMVisLayer *>();
    return m_canvas->layers().contains(base) ? qobject_cast<OpenSWMMVisAnnotationLayer *>(base) : nullptr;
}
void TextLayersDialog::reloadLayers(OpenSWMMVisAnnotationLayer *selected)
{
    QSignalBlocker block(m_layers); m_layers->clear();
    for (auto *base : m_canvas->layers()) if (auto *layer = qobject_cast<OpenSWMMVisAnnotationLayer *>(base)) {
        m_layers->addItem(layer->name(), QVariant::fromValue(base));
        if (layer == selected) m_layers->setCurrentIndex(m_layers->count()-1);
    }
    reloadItems();
}
void TextLayersDialog::reloadItems()
{
    auto *layer = selectedLayer();
    // Queue refreshes so loops mutating the model never invalidate list rows.
    if (m_observed != layer) {
        if (m_observed) disconnect(m_observed, nullptr, this, nullptr);
        m_observed = layer;
        if (layer) connect(layer, &OpenSWMMVisAnnotationLayer::annotationsChanged, this, [this] {
            if (m_refreshPending) return;
            m_refreshPending = true;
            QTimer::singleShot(0, this, [this] { m_refreshPending = false; reloadItems(); });
        });
    }
    QStringList selected;
    for (auto *row : m_items->selectedItems()) selected.append(row->data(Qt::UserRole).toString());
    m_items->clear(); QSignalBlocker block(m_layout);
    m_layout->setChecked(layer && layer->autoLayout());
    if (!layer) return;
    for (auto *item : layer->annotations()) {
        QString text = item->expression().isEmpty() ? item->text() : item->expression();
        if (!item->featureReference().isEmpty()) text += tr(" — %1").arg(item->featureReference().value("name").toString(item->featureReference().value("fid").toString()));
        if (item->pinned()) text += tr(" (pinned)");
        auto *row = new QListWidgetItem(text, m_items); row->setData(Qt::UserRole, item->id()); row->setSelected(selected.contains(item->id()));
    }
}
void TextLayersDialog::commit(const QJsonObject &before, const QString &label)
{
    auto *layer = selectedLayer(); if (!layer) return;
    const auto after = layer->layerJson();
    if (before != after) m_canvas->undoStack()->push(new EditTextLayerCommand(layer, before, after, label));
    reloadItems();
}
void TextLayersDialog::editItem()
{
    auto *layer = selectedLayer(); auto *row = m_items->currentItem(); if (!layer || !row) return;
    auto *item = layer->annotation(row->data(Qt::UserRole).toString()); if (!item) return;
    const auto before = layer->layerJson();
    AnnotationStyleDialog dlg(item, this, m_canvas); dlg.setEditMode(true);
    if (dlg.exec() == QDialog::Accepted) commit(before, tr("Edit text"));
}
void TextLayersDialog::generate()
{
    auto *layer = selectedLayer(); if (!layer) return;
    QDialog dlg(this); dlg.setWindowTitle(tr("Generate feature labels"));
    auto *form = new QFormLayout(&dlg);
    auto *source = new QComboBox(&dlg);
    for (auto *base : m_canvas->layers()) if (qobject_cast<SWMMModelLayer *>(base) || qobject_cast<GISVectorLayer *>(base))
        source->addItem(base->name(), QVariant::fromValue(base));
    auto *expression = new QLineEdit(QStringLiteral("{name}"), &dlg);
    auto *fields = new QComboBox(&dlg);
    fields->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); fields->setMinimumContentsLength(25);
    auto *category = new QComboBox(&dlg); category->addItem(tr("All features"), -1);
    const QStringList categories = {tr("Junctions"),tr("Outfalls"),tr("Storage"),tr("Dividers"),tr("Conduits"),tr("Pumps"),tr("Orifices"),tr("Weirs"),tr("Outlets"),tr("Subcatchments"),tr("Rain gages")};
    for (int i = 0; i < categories.size(); ++i) category->addItem(categories[i], i);
    QVector<AnnotationFeatures::Feature> features;
    auto updateFields = [&] {
        features = AnnotationFeatures::enumerate(source->currentData().value<OpenSWMMVisLayer *>());
        QSet<QString> names; names.insert("name");
        for (const auto &f : features) for (auto it = f.attributes.cbegin(); it != f.attributes.cend(); ++it) names.insert(it.key());
        QStringList sorted = names.values(); sorted.sort(); fields->clear(); fields->addItems(sorted);
        auto *base = source->currentData().value<OpenSWMMVisLayer *>();
        category->setEnabled(qobject_cast<SWMMModelLayer *>(base) != nullptr);
        if (base) {
            const auto cfg = base->labelConfig();
            expression->setText(!cfg.expression.isEmpty() ? cfg.expression : !cfg.fieldName.isEmpty() ? "{"+cfg.fieldName+"}" : "{name}");
        }
    };
    connect(source, &QComboBox::currentIndexChanged, &dlg, updateFields); updateFields();
    auto *insert = new QPushButton(tr("Insert field"), &dlg);
    connect(insert, &QPushButton::clicked, &dlg, [=] { expression->insert("{"+fields->currentText()+"}"); });
    auto *fieldRow = new QHBoxLayout; fieldRow->addWidget(fields, 1); fieldRow->addWidget(insert);
    auto *replaceLabels = new QCheckBox(tr("Turn off labels drawn by the source layer"), &dlg); replaceLabels->setChecked(true);
    auto *callouts = new QCheckBox(tr("Connect labels to features"), &dlg); callouts->setChecked(true);
    auto *halos = new QCheckBox(tr("Draw halos"), &dlg); halos->setChecked(true);
    auto *layout = new QCheckBox(tr("Enable automatic layout"), &dlg); layout->setChecked(true);
    form->addRow(tr("Source:"), source); form->addRow(tr("Features:"), category); form->addRow(tr("Expression:"), expression); form->addRow(tr("Attributes:"), fieldRow);
    form->addRow(callouts); form->addRow(halos); form->addRow(layout); form->addRow(replaceLabels);
    auto *hint = new QLabel(tr("Labels stay linked to these features. Generate again to add newly created features; existing labels and custom edits are kept."), &dlg); hint->setWordWrap(true); form->addRow(hint);
    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg); form->addRow(box);
    box->button(QDialogButtonBox::Ok)->setEnabled(source->count() > 0);
    connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept); connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted || expression->text().trimmed().isEmpty()) return;
    auto *base = source->currentData().value<OpenSWMMVisLayer *>();
    const auto before = layer->layerJson();
    int added = 0;
    QSet<QString> existing;
    for (auto *item : layer->annotations()) if (!item->featureReference().isEmpty()) existing.insert(AnnotationFeatures::key(item->featureReference()));
    for (const auto &f : features) {
        if (category->isEnabled() && category->currentData().toInt() >= 0 && f.reference.value("category").toInt(-1) != category->currentData().toInt()) continue;
        if (existing.contains(AnnotationFeatures::key(f.reference))) continue;
        QPointF position = f.position;
        if (!AnnotationFeatures::transform(position, base->srs(), layer->srs())) continue;
        auto *item = new AnnotationTextItem;
        item->setText(AnnotationFeatures::evaluate(expression->text(), f));
        item->setPosition(position.x(), position.y()); item->setFeatureReference(f.reference);
        item->setFont(base->labelConfig().effectiveFont()); item->setFillColor(base->labelConfig().color);
        item->setExpression(expression->text()); item->setCalloutEnabled(callouts->isChecked()); item->setHaloEnabled(halos->isChecked()); item->setOffset({12,-12});
        layer->addAnnotation(item); ++added;
    }
    if (layout->isChecked()) layer->setAutoLayout(true);
    auto *stack = m_canvas->undoStack();
    stack->beginMacro(tr("Generate feature labels"));
    commit(before, tr("Generate feature labels"));
    if (added && replaceLabels->isChecked() && base->labelConfig().enabled)
        stack->push(new SetSourceLabelsCommand(base, false));
    stack->endMacro();
    if (!added) QMessageBox::information(this, tr("Generate labels"), tr("No new labels were added. Matching features may already be labeled, or have no map geometry."));
}
