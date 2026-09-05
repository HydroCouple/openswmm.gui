/*!
 * \file   inleteditordialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/inleteditordialog.h"

#include "curve/curveprovider.h"
#include "curve/curveregistry.h"
#include "inlet/inletregistry.h"
#include "inlet/inletundocommands.h"
#include "layers/swmmmodellayer.h"   // complete type for QPointer<SWMMModelLayer>
#include "ui/dialogs/curveeditordialog.h"
#include "ui/dialogs/dialoglayoutpersistence.h"
#include "ui/dialogs/inletpropertybag.h"
#include "ui/models/inletlistmodel.h"
#include "ui/theme/iconfactory.h"
#include "ui/widgets/inletdrawingview.h"

#include <openswmm/engine/openswmm_links.h>   // SWMM_XSectShape

#include <qpropertyitemdelegate.h>
#include <qpropertymodel.h>

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QCoreApplication>
#include <QFileDialog>
#include <QGraphicsScene>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QTextEdit>
#include <QToolBar>
#include <QTreeView>
#include <QUndoStack>
#include <QVBoxLayout>

namespace openswmmvis::ui {

using openswmmvis::curve::CurveProvider;
using openswmmvis::curve::CurveRegistry;
using openswmmvis::curve::CurveType;
using openswmmvis::inlet::InletCurveKind;
using openswmmvis::inlet::InletDesignData;
using openswmmvis::inlet::InletProvider;
using openswmmvis::inlet::InletRegistry;
using openswmmvis::inlet::InletType;

namespace {

/*! Combo entries, in the legacy editor's order (Uinlet.pas:31-44). */
constexpr InletType kTypeOrder[] = {
    InletType::Grate, InletType::Curb, InletType::Combo, InletType::Slotted,
    InletType::DropGrate, InletType::DropCurb, InletType::Custom,
};

/*! \brief Which designs a host cross-section accepts (Uinlet.pas:524-575):
 *  a STREET section takes the gutter families, an open channel takes the
 *  drop families, and a CUSTOM capture curve applies anywhere. */
bool typeFitsShape(InletType t, int shape)
{
    if (shape < 0) return true;
    if (t == InletType::Custom) return true;
    if (shape == SWMM_XSECT_STREET)
        return t == InletType::Grate || t == InletType::Curb
            || t == InletType::Combo || t == InletType::Slotted;
    if (shape == SWMM_XSECT_RECT_OPEN || shape == SWMM_XSECT_TRAPEZOIDAL)
        return t == InletType::DropGrate || t == InletType::DropCurb;
    return false;
}

/*! \brief Row index of the property whose column-0 text is \p label, or -1. */
int rowForLabel(QAbstractItemModel *model, const QModelIndex &parent,
                 const QString &label)
{
    if (!model || label.isEmpty()) return -1;
    for (int i = 0; i < model->rowCount(parent); ++i) {
        if (model->index(i, 0, parent).data(Qt::DisplayRole).toString() == label)
            return i;
    }
    return -1;
}

/*! \brief Validation the legacy dialog lacks (Inlets plan §2.2): every
 *  dimension a type actually uses must be positive, a GENERIC grate's open
 *  fraction must lie in (0,1], and a CUSTOM design's curve must exist and
 *  match the chosen capture-curve kind. */
