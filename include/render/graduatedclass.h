/*!
 * \file   graduatedclass.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice GRAD.1 — one row in the graduated class-table editor. Each
 * instance carries a (minValue, maxValue, color, label, visible) tuple
 * and exposes them as Q_PROPERTYs so QPropertyModel / any property-
 * driven view can edit them without per-row glue code.
 *
 * The GraduatedRenderer owns a QList<GraduatedClass *> that drives the
 * paint loop's color picking when non-empty (overrides the ramp).
 */
#ifndef OPENSWMM_RENDER_GRADUATEDCLASS_H
#define OPENSWMM_RENDER_GRADUATEDCLASS_H

#include <QColor>
#include <QJsonObject>
#include <QObject>
#include <QString>

namespace OpenSWMM::Render
{

class GraduatedClass : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QColor   color    READ color    WRITE setColor    NOTIFY colorChanged)
    Q_PROPERTY(double   minValue READ minValue WRITE setMinValue NOTIFY minValueChanged)
    Q_PROPERTY(double   maxValue READ maxValue WRITE setMaxValue NOTIFY maxValueChanged)
    Q_PROPERTY(QString  label    READ label    WRITE setLabel    NOTIFY labelChanged)
    Q_PROPERTY(bool     visible  READ visible  WRITE setVisible  NOTIFY visibleChanged)

public:
    explicit GraduatedClass(QObject *parent = nullptr);
    GraduatedClass(double minValue, double maxValue, QColor color,
                   QString label = {}, QObject *parent = nullptr);

    [[nodiscard]] QColor  color()    const { return m_color;    }
    [[nodiscard]] double  minValue() const { return m_minValue; }
    [[nodiscard]] double  maxValue() const { return m_maxValue; }
    [[nodiscard]] QString label()    const { return m_label;    }
    [[nodiscard]] bool    visible()  const { return m_visible;  }

    void setColor(const QColor &c);
    void setMinValue(double v);
    void setMaxValue(double v);
    void setLabel(const QString &l);
    void setVisible(bool v);

    /*! True when `v` falls in [minValue, maxValue). The renderer uses
     *  this for per-feature class lookup. The last class includes its
     *  upper bound so the top of the range maps in. */
    [[nodiscard]] bool contains(double v, bool inclusiveUpper = false) const;

    [[nodiscard]] QJsonObject toJson() const;
    static GraduatedClass *fromJson(const QJsonObject &j, QObject *parent = nullptr);

signals:
    void colorChanged(const QColor &);
    void minValueChanged(double);
    void maxValueChanged(double);
    void labelChanged(const QString &);
    void visibleChanged(bool);
    /*! Aggregate signal — fires after any of the per-property NOTIFY
     *  emissions so listeners that only care "this row changed" can
     *  hook a single slot. */
    void changed();

private:
    QColor  m_color   = Qt::gray;
    double  m_minValue = 0.0;
    double  m_maxValue = 1.0;
    QString m_label;
    bool    m_visible = true;
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_GRADUATEDCLASS_H
