#include "ui/widgets/corridorsourceswidget.h"
#include "layers/gisvectorlayer.h"
#include "map/spatialreferencesystem.h"
#include "ui/theme/themehelpers.h"
#include "ui/uiscrollhelpers.h"

#include <QComboBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <ogrsf_frmts.h>
#include <algorithm>
#include <cmath>

namespace {
QStringList numericFields(GISVectorLayer *layer)
{
    QStringList names;
    if (!layer || !layer->ogrLayer()) return names;
    // Schema inspection only: never fetch features or change the OGR cursor.
    const auto *definition = layer->ogrLayer()->GetLayerDefn();
    for (int i = 0; definition && i < definition->GetFieldCount(); ++i) {
        const auto *field = definition->GetFieldDefn(i);
        if (field->GetType() == OFTInteger || field->GetType() == OFTInteger64
            || field->GetType() == OFTReal)
            names.append(QString::fromUtf8(field->GetNameRef()));
    }
    return names;
}
QString number(double value) { return QString::number(value, 'g', 17); }
}

CorridorSourcesWidget::CorridorSourcesWidget(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *hint = new QLabel(tr("Select road, river or other centreline features on the map, then add them below. "
        "Total width and Along spacing use the mesh CRS units. Along = 0 retains the original centreline vertices."), this);
    hint->setWordWrap(true);
    openswmmvis::ui::theme::applyHintRole(hint);
    layout->addWidget(hint);
    auto *form = new QFormLayout;
    layout->addLayout(form);
    auto addField = [this, form](const QString &label, QWidget *field, const char *name, const QString &help) {
        field->setObjectName(QString::fromLatin1(name));
        field->setAccessibleName(QString(label).remove('&').remove(':'));
        field->setAccessibleDescription(help);
        field->setToolTip(help);
        auto *buddy = new QLabel(label, this);
        buddy->setBuddy(field);
        form->addRow(buddy, field);
    };
    m_layer = new QComboBox(this);
    m_layer->setMinimumWidth(OpenSWMM::Ui::kComboMinWidthPx);
    addField(tr("&Source layer:"), m_layer, "corridorLayer", tr("The map layer containing the selected corridor centreline features."));
    m_selection = new QLabel(this);
    m_selection->setAccessibleName(tr("Selected corridor features"));
    form->addRow(QString(), m_selection);
    m_widthMode = new QComboBox(this);
    m_widthMode->addItem(tr("Uniform total width"), 0);
    m_widthMode->addItem(tr("Numeric feature field"), 1);
    addField(tr("Width &mode:"), m_widthMode, "corridorWidthMode", tr("Use one total width for all selected features, or read each feature's width from a numeric field."));
    m_width = new QLineEdit(QStringLiteral("10"), this);
    addField(tr("Total &width (CRS units):"), m_width, "corridorWidth", tr("Finite total corridor width greater than 0; each side extends half this width from the centreline."));
    m_widthField = new QComboBox(this);
    addField(tr("Width &field:"), m_widthField, "corridorWidthField", tr("Numeric attribute containing each feature's total width in mesh CRS units; stored using the actual field name."));
    m_across = new QLineEdit(QStringLiteral("2"), this);
    addField(tr("&Across cells:"), m_across, "corridorAcross", tr("Positive integer number of cells across the total width."));
    m_along = new QLineEdit(QStringLiteral("0"), this);
    addField(tr("A&long spacing (CRS units):"), m_along, "corridorAlong", tr("Finite spacing at least 0. Enter 0 to keep original centreline vertices."));
    m_tag = new QLineEdit(this);
    addField(tr("Region &tag:"), m_tag, "corridorTag", tr("Optional region tag. Leave blank to identify cells by source feature."));
    m_add = new QPushButton(tr("A&dd selected features"), this);
    m_add->setObjectName(QStringLiteral("corridorAdd"));
    layout->addWidget(m_add, 0, Qt::AlignLeft);

    m_table = new QTableWidget(0, 7, this);
    m_table->setObjectName(QStringLiteral("corridorSourcesTable"));
    m_table->setAccessibleName(tr("Configured corridor sources"));
    m_table->setAccessibleDescription(tr("Each row captures selected feature IDs from one source. Edit width field, total width, Across cells, Along spacing or region tag. Empty width field uses the uniform total width. Missing map layers remain available for validation when generating."));
    m_table->setHorizontalHeaderLabels({tr("Source"), tr("Feature IDs"), tr("Width field"),
        tr("Uniform total width (CRS units)"), tr("Across cells"), tr("Along spacing (CRS units)"), tr("Tag")});
    const QStringList descriptions{tr("Stored dataset and layer. Assigned source CRS is retained with this recipe."),
        tr("Captured feature IDs; later map selections do not change this row."),
        tr("Actual numeric field name. Empty uses the uniform total width. Changing this field refreshes the saved geometry check on the next successful generation."),
        tr("Used when Width field is empty. ") + m_width->toolTip(), m_across->toolTip(), m_along->toolTip(), m_tag->toolTip()};
    for (int column = 0; column < descriptions.size(); ++column) {
        m_table->horizontalHeaderItem(column)->setToolTip(descriptions[column]);
        m_table->horizontalHeaderItem(column)->setData(Qt::AccessibleDescriptionRole, descriptions[column]);
    }
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->setMinimumHeight(140);
    layout->addWidget(m_table);
    m_remove = new QPushButton(tr("&Remove selected sources"), this);
    m_remove->setObjectName(QStringLiteral("corridorRemove"));
    m_remove->setEnabled(false);
    layout->addWidget(m_remove, 0, Qt::AlignLeft);
    for (auto *button : {m_add, m_remove}) {
        button->setAccessibleName(QString(button->text()).remove('&'));
        button->setAutoDefault(false);
        button->setDefault(false);
    }
    m_message = new QLabel(this);
    m_message->setObjectName(QStringLiteral("corridorMessage"));
    m_message->setAccessibleName(tr("Corridor source guidance"));
    m_message->setWordWrap(true);
    layout->addWidget(m_message);
    connect(m_layer, &QComboBox::currentIndexChanged, this, [this] { refreshFields(); updateSelectionMessage(); });
    connect(m_widthMode, &QComboBox::currentIndexChanged, this, [this] {
        const bool field = m_widthMode->currentData().toInt() == 1;
        m_width->setEnabled(!field);
        m_widthField->setEnabled(field && m_widthField->count() > 0);
    });
    connect(m_add, &QPushButton::clicked, this, &CorridorSourcesWidget::addSelection);
    connect(m_remove, &QPushButton::clicked, this, &CorridorSourcesWidget::removeSelection);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this] {
        m_remove->setEnabled(!m_table->selectionModel()->selectedRows().isEmpty());
    });
    connect(m_table, &QTableWidget::itemChanged, this, &CorridorSourcesWidget::sourcesChanged);
    refreshLayers();
}