bool validateDesign(const InletDesignData &d, const CurveRegistry *curves,
                     QString *reason)
{
    const auto groups = InletPropertyBag::groupsFor(d.type);
    auto fail = [reason](const QString &why) {
        if (reason) *reason = why;
        return false;
    };

    if (groups.testFlag(InletPropertyBag::GrateGroup)) {
        if (d.grateLength <= 0.0 || d.grateWidth <= 0.0)
            return fail(QCoreApplication::translate(
                "InletEditorDialog", "Grate length and width must be greater than zero."));
        if (d.grateType == openswmmvis::inlet::GrateType::Generic
            && (d.openArea <= 0.0 || d.openArea > 1.0))
            return fail(QCoreApplication::translate(
                "InletEditorDialog", "A GENERIC grate's open area fraction must be in (0, 1]."));
    }
    if (groups.testFlag(InletPropertyBag::CurbGroup)) {
        if (d.curbLength <= 0.0 || d.curbHeight <= 0.0)
            return fail(QCoreApplication::translate(
                "InletEditorDialog", "Curb opening length and height must be greater than zero."));
    }
    if (groups.testFlag(InletPropertyBag::SlottedGroup)) {
        if (d.slotLength <= 0.0 || d.slotWidth <= 0.0)
            return fail(QCoreApplication::translate(
                "InletEditorDialog", "Slot length and width must be greater than zero."));
    }
    if (groups.testFlag(InletPropertyBag::CustomGroup)) {
        // A half-filled CUSTOM design is not rejected here — the curve and the
        // kind are picked one row at a time, and refusing the first of the two
        // would make the pair impossible to enter. The engine resolves the
        // curve reference at run/validate time (openswmm_infrastructure.h).
        // What IS refused is a curve whose flavour contradicts the chosen kind.
        if (curves && d.curveKind != InletCurveKind::None && !d.curveId.isEmpty()) {
            CurveProvider *cp = curves->findByName(d.curveId);
            if (!cp)
                return fail(QCoreApplication::translate(
                    "InletEditorDialog", "No curve named “%1” exists.").arg(d.curveId));
            const CurveType want = (d.curveKind == InletCurveKind::Rating)
                                       ? CurveType::Rating : CurveType::Diversion;
            if (cp->type() != want)
                return fail(QCoreApplication::translate(
                    "InletEditorDialog",
                    "Curve “%1” is a %2 curve; a %3 curve is required.")
                        .arg(d.curveId,
                             CurveProvider::typeLabel(cp->type()),
                             CurveProvider::typeLabel(want)));
        }
    }
    return true;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

InletEditorDialog::InletEditorDialog(InletRegistry *registry,
                                       SWMMModelLayer *layer,
                                       QUndoStack *undoStack,
                                       QWidget *parent)
    : QDialog(parent, openswmmvis::ui::floatingPanelFlags())
    , m_registry(registry)
    , m_layer(layer)
    , m_undoStack(undoStack)
{
    setWindowTitle(tr("Inlet Editor"));
    setModal(false);
    resize(1180, 760);

    buildUi_();
    buildToolbar_();

    if (m_registry) {
        connect(m_registry, &InletRegistry::providerRenamed,
                this, &InletEditorDialog::onProviderRenamed_);
        connect(m_registry, &InletRegistry::providerAboutToBeRemoved,
                this, [this](InletProvider *p) {
                    if (m_current.data() == p) bindProvider_(nullptr);
                });
    }
    if (m_listModel) m_listModel->setRegistry(m_registry);

    if (m_registry && m_registry->providerCount() > 0 && m_listView)
        m_listView->setCurrentIndex(m_listModel->index(0));
    else
        bindProvider_(nullptr);

    // Iteration 2 (D2) — naming wires the app-wide layout persistence.
    setObjectName(QStringLiteral("InletEditorDialog"));
    if (m_splitter) m_splitter->setObjectName(QStringLiteral("main"));
}

InletEditorDialog::~InletEditorDialog() = default;

InletEditorDialog *InletEditorDialog::createNew(InletRegistry *registry,
                                                  SWMMModelLayer *layer,
                                                  QUndoStack *undoStack,
                                                  QWidget *parent)
{
    auto *dlg = new InletEditorDialog(registry, layer, undoStack, parent);
    dlg->m_mode = Mode::CreateNew;
    dlg->setWindowTitle(tr("New Inlet"));
    dlg->invokeNew();
    return dlg;
}

void InletEditorDialog::openForInlet(const QString &name)
{
    show();
    raise();
    activateWindow();
    if (!m_registry || name.isEmpty()) return;
    if (auto *p = m_registry->findByName(name)) selectProviderInList_(p);
}

InletProvider *InletEditorDialog::currentProvider() const noexcept
{
    return m_current.data();
}

QString InletEditorDialog::pickInlet(InletRegistry *registry,
                                       SWMMModelLayer *layer,
                                       QUndoStack    *undoStack,
                                       QWidget       *parent,
                                       int            compatibleXsectShape)
{
    if (!registry) return {};

    InletEditorDialog dlg(registry, layer, undoStack, parent);
    dlg.setModal(true);
    dlg.m_shapeFilter = compatibleXsectShape;
    dlg.setWindowTitle(tr("Select Inlet"));
    if (compatibleXsectShape >= 0 && dlg.m_status) {
        dlg.m_status->showMessage(
            tr("Only designs compatible with this conduit's cross-section "
               "can be assigned."));
    }

    // Land on the first compatible design so the modal opens on something the
    // host can actually accept.
    InletProvider *first = nullptr;
    for (InletProvider *p : registry->providers()) {
        if (dlg.passesShapeFilter_(p)) { first = p; break; }
    }
    if (first) dlg.selectProviderInList_(first);
    else       dlg.bindProvider_(nullptr);

    dlg.exec();

    // Flush the registry to the engine so the caller's setter (which resolves
    // the design by name through the engine) can find it — mirrors
    // TransectEditorDialog::pickTransect.
    registry->saveToEngine();

    InletProvider *p = dlg.currentProvider();
    if (!p) return {};
    if (!dlg.passesShapeFilter_(p)) {
        QMessageBox::warning(parent, tr("Select Inlet"),
            tr("“%1” is a %2 design, which cannot be placed on this conduit's "
               "cross-section. No inlet was assigned.")
                .arg(p->name(), openswmmvis::inlet::inletTypeLabel(p->type())));
        return {};
    }
    return p->name();
}

bool InletEditorDialog::passesShapeFilter_(InletProvider *p) const
{
    return p && typeFitsShape(p->type(), m_shapeFilter);
}

// ─────────────────────────────────────────────────────────────────────────────
// UI assembly
// ─────────────────────────────────────────────────────────────────────────────

void InletEditorDialog::buildUi_()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(6);
    outer->addWidget(m_splitter, 1);

    // ── Left pane: inlet list + Add/Delete ──────────────────────────────────
    {
        auto *host = new QWidget(m_splitter);
        auto *lay  = new QVBoxLayout(host);
        lay->setContentsMargins(8, 8, 8, 8);
        lay->addWidget(new QLabel(tr("Inlets"), host));

        m_listView = new QListView(host);
        m_listView->setEditTriggers(QAbstractItemView::SelectedClicked
                                     | QAbstractItemView::EditKeyPressed);
        m_listView->setSelectionMode(QAbstractItemView::SingleSelection);
        m_listView->setUniformItemSizes(true);
        m_listModel = new InletListModel(this);
        m_listView->setModel(m_listModel);
        lay->addWidget(m_listView, 1);

        connect(m_listView->selectionModel(), &QItemSelectionModel::currentChanged,
                this, [this](const QModelIndex &, const QModelIndex &) {
                    onListSelectionChanged_();
                });

        auto *btnRow = new QHBoxLayout();
        m_addBtn = new QPushButton(IconFactory::icon(QStringLiteral("Add")), tr("Add"), host);
        m_delBtn = new QPushButton(IconFactory::icon(QStringLiteral("Delete")), tr("Delete"), host);
        m_addBtn->setToolTip(tr("Create a new inlet design"));
        m_delBtn->setToolTip(tr("Delete the selected inlet design"));
        connect(m_addBtn, &QPushButton::clicked, this, &InletEditorDialog::onAddClicked_);
        connect(m_delBtn, &QPushButton::clicked, this, &InletEditorDialog::onDeleteClicked_);
        btnRow->addWidget(m_addBtn);
        btnRow->addStretch(1);
        btnRow->addWidget(m_delBtn);
        lay->addLayout(btnRow);

        m_splitter->addWidget(host);
    }

    // ── Middle pane: name + description + type + property tree ──────────────
    {
        auto *host = new QWidget(m_splitter);
        auto *lay  = new QVBoxLayout(host);
        lay->setContentsMargins(8, 8, 8, 8);

        {
            auto *r = new QHBoxLayout();
            r->addWidget(new QLabel(tr("Name:"), host));
            m_nameEdit = new QLineEdit(host);
            m_nameEdit->setPlaceholderText(tr("Inlet name"));
            r->addWidget(m_nameEdit, 1);
            connect(m_nameEdit, &QLineEdit::editingFinished,
                    this, &InletEditorDialog::onNameEdited_);
            lay->addLayout(r);
        }

        lay->addWidget(new QLabel(tr("Description / comments:"), host));
        m_commentsEdit = new QTextEdit(host);
        m_commentsEdit->setAcceptRichText(false);
        m_commentsEdit->setMaximumHeight(80);
        connect(m_commentsEdit, &QTextEdit::textChanged,
                this, &InletEditorDialog::onCommentsEdited_);
        lay->addWidget(m_commentsEdit);

        {
            auto *r = new QHBoxLayout();
            r->addWidget(new QLabel(tr("Inlet Type:"), host));
            m_typeCombo = new QComboBox(host);
            for (InletType t : kTypeOrder)
                m_typeCombo->addItem(openswmmvis::inlet::inletTypeLabel(t),
                                     static_cast<int>(t));
            connect(m_typeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                    this, &InletEditorDialog::onTypeComboChanged_);
            r->addWidget(m_typeCombo, 1);
            lay->addLayout(r);
        }

        lay->addWidget(new QLabel(tr("Properties:"), host));
        m_propertyBag  = new InletPropertyBag(this);
        m_propertyBag->setCommitHandler(
            [this](const InletDesignData &before, const InletDesignData &after) {
                commitDesign_(before, after);
            });
        m_propertyTree = new QTreeView(host);
        m_propertyTree->setAlternatingRowColors(true);
        m_propertyTree->setEditTriggers(QAbstractItemView::AllEditTriggers);
        m_propertyTree->setRootIsDecorated(true);
        m_propertyTree->setItemsExpandable(true);
        m_propertyTree->setMinimumHeight(240);
        rebuildPropertyTree_();
        lay->addWidget(m_propertyTree, 2);

        m_pickCurveBtn = new QPushButton(tr("Choose Curve…"), host);
        m_pickCurveBtn->setToolTip(
            tr("Pick the capture curve for a CUSTOM inlet from the Curves editor"));
        connect(m_pickCurveBtn, &QPushButton::clicked,
                this, &InletEditorDialog::onPickCurveClicked_);
        auto *curveRow = new QHBoxLayout();
        curveRow->addStretch(1);
        curveRow->addWidget(m_pickCurveBtn);
        lay->addLayout(curveRow);

        m_splitter->addWidget(host);
    }

    // ── Right pane: toolbar + drawing ───────────────────────────────────────
    {
        auto *host = new QWidget(m_splitter);
        auto *lay  = new QVBoxLayout(host);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(0);

        m_toolBar = new QToolBar(host);
        m_toolBar->setIconSize(QSize(16, 16));
        lay->addWidget(m_toolBar);

        m_drawing = new InletDrawingView(host);
        lay->addWidget(m_drawing, 1);

        m_splitter->addWidget(host);
    }

    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 3);
    m_splitter->setStretchFactor(2, 3);

    m_status = new QStatusBar(this);
    m_hintLabel = new QLabel(m_status);
    m_status->addPermanentWidget(m_hintLabel);
    outer->addWidget(m_status);
}

