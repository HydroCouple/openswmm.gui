/*!
 * \file   maptooladdtext.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Click-to-place text annotation map tool.
 *
 * Click on empty map → opens AnnotationStyleDialog with defaults; on accept,
 * pushes an AddAnnotationCommand to the canvas undo stack. Click on an
 * existing annotation → opens the dialog seeded with that annotation's
 * current style (in-place edit). Cancel leaves the layer untouched.
 */
#ifndef OPENSWMMVIS_MAP_TOOLS_MAPTOOLADDTEXT_H
#define OPENSWMMVIS_MAP_TOOLS_MAPTOOLADDTEXT_H

#include "map/tools/maptool.h"
#include <QPointer>
#include <QJsonObject>
#include <QPointF>

class OpenSWMMVisAnnotationLayer;

class OpenSWMMVisMapToolAddText : public OpenSWMMVisMapTool
{
    Q_OBJECT
public:
    /*! \param layer  Annotation layer to receive new items. Non-owning;
     *                the layer's lifetime must outlive this tool. */
    explicit OpenSWMMVisMapToolAddText(MapCanvas *canvas,
                                       OpenSWMMVisAnnotationLayer *layer,
                                       QObject *parent = nullptr);

    [[nodiscard]] QCursor cursor() const override;

    void setLayer(OpenSWMMVisAnnotationLayer *layer);
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void deactivate() override;

signals:
    /*! Emitted after a new annotation is placed. The new item's id is in `id`. */
    void annotationAdded(const QString &id);

private:
    QPointer<OpenSWMMVisAnnotationLayer> m_layer;
    QString m_pressedId;
    QPointF m_pressPixel, m_startOffset;
    QJsonObject m_before;
    bool m_dragged = false;
};

#endif // OPENSWMMVIS_MAP_TOOLS_MAPTOOLADDTEXT_H
