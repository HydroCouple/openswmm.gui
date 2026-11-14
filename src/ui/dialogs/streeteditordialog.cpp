/**
 * @file      streeteditordialog.cpp
 * @brief     Mirrors TransectEditorDialog's role and entry points (createNew / pickStreet) but a
 *            street is parametric, so the middle pane is a field form (ten legacy TStreet
 *            parameters + sides) rather than a station table, and the right pane is a schematic
 *            section preview rather than a point chart.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/dialogs/streeteditordialog.h"
#include "ui/editors/dataobjectdraftsession.h"
#include "ui/editors/dataobjectstructureedit.h"
#include "ui/models/draftmarkproxymodel.h"
#include "ui/editors/paramsdraftedit.h"
#include "core/swmmidentifier.h"

#include "layers/swmmmodellayer.h" // QPointer<SWMMModelLayer> needs the complete type
#include "ui/models/streetlistmodel.h"
#include "street/streetprovider.h"
#include "street/streetregistry.h"
#include "ui/theme/iconfactory.h"

#include <QButtonGroup>
#include <QUndoStack>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QPaintEvent>
#include <QPainter>
#include <QList>
#include <QPolygonF>
#include <QPushButton>
#include <QRadioButton>
#include <QSplitter>
#include <QVBoxLayout>

#include <openswmm/engine/openswmm_infrastructure.h>

namespace openswmmvis::ui
{

using openswmmvis::street::StreetProvider;
using openswmmvis::street::StreetRegistry;

namespace
{
bool sameStreet(const StreetProvider::Params &now, const StreetProvider::Params &target)
{
    return now.sameAs(target, 1e-9);
}
} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// StreetSectionPreview
// ─────────────────────────────────────────────────────────────────────────────

StreetSectionPreview::StreetSectionPreview(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(220, 160);
    setAutoFillBackground(true);
}

void StreetSectionPreview::setProvider(StreetProvider *p)
{
    m_provider = p;
    if (p)
        m_shown = p->params();
    update();
}

void StreetSectionPreview::showParams(const StreetProvider::Params &values)
{
    m_shown = values;
    update();
}

void StreetSectionPreview::paintEvent(QPaintEvent *)
{
    QPainter pr(this);
    pr.setRenderHint(QPainter::Antialiasing, true);
    pr.fillRect(rect(), palette().base());

    if (!m_provider) {
        pr.setPen(palette().mid().color());
        pr.drawText(rect(), Qt::AlignCenter, tr("No street selected"));
        return;
    }

    // Geometry mirrors the engine's transect_createStreetTransect (street.c →
    // transect.c): the exact station/elevation points SWMM builds for a STREET
    // cross-section. Points are drawn with independent X/Y scaling (vertical
    // exaggeration) so the shallow profile stays legible while the topology and
    // proportions match the engine.
    const double w3 = qMax(0.0, m_shown.crownWidth);   // Tcrown
    const double w2 = qMax(0.0, m_shown.gutterWidth);  // W (gutter)
    const double w1 = qMax(0.0, m_shown.backingWidth); // Tback
    const double w4 = qMax(0.0, w3 - w2);
    const double slope = qMax(0.0, m_shown.crossSlope) / 100.0;       // Sx
    const double backSlope = qMax(0.0, m_shown.backingSlope) / 100.0; // Sback
    const double curb = qMax(0.0, m_shown.curbHeight);                // Hcurb
    const double dep = qMax(0.0, m_shown.gutterDepression);           // a (depression)
    const bool twoSide = (m_shown.sides == 2);

    const double y1 = curb + dep;                // top of curb
    const double y3 = dep + slope * w2;          // bottom of depressed gutter
    const double y4 = y3 + slope * w4;           // crown-side top of gutter
    double ymax = qMax(backSlope * w1 + y1, y4); // top of backing / crown
    if (ymax <= 0.0)
        ymax = qMax(curb, 0.001);

    // Station/elevation points, in engine order (P0..P4 left half; mirrored
    // P5..P8 for a two-sided street, else P5 closes the crown centerline).
    QList<QPointF> pts;
    pts.reserve(9);
    pts << QPointF(0.0, ymax)    // P0 top of backing
        << QPointF(w1, y1)       // P1 top of curb
        << QPointF(w1, 0.0)      // P2 bottom of curb (gutter invert)
        << QPointF(w1 + w2, y3)  // P3 bottom of depressed gutter
        << QPointF(w1 + w3, y4); // P4 crown (high road point)
    if (!twoSide) {
        pts << QPointF(w1 + w3, ymax); // P5 crown / centerline
    } else {
        const double s5 = w1 + w3 + w4;
        pts << QPointF(s5, y3)              // P5
            << QPointF(s5 + w2, 0.0)        // P6 gutter invert
            << QPointF(s5 + w2, y1)         // P7 top of curb
            << QPointF(s5 + w2 + w1, ymax); // P8 top of backing
    }

    // Map section coordinates to the widget (flip Y so elevation increases up).
    double xMax = pts.last().x();
    if (xMax <= 0.0)
        xMax = 1.0;
    double yTop = ymax;
    if (yTop <= 0.0)
        yTop = 1.0;

    const QRectF area = rect().adjusted(16, 22, -16, -16);
    const double sx = area.width() / xMax;
    const double sy = (area.height() - 4.0) / yTop;
    auto toWidget = [&](const QPointF &p) {
        return QPointF(area.left() + p.x() * sx, area.bottom() - p.y() * sy);
    };

    QPolygonF poly;
    for (const QPointF &p : pts)
        poly << toWidget(p);

    // Light fill of the pavement section for legibility.
    QPolygonF fill = poly;
    fill << QPointF(poly.last().x(), area.bottom()) << QPointF(poly.first().x(), area.bottom());
    QColor fillCol = palette().highlight().color();
    fillCol.setAlpha(40);
    pr.setPen(Qt::NoPen);
    pr.setBrush(fillCol);
    pr.drawPolygon(fill);

    // Profile line.
    QPen roadPen(palette().windowText().color());
    roadPen.setWidthF(2.0);
    pr.setBrush(Qt::NoBrush);
    pr.setPen(roadPen);
    pr.drawPolyline(poly);

    // Ground reference line at elevation 0 (gutter invert).
    QPen groundPen(palette().mid().color());
    groundPen.setStyle(Qt::DashLine);
    pr.setPen(groundPen);
    pr.drawLine(QPointF(area.left(), area.bottom()), QPointF(area.right(), area.bottom()));

    pr.setPen(palette().windowText().color());
    pr.drawText(rect().adjusted(6, 4, -6, -4), Qt::AlignTop | Qt::AlignHCenter,
                tr("%1  (vertical exaggeration)").arg(m_provider->name()));
}

// ─────────────────────────────────────────────────────────────────────────────
// StreetEditorDialog
// ─────────────────────────────────────────────────────────────────────────────

StreetEditorDialog::StreetEditorDialog(StreetRegistry *registry, SWMMModelLayer *layer,
                                       QWidget *parent)
    : QDialog(parent), m_registry(registry), m_layer(layer)
{
    setWindowTitle(tr("Street Cross-Sections"));
    resize(720, 420);
    buildUi_();

    setAccessibleDescription(tr("Changes are held until you choose Apply, which writes them to "
                                "the model as one undoable step."));
    m_listView->setAccessibleName(tr("Street designs"));
    if (m_registry)
        connect(m_registry, &QObject::destroyed, this, &StreetEditorDialog::invalidateContext);
    if (m_layer) {
        connect(m_layer, &QObject::destroyed, this, &StreetEditorDialog::invalidateContext);
        connect(m_layer, SIGNAL(engineAboutToClose()), this, SLOT(invalidateContext()));
    }

    if (m_registry) {
        connect(m_registry, &StreetRegistry::providerAdded, this,
                &StreetEditorDialog::onProviderAdded_);
        connect(m_registry, &StreetRegistry::providerRenamed, this,
                &StreetEditorDialog::onProviderRenamed_);
        // Changed from another view (or an Undo): with a draft that is a
        // conflict to resolve at Apply, otherwise the form follows the model.
        connect(m_registry, &StreetRegistry::providerParamsChanged, this, [this](StreetProvider *p) {
            if (m_applying)
                return;
            if (m_drafts.contains(p))
                m_session->noteExternalChange();
            else if (p == m_current)
                bindProvider_(p);
        });
        connect(m_registry, &StreetRegistry::providerAboutToBeRemoved, this,
                [this](StreetProvider *p) {
                    if (m_drafts.remove(p))
                        m_session->noteEdited();
                });
        m_listModel->setRegistry(m_registry);
        if (m_registry->providerCount() > 0)
            selectProviderInList_(m_registry->providers().first());
        else
            bindProvider_(nullptr);
    }
}

StreetEditorDialog::~StreetEditorDialog() = default;

void StreetEditorDialog::invalidateContext()
{
    if (!m_contextValid)
        return;
    m_contextValid = false;
    if (m_registry)
        disconnect(m_registry, nullptr, this, nullptr);
    if (m_layer)
        disconnect(m_layer, nullptr, this, nullptr);
    m_registry = nullptr;
    m_layer = nullptr;
    m_drafts.clear(); // the model is going away: nothing left to apply to
    bindProvider_(nullptr);
    m_listModel->setRegistry(nullptr);
    setEnabled(false);
    reject();
}

void StreetEditorDialog::openForStreet(const QString &name)
{
    show();
    raise();
    activateWindow();
    if (!m_registry || name.isEmpty())
        return;
    if (auto *p = m_registry->findByName(name))
        selectProviderInList_(p);
}

StreetProvider *StreetEditorDialog::currentProvider() const noexcept
{
    return m_current.data();
}

void StreetEditorDialog::buildUi_()
{
    auto *outer = new QVBoxLayout(this);
    m_splitter = new QSplitter(Qt::Horizontal, this);
    // Iteration 2 (D2) — naming wires the app-wide layout persistence.
    setObjectName(QStringLiteral("StreetEditorDialog"));
    m_splitter->setObjectName(QStringLiteral("main"));
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(6);

    // ── Left pane: list + add/delete ────────────────────────────────────────
    auto *leftPane = new QWidget(m_splitter);
    auto *leftLay = new QVBoxLayout(leftPane);
    leftLay->setContentsMargins(0, 0, 0, 0);
    m_listView = new QListView(leftPane);
    m_listModel = new StreetListModel(this);
    // Rows whose object holds an unapplied draft read in italics.
    m_draftMarks = new DraftMarkProxyModel(this);
    m_draftMarks->setSourceModel(m_listModel);
    m_draftMarks->setHasDraft(
        [this](int row) { return m_drafts.contains(m_listModel->providerAt(row)); });
    m_listView->setModel(m_draftMarks);
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

    // ── Middle pane: field form ─────────────────────────────────────────────
    auto *midPane = new QWidget(m_splitter);
    auto *form = new QFormLayout(midPane);

    m_nameEdit = new QLineEdit(midPane);
    form->addRow(tr("N&ame"), m_nameEdit);

    auto makeSpin = [midPane](double maxV, int decimals, double step) {
        auto *s = new QDoubleSpinBox(midPane);
        s->setRange(0.0, maxV);
        s->setDecimals(decimals);
        s->setSingleStep(step);
        return s;
    };
    m_crownWidthSpin = makeSpin(1.0e6, 4, 0.5);
    m_curbHeightSpin = makeSpin(1.0e6, 4, 0.1);
    m_crossSlopeSpin = makeSpin(1.0e3, 4, 0.5);
    m_roadRoughSpin = makeSpin(1.0, 4, 0.001);
    m_gutterDepSpin = makeSpin(1.0e6, 4, 0.1);
    m_gutterWidthSpin = makeSpin(1.0e6, 4, 0.1);
    m_backWidthSpin = makeSpin(1.0e6, 4, 0.1);
    m_backSlopeSpin = makeSpin(1.0e3, 4, 0.5);
    m_backRoughSpin = makeSpin(1.0, 4, 0.001);

    form->addRow(tr("&Road Width"), m_crownWidthSpin);
    form->addRow(tr("&Curb Height"), m_curbHeightSpin);
    form->addRow(tr("Cross Slope (%)"), m_crossSlopeSpin);
    form->addRow(tr("R&oad Roughness (n)"), m_roadRoughSpin);
    form->addRow(tr("&Gutter Depression"), m_gutterDepSpin);
    form->addRow(tr("G&utter Width"), m_gutterWidthSpin);

    m_oneSidedRadio = new QRadioButton(tr("One Sided"), midPane);
    m_twoSidedRadio = new QRadioButton(tr("Two Sided"), midPane);
    auto *sidesGroup = new QButtonGroup(this);
    sidesGroup->addButton(m_oneSidedRadio, 1);
    sidesGroup->addButton(m_twoSidedRadio, 2);
    auto *sidesRow = new QHBoxLayout;
    sidesRow->setContentsMargins(0, 0, 0, 0);
    sidesRow->addWidget(m_oneSidedRadio);
    sidesRow->addWidget(m_twoSidedRadio);
    sidesRow->addStretch(1);
    auto *sidesWidget = new QWidget(midPane);
    sidesWidget->setLayout(sidesRow);
    form->addRow(tr("Si&des"), sidesWidget);

    form->addRow(tr("&Backing Width"), m_backWidthSpin);
    form->addRow(tr("Backing Slope (%)"), m_backSlopeSpin);
    form->addRow(tr("Bac&king Roughness (n)"), m_backRoughSpin);

    // ── Right pane: section preview ─────────────────────────────────────────
    m_preview = new StreetSectionPreview(m_splitter);

    m_splitter->addWidget(leftPane);
    m_splitter->addWidget(midPane);
    m_splitter->addWidget(m_preview);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 2);
    m_splitter->setStretchFactor(2, 2);
    m_splitter->setSizes({180, 300, 240});

    outer->addWidget(m_splitter, 1);

    m_session = new DataObjectDraftSession({{}, // every field commits to the draft as edited
                                            [this] { return int(m_drafts.size()); },
                                            [this](QString *why) { return applyDrafts_(why); },
                                            [this] { revertDrafts_(); }},
                                           this);
    connect(m_session, &DataObjectDraftSession::changed, m_draftMarks,
            &DraftMarkProxyModel::refreshMarks);
    m_session->setModel(m_layer); // Save / Run / Close ask about unapplied drafts
    auto *footer = new DataObjectDraftFooter(m_session, this);
    connect(footer, &DataObjectDraftFooter::closeRequested, this, &QDialog::accept);
    outer->addWidget(footer);

    // ── Wiring ──────────────────────────────────────────────────────────────
    connect(m_listView->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            &StreetEditorDialog::onListSelectionChanged_);
    connect(m_addBtn, &QPushButton::clicked, this, &StreetEditorDialog::onAddStreetClicked_);
    connect(m_delBtn, &QPushButton::clicked, this, &StreetEditorDialog::onDeleteStreetClicked_);
    connect(m_nameEdit, &QLineEdit::editingFinished, this, &StreetEditorDialog::onNameEdited_);

    for (QDoubleSpinBox *s :
         {m_crownWidthSpin, m_curbHeightSpin, m_crossSlopeSpin, m_roadRoughSpin, m_gutterDepSpin,
          m_gutterWidthSpin, m_backWidthSpin, m_backSlopeSpin, m_backRoughSpin})
        connect(s, &QDoubleSpinBox::valueChanged, this, &StreetEditorDialog::onFieldEdited_);
    connect(m_oneSidedRadio, &QRadioButton::toggled, this, &StreetEditorDialog::onSidesToggled_);
}

void StreetEditorDialog::bindProvider_(StreetProvider *p)
{
    if (!m_contextValid)
        p = nullptr;
    m_current = p;
    m_preview->setProvider(p);

    const bool prev = m_suppressFieldSync;
    m_suppressFieldSync = true;

    const bool enabled = (p != nullptr);
    for (QWidget *w :
         {static_cast<QWidget *>(m_nameEdit), static_cast<QWidget *>(m_crownWidthSpin),
          static_cast<QWidget *>(m_curbHeightSpin), static_cast<QWidget *>(m_crossSlopeSpin),
          static_cast<QWidget *>(m_roadRoughSpin), static_cast<QWidget *>(m_gutterDepSpin),
          static_cast<QWidget *>(m_gutterWidthSpin), static_cast<QWidget *>(m_backWidthSpin),
          static_cast<QWidget *>(m_backSlopeSpin), static_cast<QWidget *>(m_backRoughSpin),
          static_cast<QWidget *>(m_oneSidedRadio), static_cast<QWidget *>(m_twoSidedRadio)})
        w->setEnabled(enabled);

    if (p) {
        const StreetProvider::Params v = shown_(p);
        m_nameEdit->setText(p->name());
        m_crownWidthSpin->setValue(v.crownWidth);
        m_curbHeightSpin->setValue(v.curbHeight);
        m_crossSlopeSpin->setValue(v.crossSlope);
        m_roadRoughSpin->setValue(v.roadRoughness);
        m_gutterDepSpin->setValue(v.gutterDepression);
        m_gutterWidthSpin->setValue(v.gutterWidth);
        m_backWidthSpin->setValue(v.backingWidth);
        m_backSlopeSpin->setValue(v.backingSlope);
        m_backRoughSpin->setValue(v.backingRoughness);
        (v.sides == 1 ? m_oneSidedRadio : m_twoSidedRadio)->setChecked(true);
        m_preview->showParams(v);
    } else {
        m_nameEdit->clear();
    }

    m_suppressFieldSync = prev;
}

void StreetEditorDialog::selectProviderInList_(StreetProvider *p)
{
    if (!p || !m_listModel) {
        bindProvider_(p);
        return;
    }
    const auto provs = m_registry ? m_registry->providers() : QVector<StreetProvider *>{};
    const int row = provs.indexOf(p);
    if (row < 0) {
        bindProvider_(p);
        return;
    }
    const QModelIndex idx = viewIndexOf(m_listView, m_listModel->index(row));
    m_listView->selectionModel()->setCurrentIndex(idx, QItemSelectionModel::ClearAndSelect);
    // bindProvider_ is triggered by the selection change.
}

QString StreetEditorDialog::suggestUniqueName_() const
{
    int n = m_registry ? m_registry->providerCount() + 1 : 1;
    QString candidate;
    do {
        candidate = QStringLiteral("Street%1").arg(n++);
    } while (m_registry && m_registry->hasName(candidate));
    return candidate;
}

void StreetEditorDialog::onListSelectionChanged_()
{
    if (!m_contextValid)
        return;
    const QModelIndex idx = m_listView->selectionModel()->currentIndex();
    bindProvider_(idx.isValid() ? m_listModel->providerAt(idx.row()) : nullptr);
}

void StreetEditorDialog::onAddStreetClicked_()
{
    if (!m_registry)
        return;
    const QString name = suggestUniqueName_();
    if (!DataObjectDraftSession::commit(
            {createObjectEdit(m_registry.data(), name, [](StreetProvider *) { return true; })},
            m_undo, tr("Create street %1").arg(name)))
        return;
    if (StreetProvider *p = m_registry->findByName(name)) {
        m_session->noteApplied(tr("Created %1").arg(name));
        selectProviderInList_(p);
    }
}

void StreetEditorDialog::onDeleteStreetClicked_()
{
    if (!m_registry || !m_current)
        return;
    // Plan D8 — say what the engine detaches, and that Undo will not re-attach it.
    QString text = tr("Delete street \"%1\"?").arg(m_current->name());
    if (void *engine = m_registry->engineHandle()) {
        const QString uses = dataObjectUses(
            engine, swmm_street_analyze_impact,
            swmm_street_index(static_cast<SWMM_Engine>(engine),
                              m_current->name().toUtf8().constData()));
        if (!uses.isEmpty())
            text += QLatin1String("\n\n") + uses + QLatin1Char(' ') +
                    tr("Those uses are removed with it; Undo brings back the street and its "
                       "settings, but not those uses.");
    }
    const auto answer = QMessageBox::question(this, tr("Delete Street"), text);
    if (answer != QMessageBox::Yes)
        return;
    deleteCurrentSilently();
}

void StreetEditorDialog::deleteCurrentSilently()
{
    if (!m_registry || !m_current)
        return;
    StreetProvider *victim = m_current;
    const QString removedName = victim->name();
    const auto restore = [registry = m_registry, kept = victim->params()](StreetProvider *p) {
        return registry && writeParams(registry.data(), p, kept, sameStreet);
    };
    m_current = nullptr;
    if (!DataObjectDraftSession::commit({deleteObjectEdit(m_registry.data(), removedName, restore)},
                                        m_undo, tr("Delete street %1").arg(removedName))) {
        bindProvider_(m_registry->findByName(removedName));
        return;
    }
    m_session->noteApplied(tr("Deleted %1").arg(removedName));
    if (m_registry->providerCount() > 0)
        selectProviderInList_(m_registry->providers().first());
    else
        bindProvider_(nullptr);
}

void StreetEditorDialog::onNameEdited_()
{
    if (m_suppressFieldSync || !m_registry || !m_current)
        return;
    const QString newName = m_nameEdit->text().trimmed();
    if (newName.isEmpty() || newName == m_current->name())
        return;
    if (const QString why = openswmmvis::swmmIdentifierProblem(newName); !why.isEmpty()) {
        QMessageBox::warning(this, tr("Rename Street"), why); // feedback C10
        if (m_current)
            m_nameEdit->setText(m_current->name());
        return;
    }
    if (!DataObjectDraftSession::commit(
            {renameObjectEdit(m_registry.data(), m_current->name(), newName)}, m_undo,
            tr("Rename street %1 to %2").arg(m_current->name(), newName))) {
        QMessageBox::warning(this, tr("Rename Street"),
                             tr("A street named \"%1\" already exists.").arg(newName));
        if (m_current)
            m_nameEdit->setText(m_current->name());
        return;
    }
    m_session->noteApplied(tr("Renamed to %1").arg(newName));
}

bool StreetEditorDialog::renameCurrent(const QString &newName)
{
    if (!m_registry || !m_current)
        return false;
    return m_registry->rename(m_current, newName);
}

void StreetEditorDialog::onFieldEdited_()
{
    draftFromForm_();
}

void StreetEditorDialog::onSidesToggled_()
{
    draftFromForm_();
}

void StreetEditorDialog::draftFromForm_()
{
    if (m_suppressFieldSync || !m_current)
        return;
    StreetProvider::Params v;
    v.crownWidth = m_crownWidthSpin->value();
    v.curbHeight = m_curbHeightSpin->value();
    v.crossSlope = m_crossSlopeSpin->value();
    v.roadRoughness = m_roadRoughSpin->value();
    v.gutterDepression = m_gutterDepSpin->value();
    v.gutterWidth = m_gutterWidthSpin->value();
    v.sides = m_oneSidedRadio->isChecked() ? 1 : 2;
    v.backingWidth = m_backWidthSpin->value();
    v.backingSlope = m_backSlopeSpin->value();
    v.backingRoughness = m_backRoughSpin->value();
    if (v == m_current->params())
        m_drafts.remove(m_current); // edited back to the model's values
    else
        m_drafts.insert(m_current, v);
    m_preview->showParams(v);
    m_session->noteEdited();
}

void StreetEditorDialog::setUndoStack(QUndoStack *stack)
{
    m_undo = stack;
}

void StreetEditorDialog::done(int result)
{
    if (m_contextValid && m_registry && !m_session->resolveBeforeClose())
        return;
    QDialog::done(result);
}

StreetProvider::Params StreetEditorDialog::shown_(StreetProvider *p) const
{
    const auto draft = m_drafts.constFind(p);
    return draft != m_drafts.cend() ? draft.value() : p->params();
}

bool StreetEditorDialog::applyDrafts_(QString *reason)
{
    QVector<DataObjectDraftSession::Edit> edits;
    QStringList names;
    for (auto it = m_drafts.cbegin(); it != m_drafts.cend(); ++it) {
        names << it.key()->name();
        edits.append(paramsDraftEdit(m_registry.data(), m_layer.data(), it.key(), it.key()->params(),
                                     it.value(), sameStreet));
    }
    m_applying = true;
    const bool ok = DataObjectDraftSession::commit(
        edits, m_undo, names.size() == 1 ? tr("Edit street %1").arg(names.first())
                                         : tr("Edit %n street(s)", nullptr, int(names.size())));
    m_applying = false;
    if (!ok) {
        *reason = tr("the model did not accept the values for %1; nothing was changed")
                      .arg(names.join(QStringLiteral(", ")));
        return false;
    }
    m_drafts.clear();
    bindProvider_(m_current);
    return true;
}

void StreetEditorDialog::revertDrafts_()
{
    m_drafts.clear();
    bindProvider_(m_current);
}

void StreetEditorDialog::onProviderAdded_(StreetProvider *)
{
    // List model already refreshes via its own subscription.
}

void StreetEditorDialog::onProviderRenamed_(StreetProvider *p, const QString &, const QString &now)
{
    if (p == m_current && m_nameEdit->text() != now) {
        const bool prev = m_suppressFieldSync;
        m_suppressFieldSync = true;
        m_nameEdit->setText(now);
        m_suppressFieldSync = prev;
    }
}

void StreetEditorDialog::invokeNew()
{
    onAddStreetClicked_();
}

// ─────────────────────────────────────────────────────────────────────────────
// Static entry points
// ─────────────────────────────────────────────────────────────────────────────

StreetEditorDialog *StreetEditorDialog::createNew(StreetRegistry *registry, SWMMModelLayer *layer,
                                                  QWidget *parent)
{
    auto *dlg = new StreetEditorDialog(registry, layer, parent);
    dlg->m_mode = Mode::CreateNew;
    dlg->setWindowTitle(tr("New Street"));
    dlg->invokeNew();
    return dlg;
}

QString StreetEditorDialog::pickStreet(StreetRegistry *registry, SWMMModelLayer *layer,
                                       const QString &initialName, QWidget *parent)
{
    if (!registry)
        return {};

    StreetEditorDialog dlg(registry, layer, parent);
    dlg.setModal(true);
    if (auto *provider = registry->findByName(initialName)) {
        dlg.setWindowTitle(tr("Edit Street"));
        dlg.selectProviderInList_(provider);
    } else {
        dlg.setWindowTitle(tr("Select Street"));
        dlg.m_listView->selectionModel()->clear();
        dlg.bindProvider_(nullptr);
    }
    dlg.exec(); // closing with a draft asks Apply / Discard first

    auto *p = dlg.currentProvider();
    return p ? p->name() : QString();
}

} // namespace openswmmvis::ui