void InletEditorDialog::buildToolbar_()
{
    if (!m_toolBar) return;
    m_toolBar->setIconSize(QSize(20, 20));
    m_toolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);

    QAction *aFit = m_toolBar->addAction(IconFactory::icon(QStringLiteral("Extent")),
                                          tr("Fit"));
    aFit->setToolTip(tr("Zoom to extent (F)"));
    aFit->setShortcut(QKeySequence(Qt::Key_F));
    connect(aFit, &QAction::triggered, m_drawing, &InletDrawingView::zoomToExtent);

    QAction *aIn = m_toolBar->addAction(IconFactory::icon(QStringLiteral("ZoomIn")),
                                         tr("Zoom in"));
    aIn->setToolTip(tr("Zoom in (Ctrl++)"));
    aIn->setShortcut(QKeySequence::ZoomIn);
    connect(aIn, &QAction::triggered, m_drawing, &InletDrawingView::zoomIn);

    QAction *aOut = m_toolBar->addAction(IconFactory::icon(QStringLiteral("ZoomOut")),
                                          tr("Zoom out"));
    aOut->setToolTip(tr("Zoom out (Ctrl+-)"));
    aOut->setShortcut(QKeySequence::ZoomOut);
    connect(aOut, &QAction::triggered, m_drawing, &InletDrawingView::zoomOut);

    m_toolBar->addSeparator();

    QAction *aCopy = m_toolBar->addAction(IconFactory::icon(QStringLiteral("Copy")),
                                           tr("Copy"));
    aCopy->setToolTip(tr("Copy the drawing to the clipboard"));
    connect(aCopy, &QAction::triggered, this, &InletEditorDialog::onCopyClicked_);

    QAction *aExport = m_toolBar->addAction(IconFactory::icon(QStringLiteral("ExportImage")),
                                             tr("Export"));
    aExport->setToolTip(tr("Export the drawing as PNG"));
    connect(aExport, &QAction::triggered, this, &InletEditorDialog::onExportImageClicked_);
}

