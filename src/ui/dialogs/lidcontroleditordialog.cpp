#include <openswmm/engine/openswmm_edit.h>
/**
 * @file      lidcontroleditordialog.cpp
 * @brief     List pane and property tabs for standard LID controls, with an ordered layer editor
 *            for NODE controls.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/dialogs/lidcontroleditordialog.h"
#include "core/swmmidentifier.h"

#include "layers/swmmmodellayer.h" // complete type for QPointer<SWMMModelLayer>
#include "lid/lidcontrolprovider.h"
#include "lid/lidcontrolregistry.h"
#include "ui/models/lidcontrollistmodel.h"
#include "ui/uiscrollhelpers.h"
#include "core/unitsystem.h"
#include "ui/sectionview/lidlayerdiagram.h"
#include "ui/sectionview/sectionpreviewwidget.h"
#include "ui/theme/iconfactory.h"

#include "ui/models/lidnodelayermodel.h"
#include "ui/editors/dataobjectdraftsession.h"
#include "ui/editors/dataobjectstructureedit.h"
#include "ui/models/draftmarkproxymodel.h"
#include "ui/panels/dataobjectattributetablemodel.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include <QListWidget>
#include <QCoreApplication>
#include <QScrollArea>
#include <QUndoStack>
#include <algorithm>
#include <memory>
#include <QTimer>
#include <QPainter>
#include <QPainterPath>
#include <cmath>
#include <QTableView>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QInputDialog>
#include "ui/widgets/treatmentexpressionedit.h"
#include <openswmm/engine/openswmm_quality.h>
#include <openswmm/engine/openswmm_pollutants.h>
#include <QSignalBlocker>
#include <QScopedValueRollback>
#include <limits>
#include <QHeaderView>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QStyle>
#include <QTabWidget>
#include <QVBoxLayout>

namespace openswmmvis::ui
{

using openswmmvis::lid::LidControlProvider;
using openswmmvis::lid::LidControlRegistry;
namespace
{
class RetentionCurves : public QWidget
{
public:
    /**
     * @brief Build a small borrowed-material retention/conductivity plot widget.
     * @param[in] parent Optional Qt owner.
     * @details Caller populates public material/porosity values before requesting repaint.
     */
    explicit RetentionCurves(QWidget *parent) : QWidget(parent)
    {
        setMinimumSize(225, 185);
        setObjectName("lidRetentionCurves");
        setAccessibleName(tr("Richards retention and conductivity curves"));
    }
    QString layerLabel;
    SWMM_LidRichardsMaterial material{};
    double porosity = 0;

protected:
    /**
     * @brief Plot sampled van Genuchten moisture and relative conductivity against log suction.
     * @details The unnamed paint event is ignored and the whole widget is drawn. Requires
     * alpha>0, n>1, porosity>theta_r and l>=0 for plotting; otherwise shows a parameter-entry
     * prompt. Samples 201 suction heads from 0.001 to 1000 metres, drawing
     * moisture/relative-conductivity curves on the zero-to-one vertical scale. Reads stored
     * material values without converting or editing them; this is a schematic, not solver
     * validation.
     */
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), palette().base());
        p.setPen(palette().text().color());
        if (layerLabel.isEmpty()) {
            p.drawText(rect().adjusted(10, 10, -10, -10), Qt::AlignCenter | Qt::TextWordWrap,
                       tr("Select a MEDIA or AGGREGATE layer to preview its Richards curves."));
            return;
        }
        const int line = fontMetrics().height();
        p.drawText(QRect(0, 4, width(), line), Qt::AlignCenter, layerLabel);
        if (!(material.alpha > 0 && material.n > 1 && porosity > material.theta_r &&
              material.l >= 0)) {
            p.drawText(
                rect().adjusted(10, line + 10, -10, -10), Qt::AlignCenter | Qt::TextWordWrap,
                tr("Set retention alpha above 0, n above 1, and residual water content below "
                   "the layer porosity to preview its curves."));
            return;
        }
        const QRectF graph = QRectF(rect()).adjusted(32, 3 * line + 12, -16, -2 * line - 12);
        p.drawLine(graph.bottomLeft(), graph.bottomRight());
        p.drawLine(graph.bottomLeft(), graph.topLeft());
        p.drawText(QRect(0, line + 6, width(), 2 * line), Qt::AlignCenter | Qt::TextWordWrap,
                   tr("Blue: water content · Orange: relative conductivity"));
        p.drawText(QRectF(graph.left(), graph.bottom() + 2, graph.width(), line), Qt::AlignLeft,
                   QStringLiteral("0.001"));
        p.drawText(QRectF(graph.left(), graph.bottom() + 2, graph.width(), line), Qt::AlignRight,
                   QStringLiteral("1,000"));
        p.drawText(QRect(0, height() - line - 4, width(), line), Qt::AlignCenter,
                   tr("Suction head (m, log scale)"));
        p.drawText(QRectF(0, graph.top() - line / 2., 26, line), Qt::AlignRight, "1");
        p.drawText(QRectF(0, graph.bottom() - line / 2., 26, line), Qt::AlignRight, "0");
        for (int curve = 0; curve < 2; ++curve) {
            QPainterPath path;
            const double m = 1 - 1 / material.n;
            for (int i = 0; i <= 200; ++i) {
                double suction = std::pow(10., -3 + 6. * i / 200);
                double se = std::pow(1 + std::pow(material.alpha * suction, material.n), -m);
                double value = curve ? std::pow(se, material.l) *
                                           std::pow(1 - std::pow(1 - std::pow(se, 1 / m), m), 2)
                                     : material.theta_r + (porosity - material.theta_r) * se;
                QPointF point(graph.left() + graph.width() * i / 200.,
                              graph.bottom() - graph.height() * value);
                if (i)
                    path.lineTo(point);
                else
                    path.moveTo(point);
            }
            p.setPen(QPen(curve ? QColor("#c87818") : QColor("#287fb8"), 2));
            p.drawPath(path);
        }
    }
};
}

namespace
{
// Use the same native numeric controls as the physical property forms.
class TreatmentRateDelegate final : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    /**
     * @brief Allocate a native numeric treatment-rate editor for the table column.
     * @param[in] parent Optional Qt owner of the returned editor.
     * @param[in] index Borrowed target index; column one uses removal range 0..100 percent and
     * step one, other columns 0..1e12 and step 0.01.
     * @return New QDoubleSpinBox with eight decimals and keyboard tracking disabled;
     * parent/caller owns it. The unnamed style option is ignored.
     */
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &,
                          const QModelIndex &index) const override
    {
        auto *spin = new QDoubleSpinBox(parent);
        spin->setDecimals(8);
        spin->setRange(0.0, index.column() == 1 ? 100.0 : 1.e12);
        spin->setSingleStep(index.column() == 1 ? 1.0 : .01);
        spin->setKeyboardTracking(false);
        return spin;
    }
    /**
     * @brief Hydrate a treatment-rate spin box from the cell's edit-role numeric value.
     * @param[in,out] editor Non-null QDoubleSpinBox produced by createEditor(), assumed by
     * static_cast.
     * @param[in] index Borrowed source cell; toDouble() and Qt widget range/precision determine
     * displayed value.
     * @details Does not write model data or block editor signals.
     */
    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        static_cast<QDoubleSpinBox *>(editor)->setValue(index.data(Qt::EditRole).toDouble());
    }
    /**
     * @brief Interpret pending numeric text and commit it to the treatment model cell.
     * @param[in,out] editor Non-null QDoubleSpinBox produced by this delegate; interpretText()
     * settles its current value.
     * @param[in,out] model Non-null borrowed model receiving setData(Qt::EditRole).
     * @param[in] index Borrowed target cell passed unchanged.
     * @details Setter success is not inspected and no arguments are retained.
     */
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override
    {
        auto *spin = static_cast<QDoubleSpinBox *>(editor);
        spin->interpretText();
        model->setData(index, spin->value(), Qt::EditRole);
    }
};
/**
 * @brief Allocate a four-decimal native LID numeric editor.
 * @param[in] parent Optional Qt parent taking ownership of the new control.
 * @param[in] minV Lower bound passed to Qt; caller supplies an ordered finite range.
 * @param[in] maxV Upper bound passed to Qt.
 * @param[in] step Single-step increment passed to Qt, in the field's native units.
 * @return Newly allocated spin box owned by parent/caller; no automatic physical-unit
 * conversion.
 */
QDoubleSpinBox *makeSpin(QWidget *parent, double minV, double maxV, double step)
{
    auto *s = new QDoubleSpinBox(parent);
    s->setRange(minV, maxV);
    s->setDecimals(4);
    s->setSingleStep(step);
    return s;
}

QVector<SWMM_LidNodeLayer> defaultNodeLayers()
{
    LidNodeLayerModel defaults;
    const bool si = UnitSystem::instance() && UnitSystem::instance()->isSI();
    for (int kind : {0, 1, 2, 3})
        defaults.append(kind, si);
    return defaults.layers;
}

constexpr auto kLids = SWMMModelLayer::DataLIDControls;
const QStringList kLayerKinds{"SURFACE", "MEDIA", "AGGREGATE", "BOTTOM"};

/*! A form value without trailing zeros ("6", "0.02"), at the field's precision. */
class FieldSpinBox final : public QDoubleSpinBox
{
public:
    using QDoubleSpinBox::QDoubleSpinBox;
    QString textFromValue(double value) const override
    {
        QLocale numbers = locale();
        numbers.setNumberOptions(numbers.numberOptions() | QLocale::OmitGroupSeparator);
        QString text = numbers.toString(value, 'f', decimals());
        const QString point = numbers.decimalPoint();
        if (text.contains(point)) {
            while (text.endsWith(QLatin1Char('0')))
                text.chop(1);
            if (text.endsWith(point))
                text.chop(point.size());
        }
        return text;
    }
};

/*! Ordered layer titles of a NODE record, top to bottom ("SURFACE 1", "MEDIA 1", ...). */
QStringList layerTitles(const QVariantMap &record)
{
    QStringList titles;
    QHash<int, int> count;
    for (const auto &kind : record.value("_kinds").toList()) {
        const int k = qBound(0, kind.toInt(), 3);
        titles << kLayerKinds[k] + QLatin1Char(' ') + QString::number(++count[k]);
    }
    return titles;
}

/*! A shared writer label split into its form tab and row: "Soil — Porosity" is row
 *  "Porosity" of tab "Soil"; "MEDIA 1 / TSS — Removal (%)" is row "TSS — Removal (%)" of
 *  tab "MEDIA 1"; a label without a group goes to "General". */
QPair<QString, QString> formPlace(const QString &label)
{
    const int dash = label.indexOf(QStringLiteral(" — "));
    if (dash < 0)
        return {QCoreApplication::translate("LidControlEditorDialog", "General"), label};
    QString tab = label.left(dash), row = label.mid(dash + 3);
    if (const int slash = tab.indexOf(QStringLiteral(" / ")); slash >= 0) {
        row = tab.mid(slash + 3) + QStringLiteral(" — ") + row;
        tab = tab.left(slash);
    }
    return {tab, row};
}

/*! One Apply step for a control's draft: forward writes \p after over \p before, backward
 *  the reverse, through the shared writer (a whole-profile step when layers moved). */
