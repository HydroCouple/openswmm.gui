/**
 * @file      markershapecombo.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     QGIS-style marker-shape combo drawing each shape as the row icon.
 *
 *         The shape enum is integer-keyed for compatibility with any
 *         marker-shape Q_ENUM in the codebase (NodeMarkerStyle::Circle,
 *         GISVectorSymbol::Circle, …). Callers feed (int, label) pairs
 *         to addShape; the current selection is exposed as shapeValue().
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_WIDGETS_MARKERSHAPECOMBO_H
#define OPENSWMMVIS_UI_WIDGETS_MARKERSHAPECOMBO_H

#include <QComboBox>

namespace openswmmvis::ui
{

class MarkerShapeCombo : public QComboBox
{
    Q_OBJECT
public:
    /**
     * @brief Create an initially empty shape combo with icon previews and selection-change forwarding.
     * @param[in] parent Optional QWidget owner. Populate rows before selecting values.
     */
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
     *  MarkerShape draws correctly. Kept for source compatibility.
     * @param[in] value Canonical marker enum integer stored as item data and used for drawing.
     * @param[in] shape Legacy selector, ignored.
     * @param[in] label Row display text.
     */
    void addShape(int value, BuiltinShape shape, const QString &label);

    /*! Add a row whose \p value is a canonical OpenSWMM::Render::MarkerShape
     *  integer; the preview icon is rendered via drawMarkerShape.
     * @param[in] value Canonical marker enum integer stored as item data.
     * @param[in] label Row display text.
     * @note Adding the first row may change selection and emit shapeValueChanged().
     */
    void addShape(int value, const QString &label);

    /*! Legacy 5/6-shape mix for consumers backed by a small per-class shape
     *  enum (e.g. PointFeatureSublayerStyle::MarkerShape, values 0–4). Do NOT
     *  use for canonical OpenSWMM::Render::MarkerShape editors — use
     *  populateCanonical() so the extra shapes' values stay valid.
     * @param[in] includeCross Add legacy value 5 after values 0–4 when true.
     * @note Clears previous rows; normal Qt selection notifications are not blocked.
     */
    void populateDefault(bool includeCross = false);

    /*! Fill with the full canonical OpenSWMM::Render::MarkerShape set (all 19
     *  shapes). For editors bound to that enum (Single Symbol point / mesh
     *  node, etc.).
     * @note Clears previous rows and appends all nineteen canonical values in enum order; normal selection notifications are not blocked.
     */
    void populateCanonical();

    /*! Current shape integer value (-1 if nothing selected). */
    [[nodiscard]] int shapeValue() const;

    /*! Select the row whose integer value matches \p v. No-op if none.
     * @param[in] v Integer item payload to select; the first match wins. A changed selection emits shapeValueChanged().
     */
    void setShapeValue(int v);

signals:
    /**
     * @brief Forward changed combo selection as a shape payload.
     * @param[in] v Selected integer, or -1 when no row is selected.
     */
    void shapeValueChanged(int v);

private:
    /*! Legacy preview helper — delegates to iconForValue.
     * @param[in] shape Legacy shape value converted to the canonical integer drawing path.
     * @param[in] sizePx Positive square icon edge in logical pixels.
     * @return Value icon with a transparent background.
     */
    static QIcon iconFor(BuiltinShape shape, int sizePx = 18);
    /*! Render a preview icon for a canonical MarkerShape int value via
     *  drawMarkerShape, so all 19 shapes (and any future ones) render.
     * @param[in] markerShapeValue Canonical marker enum integer passed to drawMarkerShape(); no separate range check occurs here.
     * @param[in] sizePx Positive square icon edge in logical pixels.
     * @return Transparent-background icon containing a glyph at 84% of the icon edge.
     */
    static QIcon iconForValue(int markerShapeValue, int sizePx = 18);
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_MARKERSHAPECOMBO_H