void InletEditorDialog::rebuildPropertyTree_()
{
    if (!m_propertyTree || !m_propertyBag) return;

    // Same idiom as TransectEditorDialog::rebuildPropertyTree_ — the model
    // groups the Q_PROPERTYs under a class-header row, so the tree is
    // reparented at that class index to show the property rows flat.
    // setData(QVariant::fromValue<QObject*>(...)) is explicit so the variant
    // carries the QObject* metatype QPropertyModel::setData() matches on.
    auto *pm = new QPropertyModel(m_propertyTree);
    pm->setData(QVariant::fromValue<QObject *>(m_propertyBag));
    m_propertyTree->setModel(pm);
    m_propertyModel = pm;

    if (!m_propertyTree->itemDelegate()
        || !qobject_cast<QPropertyItemDelegate *>(m_propertyTree->itemDelegate()))
    {
        m_propertyTree->setItemDelegate(new QPropertyItemDelegate(m_propertyTree));
    }

    const QModelIndex classRoot = pm->index(0, 0);
    if (classRoot.isValid()) m_propertyTree->setRootIndex(classRoot);
    m_propertyTree->expandAll();

    auto *hdr = m_propertyTree->header();
    hdr->setSectionResizeMode(0, QHeaderView::Interactive);
    hdr->setSectionResizeMode(1, QHeaderView::Interactive);
    hdr->setStretchLastSection(true);
    hdr->resizeSection(0, 260);
}

