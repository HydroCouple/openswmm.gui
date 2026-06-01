/*!
 * \file   meshprofileplotoptions.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Q_OBJECT facade for the user-configurable display options on the 2D
 *         mesh longitudinal profile plot.  Edited via a QPropertyModel-backed
 *         tree (see MeshProfilePlotDialog's "Display Options…") and read by
 *         MeshProfilePlotWidget directly — every setter propagates through the
 *         single `changed()` signal.  Mirrors ProfilePlotOptions' mechanics
 *         (Q_ENUM positions, displayLabelFor, QPen/QBrush props) but trimmed
 *         to the continuous-cross-section render passes (no node/link theming).
 */
#ifndef MESH_PROFILE_PLOT_OPTIONS_H
#define MESH_PROFILE_PLOT_OPTIONS_H

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QObject>
#include <QPen>
#include <QPointF>
#include <QString>

class MeshProfilePlotOptions : public QObject
{
    Q_OBJECT

public:
    enum LegendPosition    { TopRight = 0, TopLeft = 1, BottomLeft = 2, BottomRight = 3 };
    Q_ENUM(LegendPosition)
    enum TimeLabelPosition { TimeTopRight = 0, TimeTopLeft = 1,
                             TimeBottomLeft = 2, TimeBottomRight = 3 };
    Q_ENUM(TimeLabelPosition)

    // ── Visibility toggles ──────────────────────────────────────────────
    Q_PROPERTY(bool showDepthFill       READ showDepthFill       WRITE setShowDepthFill       NOTIFY changed)
    Q_PROPERTY(bool showWseLine         READ showWseLine         WRITE setShowWseLine         NOTIFY changed)
    Q_PROPERTY(bool showMaxEnvelopeFill READ showMaxEnvelopeFill WRITE setShowMaxEnvelopeFill NOTIFY changed)
    Q_PROPERTY(bool showMaxEnvelopeLine READ showMaxEnvelopeLine WRITE setShowMaxEnvelopeLine NOTIFY changed)

    // ── Terrain / water styling ─────────────────────────────────────────
    Q_PROPERTY(QBrush soilFill          READ soilFill          WRITE setSoilFill          NOTIFY changed)
    Q_PROPERTY(QPen   groundLinePen     READ groundLinePen     WRITE setGroundLinePen     NOTIFY changed)
    Q_PROPERTY(QBrush depthFillBrush    READ depthFillBrush    WRITE setDepthFillBrush    NOTIFY changed)
    Q_PROPERTY(QPen   wseLinePen        READ wseLinePen        WRITE setWseLinePen        NOTIFY changed)
    Q_PROPERTY(QPen   maxEnvelopePen    READ maxEnvelopePen    WRITE setMaxEnvelopePen    NOTIFY changed)
    Q_PROPERTY(QBrush maxEnvelopeBrush  READ maxEnvelopeBrush  WRITE setMaxEnvelopeBrush  NOTIFY changed)

    // ── Legend ──────────────────────────────────────────────────────────
    Q_PROPERTY(bool legendVisible    READ legendVisible    WRITE setLegendVisible    NOTIFY changed)
    Q_PROPERTY(MeshProfilePlotOptions::LegendPosition legendPosition
               READ legendPosition WRITE setLegendPosition NOTIFY changed)
    Q_PROPERTY(QFont legendFont      READ legendFont       WRITE setLegendFont       NOTIFY changed)
    Q_PROPERTY(double legendOpacity  READ legendOpacity    WRITE setLegendOpacity    NOTIFY changed)
    Q_PROPERTY(QPointF legendOffset  READ legendOffset     WRITE setLegendOffset     NOTIFY changed)

    // ── Timestamp overlay ───────────────────────────────────────────────
    Q_PROPERTY(bool showTimeLabel    READ showTimeLabel    WRITE setShowTimeLabel    NOTIFY changed)
    Q_PROPERTY(MeshProfilePlotOptions::TimeLabelPosition timeLabelPosition
               READ timeLabelPosition WRITE setTimeLabelPosition NOTIFY changed)
    Q_PROPERTY(QColor timeLabelColor   READ timeLabelColor  WRITE setTimeLabelColor   NOTIFY changed)
    Q_PROPERTY(QFont  timeLabelFont    READ timeLabelFont   WRITE setTimeLabelFont    NOTIFY changed)
    Q_PROPERTY(QString timeLabelFormat READ timeLabelFormat WRITE setTimeLabelFormat  NOTIFY changed)
    Q_PROPERTY(QPointF timeLabelOffset READ timeLabelOffset WRITE setTimeLabelOffset  NOTIFY changed)

public:
    explicit MeshProfilePlotOptions(QObject *parent = nullptr);

