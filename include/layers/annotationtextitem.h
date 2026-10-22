/**
 * @file      annotationtextitem.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Per-text-annotation data model.
 *
 * A pure QObject (no QGraphicsItem inheritance) so a QPropertyModel can drive
 * the style editor directly and the annotation layer can re-emit a single
 * aggregate `changed()` signal on every edit (live preview hook). Position is
 * stored in the owning layer's CRS; the layer reprojects to canvas CRS when
 * populating the scene.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_LAYERS_ANNOTATIONTEXTITEM_H
#define OPENSWMMVIS_LAYERS_ANNOTATIONTEXTITEM_H

#include <QColor>
#include <QFont>
#include <QJsonObject>
#include <QObject>
#include <QPointF>
#include <QString>
#include <QUuid>

/*!
 * \class AnnotationTextItem
 * \brief Data model for a single styled text annotation.
 *
 * Style attributes are exposed as Q_PROPERTYs so they can be driven by a
 * QPropertyModel-backed editor. A single aggregate `changed()` signal fires
 * after each setter, letting the layer's graphics item issue a single
 * `update()` per edit.
 */
class AnnotationTextItem : public QObject
{
    Q_OBJECT

    /*!
     * \name Identity & content
     */
    /*\@{*/
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged)
    /*\@}*/

    /*!
     * \name Position (layer CRS, double precision)
     * \details Stored in the owning layer's CRS. The annotation layer
     *          reprojects to canvas CRS when populating the scene so the
     *          annotation stays pinned to its map location under pan / zoom.
     */
    /*\@{*/
    Q_PROPERTY(double x READ x WRITE setX NOTIFY positionChanged)
    Q_PROPERTY(double y READ y WRITE setY NOTIFY positionChanged)
    Q_PROPERTY(double rotation READ rotation WRITE setRotation NOTIFY rotationChanged)
    /*\@}*/

    /*!
     * \name Font + fill (text body)
     */
    /*\@{*/
    Q_PROPERTY(QFont font READ font WRITE setFont NOTIFY fontChanged)
    Q_PROPERTY(QColor fillColor READ fillColor WRITE setFillColor NOTIFY fillColorChanged)
    /*\@}*/

    /*!
     * \name Outline (per-glyph stroke around the text shape)
     */
    /*\@{*/
    Q_PROPERTY(bool outlineEnabled READ outlineEnabled WRITE setOutlineEnabled NOTIFY
                   outlineEnabledChanged)
    Q_PROPERTY(
        QColor outlineColor READ outlineColor WRITE setOutlineColor NOTIFY outlineColorChanged)
    Q_PROPERTY(
        double outlineWidth READ outlineWidth WRITE setOutlineWidth NOTIFY outlineWidthChanged)
    /*\@}*/

    /*!
     * \name Halo (soft glow behind text for legibility)
     */
    /*\@{*/
    Q_PROPERTY(bool haloEnabled READ haloEnabled WRITE setHaloEnabled NOTIFY haloEnabledChanged)
    Q_PROPERTY(QColor haloColor READ haloColor WRITE setHaloColor NOTIFY haloColorChanged)
    Q_PROPERTY(double haloRadius READ haloRadius WRITE setHaloRadius NOTIFY haloRadiusChanged)
    /*\@}*/

    /*!
     * \name Background (rectangular box behind text)
     */
    /*\@{*/
    Q_PROPERTY(bool backgroundEnabled READ backgroundEnabled WRITE setBackgroundEnabled NOTIFY
                   backgroundEnabledChanged)
    Q_PROPERTY(QColor backgroundFillColor READ backgroundFillColor WRITE setBackgroundFillColor
                   NOTIFY backgroundFillColorChanged)
    Q_PROPERTY(QColor backgroundOutlineColor READ backgroundOutlineColor WRITE
                   setBackgroundOutlineColor NOTIFY backgroundOutlineColorChanged)
    Q_PROPERTY(double backgroundOutlineWidth READ backgroundOutlineWidth WRITE
                   setBackgroundOutlineWidth NOTIFY backgroundOutlineWidthChanged)
    Q_PROPERTY(double backgroundPadding READ backgroundPadding WRITE setBackgroundPadding NOTIFY
                   backgroundPaddingChanged)
    Q_PROPERTY(double backgroundCornerRadius READ backgroundCornerRadius WRITE
                   setBackgroundCornerRadius NOTIFY backgroundCornerRadiusChanged)
    /*\@}*/