void InletEditorDialog::applyRowVisibility_()
{
    if (!m_propertyTree || !m_propertyBag || !m_propertyModel) return;

    const QModelIndex root = m_propertyTree->rootIndex();
    const QMetaObject *mo  = m_propertyBag->metaObject();
    for (int i = mo->propertyOffset(); i < mo->propertyCount(); ++i) {
        const QString prop = QString::fromLatin1(mo->property(i).name());
        int row = rowForLabel(m_propertyModel, root, m_propertyBag->displayLabelFor(prop));
        if (row < 0) row = rowForLabel(m_propertyModel, root, prop);
        if (row < 0) continue;
        m_propertyTree->setRowHidden(row, root, !m_propertyBag->isPropertyVisible(prop));
    }

    if (m_pickCurveBtn) {
        m_pickCurveBtn->setEnabled(m_current
                                   && m_current->type() == InletType::Custom);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// List / provider binding
// ─────────────────────────────────────────────────────────────────────────────

void InletEditorDialog::selectProviderInList_(InletProvider *p)
{
    if (!p || !m_listModel || !m_listView) return;
    const auto provs = m_registry ? m_registry->providers() : QVector<InletProvider *>{};
    for (int i = 0; i < provs.size(); ++i) {
        if (provs.at(i) == p) {
            m_listView->setCurrentIndex(m_listModel->index(i));
            return;
        }
    }
}

void InletEditorDialog::onListSelectionChanged_()
{
    if (!m_listView || !m_listModel) return;
    bindProvider_(m_listModel->providerAt(m_listView->currentIndex().row()));
}

void InletEditorDialog::bindProvider_(InletProvider *p)
{
    if (m_current.data() != p) {
        if (m_current) m_current->disconnect(this);
        m_current = QPointer<InletProvider>(p);
        if (m_current) {
            connect(m_current, &InletProvider::paramsChanged,
                    this, &InletEditorDialog::syncFromProvider_);
            connect(m_current, &InletProvider::commentsChanged, this, [this]() {
                if (!m_commentsEdit || !m_current) return;
                if (m_commentsEdit->toPlainText() != m_current->comments()) {
                    const QSignalBlocker b(m_commentsEdit);
                    m_commentsEdit->setPlainText(m_current->comments());
                }
            });
        }
    }

    if (m_propertyBag) m_propertyBag->bind(p);
    if (m_drawing)     m_drawing->setProvider(p);

    m_suppressSync = true;
    if (m_nameEdit)     m_nameEdit->setText(p ? p->name() : QString());
    if (m_commentsEdit) m_commentsEdit->setPlainText(p ? p->comments() : QString());
    m_suppressSync = false;

    const bool enabled = (p != nullptr);
    if (m_nameEdit)     m_nameEdit->setEnabled(enabled);
    if (m_commentsEdit) m_commentsEdit->setEnabled(enabled);
    if (m_typeCombo)    m_typeCombo->setEnabled(enabled);
    if (m_propertyTree) m_propertyTree->setEnabled(enabled);
    if (m_delBtn)       m_delBtn->setEnabled(enabled);

    syncFromProvider_();
}

void InletEditorDialog::syncFromProvider_()
{
    if (m_typeCombo && m_current) {
        const int idx = m_typeCombo->findData(static_cast<int>(m_current->type()));
        if (idx >= 0 && idx != m_typeCombo->currentIndex()) {
            const QSignalBlocker b(m_typeCombo);
            m_typeCombo->setCurrentIndex(idx);
        }
    }
    if (m_propertyModel) m_propertyModel->refreshValues();
    applyRowVisibility_();
    refreshCustomCurve_();
    updateStatusBar_();

    if (m_current) setWindowTitle(tr("Inlet Editor — %1").arg(m_current->name()));
    else           setWindowTitle(tr("Inlet Editor"));
}

void InletEditorDialog::refreshCustomCurve_()
{
    if (!m_drawing) return;
    QVector<QPointF> pts;
    if (m_current && m_current->type() == InletType::Custom
        && !m_current->curveId().isEmpty() && m_layer)
    {
        if (auto *reg = qobject_cast<CurveRegistry *>(m_layer->ensureCurveRegistry())) {
            if (CurveProvider *cp = reg->findByName(m_current->curveId())) {
                pts.reserve(cp->pointCount());
                for (const auto &pt : cp->points()) pts.push_back(QPointF(pt.x, pt.y));
            }
        }
    }
    m_drawing->setCustomCurve(pts);
}

void InletEditorDialog::updateStatusBar_()
{
    if (!m_hintLabel) return;
    if (!m_current) { m_hintLabel->setText({}); return; }
    m_hintLabel->setText(openswmmvis::inlet::inletTypeLabel(m_current->type()));
}

// ─────────────────────────────────────────────────────────────────────────────
// Edits
// ─────────────────────────────────────────────────────────────────────────────

void InletEditorDialog::commitDesign_(const InletDesignData &before,
                                        const InletDesignData &after)
{
    if (!m_current) return;

    CurveRegistry *curves = m_layer
        ? qobject_cast<CurveRegistry *>(m_layer->ensureCurveRegistry())
        : nullptr;

    QString reason;
    if (!validateDesign(after, curves, &reason)) {
        if (m_status) m_status->showMessage(reason, 5000);
        // Re-sync the bag so the tree shows the design that is actually stored.
        if (m_propertyBag) m_propertyBag->bind(m_current.data());
        if (m_propertyModel) m_propertyModel->refreshValues();
        return;
    }

    if (m_undoStack) {
        m_undoStack->push(new openswmmvis::inlet::SetInletParamsCommand(
            m_current, before, after));
    } else {
        m_current->setDesign(after);
    }
}

void InletEditorDialog::onTypeComboChanged_(int index)
{
    if (m_suppressSync || !m_current || !m_typeCombo || index < 0) return;
    const auto t = static_cast<InletType>(m_typeCombo->itemData(index).toInt());
    if (t == m_current->type()) return;
    if (m_undoStack) {
        m_undoStack->push(new openswmmvis::inlet::SetInletTypeCommand(m_current, t));
    } else {
        m_current->setType(t);
    }
}

void InletEditorDialog::onNameEdited_()
{
    if (m_suppressSync || !m_current || !m_registry || !m_nameEdit) return;
    const QString newName = m_nameEdit->text().trimmed();
    if (newName.isEmpty() || newName == m_current->name()) {
        const QSignalBlocker b(m_nameEdit);
        m_nameEdit->setText(m_current->name());
        return;
    }
    // Pre-check uniqueness so a doomed rename doesn't leak an undo entry that
    // does nothing (mirrors TransectEditorDialog::onNameEdited_).
    if (m_registry->hasName(newName)) {
        if (m_status) m_status->showMessage(
            tr("An inlet named “%1” already exists.").arg(newName), 4000);
        const QSignalBlocker b(m_nameEdit);
        m_nameEdit->setText(m_current->name());
        return;
    }
    if (m_undoStack) {
        m_undoStack->push(new openswmmvis::inlet::RenameInletCommand(
            m_registry, m_current, newName));
    } else {
        m_registry->rename(m_current, newName);
    }
}

void InletEditorDialog::onCommentsEdited_()
{
    if (m_suppressSync || !m_current || !m_commentsEdit) return;
    const QString text = m_commentsEdit->toPlainText();
    if (text == m_current->comments()) return;
    if (m_undoStack) {
        m_undoStack->push(new openswmmvis::inlet::SetInletCommentsCommand(
            m_current, text));
    } else {
        m_current->setComments(text);
    }
}

void InletEditorDialog::onPickCurveClicked_()
{
    if (!m_current || !m_layer) return;
    auto *reg = qobject_cast<CurveRegistry *>(m_layer->ensureCurveRegistry());
    if (!reg) return;

    const QString chosen = CurveEditorDialog::pickCurve(
        reg, m_undoStack, m_current->curveId(), this);
    if (chosen.isEmpty()) return;

    InletDesignData after = m_current->design();
    after.curveId = chosen;
    commitDesign_(m_current->design(), after);
}

// ─────────────────────────────────────────────────────────────────────────────
// CRUD
// ─────────────────────────────────────────────────────────────────────────────

QString InletEditorDialog::suggestUniqueName_() const
{
    if (!m_registry) return tr("Inlet1");
    for (int i = 1; i < 9999; ++i) {
        const QString cand = QStringLiteral("Inlet%1").arg(i);
        if (!m_registry->hasName(cand)) return cand;
    }
    return tr("Inlet");
}

void InletEditorDialog::invokeNew() { onAddClicked_(); }

void InletEditorDialog::onAddClicked_()
{
    if (!m_registry) return;
    const QString name = suggestUniqueName_();
    if (m_undoStack) {
        m_undoStack->push(new openswmmvis::inlet::AddInletCommand(m_registry, name));
    } else {
        m_registry->create(name);
    }
    InletProvider *p = m_registry->findByName(name);
    if (!p) return;
    selectProviderInList_(p);
    m_mode = Mode::Edit;
    if (m_nameEdit) {
        m_nameEdit->setFocus(Qt::OtherFocusReason);
        m_nameEdit->selectAll();
    }
}

void InletEditorDialog::onDeleteClicked_()
{
    if (!m_current || !m_registry) return;
    const QString name    = m_current->name();
    const QString impact  = m_registry->impactSummary(m_current);

    QString detail = tr("Delete inlet design “%1”?").arg(name);
    if (!impact.isEmpty())
        detail += QStringLiteral("\n\n")
                + tr("This design is referenced by %1; those references will "
                     "be removed.").arg(impact);

    const auto reply = QMessageBox::question(
        this, tr("Delete Inlet"), detail,
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply != QMessageBox::Yes) return;
    deleteCurrentSilently();
}

void InletEditorDialog::deleteCurrentSilently()
{
    if (!m_current || !m_registry) return;
    if (m_undoStack) {
        m_undoStack->push(new openswmmvis::inlet::DeleteInletCommand(
            m_registry, m_current));
    } else {
        m_registry->remove(m_current);
    }
}

void InletEditorDialog::onProviderRenamed_(InletProvider *p,
                                             const QString &, const QString &now)
{
    if (p != m_current) return;
    if (m_nameEdit && m_nameEdit->text() != now) {
        const QSignalBlocker b(m_nameEdit);
        m_nameEdit->setText(now);
    }
    setWindowTitle(tr("Inlet Editor — %1").arg(now));
}

// ─────────────────────────────────────────────────────────────────────────────
// Copy / export
// ─────────────────────────────────────────────────────────────────────────────

namespace {

QPixmap renderScene(QGraphicsScene *scene)
{
    if (!scene) return {};
    const QRectF r = scene->sceneRect();
    if (r.isEmpty()) return {};
    // 2× so the exported/pasted image stays crisp.
    QPixmap pm(QSize(int(r.width() * 2.0), int(r.height() * 2.0)));
    pm.fill(scene->backgroundBrush().color());
    QPainter painter(&pm);
    painter.setRenderHint(QPainter::Antialiasing, true);
    scene->render(&painter, QRectF(pm.rect()), r);
    painter.end();
    return pm;
}

} // namespace

void InletEditorDialog::onCopyClicked_()
{
    if (!m_drawing) return;
    const QPixmap pm = renderScene(m_drawing->scene());
    if (pm.isNull()) return;
    QApplication::clipboard()->setPixmap(pm);
    if (m_status) m_status->showMessage(tr("Copied the drawing to the clipboard."), 3000);
}

void InletEditorDialog::onExportImageClicked_()
{
    if (!m_drawing) return;
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Drawing as PNG"), QString(), tr("PNG Image (*.png)"));
    if (path.isEmpty()) return;
    const QPixmap pm = renderScene(m_drawing->scene());
    if (pm.isNull() || !pm.save(path)) {
        QMessageBox::warning(this, tr("Export Drawing"),
            tr("Could not save the image to %1.").arg(path));
    } else if (m_status) {
        m_status->showMessage(tr("Saved the drawing to %1").arg(path), 3000);
    }
}

} // namespace openswmmvis::ui
