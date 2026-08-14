/*!
 * \file   sectionpreviewwidget.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  QWidget host that paints one SectionDiagramModel.
 *
 * Slice SP.2 (workplans/SECTION_PREVIEW_WORKPLAN.md).
 *
 * Deliberately dumb: it owns a model, repaints when the model is replaced, and
 * nothing else. Every surface that shows a section diagram — the Section View
 * dock, the cross-section editor, the LID editor — hosts one of these and
 * feeds it from its own builder, so the drawing code exists once.
 *
 * Follows the StreetSectionPreview / StylePreviewSwatch idiom already in the
 * codebase (plain QWidget + paintEvent, palette-driven colours).
 */

#ifndef OPENSWMMVIS_SECTIONVIEW_SECTIONPREVIEWWIDGET_H
#define OPENSWMMVIS_SECTIONVIEW_SECTIONPREVIEWWIDGET_H

#include <QWidget>

#include "ui/sectionview/sectiondiagram.h"

namespace openswmmvis::sectionview {

class SectionPreviewWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SectionPreviewWidget(QWidget *parent = nullptr);

    /*! Replace the drawing and repaint. Passing a default-constructed model
     *  clears the widget to its empty state. */
    void setModel(SectionDiagramModel model);

    [[nodiscard]] const SectionDiagramModel &model() const noexcept
    { return m_model; }

    /*! Message shown when the model has nothing to draw and carries no
     *  `emptyText` of its own. */
    void setPlaceholderText(const QString &text);

    /*! Render the current model into an image — used by tests (offscreen) and
     *  by any future "copy diagram" action. */
    [[nodiscard]] QImage renderToImage(const QSize &size) const;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    SectionDiagramModel m_model;
    QString             m_placeholder;
};

} // namespace openswmmvis::sectionview

#endif // OPENSWMMVIS_SECTIONVIEW_SECTIONPREVIEWWIDGET_H