DataObjectDraftSession::Edit lidRecordEdit(LidControlRegistry *registry, SWMMModelLayer *layer,
                                           LidControlProvider *live, QVariantMap before,
                                           QVariantMap after, bool layersMoved,
                                           std::shared_ptr<QString> refusal)
{
    return {[registry = QPointer<LidControlRegistry>(registry),
             layer = QPointer<SWMMModelLayer>(layer), live = QPointer<LidControlProvider>(live),
             name = live->name(), before = std::move(before), after = std::move(after),
             layersMoved, refusal](bool forward) {
        if (!registry || !layer)
            return false;
        LidControlProvider *p = stepTarget(registry.data(), live, name);
        if (!p)
            return false;
        const QVariantMap &from = forward ? before : after;
        const QVariantMap &to = forward ? after : before;
        QString why;
        const bool ok =
            layersMoved
                ? DataObjectAttributeTableModel::applyValues(
                      layer, kLids, {p->name()}, QStringLiteral("_nodeProfile"),
                      QVariantMap{{"before", from}, {"after", to}}, nullptr, &why)
                : DataObjectAttributeTableModel::applyRecord(layer, kLids, p->name(), from, to,
                                                             nullptr, &why);
        if (!ok && refusal && refusal->isEmpty())
            *refusal = why;
        return ok;
    }};
}
} // namespace

LidControlEditorDialog::LidControlEditorDialog(LidControlRegistry *registry, SWMMModelLayer *layer,
                                               QWidget *parent)
    : QDialog(parent), m_registry(registry), m_layer(layer)
{
    setWindowTitle(tr("LID Controls"));
    resize(1180, 760);
    if (m_layer) buildLiveUi_(); else buildUi_();

    if (m_layer)
        connect(m_layer, &SWMMModelLayer::engineAboutToClose, this, [this] {
            m_layerDraftDirty = false; m_drafts.clear(); m_record.clear();
            m_current = nullptr; m_registry = nullptr;
            bindProvider_(nullptr); setEnabled(false);
            if (m_session) m_session->noteEdited();
        });

    if (m_registry) {
        connect(m_registry, &LidControlRegistry::providerParamsChanged, this,
                [this](LidControlProvider *p) {
                    if (m_draftMode) followModel_(p);
                    else if (p == m_current && !m_suppressFieldSync && !m_layerDraftDirty)
                        bindProvider_(p);
                });
        connect(m_registry, &LidControlRegistry::providerRenamed, this,
                &LidControlEditorDialog::onProviderRenamed_);
        // A second modeless editor can change the selected control's type.
        // Rebind after its type and initial ordered layers have both been set.
        connect(m_registry, &LidControlRegistry::providerTypeChanged, this,
                [this](LidControlProvider *p) {
                    if (m_draftMode) followModel_(p);
                    else if (p == m_current && (!m_typeCombo || m_typeCombo->currentIndex() != p->type()))
                        bindProvider_(p);
                }, Qt::QueuedConnection);
        if (m_draftMode)
            connect(m_registry, &LidControlRegistry::providerAboutToBeRemoved, this,
                    [this](LidControlProvider *p) {
                        if (m_drafts.remove(p)) m_session->noteEdited();
                    });
        m_listModel->setRegistry(m_registry);
        if (m_registry->providerCount() > 0)
            selectProviderInList_(m_registry->providers().first());
        else
            bindProvider_(nullptr);
    }
}

LidControlEditorDialog::~LidControlEditorDialog() = default;

void LidControlEditorDialog::openForLidControl(const QString &name)
{
    show();
    raise();
    activateWindow();
    if (!m_registry || name.isEmpty())
        return;
    if (auto *p = m_registry->findByName(name))
        selectProviderInList_(p);
}

QString LidControlEditorDialog::pickLidControl(LidControlRegistry *registry, SWMMModelLayer *layer,
                                               const QString &initialName, QWidget *parent)
{
    if (!registry || !layer)
        return {};
    LidControlEditorDialog dialog(registry, layer, parent);
    if (auto *provider = registry->findByName(initialName))
        dialog.selectProviderInList_(provider);
    // "Use LID Control" closes with the selection; with unapplied changes it reads
    // "Apply and Use" and applies them first (a refusal keeps the picker open).
    QPushButton *use = dialog.m_footer->closeButton();
    const auto label = [&dialog, use] {
        use->setText(dialog.m_session->hasDraft() ? tr("Apply and Use") : tr("Use LID Control"));
    };
    label();
    connect(dialog.m_session, &DataObjectDraftSession::changed, use, label);
    disconnect(dialog.m_footer, &DataObjectDraftFooter::closeRequested, &dialog, nullptr);
    connect(dialog.m_footer, &DataObjectDraftFooter::closeRequested, &dialog, [&dialog] {
        if (dialog.m_session->hasDraft() && !dialog.m_session->apply())
            return;
        dialog.accept();
    });
    if (dialog.exec() != QDialog::Accepted || !dialog.currentProvider())
        return {};
    return dialog.currentProvider()->name();
}

LidControlProvider *LidControlEditorDialog::currentProvider() const noexcept
{
    return m_current.data();
}

void LidControlEditorDialog::buildLiveUi_()
{
    setObjectName("LidControlEditorDialog");
    m_draftMode = true;
    auto *outer = new QVBoxLayout(this);
    auto *notice = new QLabel(tr("Changes are held until you choose Apply, which writes them to the model as one undoable step. New controls start with example dimensions that should be reviewed."), this);
    notice->setWordWrap(true); outer->addWidget(notice);
    m_splitter = new QSplitter(this); m_splitter->setObjectName("main");
    m_splitter->setChildrenCollapsible(false); outer->addWidget(m_splitter, 1);
    auto *left = new QWidget(m_splitter); auto *leftLayout = new QVBoxLayout(left);
    m_listView = new QListView(left); m_listModel = new LidControlListModel(this);
    // Rows whose control holds an unapplied draft read in italics.
    m_draftMarks = new DraftMarkProxyModel(this);
    m_draftMarks->setSourceModel(m_listModel);
    m_draftMarks->setHasDraft([this](int row) { return m_drafts.contains(m_listModel->providerAt(row)); });
    m_listView->setModel(m_draftMarks); leftLayout->addWidget(m_listView, 1);
    m_addBtn = new QPushButton(tr("New standard LID"), left);
    m_delBtn = new QPushButton(tr("Delete"), left);
    auto *layered = new QPushButton(tr("New layered LID"), left); layered->setObjectName("newLayeredLid");
    leftLayout->addWidget(m_addBtn); leftLayout->addWidget(layered); leftLayout->addWidget(m_delBtn);
    auto *history = new QHBoxLayout;
    m_undoBtn = new QPushButton(tr("Undo"), left); m_undoBtn->setObjectName("lidUndo");
    m_redoBtn = new QPushButton(tr("Redo"), left); m_redoBtn->setObjectName("lidRedo");
    history->addWidget(m_undoBtn); history->addWidget(m_redoBtn); leftLayout->addLayout(history);
    connect(m_undoBtn, &QPushButton::clicked, this, [this] { if (auto *u = undoStack_()) u->undo(); });
    connect(m_redoBtn, &QPushButton::clicked, this, [this] { if (auto *u = undoStack_()) u->redo(); });
    auto *middle = new QWidget(m_splitter); auto *middleLayout = new QVBoxLayout(middle);
    m_nameEdit = new QLineEdit(middle); m_nameEdit->setObjectName("lidControlName");
    auto *nameLayout = new QFormLayout; nameLayout->addRow(tr("Name"), m_nameEdit); middleLayout->addLayout(nameLayout);
    // The shared writer's fields for the shown control (those the Properties panel edits).
    m_fieldTabs = new QTabWidget(middle); m_fieldTabs->setObjectName("lidDraftFields");
    middleLayout->addWidget(m_fieldTabs, 1);
    auto *right = new QWidget(m_splitter); auto *rightLayout = new QVBoxLayout(right);
    m_liveLayers = new QListWidget(right); m_liveLayers->setObjectName("lidLiveLayerOrder");
    rightLayout->addWidget(new QLabel(tr("Ordered layers (top to bottom)"), right)); rightLayout->addWidget(m_liveLayers);
    auto *kind = new QComboBox(right); kind->setObjectName("lidInsertKind");
    kind->addItems(kLayerKinds); rightLayout->addWidget(kind);
    auto *actions = new QHBoxLayout;
    for (const auto &item : QList<QPair<QString, QString>>{{tr("Insert"), "insert"}, {tr("Remove"), "remove"}, {tr("Up"), "up"}, {tr("Down"), "down"}}) {
        auto *button = new QPushButton(item.first, right); button->setObjectName("lidLayer_" + item.second); actions->addWidget(button);
        if (item.second == "insert") button->setToolTip(tr("Insert a starter layer before the selected row. Under Richards, a porous layer also receives the example material values stated by Initialize materials."));
        connect(button, &QPushButton::clicked, this, [this, kind, op = item.second] {
            int row = m_liveLayers->currentRow();
            if (op == "insert" && row < 0) row = m_liveLayers->count();
            layerOperation_(op, row, kind->currentIndex());
        });
    }
    rightLayout->addLayout(actions);
    auto *materials = new QPushButton(tr("Initialize materials"), right); materials->setObjectName("lidInitializeMaterials");
    materials->setToolTip(tr("Set every porous layer to residual water 0.02, alpha 1/m, n 1.6, connectivity 0.5 and specific storage 0.0001/m."));
    rightLayout->addWidget(materials);
    connect(materials, &QPushButton::clicked, this, [this] { layerOperation_("materials", 0, 0); });
    m_liveStatus = new QLabel(right); m_liveStatus->setObjectName("lidDraftStatus"); m_liveStatus->setWordWrap(true); rightLayout->addWidget(m_liveStatus);
    m_diagram = new openswmmvis::sectionview::SectionPreviewWidget(right); rightLayout->addWidget(m_diagram, 2);
    m_retentionCurves = new RetentionCurves(right); rightLayout->addWidget(m_retentionCurves, 1);
    connect(m_liveLayers, &QListWidget::currentRowChanged, this, [this](int row) {
        showLayerTab_(row);
        refreshLivePreview_();
    });
    m_splitter->setSizes({190, 570, 420});

    m_session = new DataObjectDraftSession(
        {[this] { commitOpenEditors_(); }, [this] { return int(m_drafts.size()); },
         [this](QString *why) { return applyDrafts_(why); }, [this] { revertDrafts_(); }},
        this);
    connect(m_session, &DataObjectDraftSession::changed, m_draftMarks, &DraftMarkProxyModel::refreshMarks);
    m_session->setModel(m_layer); // Save / Run / Close ask about unapplied drafts
    m_footer = new DataObjectDraftFooter(m_session, this);
    connect(m_footer, &DataObjectDraftFooter::closeRequested, this, &QDialog::accept);
    outer->addWidget(m_footer);
    setAccessibleDescription(tr("Changes are held until you choose Apply, which writes them to the model as one undoable step."));
    bindUndoButtons_();

    connect(m_listView->selectionModel(), &QItemSelectionModel::currentChanged, this, &LidControlEditorDialog::onListSelectionChanged_);
    connect(m_addBtn, &QPushButton::clicked, this, &LidControlEditorDialog::onAddClicked_);
    connect(layered, &QPushButton::clicked, this, &LidControlEditorDialog::addLayeredControl_);
    connect(m_delBtn, &QPushButton::clicked, this, &LidControlEditorDialog::onDeleteClicked_);
    connect(m_nameEdit, &QLineEdit::editingFinished, this, &LidControlEditorDialog::onNameEdited_);
    connect(m_layer, &SWMMModelLayer::attributeChanged, this, [this](const QString &name) {
        if (m_registry) followModel_(m_registry->findByName(name));
    });
}