GISVectorLayer *CorridorSourcesWidget::currentLayer() const
{
    const int index = m_layer->currentData().toInt();
    return index >= 0 && index < m_layers.size() ? m_layers[index].data() : nullptr;
}

GISVectorLayer *CorridorSourcesWidget::matchingLayer(const mesh::CorridorSource &source) const
{
    for (const auto &layer : m_layers)
        if (layer && layer->filePath() == source.path && layer->ogrLayerName() == source.layerName)
            return layer.data();
    return nullptr;
}

void CorridorSourcesWidget::setLayers(const QList<GISVectorLayer *> &layers)
{
    const QPointer<GISVectorLayer> previous = currentLayer();
    for (const auto &connection : m_layerConnections) disconnect(connection);
    m_layerConnections.clear();
    m_layers.clear();
    for (auto *layer : layers) {
        if (!layer || m_layers.contains(QPointer<GISVectorLayer>(layer))) continue;
        m_layers.append(layer);
        m_layerConnections.append(connect(layer, &GISVectorLayer::selectionChanged, this,
            [this] { updateSelectionMessage(); }));
        m_layerConnections.append(connect(layer, &QObject::destroyed, this, [this] {
            // Wait for QPointer invalidation to finish before rebuilding choices.
            QTimer::singleShot(0, this, [this] { refreshLayers(); });
        }));
        m_layerConnections.append(connect(layer, &OpenSWMMVisLayer::nameChanged, this,
            [this] { refreshLayers(); }));
    }
    refreshLayers();
    if (previous)
        for (int i = 0; i < m_layer->count(); ++i) {
            const int index = m_layer->itemData(i).toInt();
            if (index >= 0 && index < m_layers.size() && m_layers[index] == previous) {
                m_layer->setCurrentIndex(i);
                break;
            }
        }
}

