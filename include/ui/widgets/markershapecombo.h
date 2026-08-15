/*!
 * \file   markershapecombo.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  QGIS-style marker-shape combo drawing each shape as the row icon.
 *
 *         The shape enum is integer-keyed for compatibility with any
 *         marker-shape Q_ENUM in the codebase (NodeMarkerStyle::Circle,
 *         GISVectorSymbol::Circle, …). Callers feed (int, label) pairs
 *         to addShape; the current selection is exposed as shapeValue().
 */
#ifndef OPENSWMMVIS_UI_WIDGETS_MARKERSHAPECOMBO_H
#define OPENSWMMVIS_UI_WIDGETS_MARKERSHAPECOMBO_H

#include <QComboBox>

namespace openswmmvis::ui {

class MarkerShapeCombo : public QComboBox
{
    Q_OBJECT
public:
    explicit MarkerShapeCombo(QWidget *parent = nullptr);

    enum BuiltinShape {
        Circle = 0,
        Square,
        Triangle,
        Diamond,
        Star,
        Cross,
    };

    /*! Legacy overload — the BuiltinShape arg is ignored; the preview icon
     *  is rendered from \p value via the canonical drawMarkerShape so every
     *  MarkerShape draws correctly. Kept for source compatibility. */
    void addShape(int value, BuiltinShape shape, const QString &label);

    /*! Add a row whose \p value is a canonical OpenSWMM::Render::MarkerShape
     *  integer; the preview icon is rendered via drawMarkerShape. */
    void addShape(int value, const QString &label);

    /*! Legacy 5/6-shape mix for consumers backed by a small per-class shape
     *  enum (e.g. PointFeatureSublayerStyle::MarkerShape, values 0–4). Do NOT
     *  use for canonical OpenSWMM::Render::MarkerShape editors — use
     *  populateCanonical() so the extra shapes' values stay valid. */
    void populateDefault(bool includeCross = false);

    /*! Fill with the full canonical OpenSWMM::Render::MarkerShape set (all 19
     *  shapes). For editors bound to that enum (Single Symbol point / mesh
     *  node, etc.). */
    void populateCanonical();

    /*! Current shape integer value (-1 if nothing selected). */
    [[nodiscard]] int shapeValue() const;

    /*! Select the row whose integer value matches \p v. No-op if none. */
    void setShapeValue(int v);

signals:
    void shapeValueChanged(int v);

private:
    /*! Legacy preview helper — delegates to iconForValue. */
    static QIcon iconFor(BuiltinShape shape, int sizePx = 18);
    /*! Render a preview icon for a canonical MarkerShape int value via
     *  drawMarkerShape, so all 19 shapes (and any future ones) render. */
    static QIcon iconForValue(int markerShapeValue, int sizePx = 18);
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_MARKERSHAPECOMBO_H