void LidControlEditorDialog::bindLiveProvider_(LidControlProvider *p)
{
    {
        const QSignalBlocker nameSignals(m_nameEdit);
        m_nameEdit->setEnabled(p); m_nameEdit->setText(p ? p->name() : QString());
    }
    m_delBtn->setEnabled(p);
    m_record = p ? shownRecord_(p) : QVariantMap{};
    if (p && m_record.isEmpty())
        m_liveStatus->setText(tr("%1 cannot be read from the model now (it cannot be edited while a simulation runs).").arg(p->name()));
    refreshForm_(true);
    refreshLiveLayers_();
    refreshLivePreview_();
}

QVariantMap LidControlEditorDialog::shownRecord_(LidControlProvider *p) const
{
    const auto draft = m_drafts.constFind(p);
    if (draft != m_drafts.cend())
        return draft->record;
    return m_layer && p ? DataObjectAttributeTableModel::lidProfile(m_layer, p->name()) : QVariantMap{};
}

void LidControlEditorDialog::showLayerTab_(int row)
{
    // The selected layer's fields are in the tab of the same title.
    if (auto *item = m_liveLayers->item(row))
        for (int i = 0; i < m_fieldTabs->count(); ++i)
            if (m_fieldTabs->tabText(i) == item->text()) m_fieldTabs->setCurrentIndex(i);
}

void LidControlEditorDialog::refreshLiveLayers_(int select)
{
    const QSignalBlocker rows(m_liveLayers);
    const int selected = select >= 0 ? select : m_liveLayers->currentRow();
    m_liveLayers->clear();
    const bool node = m_current && m_record.value("type").toInt() == 8;
    if (node) {
        m_liveLayers->addItems(layerTitles(m_record));
        m_liveLayers->setCurrentRow(qBound(0, selected, m_liveLayers->count() - 1));
    }
    m_liveLayers->setEnabled(node);
    if (node && select >= 0)
        showLayerTab_(m_liveLayers->currentRow());
    for (auto *button : findChildren<QPushButton *>())
        if (button->objectName().startsWith("lidLayer_") || button->objectName() == "lidInitializeMaterials") button->setEnabled(node);
}

QWidget *LidControlEditorDialog::makeFieldEditor_(const QString &key, const QString &label, int editor,
                                                  const QVariantList &choices, double minimum, double maximum,
                                                  int decimals, bool editable, const QString &tip)
{
    QWidget *widget = nullptr;
    switch (static_cast<openswmmvis::EditorKind>(editor)) {
    case openswmmvis::EditorKind::Enum: {
        auto *combo = new QComboBox;
        for (const auto &choice : choices) combo->addItem(choice.toList().value(0).toString(), choice.toList().value(1));
        connect(combo, &QComboBox::currentIndexChanged, this, [this, combo, key](int i) { fieldEdited_(key, combo->itemData(i)); });
        widget = combo;
        break;
    }
    case openswmmvis::EditorKind::Integer: {
        auto *spin = new QSpinBox;
        spin->setRange(int(qMax(-1.e9, minimum)), int(qMin(1.e9, maximum)));
        spin->setKeyboardTracking(false);
        connect(spin, &QSpinBox::valueChanged, this, [this, key](int v) { fieldEdited_(key, v); });
        widget = spin;
        break;
    }
    case openswmmvis::EditorKind::Text: {
        auto *edit = new QLineEdit;
        connect(edit, &QLineEdit::editingFinished, this, [this, edit, key] { fieldEdited_(key, edit->text()); });
        widget = edit;
        break;
    }
    default: {
        auto *spin = new FieldSpinBox;
        spin->setDecimals(qBound(0, decimals, 12));
        spin->setRange(qMax(-1.e12, minimum), qMin(1.e12, maximum));
        spin->setKeyboardTracking(false);
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this, key](double v) { fieldEdited_(key, v); });
        widget = spin;
    }
    }
    widget->setObjectName(QStringLiteral("lidField:") + key);
    widget->setAccessibleName(label);
    widget->setToolTip(tip);
    widget->setEnabled(editable);
    return widget;
}

void LidControlEditorDialog::refreshForm_(bool rebuild)
{
    if (!m_fieldTabs)
        return;
    const auto fields = m_current && m_layer && !m_record.isEmpty()
        ? DataObjectAttributeTableModel::lidDraftFields(m_layer, m_current->name(), m_record)
        : QList<DataObjectAttributeTableModel::RecordField>{};
    QStringList keys;
    for (const auto &f : fields) if (f.applies) keys << f.spec.key;
    if (rebuild || keys != m_formKeys) {
        const QString shownTab = m_fieldTabs->currentIndex() >= 0 ? m_fieldTabs->tabText(m_fieldTabs->currentIndex()) : QString();
        // Old editors live until deleteLater runs; a late signal must not reach the draft.
        for (QWidget *editor : std::as_const(m_fieldEditors))
            disconnect(editor, nullptr, this, nullptr);
        while (m_fieldTabs->count() > 0) {
            QWidget *page = m_fieldTabs->widget(0);
            m_fieldTabs->removeTab(0);
            page->setParent(nullptr); // out of the dialog now (deleted later: a caller may be in it)
            page->deleteLater();
        }
        m_fieldEditors.clear();
        m_formKeys = keys;
        // Tabs in field order, a NODE control's layer tabs in stack order.
        QStringList tabs; QHash<QString, QFormLayout *> forms;
        const QStringList layers = layerTitles(m_record);
        QList<QPair<QString, const DataObjectAttributeTableModel::RecordField *>> placed;
        for (const auto &f : fields) {
            if (!f.applies) continue;
            const auto place = formPlace(f.spec.label);
            if (!tabs.contains(place.first)) tabs << place.first;
            placed.append({place.second, &f});
        }
        std::stable_sort(tabs.begin(), tabs.end(), [&layers](const QString &a, const QString &b) {
            return layers.indexOf(a) < layers.indexOf(b); // non-layer tabs (-1) first, in field order
        });
        for (const auto &tab : tabs) {
            auto *scroll = new QScrollArea; scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
            auto *page = new QWidget; forms.insert(tab, new QFormLayout(page));
            scroll->setWidget(page);
            m_fieldTabs->addTab(scroll, tab);
        }
        for (const auto &[row, f] : placed) {
            QWidget *editor = makeFieldEditor_(f->spec.key, f->spec.label, int(f->spec.editor), f->spec.enumValues,
                                               f->spec.minValue, f->spec.maxValue, f->spec.decimals, f->editable,
                                               f->editable ? f->spec.tooltip : f->reason);
            forms.value(formPlace(f->spec.label).first)->addRow(row, editor);
            m_fieldEditors.insert(f->spec.key, editor);
        }
        for (int i = 0; i < m_fieldTabs->count(); ++i)
            if (m_fieldTabs->tabText(i) == shownTab) m_fieldTabs->setCurrentIndex(i);
    }
    const QScopedValueRollback<bool> syncing(m_suppressFieldSync, true);
    for (auto it = m_fieldEditors.cbegin(); it != m_fieldEditors.cend(); ++it) {
        const QVariant value = m_record.value(it.key());
        const QSignalBlocker block(it.value());
        if (auto *combo = qobject_cast<QComboBox *>(it.value())) combo->setCurrentIndex(combo->findData(value));
        else if (auto *spin = qobject_cast<QSpinBox *>(it.value())) spin->setValue(value.toInt());
        else if (auto *real = qobject_cast<QDoubleSpinBox *>(it.value())) real->setValue(value.toDouble());
        else if (auto *edit = qobject_cast<QLineEdit *>(it.value())) edit->setText(value.toString());
    }
}

void LidControlEditorDialog::fieldEdited_(const QString &key, const QVariant &value)
{
    if (m_suppressFieldSync || !m_current || !m_layer || m_record.isEmpty())
        return;
    QVariantMap after; QString error;
    if (!DataObjectAttributeTableModel::proposeRecord(m_layer, kLids, m_current->name(), key, value, m_record, &after, &error)) {
        m_liveStatus->setText(error.isEmpty() ? tr("The value was not accepted.") : error);
        QTimer::singleShot(0, this, [this] { refreshForm_(false); }); // show the draft's value again
        return;
    }
    m_liveStatus->clear();
    setDraftRecord_(after, false);
    // Later: the form may rebuild (a field's rules changed), which deletes this editor.
    QTimer::singleShot(0, this, [this] { refreshForm_(false); refreshLiveLayers_(); refreshLivePreview_(); });
}

void LidControlEditorDialog::layerOperation_(const QString &operation, int row, int kind)
{
    if (!m_current || !m_layer || m_record.isEmpty()) return;
    QVariantMap after, checked; QString error;
    if (!DataObjectAttributeTableModel::lidProfileOperation(m_layer, m_record, operation, row, kind, &after)) {
        m_liveStatus->setText(tr("This layer operation is unavailable."));
        return;
    }
    // The same whole-profile check the model applies (stack order, materials, outlets).
    if (!DataObjectAttributeTableModel::proposeRecord(m_layer, kLids, m_current->name(), QStringLiteral("_nodeProfile"),
                                                      QVariantMap{{"before", m_record}, {"after", after}}, m_record, &checked, &error)) {
        m_liveStatus->setText(error.isEmpty() ? tr("This layer operation is unavailable.") : error);
        return;
    }
    setDraftRecord_(checked, operation != QLatin1String("materials"));
    m_liveStatus->setText(tr("Changed the draft; Apply writes it to the model."));
    const int count = checked.value("_kinds").toList().size();
    const int select = operation == "up" ? row - 1 : operation == "down" ? row + 1
        : operation == "remove" ? qMin(row, count - 1) : operation == "insert" ? row : -1;
    refreshForm_(false);
    refreshLiveLayers_(select < 0 ? -1 : qMax(0, select));
    refreshLivePreview_();
}

void LidControlEditorDialog::setDraftRecord_(const QVariantMap &record, bool layersMoved)
{
    if (!m_current) return;
    const auto draft = m_drafts.constFind(m_current);
    const bool had = draft != m_drafts.cend();
    const QVariantMap base = had ? draft->base : m_record;
    const bool moved = layersMoved || (had && draft->layersMoved);
    if (record == base)
        m_drafts.remove(m_current); // edited back to the model's values
    else
        m_drafts.insert(m_current, {base, record, moved});
    m_record = record;
    m_session->noteEdited();
}

