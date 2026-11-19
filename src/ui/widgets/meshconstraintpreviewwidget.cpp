// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/meshconstraintpreviewwidget.h"
#include <QCheckBox>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGraphicsEllipseItem>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainterPath>
#include <QPainter>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>

namespace
{
class PreviewView : public QGraphicsView
{
public:
    using QGraphicsView::QGraphicsView;
protected:
    void wheelEvent(QWheelEvent *event) override
    {
        const double factor = event->angleDelta().y() >= 0 ? 1.2 : 1 / 1.2;
        scale(factor, factor);
        event->accept();
    }
};
}
MeshConstraintPreviewWidget::MeshConstraintPreviewWidget(QWidget *parent) : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    m_summary = new QLabel(tr("Choose Preview constraints… after setting a preserve or bounded policy on Quality → Boundaries to compare prepared source geometry."), this);
    m_summary->setWordWrap(true);
    m_summary->setTextFormat(Qt::PlainText);
    m_summary->setObjectName("constraintPreviewSummary");
    outer->addWidget(m_summary);
    auto *tools = new QHBoxLayout;
    m_original = new QCheckBox(tr("Original — blue dashed"), this);
    m_proposed = new QCheckBox(tr("Proposed — green"), this);
    m_removed = new QCheckBox(tr("Removed vertices — red"), this);
    for (auto *check : {m_original, m_proposed, m_removed}) {
        check->setChecked(true); tools->addWidget(check);
        connect(check, &QCheckBox::toggled, this, [this] { render(); });
    }
    tools->addStretch();
    auto *fitButton = new QPushButton(tr("Fit overview"), this);
    tools->addWidget(fitButton);
    outer->addLayout(tools);
    m_scene = new QGraphicsScene(this);
    m_view = new PreviewView(m_scene, this);
    m_view->setObjectName("constraintPreviewView");
    m_view->setAccessibleName(tr("Original and proposed constraint geometry"));
    m_view->setRenderHint(QPainter::Antialiasing);
    m_view->setDragMode(QGraphicsView::ScrollHandDrag);
    m_view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    m_view->setTransform(QTransform::fromScale(1, -1));
    outer->addWidget(m_view, 1);
    m_features = new QTreeWidget(this);
    m_features->setObjectName("constraintPreviewFeatures");
    m_features->setRootIsDecorated(false);
    m_features->setHeaderLabels({tr("Source"), tr("Removed"), tr("XY change"), tr("Z change"), tr("Area change"), tr("Outcome")});
    m_features->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_features->header()->setStretchLastSection(false);
    m_features->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_features->setMaximumHeight(180);
    outer->addWidget(m_features);
    m_detail = new QLabel(this); m_detail->setWordWrap(true);
    m_detail->setTextFormat(Qt::PlainText);
    outer->addWidget(m_detail);
    connect(fitButton, &QPushButton::clicked, this, [this] { m_features->clearSelection(); render(); fit(); });
    connect(m_features, &QTreeWidget::itemSelectionChanged, this, [this] { render(); fit(); });
}
void MeshConstraintPreviewWidget::setPreview(mesh::ConstraintPreview preview, const QString &auditSummary)
{
    m_preview = std::move(preview);
    m_summary->setProperty("stale", false);
    m_origin = m_preview.bounds.center();
    m_features->clear();
    const QString horizontal = m_preview.horizontalUnit.isEmpty() ? tr("mesh units") : m_preview.horizontalUnit;
    const QString vertical = m_preview.verticalUnit.isEmpty() ? tr("elevation units") : m_preview.verticalUnit;
    m_features->setHeaderLabels({tr("Source"), tr("Removed"), tr("XY (%1)").arg(horizontal),
        tr("Z (%1)").arg(vertical), tr("Area (%1²)").arg(horizontal), tr("Outcome")});
    for (int i = 0; i < m_preview.features.size(); ++i) {
        const auto &feature = m_preview.features[i];
        auto *row = new QTreeWidgetItem(m_features, {
            feature.label, QString::number(feature.removedCount), QString::number(feature.maxDeviation, 'g', 6),
            QString::number(feature.maxZDeviation, 'g', 6), QString::number(feature.areaChange, 'g', 6),
            feature.budgetRetained ? tr("Retained: work limit") : feature.rolledBack ? tr("Rolled back")
                : feature.removedCount ? tr("Simplified") : tr("Unchanged")});
        row->setData(0, Qt::UserRole, i);
        row->setToolTip(0, feature.id);
    }
    m_summary->setText(tr("%1 constraint features; %2 → %3 vertices; %4 removed. %5%6")
        .arg(m_preview.featuresTotal).arg(m_preview.verticesBefore).arg(m_preview.verticesAfter)
        .arg(m_preview.removedTotal).arg(auditSummary)
        .arg(m_preview.displayReduced ? tr(" Display is sampled; the audit used the full geometry.") : QString()));
    render(); fit();
}
void MeshConstraintPreviewWidget::markStale()
{
    if (!m_preview.features.isEmpty() && !m_summary->property("stale").toBool()) {
        m_summary->setProperty("stale", true);
        m_summary->setText(tr("Inputs changed — run the preview again. ") + m_summary->text());
    }
}
void MeshConstraintPreviewWidget::render()
{
    m_scene->clear();
    int selected = -1;
    if (!m_features->selectedItems().isEmpty()) selected = m_features->selectedItems().first()->data(0, Qt::UserRole).toInt();
    auto path = [&](const QVector<QPointF> &points, bool closed) {
        QPainterPath shape;
        if (points.isEmpty()) return shape;
        shape.moveTo(points.first() - m_origin);
        for (int i = 1; i < points.size(); ++i) shape.lineTo(points[i] - m_origin);
        if (closed) shape.closeSubpath();
        return shape;
    };
    for (int i = 0; i < m_preview.features.size(); ++i) {
        const auto &feature = m_preview.features[i];
        const bool focus = selected < 0 || selected == i;
        if (m_original->isChecked()) {
            QPen pen(focus ? QColor(35, 105, 190) : QColor(170, 180, 195));
            pen.setCosmetic(true); pen.setWidthF(focus ? 1.5 : .7); pen.setStyle(Qt::DashLine);
            m_scene->addPath(path(feature.original, feature.closed), pen);
        }
        if (m_proposed->isChecked()) {
            QPen pen(focus ? QColor(0, 140, 85) : QColor(160, 190, 175));
            pen.setCosmetic(true); pen.setWidthF(focus ? 2 : .7);
            m_scene->addPath(path(feature.proposed, feature.closed), pen);
        }
        if (m_removed->isChecked() && focus)
            for (auto point : feature.removed) {
                point -= m_origin;
                auto *marker = m_scene->addEllipse(-3, -3, 6, 6, QPen(Qt::red, 0), QBrush(Qt::red));
                marker->setPos(point);
                marker->setFlag(QGraphicsItem::ItemIgnoresTransformations);
            }
    }
    if (selected >= 0) {
        const auto &feature = m_preview.features[selected];
        m_detail->setText(tr("%1 — %2 → %3 prepared vertices. Source identity: %4. XY/area units are shown in the table; Z uses the mesh elevation unit.")
            .arg(feature.label).arg(feature.verticesBefore).arg(feature.verticesAfter).arg(feature.id));
    } else m_detail->setText(tr("Drag to pan; use the mouse wheel to zoom. Select a source row to focus its geometry. This compares prepared source constraints; it does not generate or replace a mesh."));
    m_scene->setSceneRect(m_preview.bounds.translated(-m_origin));
}
void MeshConstraintPreviewWidget::fit()
{
    QRectF bounds = m_preview.bounds.translated(-m_origin);
    if (!m_features->selectedItems().isEmpty()) {
        const int selected = m_features->selectedItems().first()->data(0, Qt::UserRole).toInt();
        const auto &feature = m_preview.features[selected];
        if (!feature.original.isEmpty()) bounds = QPolygonF(feature.original).boundingRect().translated(-m_origin);
    }
    const double margin = std::max({bounds.width(), bounds.height(), 1e-6}) * .05;
    m_view->fitInView(bounds.adjusted(-margin, -margin, margin, margin), Qt::KeepAspectRatio);
}
