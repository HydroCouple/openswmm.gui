/*!
 * \file   maptooladdgage.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \brief  Click-to-create tool for SWMM rain gages.
 */

#ifndef MAPTOOLADDGAGE_H
#define MAPTOOLADDGAGE_H

#include "map/tools/maptool.h"
#include <QString>

class SWMMModelLayer;

class OpenSWMMVisMapToolAddGage : public OpenSWMMVisMapTool
{
    Q_OBJECT

public:
    explicit OpenSWMMVisMapToolAddGage(MapCanvas *canvas, QObject *parent = nullptr);
    [[nodiscard]] QCursor cursor() const override;
    void mousePressEvent(QMouseEvent *event) override;

signals:
    void gageAdded(const QString &name, double x, double y);

private:
    [[nodiscard]] SWMMModelLayer *activeModelLayer() const;
    [[nodiscard]] QString nextAvailableName(SWMMModelLayer *layer) const;
};

#endif // MAPTOOLADDGAGE_H