public:
    /**
     * @brief Create text/style state with a fresh UUID and ten-point font.
     * @param[in] parent Optional QObject owner. No scene item or render resources are allocated.
     */
    explicit AnnotationTextItem(QObject *parent = nullptr);
    /**
     * @brief Release the annotation value model through QObject ownership; an attached layer must remove its raw membership first if deleting independently.
     */
    ~AnnotationTextItem() override = default;

    /*! Persistent identifier — used by undo commands to find the right item
     *  across redo/undo and by the project serializer for round-tripping.
     * @return Current exact identity by value; constructor generates a UUID but setId()/fromJson() may replace it.
     */
    [[nodiscard]] QString id() const { return m_id; }

    /*! Mostly used by load-from-JSON to preserve identifiers across saves.
     * @param[in] id New identity copied verbatim, including empty; no uniqueness check or notification occurs.
     * @note Use before layer attachment/undo-reference binding, because layer membership/cache lookups use this key.
     */
    void setId(const QString &id) { m_id = id; }

    // ----- Getters -----------------------------------------------------------
    /**
     * @brief Inspect stored annotation state without layout/source resolution.
     * @return Literal text, including newlines; linked expressions can replace it at paint time. Returns a value copy.
     */
    [[nodiscard]] QString text() const { return m_text; }
    /**
     * @brief Inspect stored annotation state without layout/source resolution.
     * @return Anchor map X in the owning layer's CRS, copied without finite/range checks. Returns a value copy.
     */
    [[nodiscard]] double x() const { return m_x; }
    /**
     * @brief Inspect stored annotation state without layout/source resolution.
     * @return Anchor map Y in the owning layer's CRS (not scene-inverted Y). Returns a value copy.
     */
    [[nodiscard]] double y() const { return m_y; }
    /**
     * @brief Inspect stored annotation state without layout/source resolution.
     * @return Counterclockwise angle in degrees; rendering applies negative rotation in pixel coordinates. No normalization occurs. Returns a value copy.
     */
    [[nodiscard]] double rotation() const { return m_rotation; }
    /**
     * @brief Inspect stored annotation state without layout/source resolution.
     * @return Glyph font with Qt point/pixel sizing; no validity/minimum-size enforcement. Returns a value copy.
     */
    [[nodiscard]] QFont font() const { return m_font; }
    /**
     * @brief Inspect stored annotation state without layout/source resolution.
     * @return Glyph fill colour including alpha, copied without validity checks. Returns a value copy.
     */
    [[nodiscard]] QColor fillColor() const { return m_fillColor; }

    /**
     * @brief Inspect stored annotation state without layout/source resolution.
     * @return Whether the glyph-outline pass is requested. Returns a value copy.
     */
    [[nodiscard]] bool outlineEnabled() const { return m_outlineEnabled; }
    /**
     * @brief Inspect stored annotation state without layout/source resolution.
     * @return Glyph-outline colour including alpha; no validity check. Returns a value copy.
     */
    [[nodiscard]] QColor outlineColor() const { return m_outlineColor; }
    /**
     * @brief Inspect stored annotation state without layout/source resolution.
     * @return Glyph-outline width in logical pixels; nonpositive disables its paint pass but the raw value is stored. Returns a value copy.
     */
    [[nodiscard]] double outlineWidth() const { return m_outlineWidth; }

    /**
     * @brief Inspect stored annotation state without layout/source resolution.
     * @return Whether a rounded halo stroke is requested behind glyphs. Returns a value copy.
     */
    [[nodiscard]] bool haloEnabled() const { return m_haloEnabled; }
    /**
     * @brief Inspect stored annotation state without layout/source resolution.
     * @return Halo stroke colour including alpha. Returns a value copy.
     */
    [[nodiscard]] QColor haloColor() const { return m_haloColor; }
    /**
     * @brief Inspect stored annotation state without layout/source resolution.
     * @return Halo radius in logical pixels, painted at twice this stroke width; nonpositive suppresses the pass. Returns a value copy.
     */
    [[nodiscard]] double haloRadius() const { return m_haloRadius; }

    /**
     * @brief Inspect stored annotation state without layout/source resolution.
     * @return Whether to paint a padded rounded card behind glyphs. Returns a value copy.
     */
    [[nodiscard]] bool backgroundEnabled() const { return m_bgEnabled; }
    /**
     * @brief Inspect stored annotation state without layout/source resolution.
     * @return Background card colour including alpha. Returns a value copy.
     */
    [[nodiscard]] QColor backgroundFillColor() const { return m_bgFill; }
    /**
     * @brief Inspect stored annotation state without layout/source resolution.
     * @return Card border colour including alpha. Returns a value copy.
     */
    [[nodiscard]] QColor backgroundOutlineColor() const { return m_bgOutline; }
    /**
     * @brief Inspect stored annotation state without layout/source resolution.
     * @return Card border width in logical pixels; nonpositive uses NoPen. Returns a value copy.
     */
    [[nodiscard]] double backgroundOutlineWidth() const { return m_bgOutlineWidth; }
    /**
     * @brief Inspect stored annotation state without layout/source resolution.
     * @return Card expansion around glyph path bounds in logical pixels; copied without clamping. Returns a value copy.
     */
    [[nodiscard]] double backgroundPadding() const { return m_bgPadding; }
    /**
     * @brief Inspect stored annotation state without layout/source resolution.
     * @return Card corner radius in logical pixels, copied without clamping. Returns a value copy.
     */
    [[nodiscard]] double backgroundCornerRadius() const { return m_bgCornerRadius; }

    // A typed, persistent feature reference. Empty source means free text.
    /**
     * @brief Inspect linked annotation/layout policy.
     * @return Persistent model/GIS feature identity; empty makes it free text. No source lookup or schema validation occurs. Returns a value copy.
     */
    QJsonObject featureReference() const { return m_featureReference; }
    /**
     * @brief Replace linked annotation/layout policy.
     * @param[in] ref Persistent model/GIS feature identity; empty makes it free text. No source lookup or schema validation occurs.
     * @note Exact equality is a no-op; otherwise emits changed() only, without a typed style notification or automatic source resolution.
     */
    void setFeatureReference(const QJsonObject &ref);
    /**
     * @brief Inspect linked annotation/layout policy.
     * @return Brace-token linked-text template interpreted by AnnotationFeatures::evaluate(), not a general expression language. Returns a value copy.
     */
    QString expression() const { return m_expression; }
    /**
     * @brief Replace linked annotation/layout policy.
     * @param[in] expression Brace-token linked-text template interpreted by AnnotationFeatures::evaluate(), not a general expression language.
     * @note Exact equality is a no-op; otherwise emits changed() only, without a typed style notification or automatic source resolution.
     */
    void setExpression(const QString &expression);
    /**
     * @brief Inspect linked annotation/layout policy.
     * @return Whether a leader line is painted for a successfully resolved linked feature outside the label rectangle. Returns a value copy.
     */
    bool calloutEnabled() const { return m_calloutEnabled; }
    /**
     * @brief Replace linked annotation/layout policy.
     * @param[in] enabled Whether a leader line is painted for a successfully resolved linked feature outside the label rectangle.
     * @note Exact equality is a no-op; otherwise emits changed() only, without a typed style notification or automatic source resolution.
     */
    void setCalloutEnabled(bool enabled);
    /**
     * @brief Inspect linked annotation/layout policy.
     * @return Stored logical-pixel XY displacement from anchor, positive right/down; automatic placement adds a transient cached offset without rewriting this value. Returns a value copy.
     */
    QPointF offset() const { return m_offset; }
    /**
     * @brief Replace linked annotation/layout policy.
     * @param[in] offset Stored logical-pixel XY displacement from anchor, positive right/down; automatic placement adds a transient cached offset without rewriting this value.
     * @note Exact equality is a no-op; otherwise emits changed() only, without a typed style notification or automatic source resolution.
     */
    void setOffset(const QPointF &offset);
    /**
     * @brief Inspect linked annotation/layout policy.
     * @return Whether shared automatic collision placement must retain this stored offset; manual/pinned labels may overlap. Returns a value copy.
     */
    bool pinned() const { return m_pinned; }
    /**
     * @brief Replace linked annotation/layout policy.
     * @param[in] pinned Whether shared automatic collision placement must retain this stored offset; manual/pinned labels may overlap.
     * @note Exact equality is a no-op; otherwise emits changed() only, without a typed style notification or automatic source resolution.
     */
    void setPinned(bool pinned);

    /*! Serialize to a project-file JSON entry.
     * @return Owned raw identity/content/position/style/link/offset/pin object. Colours encode ARGB, font uses QFont text; no source resolution, validation or disk IO occurs.
     */
    [[nodiscard]] QJsonObject toJson() const;

    /*! Restore a permissive JSON entry. Traditional style fields retain
     *  current values for missing keys; linked/layout fields use their
     *  defaults. Restores a supplied `id` instead of regenerating it.
     * @param[in] obj Permissive entry decoded directly without typed setters or numeric clamps. Missing/ill-typed traditional style fields retain current values; absent feature/expression/callout/offset/pinned fields reset those newer fields to empty/false/zero.
     * @note Restores an explicitly present string ID, including empty, without uniqueness checks. Invalid colour/font strings retain prior values. Always emits positionChanged(), textChanged() then changed(), without all typed style notifications; no error result/rollback is supplied.
     */
    void fromJson(const QJsonObject &obj);

