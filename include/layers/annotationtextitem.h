/*!
 * \file   annotationtextitem.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Per-text-annotation data model.
 *
 * A pure QObject (no QGraphicsItem inheritance) so a QPropertyModel can drive
 * the style editor directly and the annotation layer can re-emit a single
 * aggregate `changed()` signal on every edit (live preview hook). Position is
 * stored in the owning layer's CRS; the layer reprojects to canvas CRS when
 * populating the scene.
 */
#ifndef OPENSWMMVIS_LAYERS_ANNOTATIONTEXTITEM_H
#define OPENSWMMVIS_LAYERS_ANNOTATIONTEXTITEM_H

#include <QColor>
#include <QFont>
#include <QJsonObject>
#include <QObject>
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
    Q_PROPERTY(QString text       READ text       WRITE setText       NOTIFY textChanged)
    /*\@}*/

    /*!
     * \name Position (layer CRS, double precision)
     * \details Stored in the owning layer's CRS. The annotation layer
     *          reprojects to canvas CRS when populating the scene so the
     *          annotation stays pinned to its map location under pan / zoom.
     */
    /*\@{*/
    Q_PROPERTY(double  x          READ x          WRITE setX          NOTIFY positionChanged)
    Q_PROPERTY(double  y          READ y          WRITE setY          NOTIFY positionChanged)
    Q_PROPERTY(double  rotation   READ rotation   WRITE setRotation   NOTIFY rotationChanged)
    /*\@}*/

    /*!
     * \name Font + fill (text body)
     */
    /*\@{*/
    Q_PROPERTY(QFont   font       READ font       WRITE setFont       NOTIFY fontChanged)
    Q_PROPERTY(QColor  fillColor  READ fillColor  WRITE setFillColor  NOTIFY fillColorChanged)
    /*\@}*/

    /*!
     * \name Outline (per-glyph stroke around the text shape)
     */
    /*\@{*/
    Q_PROPERTY(bool    outlineEnabled READ outlineEnabled WRITE setOutlineEnabled NOTIFY outlineEnabledChanged)
    Q_PROPERTY(QColor  outlineColor   READ outlineColor   WRITE setOutlineColor   NOTIFY outlineColorChanged)
    Q_PROPERTY(double  outlineWidth   READ outlineWidth   WRITE setOutlineWidth   NOTIFY outlineWidthChanged)
    /*\@}*/

    /*!
     * \name Halo (soft glow behind text for legibility)
     */
    /*\@{*/
    Q_PROPERTY(bool    haloEnabled READ haloEnabled WRITE setHaloEnabled NOTIFY haloEnabledChanged)
    Q_PROPERTY(QColor  haloColor   READ haloColor   WRITE setHaloColor   NOTIFY haloColorChanged)
    Q_PROPERTY(double  haloRadius  READ haloRadius  WRITE setHaloRadius  NOTIFY haloRadiusChanged)
    /*\@}*/

    /*!
     * \name Background (rectangular box behind text)
     */
    /*\@{*/
    Q_PROPERTY(bool    backgroundEnabled    READ backgroundEnabled    WRITE setBackgroundEnabled    NOTIFY backgroundEnabledChanged)
    Q_PROPERTY(QColor  backgroundFillColor  READ backgroundFillColor  WRITE setBackgroundFillColor  NOTIFY backgroundFillColorChanged)
    Q_PROPERTY(QColor  backgroundOutlineColor READ backgroundOutlineColor WRITE setBackgroundOutlineColor NOTIFY backgroundOutlineColorChanged)
    Q_PROPERTY(double  backgroundOutlineWidth READ backgroundOutlineWidth WRITE setBackgroundOutlineWidth NOTIFY backgroundOutlineWidthChanged)
    Q_PROPERTY(double  backgroundPadding    READ backgroundPadding    WRITE setBackgroundPadding    NOTIFY backgroundPaddingChanged)
    Q_PROPERTY(double  backgroundCornerRadius READ backgroundCornerRadius WRITE setBackgroundCornerRadius NOTIFY backgroundCornerRadiusChanged)
    /*\@}*/