    /*! \brief Human-readable label for the QPropertyModel row header. */
    Q_INVOKABLE QString displayLabelFor(const QString &propertyName) const;

    // ── Getters ─────────────────────────────────────────────────────────
    bool   showDepthFill()       const { return m_showDepthFill; }
    bool   showWseLine()         const { return m_showWseLine; }
    bool   showMaxEnvelopeFill() const { return m_showMaxEnvelopeFill; }
    bool   showMaxEnvelopeLine() const { return m_showMaxEnvelopeLine; }
    QBrush soilFill()            const { return m_soilFill; }
    QPen   groundLinePen()       const { return m_groundLinePen; }
    QBrush depthFillBrush()      const { return m_depthFillBrush; }
    QPen   wseLinePen()          const { return m_wseLinePen; }
    QPen   maxEnvelopePen()      const { return m_maxEnvelopePen; }
    QBrush maxEnvelopeBrush()    const { return m_maxEnvelopeBrush; }
    bool   legendVisible()       const { return m_legendVisible; }
    LegendPosition legendPosition() const { return m_legendPosition; }
    QFont  legendFont()          const { return m_legendFont; }
    double legendOpacity()       const { return m_legendOpacity; }
    QPointF legendOffset()       const { return m_legendOffset; }
    bool   showTimeLabel()       const { return m_showTimeLabel; }
    TimeLabelPosition timeLabelPosition() const { return m_timeLabelPosition; }
    QColor timeLabelColor()      const { return m_timeLabelColor; }
    QFont  timeLabelFont()       const { return m_timeLabelFont; }
    QString timeLabelFormat()    const { return m_timeLabelFormat; }
    QPointF timeLabelOffset()    const { return m_timeLabelOffset; }

public slots:
    void setShowDepthFill(bool v);
    void setShowWseLine(bool v);
    void setShowMaxEnvelopeFill(bool v);
    void setShowMaxEnvelopeLine(bool v);
    void setSoilFill(const QBrush &b);
    void setGroundLinePen(const QPen &p);
    void setDepthFillBrush(const QBrush &b);
    void setWseLinePen(const QPen &p);
    void setMaxEnvelopePen(const QPen &p);
    void setMaxEnvelopeBrush(const QBrush &b);
    void setLegendVisible(bool v);
    void setLegendPosition(LegendPosition p);
    void setLegendFont(const QFont &f);
    void setLegendOpacity(double a);
    void setLegendOffset(const QPointF &p);
    void setShowTimeLabel(bool v);
    void setTimeLabelPosition(TimeLabelPosition p);
    void setTimeLabelColor(const QColor &c);
    void setTimeLabelFont(const QFont &f);
    void setTimeLabelFormat(const QString &fmt);
    void setTimeLabelOffset(const QPointF &p);

signals:
    void changed();

private:
    bool m_showDepthFill       = true;
    bool m_showWseLine         = true;
    bool m_showMaxEnvelopeFill = true;
    bool m_showMaxEnvelopeLine = true;

    // Terrain earth tone (matches the 1D profile soil fill) + crisp ground
    // line; animated depth in a translucent water blue; max envelope a
    // dashed line over a faint band.
    QBrush m_soilFill         {QColor(0xC6, 0xA9, 0x7A, 160)};
    QPen   m_groundLinePen    = QPen(QColor(0x6B, 0x52, 0x2E), 1.6, Qt::SolidLine);
    QBrush m_depthFillBrush   {QColor(0x55, 0xA8, 0xE6, 120)};
    QPen   m_wseLinePen       = QPen(QColor(0x1F, 0x6F, 0xB7), 2.0, Qt::SolidLine);
    QPen   m_maxEnvelopePen   = QPen(QColor(0x1F, 0x6F, 0xB7), 1.4, Qt::DashLine);
    QBrush m_maxEnvelopeBrush {QColor(0x55, 0xA8, 0xE6, 60)};

    bool           m_legendVisible  = true;
    LegendPosition m_legendPosition = TopRight;
    QFont          m_legendFont;
    double         m_legendOpacity  = 0.86;
    QPointF        m_legendOffset   {0.0, 0.0};

    bool              m_showTimeLabel     = true;
    TimeLabelPosition m_timeLabelPosition = TimeTopLeft;
    QColor            m_timeLabelColor    {0x10, 0x10, 0x10};
    QFont             m_timeLabelFont;
    QString           m_timeLabelFormat   = QStringLiteral("dd-MMM-yyyy HH:mm:ss");
    QPointF           m_timeLabelOffset   {0.0, 0.0};
};

#endif // MESH_PROFILE_PLOT_OPTIONS_H