void LidControlEditorDialog::rebaseDrafts_()
{
    if (!m_layer) return;
    for (auto it = m_drafts.begin(); it != m_drafts.end(); ++it) {
        const QVariantMap now = DataObjectAttributeTableModel::lidProfile(m_layer, it.key()->name());
        if (now.isEmpty() || now == it->base) continue;
        const bool node = it->record.value("type").toInt() == 8;
        if (node && (now.value("_kinds") != it->base.value("_kinds") || now.value("type") != it->base.value("type")))
            continue; // the layers changed here and elsewhere: Apply refuses; Revert starts again
        QVariantMap edited = now;
        if (it->layersMoved) {
            if (now.value("_pollutants") != it->base.value("_pollutants") || now.value("_species") != it->base.value("_species"))
                continue;
            for (auto k = edited.begin(); k != edited.end();)
                k = k.key().startsWith("node/") ? edited.erase(k) : std::next(k);
            for (auto f = it->record.cbegin(); f != it->record.cend(); ++f)
                if (f.key().startsWith("node/") || f.key() == "_kinds") edited[f.key()] = f.value();
        }
        for (auto f = it->record.cbegin(); f != it->record.cend(); ++f)
            if (!f.key().startsWith('_') && !(it->layersMoved && f.key().startsWith("node/")) &&
                it->base.value(f.key()) != f.value() && now.contains(f.key()))
                edited[f.key()] = f.value();
        *it = {now, edited, it->layersMoved};
    }
    if (m_current && m_drafts.contains(m_current))
        m_record = m_drafts.value(m_current).record;
}

bool LidControlEditorDialog::applyDrafts_(QString *reason)
{
    rebaseDrafts_(); // the edits go on what the model holds now
    QVector<DataObjectDraftSession::Edit> edits;
    QStringList names;
    const auto refusal = std::make_shared<QString>();
    for (auto it = m_drafts.cbegin(); it != m_drafts.cend(); ++it) {
        if (it->record == it->base) continue;
        names << it.key()->name();
        edits.append(lidRecordEdit(m_registry.data(), m_layer.data(), it.key(), it->base, it->record, it->layersMoved, refusal));
    }
    m_applying = true;
    const bool ok = edits.isEmpty() || DataObjectDraftSession::commit(
        edits, undoStack_(),
        names.size() == 1 ? tr("Edit LID control %1").arg(names.first())
                          : tr("Edit %n LID control(s)", nullptr, int(names.size())));
    m_applying = false;
    if (!ok) {
        *reason = refusal->isEmpty()
            ? tr("the model did not accept the values for %1; nothing was changed").arg(names.join(QStringLiteral(", ")))
            : *refusal;
        return false;
    }
    m_drafts.clear();
    bindProvider_(m_current);
    return true;
}

void LidControlEditorDialog::revertDrafts_()
{
    m_drafts.clear();
    m_liveStatus->clear();
    bindProvider_(m_current);
}

void LidControlEditorDialog::commitOpenEditors_()
{
    // Spin boxes and text fields commit on Enter or focus loss: hand over a value still typed.
    for (QWidget *w : std::as_const(m_fieldEditors)) {
        if (!w->hasFocus()) continue;
        if (auto *real = qobject_cast<QDoubleSpinBox *>(w)) real->interpretText();
        else if (auto *spin = qobject_cast<QSpinBox *>(w)) spin->interpretText();
        else if (auto *edit = qobject_cast<QLineEdit *>(w)) emit edit->editingFinished();
    }
    if (m_nameEdit && m_nameEdit->hasFocus()) onNameEdited_();
}

void LidControlEditorDialog::followModel_(LidControlProvider *p)
{
    if (m_applying || !p || !m_session) return;
    if (m_drafts.contains(p)) {
        m_session->noteExternalChange();
        return;
    }
    if (p == m_current)
        QTimer::singleShot(0, this, [this] {
            if (!m_drafts.contains(m_current)) bindProvider_(m_current);
        });
}

QUndoStack *LidControlEditorDialog::undoStack_() const
{
    if (m_undo) return m_undo;
    return m_layer && m_layer->editCanvas() ? m_layer->editCanvas()->undoStack() : nullptr;
}

void LidControlEditorDialog::setUndoStack(QUndoStack *stack)
{
    m_undo = stack;
    bindUndoButtons_();
}

void LidControlEditorDialog::bindUndoButtons_()
{
    if (!m_undoBtn) return;
    for (const auto &c : std::as_const(m_undoWatch)) disconnect(c);
    m_undoWatch.clear();
    QUndoStack *stack = undoStack_();
    m_undoBtn->setEnabled(stack && stack->canUndo());
    m_redoBtn->setEnabled(stack && stack->canRedo());
    if (!stack) return;
    m_undoWatch << connect(stack, &QUndoStack::canUndoChanged, m_undoBtn, &QPushButton::setEnabled)
                << connect(stack, &QUndoStack::canRedoChanged, m_redoBtn, &QPushButton::setEnabled);
}

void LidControlEditorDialog::refreshLivePreview_()
{
    namespace sv = openswmmvis::sectionview;
    auto *curves = static_cast<RetentionCurves *>(m_retentionCurves);
    curves->layerLabel.clear(); curves->material = {}; curves->porosity = 0;
    const auto &r = m_record;
    if (!m_current || !m_layer || r.isEmpty()) { m_diagram->setModel({}); curves->update(); return; }
    sv::LidDiagramInput in; in.name = m_current->name(); in.type = static_cast<sv::LidType>(r["type"].toInt());
    int units = 0; swmm_get_flow_units(m_layer->engine(), &units); in.lengthLabel = units >= 3 ? "mm" : "in";
    if (r["type"].toInt() == 8) {
        // The draft's layers, top to bottom (BOTTOM is a boundary, not drawn).
        const QStringList titles = layerTitles(r);
        const auto kinds = r["_kinds"].toList();
        QHash<int, int> count;
        for (int row = 0; row < kinds.size(); ++row) {
            const int k = kinds[row].toInt();
            const auto prefix = QStringLiteral("node/%1/%2/").arg(k).arg(++count[k]);
            auto value = [&](const char *member) { return r[prefix + member].toDouble(); };
            if (k != 3)
                in.orderedLayers << sv::LidDiagramInput::OrderedLayer{k == 0 ? sv::LidLayer::Surface : k == 1 ? sv::LidLayer::Soil : sv::LidLayer::Storage,
                    value("p/0"), k == 0 ? 1 - value("p/1") : value("p/1"), k == 1 ? value("p/4") : k == 2 ? value("p/2") : 0.};
            if (row == m_liveLayers->currentRow() && (k == 1 || k == 2)) {
                curves->layerLabel = titles.value(row);
                curves->material = {value("r/0"), value("r/1"), value("r/2"), value("r/3"), value("r/4")};
                curves->porosity = value("p/1");
            }
        }
    } else {
        in.surfaceStorage = r["surfStorage"].toDouble(); in.surfaceRoughness = r["surfRoughness"].toDouble(); in.surfaceSlope = r["surfSlope"].toDouble();
        in.soilThickness = r["soilThick"].toDouble(); in.soilPorosity = r["soilPorosity"].toDouble(); in.soilConductivity = r["soilKsat"].toDouble();
        in.storageThickness = r["storThick"].toDouble(); in.storageVoidFrac = r["storVoidFrac"].toDouble(); in.storageSeepage = r["storKsat"].toDouble();
        in.drainCoeff = r["drainCoeff"].toDouble(); in.drainExponent = r["drainExpon"].toDouble(); in.drainOffset = r["drainOffset"].toDouble();
        in.pavementThickness = r["paveThick"].toDouble(); in.drainmatThickness = r["matThick"].toDouble();
    }
    m_diagram->setModel(sv::buildLidLayerDiagram(in));
    curves->setVisible(r["type"].toInt() == 8 && r["flowModel"].toInt() == 1); curves->update();
}