void CorridorSourcesWidget::refreshLayers()
{
    const QPointer<GISVectorLayer> previous = currentLayer();
    {
        const QSignalBlocker block(m_layer);
        m_layer->clear();
        for (int i = 0; i < m_layers.size(); ++i) {
            if (!m_layers[i]) continue;
            const auto *layer = m_layers[i].data();
            const QString name = layer->name().isEmpty() ? QFileInfo(layer->filePath()).fileName() : layer->name();
            m_layer->addItem(name, i);
            m_layer->setItemData(m_layer->count() - 1,
                layer->filePath() + QLatin1Char('\n') + layer->ogrLayerName(), Qt::ToolTipRole);
            if (m_layers[i] == previous) m_layer->setCurrentIndex(m_layer->count() - 1);
        }
        if (m_layer->count() == 0) m_layer->addItem(tr("No vector layers available"), -1);
    }
    m_layer->setEnabled(currentLayer() != nullptr);
    refreshFields();
    updateSelectionMessage();
    updateSourceLabels();
}

void CorridorSourcesWidget::refreshFields()
{
    const QString previous = m_widthField->currentData().toString();
    m_widthField->clear();
    for (const auto &field : numericFields(currentLayer())) m_widthField->addItem(field, field);
    const int match = m_widthField->findData(previous);
    if (match >= 0) m_widthField->setCurrentIndex(match);
    const bool field = m_widthMode->currentData().toInt() == 1;
    m_width->setEnabled(!field);
    m_widthField->setEnabled(field && m_widthField->count() > 0);
}

void CorridorSourcesWidget::updateSelectionMessage()
{
    const auto *layer = currentLayer();
    const qsizetype count = layer ? layer->selectedFeatureIds().size() : 0;
    m_selection->setText(count == 1 ? tr("1 selected feature") : tr("%1 selected features").arg(count));
    m_add->setEnabled(layer != nullptr);
    m_message->setText(!layer ? tr("Add a line layer to the map, then select its corridor features.")
        : count == 0 ? tr("Select line features on the map, then choose Add selected features.") : QString());
}

bool CorridorSourcesWidget::fail(const QString &message, QWidget *field, QString *error) const
{
    if (error) *error = message;
    m_message->setText(message);
    if (field) {
        // Validation can be requested while another generation tab is active.
        // Reveal the field before assigning focus or scrolling to its row.
        for (QWidget *ancestor = field->parentWidget(); ancestor; ancestor = ancestor->parentWidget())
            if (auto *tabs = qobject_cast<QTabWidget *>(ancestor))
                for (int i = 0; i < tabs->count(); ++i)
                    if (tabs->widget(i)->isAncestorOf(field)) tabs->setCurrentIndex(i);
        for (QWidget *ancestor = field->parentWidget(); ancestor; ancestor = ancestor->parentWidget())
            if (auto *scroll = qobject_cast<QScrollArea *>(ancestor)) scroll->ensureWidgetVisible(field);
        field->setFocus(Qt::OtherFocusReason);
    }
    return false;
}

bool CorridorSourcesWidget::failRow(int row, int column, const QString &message, QString *error) const
{
    m_table->setCurrentCell(row, column);
    const bool result = fail(tr("Corridor source row %1: %2").arg(row + 1).arg(message), m_table, error);
    m_table->scrollToItem(m_table->item(row, column));
    return result;
}

void CorridorSourcesWidget::addSelection()
{
    auto *layer = currentLayer();
    if (!layer) { fail(tr("Add a line layer to the map, then select its corridor features."), m_layer, nullptr); return; }
    const auto ids = layer->selectedFeatureIds();
    if (ids.isEmpty()) { fail(tr("Select line features on the map, then choose Add selected features."), m_layer, nullptr); return; }
    if (!layer->srs() || layer->srs()->toWkt().isEmpty()) {
        fail(tr("Assign a coordinate reference system to the source layer before adding its features."), m_layer, nullptr); return;
    }
    mesh::CorridorSource source;
    source.path = layer->filePath();
    source.layerName = layer->ogrLayerName();
    source.sourceCRSWkt = layer->srs()->toWkt();
    for (qint64 id : ids) source.featureIds.append(id);
    std::sort(source.featureIds.begin(), source.featureIds.end());
    const bool fromField = m_widthMode->currentData().toInt() == 1;
    if (fromField) {
        source.widthField = m_widthField->currentData().toString();
        if (source.widthField.isEmpty()) { fail(tr("Choose a numeric width field, or use a uniform total width."), m_widthMode, nullptr); return; }
    } else {
        bool ok = false;
        source.width = m_width->text().trimmed().toDouble(&ok);
        if (!ok || !std::isfinite(source.width) || source.width <= 0) {
            fail(tr("Total width must be a finite number greater than 0 in mesh CRS units."), m_width, nullptr); return;
        }
    }
    bool ok = false;
    source.across = m_across->text().trimmed().toInt(&ok);
    if (!ok || source.across < 1) { fail(tr("Across cells must be a positive integer within the supported integer range."), m_across, nullptr); return; }
    source.along = m_along->text().trimmed().toDouble(&ok);
    if (!ok || !std::isfinite(source.along) || source.along < 0) {
        fail(tr("Along spacing must be a finite number at least 0. Enter 0 to keep original centreline vertices."), m_along, nullptr); return;
    }
    source.tag = m_tag->text().trimmed();
    appendSource(source);
    m_message->clear();
    emit sourcesChanged();
}

