/*!
 * \file   swmm2dresultsstylepanel.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  O2-1 / VS.8 — styling panel for a SWMM2DResultsLayer.
 *
 *         One tab per sub-layer visualization type (Contour bands,
 *         Isolines, Velocity vectors), mirroring the
 *         QGIS mesh-layer styling idiom. Every control writes live into
 *         the owning sublayer / its style bag — the single source of
 *         truth the paint passes consult — so edits repaint immediately
 *         and survive sublayer-JSON round-trips.
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_SWMM2DRESULTSSTYLEPANEL_H
#define OPENSWMMVIS_UI_DIALOGS_SWMM2DRESULTSSTYLEPANEL_H

#include <QWidget>
#include <QPointer>

class SWMM2DResultsLayer;

namespace openswmmvis::ui {

class Swmm2DResultsStylePanel : public QWidget
{
    Q_OBJECT
public:
    explicit Swmm2DResultsStylePanel(SWMM2DResultsLayer *layer, QWidget *parent = nullptr);

private:
    [[nodiscard]] QWidget *buildContourBandTab(QWidget *parent);
    [[nodiscard]] QWidget *buildIsolineTab(QWidget *parent);
    [[nodiscard]] QWidget *buildVelocityTab(QWidget *parent);

    QPointer<SWMM2DResultsLayer> m_layer;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_SWMM2DRESULTSSTYLEPANEL_H