void LidControlEditorDialog::buildUi_()
{
    auto *outer = new QVBoxLayout(this);
    m_splitter = new QSplitter(Qt::Horizontal, this);
    // Iteration 2 (D2) — naming wires the app-wide layout persistence.
    setObjectName(QStringLiteral("LidControlEditorDialog"));
    m_splitter->setObjectName(QStringLiteral("main"));
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(6);

    // ── Left pane ───────────────────────────────────────────────────────────
    auto *leftPane = new QWidget(m_splitter);
    auto *leftLay = new QVBoxLayout(leftPane);
    leftLay->setContentsMargins(0, 0, 0, 0);
    m_listView = new QListView(leftPane);
    m_listModel = new LidControlListModel(this);
    m_listView->setModel(m_listModel);
    m_listView->setEditTriggers(QAbstractItemView::DoubleClicked |
                                QAbstractItemView::EditKeyPressed);
    leftLay->addWidget(m_listView, 1);

    auto *btnRow = new QHBoxLayout;
    m_addBtn = new QPushButton(openswmmvis::ui::IconFactory::icon(QStringLiteral("Add")), tr("New"),
                               leftPane);
    m_delBtn = new QPushButton(openswmmvis::ui::IconFactory::icon(QStringLiteral("Delete")),
                               tr("Delete"), leftPane);
    btnRow->addWidget(m_addBtn);
    btnRow->addWidget(m_delBtn);
    leftLay->addLayout(btnRow);
    auto *newLayered = new QPushButton(tr("New layered LID"), leftPane);
    newLayered->setObjectName(QStringLiteral("newLayeredLid"));
    newLayered->setToolTip(
        tr("Create a storage-node LID with any number of media and aggregate layers."));
    leftLay->addWidget(newLayered);
    connect(newLayered, &QPushButton::clicked, this, &LidControlEditorDialog::addLayeredControl_);

    // ── Right pane: name + type + layer tabs ────────────────────────────────
    auto *rightPane = new QWidget(m_splitter);
    auto *rightLay = new QVBoxLayout(rightPane);
    rightLay->setContentsMargins(0, 0, 0, 0);

    auto *headForm = new QFormLayout;
    m_nameEdit = new QLineEdit(rightPane);
    headForm->addRow(tr("N&ame"), m_nameEdit);
    m_typeCombo = new QComboBox(rightPane);
    m_typeCombo->setObjectName(QStringLiteral("lidControlType"));
    for (const char *t : {"Bio-Retention Cell", "Rain Garden", "Green Roof", "Infiltration Trench",
                          "Permeable Pavement", "Rain Barrel", "Rooftop Disconnection",
                          "Vegetative Swale", "Storage Node (ordered layers)"})
        m_typeCombo->addItem(QString::fromLatin1(t));
    headForm->addRow(tr("T&ype"), m_typeCombo);
    rightLay->addLayout(headForm);

    auto *tabs = new QTabWidget(rightPane);
    m_tabs = tabs;

    // Surface tab.
    auto *surf = new QWidget;
    auto *surfForm = new QFormLayout(surf);
    m_surfStorage = makeSpin(surf, 0.0, 1.0e6, 0.1);
    m_surfStorage->setObjectName(QStringLiteral("lidSurfaceStorage"));
    m_surfRough = makeSpin(surf, 0.0, 1.0, 0.01);
    m_surfSlope = makeSpin(surf, 0.0, 100.0, 0.1);
    surfForm->addRow(tr("Sto&rage Depth"), m_surfStorage);
    surfForm->addRow(tr("Ro&ughness (n)"), m_surfRough);
    surfForm->addRow(tr("Slope (%)"), m_surfSlope);
    tabs->addTab(OpenSWMM::Ui::wrapInScrollArea(surf, tabs), tr("&Surface"));

    // Soil tab.
    auto *soil = new QWidget;
    auto *soilForm = new QFormLayout(soil);
    m_soilThick = makeSpin(soil, 0.0, 1.0e6, 0.1);
    m_soilPoro = makeSpin(soil, 0.0, 1.0, 0.01);
    m_soilFc = makeSpin(soil, 0.0, 1.0, 0.01);
    m_soilWp = makeSpin(soil, 0.0, 1.0, 0.01);
    m_soilKsat = makeSpin(soil, 0.0, 1.0e6, 0.1);
    m_soilKslope = makeSpin(soil, 0.0, 1.0e6, 1.0);
    soilForm->addRow(tr("T&hickness"), m_soilThick);
    soilForm->addRow(tr("&Porosity"), m_soilPoro);
    soilForm->addRow(tr("&Field Capacity"), m_soilFc);
    soilForm->addRow(tr("&Wilting Point"), m_soilWp);
    soilForm->addRow(tr("&Conductivity"), m_soilKsat);
    soilForm->addRow(tr("Conduct&ivity Slope"), m_soilKslope);
    tabs->addTab(OpenSWMM::Ui::wrapInScrollArea(soil, tabs), tr("S&oil"));

    // Storage tab.
    auto *stor = new QWidget;
    auto *storForm = new QFormLayout(stor);
    m_storThick = makeSpin(stor, 0.0, 1.0e6, 0.1);
    m_storVoid = makeSpin(stor, 0.0, 1.0e6, 0.01);
    m_storKsat = makeSpin(stor, 0.0, 1.0e6, 0.1);
    storForm->addRow(tr("Thic&kness"), m_storThick);
    storForm->addRow(tr("&Void Ratio"), m_storVoid);
    storForm->addRow(tr("S&eepage Rate"), m_storKsat);
    tabs->addTab(OpenSWMM::Ui::wrapInScrollArea(stor, tabs), tr("S&torage"));

    // Drain tab.
    auto *drain = new QWidget;
    auto *drainForm = new QFormLayout(drain);
    m_drainCoeff = makeSpin(drain, 0.0, 1.0e6, 0.1);
    m_drainExpon = makeSpin(drain, 0.0, 100.0, 0.1);
    m_drainOffset = makeSpin(drain, 0.0, 1.0e6, 0.1);
    drainForm->addRow(tr("Coefficient"), m_drainCoeff);
    drainForm->addRow(tr("E&xponent"), m_drainExpon);
    drainForm->addRow(tr("Offset"), m_drainOffset);
    tabs->addTab(OpenSWMM::Ui::wrapInScrollArea(drain, tabs), tr("&Drain"));

    m_nodeLayerPage = new QWidget(tabs);
    auto *stackLayout = new QVBoxLayout(m_nodeLayerPage);
    auto *countForm = new QFormLayout;
    m_mediaCount = new QSpinBox(m_nodeLayerPage);
    m_mediaCount->setObjectName(QStringLiteral("lidMediaLayerCount"));
    m_mediaCount->setRange(1, std::numeric_limits<int>::max());
    m_mediaCount->setKeyboardTracking(false);
    countForm->addRow(tr("Media / aggregate layers"), m_mediaCount);
    m_flowModel = new QComboBox(m_nodeLayerPage);
    m_flowModel->setObjectName("lidFlowModel");
    m_flowModel->addItems({tr("Existing formulation (with backwater)"), tr("Richards 1D")});
    countForm->addRow(tr("Flow model"), m_flowModel);
    m_richardsBoundaryNotice = new QLabel(
        tr("Richards supports sealed bottoms, native soil drainage, and a mesh aquifer connection. Set bottom ownership and contact area in the storage node’s Bottom connection editor. Mesh coupling uses the CPU water-only solver."),
        m_nodeLayerPage);
    m_richardsBoundaryNotice->setWordWrap(true);
    m_richardsBoundaryNotice->setVisible(false);
    countForm->addRow(m_richardsBoundaryNotice);
    auto *numericsGroup = new QGroupBox(tr("Numerical settings"), m_nodeLayerPage);
    numericsGroup->setObjectName("lidRichardsNumerics");
    numericsGroup->setCheckable(true);
    numericsGroup->setChecked(false);
    auto *numericsPage = new QWidget(numericsGroup);
    auto *numericsForm = new QFormLayout(numericsPage);
    auto *numericsLayout = new QVBoxLayout(numericsGroup);
    numericsLayout->addWidget(numericsPage);
    numericsPage->setVisible(false);
    connect(numericsGroup, &QGroupBox::toggled, numericsPage, &QWidget::setVisible);
    m_richardsSettings = numericsGroup;
    m_richardsCells = new QSpinBox(m_nodeLayerPage);
    m_richardsCells->setRange(1, 256);
    m_richardsCells->setValue(8);
    m_richardsCells->setObjectName("lidRichardsCells");
    numericsForm->addRow(tr("Numerical cells per porous layer"), m_richardsCells);
    m_richardsAtol = makeSpin(m_nodeLayerPage, 1.e-12, .1, 1.e-7);
    m_richardsAtol->setDecimals(12);
    m_richardsAtol->setValue(1.e-7);
    m_richardsRtol = makeSpin(m_nodeLayerPage, 1.e-12, .1, 1.e-5);
    m_richardsRtol->setDecimals(12);
    m_richardsRtol->setValue(1.e-5);
    m_richardsMaxStep = makeSpin(m_nodeLayerPage, .001, 86400, 1);
    m_richardsMaxStep->setValue(30);
    numericsForm->addRow(tr("Absolute water-content tolerance"), m_richardsAtol);
    numericsForm->addRow(tr("Relative tolerance"), m_richardsRtol);
    numericsForm->addRow(tr("Maximum internal step (s)"), m_richardsMaxStep);
    auto flowChanged = [this] {
        if (!m_suppressFieldSync)
            m_layerDraftDirty = true;
        const bool richards = m_flowModel->currentIndex() == 1;
        m_richardsSettings->setVisible(richards);
        m_richardsBoundaryNotice->setVisible(richards);
        if (m_layerDetails)
            m_layerDetails->setTabVisible(1, richards);
        for (auto *widget :
             {static_cast<QWidget *>(m_richardsCells), static_cast<QWidget *>(m_richardsAtol),
              static_cast<QWidget *>(m_richardsRtol), static_cast<QWidget *>(m_richardsMaxStep)})
            widget->setEnabled(richards);
        for (int col = 10; col < 15; ++col)
            m_nodeLayerTable->setColumnHidden(col, !richards);
        refreshLayerFields_();
    };
    m_layerSummary = new QLabel(m_nodeLayerPage);
    countForm->addRow(m_layerSummary);
    stackLayout->addLayout(countForm);
    stackLayout->addWidget(numericsGroup);
    auto *units = new QLabel(tr(
        "Top to bottom. Thickness and suction: mm (SI) or in (US). Conductivity: mm/hr or in/hr. BOTTOM is the native-soil boundary."));
    units->setWordWrap(true);
    stackLayout->addWidget(units);
    m_nodeLayerModel = new LidNodeLayerModel(this);
    m_nodeLayerTable = new QTableView(m_nodeLayerPage);
    m_nodeLayerTable->setObjectName(QStringLiteral("lidNodeLayers"));
    m_nodeLayerTable->setModel(m_nodeLayerModel);
    m_nodeLayerTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_nodeLayerTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    stackLayout->addWidget(m_nodeLayerTable, 1);
    auto *details = new QTabWidget(m_nodeLayerPage);
    m_layerDetails = details;
    auto *retentionPage = new QWidget(details);
    auto *retentionLayout = new QHBoxLayout(retentionPage);
    auto *retentionInputs = new QWidget(retentionPage);
    m_retentionFields = new QFormLayout(retentionInputs);
    m_retentionFields->setContentsMargins(0, 0, 0, 0);
    m_retentionFields->setRowWrapPolicy(QFormLayout::WrapLongRows);
    auto *physicalPage = new QWidget(details);
    m_layerFields = new QFormLayout(physicalPage);
    for (int column = 1; column < 15; ++column) {
        auto *field = makeSpin(m_nodeLayerPage, 0.0, 1.e12, 0.1);
        field->setDecimals(6);
        if (column >= 10)
            field->setDecimals(10);
        field->setObjectName(QStringLiteral("lidLayerParameter%1").arg(column));
        m_layerValues[column - 1] = field;
        (column >= 10 ? m_retentionFields : m_layerFields)
            ->addRow(m_nodeLayerModel->headerData(column, Qt::Horizontal).toString(), field);
        connect(field, &QDoubleSpinBox::valueChanged, this, [this, column](double value) {
            if (!m_syncLayerFields)
                m_nodeLayerModel->setData(
                    m_nodeLayerModel->index(m_nodeLayerTable->currentIndex().row(), column), value);
        });
    }
    details->addTab(OpenSWMM::Ui::wrapInScrollArea(physicalPage, details),
                    tr("Physical properties"));
    // Scroll the fields independently so the complete plot stays beside them as the pane narrows.
    auto *inputsScroll = OpenSWMM::Ui::wrapInScrollArea(retentionInputs, retentionPage);
    inputsScroll->setMinimumWidth(retentionInputs->minimumSizeHint().width() +
                                  inputsScroll->style()->pixelMetric(QStyle::PM_ScrollBarExtent));
    retentionLayout->addWidget(inputsScroll);
    m_retentionCurves = new RetentionCurves(retentionPage);
    retentionLayout->addWidget(m_retentionCurves, 1);
    details->addTab(retentionPage, tr("Retention and conductivity"));
    auto *treatmentPage = new QWidget(details);
    auto *treatmentLayout = new QVBoxLayout(treatmentPage);
    auto *treatmentHelp = new QLabel(
        tr("Treatment for the selected layer. Removal acts on outgoing pollutant mass. Decay is a first-order rate per day. An optional R = or C = expression acts after removal; leave it blank for rates only."),
        treatmentPage);
    treatmentHelp->setWordWrap(true);
    treatmentLayout->addWidget(treatmentHelp);
    m_treatmentTable = new QTableView(treatmentPage);
    m_treatmentTable->setObjectName(QStringLiteral("lidLayerTreatment"));
    m_treatmentModel = new QStandardItemModel(0, 4, this);
    m_treatmentModel->setHorizontalHeaderLabels(
        {tr("Pollutant"), tr("Removal (%)"), tr("Decay (1/day)"), tr("Expression")});
    m_treatmentTable->setModel(m_treatmentModel);
    m_treatmentTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_treatmentTable->setAlternatingRowColors(true);
    m_treatmentTable->setEditTriggers(QAbstractItemView::DoubleClicked |
                                      QAbstractItemView::SelectedClicked |
                                      QAbstractItemView::EditKeyPressed);
    auto *rateDelegate = new TreatmentRateDelegate(m_treatmentTable);
    m_treatmentTable->setItemDelegateForColumn(1, rateDelegate);
    m_treatmentTable->setItemDelegateForColumn(2, rateDelegate);
    m_treatmentTable->setToolTip(
        tr("Double-click a rate or expression to edit. Use Ctrl+Space for expression completion."));
    m_treatmentTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_treatmentTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Interactive);
    auto *expressionDelegate = new TreatmentExpressionDelegate(
        m_registry ? m_registry->engineHandle() : nullptr, m_treatmentTable);
    m_treatmentTable->setItemDelegateForColumn(3, expressionDelegate);
    treatmentLayout->addWidget(m_treatmentTable);
    m_treatmentMessage = new QLabel(treatmentPage);
    m_treatmentMessage->setWordWrap(true);
    treatmentLayout->addWidget(m_treatmentMessage);
    connect(expressionDelegate, &TreatmentExpressionDelegate::validationChanged, this,
            [this](bool valid, const QString &message, int) {
                m_treatmentMessage->setText(valid ? tr("Expression is valid.") : message);
            });
    auto *treatmentButtons = new QHBoxLayout;
    auto *addTreatment = new QPushButton(tr("Add pollutant"), treatmentPage);
    auto *removeTreatment = new QPushButton(tr("Remove pollutant"), treatmentPage);
    treatmentButtons->addWidget(addTreatment);
    treatmentButtons->addWidget(removeTreatment);
    treatmentButtons->addStretch();
    treatmentLayout->addLayout(treatmentButtons);
    details->addTab(treatmentPage, tr("Pollutant treatment"));
    stackLayout->addWidget(details, 1);
    connect(m_flowModel, &QComboBox::currentIndexChanged, this, flowChanged);
    connect(m_richardsCells, &QSpinBox::valueChanged, this, flowChanged);
    for (auto *field : {m_richardsAtol, m_richardsRtol, m_richardsMaxStep})
        connect(field, &QDoubleSpinBox::valueChanged, this, flowChanged);
    connect(m_treatmentModel, &QStandardItemModel::dataChanged, this,
            [this] { storeTreatmentRows_(); });
    connect(addTreatment, &QPushButton::clicked, this, [this] {
        const int row = m_nodeLayerTable->currentIndex().row();
        if (row < 0 || m_nodeLayerModel->layers[row].kind == 3)
            return;
        auto eng = m_registry ? static_cast<SWMM_Engine>(m_registry->engineHandle()) : nullptr;
        QStringList names;
        if (eng)
            for (int p = 0; p < swmm_pollutant_count(eng); ++p) {
                const QString name = QString::fromUtf8(swmm_pollutant_id(eng, p));
                bool used = false;
                for (int j = 0; j < m_treatmentModel->rowCount(); ++j)
                    used |= m_treatmentModel->index(j, 0).data().toString() == name;
                if (!used)
                    names.append(name);
            }
        if (names.isEmpty()) {
            m_treatmentMessage->setText(tr(
                "Define pollutants in Model → Pollutant first, or edit the pollutants already listed."));
            return;
        }
        bool ok = false;
        const QString name = QInputDialog::getItem(this, tr("Layer treatment"), tr("Pollutant"),
                                                   names, 0, false, &ok);
        if (!ok)
            return;
        auto *id = new QStandardItem(name);
        id->setEditable(false);
        m_treatmentModel->appendRow({id, new QStandardItem(QStringLiteral("0")),
                                     new QStandardItem(QStringLiteral("0")), new QStandardItem});
        storeTreatmentRows_();
    });
    connect(removeTreatment, &QPushButton::clicked, this, [this] {
        m_treatmentModel->removeRow(m_treatmentTable->currentIndex().row());
        storeTreatmentRows_();
    });
    connect(m_nodeLayerTable->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this] {
                refreshLayerFields_();
                refreshTreatmentRows_();
            });
    connect(m_mediaCount, &QSpinBox::valueChanged, this, [this](int count) {
        if (!m_syncLayerFields)
            m_nodeLayerModel->setMediaCount(count, UnitSystem::instance() &&
                                                       UnitSystem::instance()->isSI());
    });
    auto changedLayers = [this] {
        if (!m_suppressFieldSync)
            m_layerDraftDirty = true;
        refreshLayerFields_();
        refreshTreatmentRows_();
        refreshLayerDiagram_();
    };
    connect(m_nodeLayerModel, &QAbstractItemModel::rowsInserted, this, changedLayers);
    connect(m_nodeLayerModel, &QAbstractItemModel::rowsRemoved, this, changedLayers);
    connect(m_nodeLayerModel, &QAbstractItemModel::rowsMoved, this, changedLayers);
    connect(m_nodeLayerModel, &QAbstractItemModel::modelReset, this, changedLayers);
    auto *rowActions = new QHBoxLayout;
    for (int kind = 0; kind < 4; ++kind) {
        auto *add = new QPushButton(
            tr("Add %1").arg(QStringList{"SURFACE", "MEDIA", "AGGREGATE", "BOTTOM"}[kind]));
        rowActions->addWidget(add);
        connect(add, &QPushButton::clicked, this, [this, kind] {
            m_nodeLayerModel->append(kind,
                                     UnitSystem::instance() && UnitSystem::instance()->isSI());
            const auto &rows = m_nodeLayerModel->layers;
            int selected = kind == 0 ? 0 : rows.size() - 1;
            if (kind != 3 && kind != 0 && !rows.isEmpty() && rows.back().kind == 3)
                --selected;
            m_nodeLayerTable->selectRow(selected);
            refreshLayerDiagram_();
        });
    }
    stackLayout->addLayout(rowActions);
    auto *editActions = new QHBoxLayout;
    auto *remove = new QPushButton(tr("Remove"));
    auto *up = new QPushButton(tr("Move up"));
    auto *down = new QPushButton(tr("Move down"));
    auto *apply = new QPushButton(tr("Apply layers and treatment"));
    for (auto *b : {remove, up, down, apply})
        editActions->addWidget(b);
    stackLayout->addLayout(editActions);
    connect(remove, &QPushButton::clicked, this, [this] {
        m_nodeLayerModel->remove(m_nodeLayerTable->currentIndex().row());
        refreshLayerDiagram_();
    });
    for (auto pair : {qMakePair(up, -1), qMakePair(down, 1)})
        connect(pair.first, &QPushButton::clicked, this, [this, delta = pair.second] {
            const int r = m_nodeLayerTable->currentIndex().row();
            m_nodeLayerModel->move(r, delta);
            m_nodeLayerTable->selectRow(qBound(0, r + delta, m_nodeLayerModel->rowCount() - 1));
            refreshLayerDiagram_();
        });
    connect(m_nodeLayerModel, &QAbstractItemModel::dataChanged, this, [this] {
        if (!m_suppressFieldSync)
            m_layerDraftDirty = true;
        refreshLayerFields_();
        refreshLayerDiagram_();
    });
    connect(apply, &QPushButton::clicked, this, [this] { applyLayers_(); });
    tabs->addTab(m_nodeLayerPage, tr("Ordered layers"));
    rightLay->addWidget(tabs, 1);

    auto *note = new QLabel(
        tr("NODE controls use the ordered layer table. Apply layers updates every assigned storage node and its anchored outlets."),
        rightPane);
    note->setWordWrap(true);
    note->setEnabled(false);
    rightLay->addWidget(note);

    // Slice SP.6 — third pane: the layer-stack diagram. Which layers exist is
    // a function of the LID type (a rain barrel has no soil, a green roof has
    // a drainage mat instead of storage), so the drawing is the fastest way to
    // see that the type and the numbers agree.
    m_diagram = new openswmmvis::sectionview::SectionPreviewWidget(m_splitter);
    m_diagram->setObjectName(QStringLiteral("lidLayerDiagram"));
    m_diagram->setPlaceholderText(tr("Select or create a LID control."));
    flowChanged();

    m_splitter->addWidget(leftPane);
    m_splitter->addWidget(rightPane);
    m_splitter->addWidget(m_diagram);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 2);
    m_splitter->setStretchFactor(2, 2);
    m_splitter->setSizes({170, 650, 300});

    outer->addWidget(m_splitter, 1);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    outer->addWidget(bb);

    // ── Wiring ──────────────────────────────────────────────────────────────
    connect(m_listView->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            &LidControlEditorDialog::onListSelectionChanged_);
    connect(m_addBtn, &QPushButton::clicked, this, &LidControlEditorDialog::onAddClicked_);
    connect(m_delBtn, &QPushButton::clicked, this, &LidControlEditorDialog::onDeleteClicked_);
    connect(m_nameEdit, &QLineEdit::editingFinished, this, &LidControlEditorDialog::onNameEdited_);
    connect(m_typeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &LidControlEditorDialog::onTypeEdited_);
    // The active tab drives which layer the diagram highlights; it changes no
    // data, so it refreshes the drawing directly rather than via onFieldEdited_.
    connect(tabs, &QTabWidget::currentChanged, this, [this](int) { refreshLayerDiagram_(); });
    for (QDoubleSpinBox *s : {m_surfStorage, m_surfRough, m_surfSlope, m_soilThick, m_soilPoro,
                              m_soilFc, m_soilWp, m_soilKsat, m_soilKslope, m_storThick, m_storVoid,
                              m_storKsat, m_drainCoeff, m_drainExpon, m_drainOffset})
        connect(s, &QDoubleSpinBox::valueChanged, this, &LidControlEditorDialog::onFieldEdited_);
}