public slots:
    /**
     * @brief Edit the persisted annotation presentation value.
     * @param[in] t Literal text, including newlines; linked expressions can replace it at paint time.
     * @note Exact equality is a no-op. A change emits textChanged() then changed(). Numeric/style inputs are not otherwise validated or clamped; use finite meaningful geometry.
     */
    void setText(const QString &t);
    /**
     * @brief Edit the persisted annotation presentation value.
     * @param[in] v Anchor map X in the owning layer's CRS, copied without finite/range checks.
     * @note Exact equality is a no-op. A change emits positionChanged() then changed(). Numeric/style inputs are not otherwise validated or clamped; use finite meaningful geometry.
     */
    void setX(double v);
    /**
     * @brief Edit the persisted annotation presentation value.
     * @param[in] v Anchor map Y in the owning layer's CRS (not scene-inverted Y).
     * @note Exact equality is a no-op. A change emits positionChanged() then changed(). Numeric/style inputs are not otherwise validated or clamped; use finite meaningful geometry.
     */
    void setY(double v);
    /**
     * @brief Update both anchor coordinates as one notification unit.
     * @param[in] x Layer-CRS map X, expected finite.
     * @param[in] y Layer-CRS map Y, expected finite.
     * @note Exact pair equality is a no-op; otherwise emits positionChanged() then changed() once. No reprojection/range validation occurs.
     */
    void setPosition(double x, double y);
    /**
     * @brief Edit the persisted annotation presentation value.
     * @param[in] deg Counterclockwise angle in degrees; rendering applies negative rotation in pixel coordinates. No normalization occurs.
     * @note Exact equality is a no-op. A change emits rotationChanged() then changed(). Numeric/style inputs are not otherwise validated or clamped; use finite meaningful geometry.
     */
    void setRotation(double deg);
    /**
     * @brief Edit the persisted annotation presentation value.
     * @param[in] f Glyph font with Qt point/pixel sizing; no validity/minimum-size enforcement.
     * @note Exact equality is a no-op. A change emits fontChanged() then changed(). Numeric/style inputs are not otherwise validated or clamped; use finite meaningful geometry.
     */
    void setFont(const QFont &f);
    /**
     * @brief Edit the persisted annotation presentation value.
     * @param[in] c Glyph fill colour including alpha, copied without validity checks.
     * @note Exact equality is a no-op. A change emits fillColorChanged() then changed(). Numeric/style inputs are not otherwise validated or clamped; use finite meaningful geometry.
     */
    void setFillColor(const QColor &c);

    /**
     * @brief Edit the persisted annotation presentation value.
     * @param[in] on Whether the glyph-outline pass is requested.
     * @note Exact equality is a no-op. A change emits outlineEnabledChanged() then changed(). Numeric/style inputs are not otherwise validated or clamped; use finite meaningful geometry.
     */
    void setOutlineEnabled(bool on);
    /**
     * @brief Edit the persisted annotation presentation value.
     * @param[in] c Glyph-outline colour including alpha; no validity check.
     * @note Exact equality is a no-op. A change emits outlineColorChanged() then changed(). Numeric/style inputs are not otherwise validated or clamped; use finite meaningful geometry.
     */
    void setOutlineColor(const QColor &c);
    /**
     * @brief Edit the persisted annotation presentation value.
     * @param[in] w Glyph-outline width in logical pixels; nonpositive disables its paint pass but the raw value is stored.
     * @note Exact equality is a no-op. A change emits outlineWidthChanged() then changed(). Numeric/style inputs are not otherwise validated or clamped; use finite meaningful geometry.
     */
    void setOutlineWidth(double w);

    /**
     * @brief Edit the persisted annotation presentation value.
     * @param[in] on Whether a rounded halo stroke is requested behind glyphs.
     * @note Exact equality is a no-op. A change emits haloEnabledChanged() then changed(). Numeric/style inputs are not otherwise validated or clamped; use finite meaningful geometry.
     */
    void setHaloEnabled(bool on);
    /**
     * @brief Edit the persisted annotation presentation value.
     * @param[in] c Halo stroke colour including alpha.
     * @note Exact equality is a no-op. A change emits haloColorChanged() then changed(). Numeric/style inputs are not otherwise validated or clamped; use finite meaningful geometry.
     */
    void setHaloColor(const QColor &c);
    /**
     * @brief Edit the persisted annotation presentation value.
     * @param[in] r Halo radius in logical pixels, painted at twice this stroke width; nonpositive suppresses the pass.
     * @note Exact equality is a no-op. A change emits haloRadiusChanged() then changed(). Numeric/style inputs are not otherwise validated or clamped; use finite meaningful geometry.
     */
    void setHaloRadius(double r);

    /**
     * @brief Edit the persisted annotation presentation value.
     * @param[in] on Whether to paint a padded rounded card behind glyphs.
     * @note Exact equality is a no-op. A change emits backgroundEnabledChanged() then changed(). Numeric/style inputs are not otherwise validated or clamped; use finite meaningful geometry.
     */
    void setBackgroundEnabled(bool on);
    /**
     * @brief Edit the persisted annotation presentation value.
     * @param[in] c Background card colour including alpha.
     * @note Exact equality is a no-op. A change emits backgroundFillColorChanged() then changed(). Numeric/style inputs are not otherwise validated or clamped; use finite meaningful geometry.
     */
    void setBackgroundFillColor(const QColor &c);
    /**
     * @brief Edit the persisted annotation presentation value.
     * @param[in] c Card border colour including alpha.
     * @note Exact equality is a no-op. A change emits backgroundOutlineColorChanged() then changed(). Numeric/style inputs are not otherwise validated or clamped; use finite meaningful geometry.
     */
    void setBackgroundOutlineColor(const QColor &c);
    /**
     * @brief Edit the persisted annotation presentation value.
     * @param[in] w Card border width in logical pixels; nonpositive uses NoPen.
     * @note Exact equality is a no-op. A change emits backgroundOutlineWidthChanged() then changed(). Numeric/style inputs are not otherwise validated or clamped; use finite meaningful geometry.
     */
    void setBackgroundOutlineWidth(double w);
    /**
     * @brief Edit the persisted annotation presentation value.
     * @param[in] p Card expansion around glyph path bounds in logical pixels; copied without clamping.
     * @note Exact equality is a no-op. A change emits backgroundPaddingChanged() then changed(). Numeric/style inputs are not otherwise validated or clamped; use finite meaningful geometry.
     */
    void setBackgroundPadding(double p);
    /**
     * @brief Edit the persisted annotation presentation value.
     * @param[in] r Card corner radius in logical pixels, copied without clamping.
     * @note Exact equality is a no-op. A change emits backgroundCornerRadiusChanged() then changed(). Numeric/style inputs are not otherwise validated or clamped; use finite meaningful geometry.
     */
    void setBackgroundCornerRadius(double r);