public:
    explicit AnnotationTextItem(QObject *parent = nullptr);
    ~AnnotationTextItem() override = default;

    /*! Immutable identifier — used by undo commands to find the right item
     *  across redo/undo and by the project serializer for round-tripping. */
    [[nodiscard]] QString id() const { return m_id; }

    /*! Mostly used by load-from-JSON to preserve identifiers across saves. */
    void setId(const QString &id) { m_id = id; }

    // ----- Getters -----------------------------------------------------------
    [[nodiscard]] QString text()    const { return m_text; }
    [[nodiscard]] double  x()       const { return m_x; }
    [[nodiscard]] double  y()       const { return m_y; }
    [[nodiscard]] double  rotation() const { return m_rotation; }
    [[nodiscard]] QFont   font()    const { return m_font; }
    [[nodiscard]] QColor  fillColor() const { return m_fillColor; }

    [[nodiscard]] bool   outlineEnabled() const { return m_outlineEnabled; }
    [[nodiscard]] QColor outlineColor()   const { return m_outlineColor; }
    [[nodiscard]] double outlineWidth()   const { return m_outlineWidth; }

    [[nodiscard]] bool   haloEnabled() const { return m_haloEnabled; }
    [[nodiscard]] QColor haloColor()   const { return m_haloColor; }
    [[nodiscard]] double haloRadius()  const { return m_haloRadius; }

    [[nodiscard]] bool   backgroundEnabled()      const { return m_bgEnabled; }
    [[nodiscard]] QColor backgroundFillColor()    const { return m_bgFill; }
    [[nodiscard]] QColor backgroundOutlineColor() const { return m_bgOutline; }
    [[nodiscard]] double backgroundOutlineWidth() const { return m_bgOutlineWidth; }
    [[nodiscard]] double backgroundPadding()      const { return m_bgPadding; }
    [[nodiscard]] double backgroundCornerRadius() const { return m_bgCornerRadius; }

    /*! Serialize to a project-file JSON entry. */
    [[nodiscard]] QJsonObject toJson() const;

    /*! Replace every field from a JSON entry. Missing keys keep current
     *  defaults so older saves restore cleanly under additive schema
     *  changes. Restores the persisted `id` (does NOT regenerate). */
    void fromJson(const QJsonObject &obj);

public slots:
    void setText(const QString &t);
    void setX(double v);
    void setY(double v);
    void setPosition(double x, double y);
    void setRotation(double deg);
    void setFont(const QFont &f);
    void setFillColor(const QColor &c);

    void setOutlineEnabled(bool on);
    void setOutlineColor(const QColor &c);
    void setOutlineWidth(double w);

    void setHaloEnabled(bool on);
    void setHaloColor(const QColor &c);
    void setHaloRadius(double r);

    void setBackgroundEnabled(bool on);
    void setBackgroundFillColor(const QColor &c);
    void setBackgroundOutlineColor(const QColor &c);
    void setBackgroundOutlineWidth(double w);
    void setBackgroundPadding(double p);
    void setBackgroundCornerRadius(double r);

signals:
    void textChanged(const QString &);
    void positionChanged();
    void rotationChanged(double);
    void fontChanged(const QFont &);
    void fillColorChanged(const QColor &);

    void outlineEnabledChanged(bool);
    void outlineColorChanged(const QColor &);
    void outlineWidthChanged(double);

    void haloEnabledChanged(bool);
    void haloColorChanged(const QColor &);
    void haloRadiusChanged(double);

    void backgroundEnabledChanged(bool);
    void backgroundFillColorChanged(const QColor &);
    void backgroundOutlineColorChanged(const QColor &);
    void backgroundOutlineWidthChanged(double);
    void backgroundPaddingChanged(double);
    void backgroundCornerRadiusChanged(double);

    /*! Aggregate signal — fires once after any setter mutates state. The
     *  layer's graphics item connects to this to trigger a single repaint
     *  per edit, instead of subscribing to every per-field NOTIFY. */
    void changed();

private:
    QString m_id;              ///< Stable per-item identifier (UUID).
    QString m_text = QStringLiteral("Text");
    double  m_x = 0.0;
    double  m_y = 0.0;
    double  m_rotation = 0.0;  ///< Degrees, CCW.

    QFont   m_font;
    QColor  m_fillColor = Qt::black;

    bool    m_outlineEnabled = false;
    QColor  m_outlineColor   = Qt::white;
    double  m_outlineWidth   = 1.0;

    bool    m_haloEnabled = true;
    QColor  m_haloColor   = QColor(255, 255, 255, 220);
    double  m_haloRadius  = 2.0;

    bool    m_bgEnabled       = false;
    QColor  m_bgFill          = QColor(255, 255, 255, 200);
    QColor  m_bgOutline       = QColor(80, 80, 80, 255);
    double  m_bgOutlineWidth  = 1.0;
    double  m_bgPadding       = 4.0;
    double  m_bgCornerRadius  = 3.0;
};

#endif // OPENSWMMVIS_LAYERS_ANNOTATIONTEXTITEM_H