bool LidControlEditorDialog::applyLayers_()
{
    if (!m_current || !m_registry)
        return false;
    auto eng = static_cast<SWMM_Engine>(m_registry->engineHandle());
    if (!eng)
        return false;
    int idx = swmm_lid_index(eng, m_current->name().toUtf8().constData());
    const bool created = idx < 0;
    if (idx < 0) {
        if (swmm_lid_add(eng, m_current->name().toUtf8().constData(), 8) != SWMM_OK)
            return false;
        idx = swmm_lid_index(eng, m_current->name().toUtf8().constData());
    }
    const auto &rows = m_nodeLayerModel->layers;
    const auto treatment = m_nodeLayerModel->treatmentRows();
    QVector<QByteArray> expressions;
    expressions.reserve(treatment.size());
    QVector<SWMM_LidLayerTreatment> rules;
    rules.reserve(treatment.size());
    for (const auto &t : treatment) {
        expressions.append(t.expression.toUtf8());
        rules.append({t.layer, swmm_pollutant_index(eng, t.pollutant.toUtf8().constData()),
                      t.removal, t.decay, expressions.back().constData()});
    }
    const SWMM_LidRichardsOptions flow{m_flowModel->currentIndex(), m_richardsCells->value(),
                                       m_richardsAtol->value(), m_richardsRtol->value(),
                                       m_richardsMaxStep->value()};
    if (swmm_lid_node_configure_flow(eng, idx, rows.constData(), rows.size(), rules.constData(),
                                     rules.size(), &flow,
                                     m_nodeLayerModel->retention.constData()) != SWMM_OK) {
        if (created)
            swmm_lid_delete(eng, idx, nullptr);
        QMessageBox::warning(
            this, tr("Invalid layer stack"),
            tr("Check layer order, parameters and outlet anchors. Richards requires SURFACE first and explicit retention parameters on every porous layer: residual content below porosity (and media wilting point), alpha > 0, n > 1, l ≥ 0, and specific storage > 0. Alpha and specific storage use 1/m in both unit systems. Treatment requires known pollutants and valid rates/expressions."));
        return false;
    }
    m_current->treatments = treatment;
    m_current->setNodeLayers(rows);
    m_current->setNodeFlow(flow, m_nodeLayerModel->retention);
    m_current->clearDirty();
    m_layerDraftDirty = false;
    m_typeCombo->setEnabled(false);
    m_typeCombo->setToolTip(tr("This control has an applied ordered layer stack."));
    if (m_layer)
        m_layer->markEdited();
    return true;
}
bool LidControlEditorDialog::finishLayerDraft_()
{
    if (!m_layerDraftDirty || !m_current || m_current->type() != 8)
        return true;
    const auto answer = QMessageBox::question(
        this, tr("Unsaved layer changes"),
        tr("Apply the layer and treatment changes to %1?").arg(m_current->name()),
        QMessageBox::Apply | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Apply);
    if (answer == QMessageBox::Cancel)
        return false;
    if (answer == QMessageBox::Apply)
        return applyLayers_();
    m_layerDraftDirty = false;
    return true;
}
void LidControlEditorDialog::done(int result)
{
    if (m_draftMode) {
        if (m_registry && m_session && !m_session->resolveBeforeClose())
            return; // Cancel, or an Apply the model refused
        QDialog::done(result);
        return;
    }
    if (finishLayerDraft_())
        QDialog::done(result);
}