signals:
    /**
     * @brief Notify a typed annotation-state edit before aggregate changed().
     * @param[in] value New raw value. Literal text, including newlines; linked expressions can replace it at paint time.
     * @note fromJson() emits only position/text/aggregate notifications, not every typed style signal.
     */
    void textChanged(const QString &value);
    /**
     * @brief Notify X/Y edits or a JSON restore; inspect x()/y() for the new layer-CRS anchor.
     */
    void positionChanged();
    /**
     * @brief Notify a typed annotation-state edit before aggregate changed().
     * @param[in] value New raw value. Counterclockwise angle in degrees; rendering applies negative rotation in pixel coordinates. No normalization occurs.
     * @note fromJson() emits only position/text/aggregate notifications, not every typed style signal.
     */
    void rotationChanged(double value);
    /**
     * @brief Notify a typed annotation-state edit before aggregate changed().
     * @param[in] value New raw value. Glyph font with Qt point/pixel sizing; no validity/minimum-size enforcement.
     * @note fromJson() emits only position/text/aggregate notifications, not every typed style signal.
     */
    void fontChanged(const QFont &value);
    /**
     * @brief Notify a typed annotation-state edit before aggregate changed().
     * @param[in] value New raw value. Glyph fill colour including alpha, copied without validity checks.
     * @note fromJson() emits only position/text/aggregate notifications, not every typed style signal.
     */
    void fillColorChanged(const QColor &value);

    /**
     * @brief Notify a typed annotation-state edit before aggregate changed().
     * @param[in] value New raw value. Whether the glyph-outline pass is requested.
     * @note fromJson() emits only position/text/aggregate notifications, not every typed style signal.
     */
    void outlineEnabledChanged(bool value);
    /**
     * @brief Notify a typed annotation-state edit before aggregate changed().
     * @param[in] value New raw value. Glyph-outline colour including alpha; no validity check.
     * @note fromJson() emits only position/text/aggregate notifications, not every typed style signal.
     */
    void outlineColorChanged(const QColor &value);
    /**
     * @brief Notify a typed annotation-state edit before aggregate changed().
     * @param[in] value New raw value. Glyph-outline width in logical pixels; nonpositive disables its paint pass but the raw value is stored.
     * @note fromJson() emits only position/text/aggregate notifications, not every typed style signal.
     */
    void outlineWidthChanged(double value);

    /**
     * @brief Notify a typed annotation-state edit before aggregate changed().
     * @param[in] value New raw value. Whether a rounded halo stroke is requested behind glyphs.
     * @note fromJson() emits only position/text/aggregate notifications, not every typed style signal.
     */
    void haloEnabledChanged(bool value);
    /**
     * @brief Notify a typed annotation-state edit before aggregate changed().
     * @param[in] value New raw value. Halo stroke colour including alpha.
     * @note fromJson() emits only position/text/aggregate notifications, not every typed style signal.
     */
    void haloColorChanged(const QColor &value);
    /**
     * @brief Notify a typed annotation-state edit before aggregate changed().
     * @param[in] value New raw value. Halo radius in logical pixels, painted at twice this stroke width; nonpositive suppresses the pass.
     * @note fromJson() emits only position/text/aggregate notifications, not every typed style signal.
     */
    void haloRadiusChanged(double value);

    /**
     * @brief Notify a typed annotation-state edit before aggregate changed().
     * @param[in] value New raw value. Whether to paint a padded rounded card behind glyphs.
     * @note fromJson() emits only position/text/aggregate notifications, not every typed style signal.
     */
    void backgroundEnabledChanged(bool value);
    /**
     * @brief Notify a typed annotation-state edit before aggregate changed().
     * @param[in] value New raw value. Background card colour including alpha.
     * @note fromJson() emits only position/text/aggregate notifications, not every typed style signal.
     */
    void backgroundFillColorChanged(const QColor &value);
    /**
     * @brief Notify a typed annotation-state edit before aggregate changed().
     * @param[in] value New raw value. Card border colour including alpha.
     * @note fromJson() emits only position/text/aggregate notifications, not every typed style signal.
     */
    void backgroundOutlineColorChanged(const QColor &value);
    /**
     * @brief Notify a typed annotation-state edit before aggregate changed().
     * @param[in] value New raw value. Card border width in logical pixels; nonpositive uses NoPen.
     * @note fromJson() emits only position/text/aggregate notifications, not every typed style signal.
     */
    void backgroundOutlineWidthChanged(double value);
    /**
     * @brief Notify a typed annotation-state edit before aggregate changed().
     * @param[in] value New raw value. Card expansion around glyph path bounds in logical pixels; copied without clamping.
     * @note fromJson() emits only position/text/aggregate notifications, not every typed style signal.
     */
    void backgroundPaddingChanged(double value);
    /**
     * @brief Notify a typed annotation-state edit before aggregate changed().
     * @param[in] value New raw value. Card corner radius in logical pixels, copied without clamping.
     * @note fromJson() emits only position/text/aggregate notifications, not every typed style signal.
     */
    void backgroundCornerRadiusChanged(double value);

    /*! Aggregate signal — fires once after any setter mutates state. The
     *  layer's graphics item connects to this to trigger a single repaint
     *  per edit, instead of subscribing to every per-field NOTIFY.
     * @note Emitted after effective state setters and every fromJson(); setId() is silent. The layer forwards this into annotation/repaint requests but no drawing happens inside the item.
     */
    void changed();

private:
    QJsonObject m_featureReference;
    QString m_expression;
    bool m_calloutEnabled = false;
    QPointF m_offset;
    bool m_pinned = false;
    QString m_id; ///< Stable per-item identifier (UUID).
    QString m_text = QStringLiteral("Text");
    double m_x = 0.0;
    double m_y = 0.0;
    double m_rotation = 0.0; ///< Degrees, CCW.

    QFont m_font;
    QColor m_fillColor = Qt::black;

    bool m_outlineEnabled = false;
    QColor m_outlineColor = Qt::white;
    double m_outlineWidth = 1.0;

    bool m_haloEnabled = true;
    QColor m_haloColor = QColor(255, 255, 255, 220);
    double m_haloRadius = 2.0;

    bool m_bgEnabled = false;
    QColor m_bgFill = QColor(255, 255, 255, 200);
    QColor m_bgOutline = QColor(80, 80, 80, 255);
    double m_bgOutlineWidth = 1.0;
    double m_bgPadding = 4.0;
    double m_bgCornerRadius = 3.0;
};

#endif // OPENSWMMVIS_LAYERS_ANNOTATIONTEXTITEM_H
