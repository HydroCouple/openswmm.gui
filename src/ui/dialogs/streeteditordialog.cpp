/*!
 * \file   streeteditordialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/streeteditordialog.h"

#include "layers/swmmmodellayer.h"   // QPointer<SWMMModelLayer> needs the complete type
#include "ui/models/streetlistmodel.h"
#include "street/streetprovider.h"
#include "street/streetregistry.h"
#include "ui/theme/iconfactory.h"

#include <QButtonGroup>
#include <QDialogButtonBox>
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

namespace openswmmvis::ui {

using openswmmvis::street::StreetProvider;
using openswmmvis::street::StreetRegistry;

// ─────────────────────────────────────────────────────────────────────────────
// StreetSectionPreview
// ─────────────────────────────────────────────────────────────────────────────

StreetSectionPreview::StreetSectionPreview(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(220, 160);
    setAutoFillBackground(true);
}

void StreetSectionPreview::setProvider(StreetProvider *p)
{
    if (m_provider == p) return;
    if (m_provider) m_provider->disconnect(this);
    m_provider = p;
    if (m_provider)
        connect(m_provider, &StreetProvider::paramsChanged,
                this, qOverload<>(&QWidget::update));
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
    const double w3 = qMax(0.0, m_provider->crownWidth());          // Tcrown
    const double w2 = qMax(0.0, m_provider->gutterWidth());         // W (gutter)
    const double w1 = qMax(0.0, m_provider->backingWidth());        // Tback
    const double w4 = qMax(0.0, w3 - w2);
    const double slope     = qMax(0.0, m_provider->crossSlope())   / 100.0; // Sx
    const double backSlope = qMax(0.0, m_provider->backingSlope()) / 100.0;  // Sback
    const double curb  = qMax(0.0, m_provider->curbHeight());       // Hcurb
    const double dep   = qMax(0.0, m_provider->gutterDepression()); // a (depression)
    const bool   twoSide = (m_provider->sides() == 2);

    const double y1 = curb + dep;                  // top of curb
    const double y3 = dep + slope * w2;            // bottom of depressed gutter
    const double y4 = y3 + slope * w4;             // crown-side top of gutter
    double ymax = qMax(backSlope * w1 + y1, y4);   // top of backing / crown
    if (ymax <= 0.0) ymax = qMax(curb, 0.001);

    // Station/elevation points, in engine order (P0..P4 left half; mirrored
    // P5..P8 for a two-sided street, else P5 closes the crown centreline).
    QList<QPointF> pts;
    pts.reserve(9);
    pts << QPointF(0.0,     ymax)   // P0 top of backing
        << QPointF(w1,      y1)     // P1 top of curb
        << QPointF(w1,      0.0)    // P2 bottom of curb (gutter invert)
        << QPointF(w1 + w2, y3)     // P3 bottom of depressed gutter
        << QPointF(w1 + w3, y4);    // P4 crown (high road point)
    if (!twoSide) {
        pts << QPointF(w1 + w3, ymax);          // P5 crown / centreline
    } else {
        const double s5 = w1 + w3 + w4;
        pts << QPointF(s5,           y3)        // P5
            << QPointF(s5 + w2,      0.0)       // P6 gutter invert
            << QPointF(s5 + w2,      y1)        // P7 top of curb
            << QPointF(s5 + w2 + w1, ymax);     // P8 top of backing
    }

    // Map section coordinates to the widget (flip Y so elevation increases up).
    double xMax = pts.last().x();
    if (xMax <= 0.0) xMax = 1.0;
    double yTop = ymax;
    if (yTop <= 0.0) yTop = 1.0;

    const QRectF area = rect().adjusted(16, 22, -16, -16);
    const double sx = area.width() / xMax;
    const double sy = (area.height() - 4.0) / yTop;
    auto toWidget = [&](const QPointF &p) {
        return QPointF(area.left() + p.x() * sx, area.bottom() - p.y() * sy);
    };

    QPolygonF poly;
    for (const QPointF &p : pts) poly << toWidget(p);

    // Light fill of the pavement section for legibility.
    QPolygonF fill = poly;
    fill << QPointF(poly.last().x(),  area.bottom())
         << QPointF(poly.first().x(), area.bottom());
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
    pr.drawLine(QPointF(area.left(), area.bottom()),
                QPointF(area.right(), area.bottom()));

    pr.setPen(palette().windowText().color());
    pr.drawText(rect().adjusted(6, 4, -6, -4), Qt::AlignTop | Qt::AlignHCenter,
                tr("%1  (vertical exaggeration)").arg(m_provider->name()));
}

// ─────────────────────────────────────────────────────────────────────────────
// StreetEditorDialog
// ─────────────────────────────────────────────────────────────────────────────

StreetEditorDialog::StreetEditorDialog(StreetRegistry *registry,
                                       SWMMModelLayer *layer,
                                       QWidget *parent)
    : QDialog(parent),
      m_registry(registry),
      m_layer(layer)
{
    setWindowTitle(tr("Street Cross-Sections"));
    resize(720, 420);
    buildUi_();

    if (m_registry) {
        connect(m_registry, &StreetRegistry::providerAdded,
                this, &StreetEditorDialog::onProviderAdded_);
        connect(m_registry, &StreetRegistry::providerRenamed,
                this, &StreetEditorDialog::onProviderRenamed_);
        m_listModel->setRegistry(m_registry);
        if (m_registry->providerCount() > 0)
            selectProviderInList_(m_registry->providers().first());
        else
            bindProvider_(nullptr);
    }
}

StreetEditorDialog::~StreetEditorDialog() = default;

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
    auto *leftLay  = new QVBoxLayout(leftPane);
    leftLay->setContentsMargins(0, 0, 0, 0);
    m_listView  = new QListView(leftPane);
    m_listModel = new StreetListModel(this);
    m_listView->setModel(m_listModel);
    m_listView->setEditTriggers(QAbstractItemView::DoubleClicked
                                | QAbstractItemView::EditKeyPressed);
    leftLay->addWidget(m_listView, 1);

    auto *btnRow = new QHBoxLayout;
    m_addBtn = new QPushButton(openswmmvis::ui::IconFactory::icon(QStringLiteral("Add")),
                               tr("New"), leftPane);
    m_delBtn = new QPushButton(openswmmvis::ui::IconFactory::icon(QStringLiteral("Delete")),
                               tr("Delete"), leftPane);
    btnRow->addWidget(m_addBtn);
    btnRow->addWidget(m_delBtn);
    leftLay->addLayout(btnRow);

    // ── Middle pane: field form ─────────────────────────────────────────────
    auto *midPane = new QWidget(m_splitter);
    auto *form    = new QFormLayout(midPane);

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
    m_roadRoughSpin  = makeSpin(1.0,   4, 0.001);
    m_gutterDepSpin  = makeSpin(1.0e6, 4, 0.1);
    m_gutterWidthSpin= makeSpin(1.0e6, 4, 0.1);
    m_backWidthSpin  = makeSpin(1.0e6, 4, 0.1);
    m_backSlopeSpin  = makeSpin(1.0e3, 4, 0.5);
    m_backRoughSpin  = makeSpin(1.0,   4, 0.001);

    form->addRow(tr("&Road Width"),         m_crownWidthSpin);
    form->addRow(tr("&Curb Height"),        m_curbHeightSpin);
    form->addRow(tr("Cross Slope (%)"),    m_crossSlopeSpin);
    form->addRow(tr("R&oad Roughness (n)"), m_roadRoughSpin);
    form->addRow(tr("&Gutter Depression"),  m_gutterDepSpin);
    form->addRow(tr("G&utter Width"),       m_gutterWidthSpin);

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

    form->addRow(tr("&Backing Width"),      m_backWidthSpin);
    form->addRow(tr("Backing Slope (%)"),  m_backSlopeSpin);
    form->addRow(tr("Bac&king Roughness (n)"), m_backRoughSpin);

    // ── Right pane: section preview ─────────────────────────────────────────
    m_preview = new StreetSectionPreview(m_splitter);

    m_splitter->addWidget(leftPane);
    m_splitter->addWidget(midPane);
    m_splitter->addWidget(m_preview);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 2);
    m_splitter->setStretchFactor(2, 2);
    m_splitter->setSizes({ 180, 300, 240 });

    outer->addWidget(m_splitter, 1);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    outer->addWidget(bb);

    // ── Wiring ──────────────────────────────────────────────────────────────
    connect(m_listView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &StreetEditorDialog::onListSelectionChanged_);
    connect(m_addBtn, &QPushButton::clicked,
            this, &StreetEditorDialog::onAddStreetClicked_);
    connect(m_delBtn, &QPushButton::clicked,
            this, &StreetEditorDialog::onDeleteStreetClicked_);
    connect(m_nameEdit, &QLineEdit::editingFinished,
            this, &StreetEditorDialog::onNameEdited_);

    for (QDoubleSpinBox *s : { m_crownWidthSpin, m_curbHeightSpin, m_crossSlopeSpin,
                                m_roadRoughSpin, m_gutterDepSpin, m_gutterWidthSpin,
                                m_backWidthSpin, m_backSlopeSpin, m_backRoughSpin })
        connect(s, &QDoubleSpinBox::valueChanged,
                this, &StreetEditorDialog::onFieldEdited_);
    connect(m_oneSidedRadio, &QRadioButton::toggled,
            this, &StreetEditorDialog::onSidesToggled_);
}

void StreetEditorDialog::bindProvider_(StreetProvider *p)
{
    m_current = p;
    m_preview->setProvider(p);

    const bool prev = m_suppressFieldSync;
    m_suppressFieldSync = true;

    const bool enabled = (p != nullptr);
    for (QWidget *w : { static_cast<QWidget*>(m_nameEdit),
                        static_cast<QWidget*>(m_crownWidthSpin),
                        static_cast<QWidget*>(m_curbHeightSpin),
                        static_cast<QWidget*>(m_crossSlopeSpin),
                        static_cast<QWidget*>(m_roadRoughSpin),
                        static_cast<QWidget*>(m_gutterDepSpin),
                        static_cast<QWidget*>(m_gutterWidthSpin),
                        static_cast<QWidget*>(m_backWidthSpin),
                        static_cast<QWidget*>(m_backSlopeSpin),
                        static_cast<QWidget*>(m_backRoughSpin),
                        static_cast<QWidget*>(m_oneSidedRadio),
                        static_cast<QWidget*>(m_twoSidedRadio) })
        w->setEnabled(enabled);

    if (p) {
        m_nameEdit->setText(p->name());
        m_crownWidthSpin->setValue(p->crownWidth());
        m_curbHeightSpin->setValue(p->curbHeight());
        m_crossSlopeSpin->setValue(p->crossSlope());
        m_roadRoughSpin->setValue(p->roadRoughness());
        m_gutterDepSpin->setValue(p->gutterDepression());
        m_gutterWidthSpin->setValue(p->gutterWidth());
        m_backWidthSpin->setValue(p->backingWidth());
        m_backSlopeSpin->setValue(p->backingSlope());
        m_backRoughSpin->setValue(p->backingRoughness());
        (p->sides() == 1 ? m_oneSidedRadio : m_twoSidedRadio)->setChecked(true);
    } else {
        m_nameEdit->clear();
    }

    m_suppressFieldSync = prev;
}

void StreetEditorDialog::selectProviderInList_(StreetProvider *p)
{
    if (!p || !m_listModel) { bindProvider_(p); return; }
    const auto provs = m_registry ? m_registry->providers() : QVector<StreetProvider*>{};
    const int row = provs.indexOf(p);
    if (row < 0) { bindProvider_(p); return; }
    const QModelIndex idx = m_listModel->index(row);
    m_listView->selectionModel()->setCurrentIndex(
        idx, QItemSelectionModel::ClearAndSelect);
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
    const QModelIndex idx = m_listView->selectionModel()->currentIndex();
    bindProvider_(idx.isValid() ? m_listModel->providerAt(idx.row()) : nullptr);
}

void StreetEditorDialog::onAddStreetClicked_()
{
    if (!m_registry) return;
    StreetProvider *p = m_registry->create(suggestUniqueName_());
    if (p) selectProviderInList_(p);
}

void StreetEditorDialog::onDeleteStreetClicked_()
{
    if (!m_registry || !m_current) return;
    const auto answer = QMessageBox::question(
        this, tr("Delete Street"),
        tr("Delete street \"%1\"?").arg(m_current->name()));
    if (answer != QMessageBox::Yes) return;
    deleteCurrentSilently();
}

void StreetEditorDialog::deleteCurrentSilently()
{
    if (!m_registry || !m_current) return;
    StreetProvider *victim = m_current;
    m_current = nullptr;
    m_registry->remove(victim);
    if (m_registry->providerCount() > 0)
        selectProviderInList_(m_registry->providers().first());
    else
        bindProvider_(nullptr);
}

void StreetEditorDialog::onNameEdited_()
{
    if (m_suppressFieldSync || !m_registry || !m_current) return;
    const QString newName = m_nameEdit->text().trimmed();
    if (newName.isEmpty() || newName == m_current->name()) return;
    if (!m_registry->rename(m_current, newName)) {
        QMessageBox::warning(this, tr("Rename Street"),
            tr("A street named \"%1\" already exists.").arg(newName));
        m_nameEdit->setText(m_current->name());
    }
}

bool StreetEditorDialog::renameCurrent(const QString &newName)
{
    if (!m_registry || !m_current) return false;
    return m_registry->rename(m_current, newName);
}

void StreetEditorDialog::onFieldEdited_()
{
    if (m_suppressFieldSync || !m_current) return;
    m_current->setCrownWidth(m_crownWidthSpin->value());
    m_current->setCurbHeight(m_curbHeightSpin->value());
    m_current->setCrossSlope(m_crossSlopeSpin->value());
    m_current->setRoadRoughness(m_roadRoughSpin->value());
    m_current->setGutterDepression(m_gutterDepSpin->value());
    m_current->setGutterWidth(m_gutterWidthSpin->value());
    m_current->setBackingWidth(m_backWidthSpin->value());
    m_current->setBackingSlope(m_backSlopeSpin->value());
    m_current->setBackingRoughness(m_backRoughSpin->value());
}

void StreetEditorDialog::onSidesToggled_()
{
    if (m_suppressFieldSync || !m_current) return;
    m_current->setSides(m_oneSidedRadio->isChecked() ? 1 : 2);
}

void StreetEditorDialog::onProviderAdded_(StreetProvider *)
{
    // List model already refreshes via its own subscription.
}

void StreetEditorDialog::onProviderRenamed_(StreetProvider *p,
                                             const QString &, const QString &now)
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

StreetEditorDialog *StreetEditorDialog::createNew(StreetRegistry *registry,
                                                  SWMMModelLayer *layer,
                                                  QWidget *parent)
{
    auto *dlg = new StreetEditorDialog(registry, layer, parent);
    dlg->m_mode = Mode::CreateNew;
    dlg->setWindowTitle(tr("New Street"));
    dlg->invokeNew();
    return dlg;
}

QString StreetEditorDialog::pickStreet(StreetRegistry *registry,
                                       SWMMModelLayer *layer,
                                       const QString  &initialName,
                                       QWidget        *parent)
{
    if (!registry) return {};

    StreetEditorDialog dlg(registry, layer, parent);
    dlg.setModal(true);
    if (initialName.isEmpty()) {
        dlg.m_mode = Mode::CreateNew;
        dlg.setWindowTitle(tr("New Street"));
        dlg.invokeNew();
    } else {
        dlg.setWindowTitle(tr("Edit Street"));
        if (auto *p = registry->findByName(initialName))
            dlg.selectProviderInList_(p);
    }
    dlg.exec();

    // Flush the registry to the engine so a subsequent name → index lookup
    // resolves through engine setters (mirrors pickTransect).
    registry->saveToEngine();

    auto *p = dlg.currentProvider();
    return p ? p->name() : QString();
}

} // namespace openswmmvis::ui