void LidControlEditorDialog::bindProvider_(LidControlProvider *p)
{
    m_current = p;
    if (m_draftMode) { bindLiveProvider_(p); return; }

    const bool prev = m_suppressFieldSync;
    m_suppressFieldSync = true;

    const bool enabled = (p != nullptr);
    m_nameEdit->setEnabled(enabled);
    for (QDoubleSpinBox *s : {m_surfStorage, m_surfRough, m_surfSlope, m_soilThick, m_soilPoro,
                              m_soilFc, m_soilWp, m_soilKsat, m_soilKslope, m_storThick, m_storVoid,
                              m_storKsat, m_drainCoeff, m_drainExpon, m_drainOffset})
        s->setEnabled(enabled);

    if (p) {
        m_nameEdit->setText(p->name());
        m_typeCombo->setCurrentIndex(p->type() >= 0 && p->type() <= 8 ? p->type() : 0);
        m_surfStorage->setValue(p->surfStorage());
        m_surfRough->setValue(p->surfRoughness());
        m_surfSlope->setValue(p->surfSlope());
        m_soilThick->setValue(p->soilThick());
        m_soilPoro->setValue(p->soilPorosity());
        m_soilFc->setValue(p->soilFc());
        m_soilWp->setValue(p->soilWp());
        m_soilKsat->setValue(p->soilKsat());
        m_soilKslope->setValue(p->soilKslope());
        m_storThick->setValue(p->storThick());
        m_storVoid->setValue(p->storVoidFrac());
        m_storKsat->setValue(p->storKsat());
        m_drainCoeff->setValue(p->drainCoeff());
        m_drainExpon->setValue(p->drainExpon());
        m_drainOffset->setValue(p->drainOffset());
    } else {
        m_nameEdit->clear();
    }

    m_nodeLayerModel->setLayers(p ? p->nodeLayers() : QVector<SWMM_LidNodeLayer>{});
    if (p && p->retention.size() == m_nodeLayerModel->layers.size())
        m_nodeLayerModel->retention = p->retention;
    const auto flow = p ? p->flowOptions : SWMM_LidRichardsOptions{0, 8, 1.e-7, 1.e-5, 30};
    m_flowModel->setCurrentIndex(flow.model);
    m_richardsSettings->setVisible(flow.model == 1);
    m_richardsBoundaryNotice->setVisible(flow.model == 1);
    m_layerDetails->setTabVisible(1, flow.model == 1);
    m_richardsCells->setValue(flow.cells_per_layer);
    m_richardsAtol->setValue(flow.atol);
    m_richardsRtol->setValue(flow.rtol);
    m_richardsMaxStep->setValue(flow.max_step);
    for (int col = 10; col < 15; ++col)
        m_nodeLayerTable->setColumnHidden(col, flow.model != 1);
    m_nodeLayerModel->setTreatments(p ? p->treatments
                                      : QVector<openswmmvis::lid::LidLayerTreatment>{});
    const bool node = p && p->type() == 8;
    for (int i = 0; i < 4; ++i)
        m_tabs->setTabVisible(i, !node);
    m_tabs->setTabVisible(4, node);
    if (node) {
        m_tabs->setCurrentIndex(4);
        m_nodeLayerTable->selectRow(0);
    }
    m_nodeLayerPage->setEnabled(node);
    const auto eng = m_registry ? static_cast<SWMM_Engine>(m_registry->engineHandle()) : nullptr;
    const bool appliedStack =
        node && eng &&
        swmm_lid_node_layer_count(eng, swmm_lid_index(eng, p->name().toUtf8().constData())) > 0;
    m_typeCombo->setEnabled(enabled && !appliedStack);
    m_typeCombo->setToolTip(appliedStack ? tr("This control has an applied ordered layer stack.")
                                         : QString());
    m_suppressFieldSync = prev;
    m_layerDraftDirty = false;
    refreshTreatmentRows_();
    refreshLayerDiagram_();
    // Binding a different control is a new subject; field edits keep the view.
    if (m_diagram)
        m_diagram->zoomToExtents();
}

void LidControlEditorDialog::selectProviderInList_(LidControlProvider *p)
{
    if (!p || !m_listModel) {
        bindProvider_(p);
        return;
    }
    const auto provs = m_registry ? m_registry->providers() : QVector<LidControlProvider *>{};
    const int row = provs.indexOf(p);
    if (row < 0) {
        bindProvider_(p);
        return;
    }
    const QModelIndex idx = viewIndexOf(m_listView, m_listModel->index(row));
    m_listView->selectionModel()->setCurrentIndex(idx, QItemSelectionModel::ClearAndSelect);
}

QString LidControlEditorDialog::suggestUniqueName_() const
{
    int n = m_registry ? m_registry->providerCount() + 1 : 1;
    QString candidate;
    do {
        candidate = QStringLiteral("LID%1").arg(n++);
    } while (m_registry && m_registry->hasName(candidate));
    return candidate;
}

void LidControlEditorDialog::onListSelectionChanged_()
{
    if (!m_draftMode && !finishLayerDraft_()) {
        QSignalBlocker blocker(m_listView->selectionModel());
        selectProviderInList_(m_current);
        return;
    }
    const QModelIndex idx = m_listView->selectionModel()->currentIndex();
    bindProvider_(idx.isValid() ? m_listModel->providerAt(idx.row()) : nullptr);
}

void LidControlEditorDialog::onAddClicked_()
{
    if (!m_registry)
        return;
    if (m_draftMode) {
        createControl_(suggestUniqueName_(), 0);
        return;
    }
    if (LidControlProvider *p = m_registry->create(suggestUniqueName_()))
        selectProviderInList_(p);
}

void LidControlEditorDialog::addLayeredControl_()
{
    if (!m_registry)
        return;
    if (m_draftMode) {
        createControl_(suggestUniqueName_(), 8);
        return;
    }
    auto *provider = m_registry->create(suggestUniqueName_(), 8);
    if (!provider)
        return;
    provider->setNodeLayers(defaultNodeLayers());
    selectProviderInList_(provider);
}

void LidControlEditorDialog::createControl_(const QString &name, int type)
{
    // One undo step: the control with its example design (plan D5).
    const auto make = [type](LidControlRegistry *r, const QString &n) { return r->create(n, type); };
    const auto setup = [layer = m_layer](LidControlProvider *p) { // a refusal removes it again
        p->clearDirty();
        return layer && DataObjectAttributeTableModel::initializeLidDesign(layer, p->name());
    };
    if (!DataObjectDraftSession::commit({createObjectEditWith(m_registry.data(), name, make, setup)},
                                        undoStack_(), tr("Create LID control %1").arg(name))) {
        m_liveStatus->setText(tr("The model did not create %1.").arg(name));
        return;
    }
    if (LidControlProvider *p = m_registry->findByName(name)) {
        m_session->noteApplied(tr("Created %1").arg(name));
        selectProviderInList_(p);
    }
}

void LidControlEditorDialog::refreshLayerFields_()
{
    if (!m_mediaCount || !m_layerFields)
        return;
    m_syncLayerFields = true;
    m_mediaCount->setValue(qMax(1, m_nodeLayerModel->mediaCount()));
    int physical = 0;
    for (const auto &row : m_nodeLayerModel->layers)
        if (row.kind != 3)
            ++physical;
    m_layerSummary->setText(
        tr("%1 physical layers · top to bottom. Select a row to edit its parameters below.")
            .arg(physical));
    const int row = m_nodeLayerTable->currentIndex().row();
    const bool si = UnitSystem::instance() && UnitSystem::instance()->isSI();
    const bool richards = m_flowModel && m_flowModel->currentIndex() == 1;
    for (int column = 1; column < 15; ++column) {
        const auto index = m_nodeLayerModel->index(row, column);
        const bool visible =
            index.isValid() && (m_nodeLayerModel->flags(index) & Qt::ItemIsEditable) &&
            (column < 10 || richards) && !(richards && (column == 6 || column == 7));
        auto *field = m_layerValues[column - 1];
        auto *form = column >= 10 ? m_retentionFields : m_layerFields;
        form->setRowVisible(field, visible);
        if (!visible)
            continue;
        QString label = m_nodeLayerModel->headerData(column, Qt::Horizontal).toString();
        if (richards && column == 3)
            label += tr(" (existing model)");
        if (column == 1 || column == 7)
            label += si ? tr(" (mm)") : tr(" (in)");
        if (column == 5)
            label += si ? tr(" (mm/hr)") : tr(" (in/hr)");
        qobject_cast<QLabel *>(form->labelForField(field))->setText(label);
        field->setValue(index.data(Qt::EditRole).toDouble());
    }
    auto *curves = static_cast<RetentionCurves *>(m_retentionCurves);
    const bool porous =
        row >= 0 && row < m_nodeLayerModel->layers.size() &&
        (m_nodeLayerModel->layers[row].kind == 1 || m_nodeLayerModel->layers[row].kind == 2);
    curves->setVisible(m_current && m_current->type() == 8 && richards);
    curves->layerLabel.clear();
    if (porous && row < m_nodeLayerModel->retention.size()) {
        curves->layerLabel = tr("Layer %1 · %2")
                                 .arg(row + 1)
                                 .arg(m_nodeLayerModel->index(row, 0).data().toString());
        curves->material = m_nodeLayerModel->retention[row];
        curves->porosity = m_nodeLayerModel->layers[row].params[1];
    }
    curves->update();
    m_syncLayerFields = false;
}

