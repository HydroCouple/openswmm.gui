/*!
 * \file   sectionpreviewwidget.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "ui/sectionview/sectionpreviewwidget.h"

#include <QImage>
#include <QPainter>
#include <QPaintEvent>

#include <utility>

namespace openswmmvis::sectionview {

SectionPreviewWidget::SectionPreviewWidget(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("sectionPreviewWidget"));
    setMinimumSize(180, 130);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAutoFillBackground(true);
    setBackgroundRole(QPalette::Base);
    m_placeholder = tr("No section to display");
    // Accessible name: the drawing carries no focusable children, so screen
    // readers would otherwise announce an unlabelled pane (dialog_a11y_checks).
    setAccessibleName(tr("Section preview"));
}

void SectionPreviewWidget::setModel(SectionDiagramModel model)
{
    m_model = std::move(model);
    update();
}

void SectionPreviewWidget::setPlaceholderText(const QString &text)
{
    if (m_placeholder == text) return;
    m_placeholder = text;
    update();
}

QImage SectionPreviewWidget::renderToImage(const QSize &size) const
{
    QImage img(size.isValid() && !size.isEmpty() ? size : QSize(320, 240),
               QImage::Format_ARGB32_Premultiplied);
    img.fill(palette().color(QPalette::Base));

    {
        QPainter p(&img);
        SectionDiagramModel m = m_model;
        if (m.emptyText.isEmpty()) m.emptyText = m_placeholder;
        paintSectionDiagram(p, QRectF(QPointF(0.0, 0.0), QSizeF(img.size())),
                            m, palette());
    }   // painter must be finished before the image is handed back
    return img;
}

void SectionPreviewWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), palette().color(QPalette::Base));

    SectionDiagramModel m = m_model;
    if (m.emptyText.isEmpty()) m.emptyText = m_placeholder;
    paintSectionDiagram(p, QRectF(rect()), m, palette());
}

} // namespace openswmmvis::sectionview
