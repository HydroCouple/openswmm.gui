// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "mesh/constraintpreview.h"
#include <QWidget>
class QLabel;
class QCheckBox;
class QTreeWidget;
class QGraphicsScene;
class QGraphicsView;

class MeshConstraintPreviewWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MeshConstraintPreviewWidget(QWidget *parent = nullptr);
    void setPreview(mesh::ConstraintPreview preview, const QString &auditSummary);
    void markStale();
private:
    mesh::ConstraintPreview m_preview;
    QLabel *m_summary = nullptr, *m_detail = nullptr;
    QCheckBox *m_original = nullptr, *m_proposed = nullptr, *m_removed = nullptr;
    QTreeWidget *m_features = nullptr;
    QGraphicsScene *m_scene = nullptr;
    QGraphicsView *m_view = nullptr;
    QPointF m_origin;
    void render();
    void fit();
};