void LidControlEditorDialog::refreshTreatmentRows_()
{
    if (!m_treatmentModel)
        return;
    m_syncTreatment = true;
    m_treatmentModel->removeRows(0, m_treatmentModel->rowCount());
    const int row = m_nodeLayerTable->currentIndex().row();
    const bool valid = row >= 0 && row < m_nodeLayerModel->treatments.size() &&
                       m_nodeLayerModel->layers[row].kind != 3;
    m_treatmentTable->setEnabled(valid);
    if (valid)
        for (const auto &t : m_nodeLayerModel->treatments[row]) {
            auto *id = new QStandardItem(t.pollutant);
            id->setEditable(false);
            m_treatmentModel->appendRow({id, new QStandardItem(QString::number(t.removal, 'g', 15)),
                                         new QStandardItem(QString::number(t.decay, 'g', 15)),
                                         new QStandardItem(t.expression)});
        }
    m_treatmentMessage->setText(
        valid
            ? tr("Layer %1 — double-click a value to edit. Expressions use the existing treatment syntax and completion.")
                  .arg(row + 1)
            : tr("Select a physical layer. BOTTOM is a seepage boundary, not a treatment layer."));
    m_syncTreatment = false;
}
void LidControlEditorDialog::storeTreatmentRows_()
{
    if (m_syncTreatment)
        return;
    m_layerDraftDirty = true;
    const int row = m_nodeLayerTable->currentIndex().row();
    if (row < 0 || row >= m_nodeLayerModel->treatments.size())
        return;
    auto &rules = m_nodeLayerModel->treatments[row];
    rules.clear();
    for (int i = 0; i < m_treatmentModel->rowCount(); ++i) {
        bool r = false, k = false;
        const double removal = m_treatmentModel->index(i, 1).data().toDouble(&r),
                     decay = m_treatmentModel->index(i, 2).data().toDouble(&k);
        rules.append({row + 1, m_treatmentModel->index(i, 0).data().toString(),
                      r ? removal : std::numeric_limits<double>::quiet_NaN(),
                      k ? decay : std::numeric_limits<double>::quiet_NaN(),
                      m_treatmentModel->index(i, 3).data().toString()});
    }
}

void LidControlEditorDialog::onDeleteClicked_()
{
    if (!m_registry || !m_current)
        return;
    if (m_draftMode) {
        // Plan D8 — say what the engine detaches, and that Undo will not re-attach it.
        const QString removedName = m_current->name();
        QString text = tr("Delete LID control \"%1\"?").arg(removedName);
        if (m_layer && m_layer->engine()) {
            const QString uses = dataObjectUses(m_layer->engine(), swmm_lid_analyze_impact,
                                                swmm_lid_index(m_layer->engine(), removedName.toUtf8().constData()));
            if (!uses.isEmpty())
                text += QLatin1String("\n\n") + uses + QLatin1Char(' ') +
                        tr("Those uses are removed with it; Undo brings back the LID control and its "
                           "settings, but not those uses.");
        }
        if (QMessageBox::question(this, tr("Delete LID Control"), text) != QMessageBox::Yes || !m_registry || !m_current)
            return;
        // Undo makes the control again and writes the settings the model held (a draft is dropped).
        const QVariantMap kept = DataObjectAttributeTableModel::lidProfile(m_layer, removedName);
        const auto make = [type = kept.value("type").toInt()](LidControlRegistry *r, const QString &n) { return r->create(n, type); };
        const auto restore = [layer = m_layer, kept](LidControlProvider *p) {
            if (!layer) return false;
            p->clearDirty();
            const QVariantMap fresh = DataObjectAttributeTableModel::lidProfile(layer, p->name());
            return !fresh.isEmpty() && !kept.isEmpty() &&
                   DataObjectAttributeTableModel::applyRecord(layer, kLids, p->name(), fresh, kept, nullptr);
        };
        m_current = nullptr;
        if (!DataObjectDraftSession::commit({deleteObjectEditWith(m_registry.data(), removedName, make, restore)},
                                            undoStack_(), tr("Delete LID control %1").arg(removedName))) {
            bindProvider_(m_registry->findByName(removedName));
            return;
        }
        m_session->noteApplied(tr("Deleted %1").arg(removedName));
        if (m_registry->providerCount() > 0)
            selectProviderInList_(m_registry->providers().first());
        else
            bindProvider_(nullptr);
        return;
    }
    const auto answer = QMessageBox::question(
        this, tr("Delete LID Control"), tr("Delete LID control \"%1\"?").arg(m_current->name()));
    if (answer != QMessageBox::Yes)
        return;

    LidControlProvider *victim = m_current;
    m_current = nullptr;
    m_registry->remove(victim);
    if (m_registry->providerCount() > 0)
        selectProviderInList_(m_registry->providers().first());
    else
        bindProvider_(nullptr);
}

void LidControlEditorDialog::onNameEdited_()
{
    if (m_suppressFieldSync || !m_registry || !m_current)
        return;
    const QString newName = m_nameEdit->text().trimmed();
    if (newName.isEmpty() || newName == m_current->name())
        return;
    if (const QString why = openswmmvis::swmmIdentifierProblem(newName); !why.isEmpty()) {
        QMessageBox::warning(this, tr("Rename LID Control"), why); // feedback C10
        m_nameEdit->setText(m_current->name());
        return;
    }
    if (m_draftMode) {
        const QString from = m_current->name();
        if (!DataObjectDraftSession::commit({renameObjectEdit(m_registry.data(), from, newName)}, undoStack_(),
                                            tr("Rename LID control %1 to %2").arg(from, newName))) {
            QMessageBox::warning(this, tr("Rename LID Control"),
                                 tr("A LID control named \"%1\" already exists.").arg(newName));
            if (m_current) m_nameEdit->setText(m_current->name());
            return;
        }
        m_session->noteApplied(tr("Renamed to %1").arg(newName));
        return;
    }
    if (!m_registry->rename(m_current, newName)) {
        QMessageBox::warning(this, tr("Rename LID Control"),
                             tr("A LID control named \"%1\" already exists.").arg(newName));
        m_nameEdit->setText(m_current->name());
    }
}

void LidControlEditorDialog::onTypeEdited_(int type)
{
    if (m_suppressFieldSync || !m_current || !m_registry)
        return;
    if (m_current->type() == 8 && m_layerDraftDirty &&
        QMessageBox::question(this, tr("Change LID type"),
                              tr("Discard the unapplied layer edits and change type?"),
                              QMessageBox::Discard | QMessageBox::Cancel,
                              QMessageBox::Cancel) != QMessageBox::Discard) {
        const QSignalBlocker blocker(m_typeCombo);
        m_typeCombo->setCurrentIndex(m_current->type());
        return;
    }
    if (!m_registry->setType(m_current, type)) {
        const QSignalBlocker blocker(m_typeCombo);
        m_typeCombo->setCurrentIndex(m_current->type());
        QMessageBox::warning(this, tr("Change LID type"),
                             tr("The model could not change this LID type. A control in use or "
                                "with an applied ordered layer stack must keep its type."));
        return;
    }
    if (type == 8 && m_current->nodeLayers().isEmpty())
        m_current->setNodeLayers(defaultNodeLayers());
    bindProvider_(m_current);
}

void LidControlEditorDialog::onFieldEdited_()
{
    if (m_suppressFieldSync || !m_current || m_current->type() == 8)
        return;
    const QScopedValueRollback<bool> syncing(m_suppressFieldSync, true);
    m_current->setSurfStorage(m_surfStorage->value());
    m_current->setSurfRoughness(m_surfRough->value());
    m_current->setSurfSlope(m_surfSlope->value());
    m_current->setSoilThick(m_soilThick->value());
    m_current->setSoilPorosity(m_soilPoro->value());
    m_current->setSoilFc(m_soilFc->value());
    m_current->setSoilWp(m_soilWp->value());
    m_current->setSoilKsat(m_soilKsat->value());
    m_current->setSoilKslope(m_soilKslope->value());
    m_current->setStorThick(m_storThick->value());
    m_current->setStorVoidFrac(m_storVoid->value());
    m_current->setStorKsat(m_storKsat->value());
    m_current->setDrainCoeff(m_drainCoeff->value());
    m_current->setDrainExpon(m_drainExpon->value());
    m_current->setDrainOffset(m_drainOffset->value());
    refreshLayerDiagram_();
}

// Slice SP.6 — layer-stack diagram, built from the widgets rather than the
// provider so it tracks typing before the value is pushed to the model.
void LidControlEditorDialog::refreshLayerDiagram_()
{
    if (!m_diagram)
        return;

    namespace sv = openswmmvis::sectionview;

    if (!m_current) {
        m_diagram->setModel(sv::SectionDiagramModel{});
        return;
    }

    sv::LidDiagramInput in;
    in.name = m_nameEdit ? m_nameEdit->text() : QString();
    in.type = static_cast<sv::LidType>(m_typeCombo ? qBound(0, m_typeCombo->currentIndex(), 8) : 0);

    if (in.type == sv::LidType::Node) {
        for (const auto &row : m_nodeLayerModel->layers) {
            if (row.kind == 3)
                continue;
            const auto layer = row.kind == 0   ? sv::LidLayer::Surface
                               : row.kind == 1 ? sv::LidLayer::Soil
                                               : sv::LidLayer::Storage;
            in.orderedLayers.append({layer, row.params[0],
                                     row.kind == 0 ? 1.0 - row.params[1] : row.params[1],
                                     row.kind == 1   ? row.params[4]
                                     : row.kind == 2 ? row.params[2]
                                                     : 0.0});
        }
        in.lengthLabel = UnitSystem::instance() && UnitSystem::instance()->isSI()
                             ? QStringLiteral("mm")
                             : QStringLiteral("in");
        m_diagram->setModel(sv::buildLidLayerDiagram(in));
        return;
    }
    in.surfaceStorage = m_surfStorage->value();
    in.surfaceRoughness = m_surfRough->value();
    in.surfaceSlope = m_surfSlope->value();
    in.soilThickness = m_soilThick->value();
    in.soilPorosity = m_soilPoro->value();
    in.soilConductivity = m_soilKsat->value();
    in.storageThickness = m_storThick->value();
    in.storageVoidFrac = m_storVoid->value();
    in.storageSeepage = m_storKsat->value();
    in.drainCoeff = m_drainCoeff->value();
    in.drainExponent = m_drainExpon->value();
    in.drainOffset = m_drainOffset->value();

    if (auto *us = UnitSystem::instance())
        in.lengthLabel = us->lengthLabel();

    // Highlight the layer whose tab is in front. Tab order is fixed at build
    // time (Surface / Soil / Storage / Drain). Drain is not a layer, so it
    // leaves the explicit active-layer highlight unset.
    if (m_tabs) {
        switch (m_tabs->currentIndex()) {
        case 0:
            in.activeLayer = sv::LidLayer::Surface;
            in.hasActiveLayer = true;
            break;
        case 1:
            in.activeLayer = sv::LidLayer::Soil;
            in.hasActiveLayer = true;
            break;
        case 2:
            in.activeLayer = sv::LidLayer::Storage;
            in.hasActiveLayer = true;
            break;
        default:
            in.hasActiveLayer = false;
            break;
        }
    }

    m_diagram->setModel(sv::buildLidLayerDiagram(in));
}

void LidControlEditorDialog::onProviderRenamed_(LidControlProvider *p, const QString &,
                                                const QString &now)
{
    if (p == m_current && m_nameEdit->text() != now) {
        const bool prev = m_suppressFieldSync;
        m_suppressFieldSync = true;
        m_nameEdit->setText(now);
        m_suppressFieldSync = prev;
    }
}

void LidControlEditorDialog::invokeNew()
{
    onAddClicked_();
}

LidControlEditorDialog *LidControlEditorDialog::createNew(LidControlRegistry *registry,
                                                          SWMMModelLayer *layer, QWidget *parent)
{
    auto *dlg = new LidControlEditorDialog(registry, layer, parent);
    dlg->m_mode = Mode::CreateNew;
    dlg->setWindowTitle(tr("New LID Control"));
    dlg->invokeNew();
    return dlg;
}

} // namespace openswmmvis::ui