void CorridorSourcesWidget::appendSource(const mesh::CorridorSource &source)
{
    const QSignalBlocker block(m_table);
    const int row = m_table->rowCount();
    m_sources.append(source);
    m_table->insertRow(row);
    QStringList ids;
    for (qint64 id : source.featureIds) ids.append(QString::number(id));
    const QStringList values{QString(), ids.join(QStringLiteral(", ")), source.widthField,
        number(source.width), QString::number(source.across), number(source.along), source.tag};
    for (int column = 0; column < values.size(); ++column) {
        auto *item = new QTableWidgetItem(values[column]);
        if (column < 2) item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        item->setToolTip(m_table->horizontalHeaderItem(column)->toolTip());
        item->setData(Qt::AccessibleDescriptionRole, item->toolTip());
        m_table->setItem(row, column, item);
    }
    updateSourceLabels();
}

void CorridorSourcesWidget::updateSourceLabels()
{
    const QSignalBlocker block(m_table);
    for (int row = 0; row < m_sources.size(); ++row) {
        const auto &source = m_sources[row];
        QString title = QFileInfo(source.path).fileName() + QStringLiteral(" — ") + source.layerName;
        if (!matchingLayer(source)) title += tr(" (not on map)");
        m_table->item(row, 0)->setText(title);
        const QString help = tr("Dataset: %1\nLayer: %2\nCaptured source CRS: %3")
            .arg(source.path, source.layerName, source.sourceCRSWkt);
        m_table->item(row, 0)->setToolTip(help);
        m_table->item(row, 0)->setData(Qt::AccessibleDescriptionRole, help);
    }
}

void CorridorSourcesWidget::setSources(const QVector<mesh::CorridorSource> &sources)
{
    const QSignalBlocker block(m_table);
    m_sources.clear();
    m_table->setRowCount(0);
    for (const auto &source : sources) appendSource(source);
    m_remove->setEnabled(false);
    m_message->clear();
}

bool CorridorSourcesWidget::sources(QVector<mesh::CorridorSource> *out, QString *error) const
{
    if (!out) return fail(tr("No destination for corridor source settings."), nullptr, error);
    QVector<mesh::CorridorSource> result;
    for (int row = 0; row < m_sources.size(); ++row) {
        mesh::CorridorSource source = m_sources[row];
        if (source.featureIds.isEmpty())
            return failRow(row, 1, tr("No selected features were captured. Remove this row and add selected map features."), error);
        source.widthField = m_table->item(row, 2)->text();
        if (auto *layer = matchingLayer(source); layer && !source.widthField.isEmpty()
            && !numericFields(layer).contains(source.widthField))
            return failRow(row, 2, tr("Width field must name a numeric field in the source layer."), error);
        bool ok = false;
        source.width = m_table->item(row, 3)->text().trimmed().toDouble(&ok);
        if (!ok || !std::isfinite(source.width) || source.width <= 0)
            return failRow(row, 3, tr("Total width must be a finite number greater than 0 in mesh CRS units."), error);
        source.across = m_table->item(row, 4)->text().trimmed().toInt(&ok);
        if (!ok || source.across < 1)
            return failRow(row, 4, tr("Across cells must be a positive integer within the supported integer range."), error);
        source.along = m_table->item(row, 5)->text().trimmed().toDouble(&ok);
        if (!ok || !std::isfinite(source.along) || source.along < 0)
            return failRow(row, 5, tr("Along spacing must be a finite number at least 0; enter 0 to retain centreline vertices."), error);
        source.tag = m_table->item(row, 6)->text().trimmed();
        if (source.widthField != m_sources[row].widthField) source.geometryDigest.clear();
        result.append(std::move(source));
    }
    *out = std::move(result);
    if (error) error->clear();
    m_message->clear();
    return true;
}

void CorridorSourcesWidget::removeSelection()
{
    QVector<int> rows;
    for (const auto &index : m_table->selectionModel()->selectedRows()) rows.append(index.row());
    std::sort(rows.begin(), rows.end(), [](int a, int b) { return a > b; });
    for (int row : rows) {
        m_sources.removeAt(row);
        m_table->removeRow(row);
    }
    if (!rows.isEmpty()) emit sourcesChanged();
}
