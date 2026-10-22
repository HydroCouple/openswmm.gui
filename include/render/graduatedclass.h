/**
 * @file      graduatedclass.h
 * @brief     one row in the graduated class-table editor.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Slice GRAD.1 — one row in the graduated class-table editor. Each
 * instance carries a (minValue, maxValue, color, label, visible) tuple
 * and exposes them as Q_PROPERTYs so QPropertyModel / any property-
 * driven view can edit them without per-row glue code.
 *
 * The GraduatedRenderer owns a QList<GraduatedClass *> that drives the
 * paint loop's color picking when non-empty (overrides the ramp).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)
    Q_PROPERTY(double minValue READ minValue WRITE setMinValue NOTIFY minValueChanged)
    Q_PROPERTY(double maxValue READ maxValue WRITE setMaxValue NOTIFY maxValueChanged)
    Q_PROPERTY(QString label READ label WRITE setLabel NOTIFY labelChanged)
    Q_PROPERTY(bool visible READ visible WRITE setVisible NOTIFY visibleChanged)

public:
    /**
     * @brief Create a visible gray class row with bounds [0,1] and an empty label.
     * @param[in] parent Optional QObject owner; null leaves lifetime management to the caller.
     * @note Construction emits no property-change signals.
     */
    explicit GraduatedClass(QObject *parent = nullptr);

    /**
     * @brief Create a visible class row from explicitly supplied range and style values.
     * @param[in] minValue Lower class boundary in the classified variable's unit.
     * @param[in] maxValue Upper boundary in the same unit; not reordered or validated.
     * @param[in] color Initial display color, copied/moved without constructor-time validation.
     * @param[in] label User label; empty allows a renderer to provide a generated label.
     * @param[in] parent Optional QObject lifetime owner; null means caller-owned.
     * @note Unlike setColor(), this constructor does not reject an invalid QColor.
     */
    GraduatedClass(double minValue, double maxValue, QColor color, QString label = {},
                   QObject *parent = nullptr);

    /** @brief Read the class swatch. @return Current color by value, without changing the row. */
    [[nodiscard]] QColor color() const { return m_color; }
    /** @brief Read the lower class boundary. @return Stored lower bound in attribute units. */
    [[nodiscard]] double minValue() const { return m_minValue; }
    /** @brief Read the upper class boundary. @return Stored upper bound in attribute units. */
    [[nodiscard]] double maxValue() const { return m_maxValue; }
    /** @brief Read the optional user label. @return Stored label by value; empty is allowed. */
    [[nodiscard]] QString label() const { return m_label; }
    /** @brief Query class visibility. @return Stored visibility flag; no range check occurs. */
    [[nodiscard]] bool visible() const { return m_visible; }

    /**
     * @brief Replace the swatch only for a valid, changed color.
     * @param[in] c New QColor; invalid or equal input is ignored without signals.
     * @post A change emits colorChanged() followed by changed(). No other field is updated.
     */
    void setColor(const QColor &c);

    /**
     * @brief Replace the lower boundary without validating the resulting interval.
     * @param[in] v New lower value in attribute units; even non-finite/reversed bounds
     *              are stored, so callers must validate classification configuration separately.
     * @post A numerically unequal value emits minValueChanged() and then changed().
     */
    void setMinValue(double v);

    /**
     * @brief Replace the upper boundary without reordering or checking the interval.
     * @param[in] v New upper value in attribute units; not clamped or checked for finiteness.
     * @post A numerically unequal value emits maxValueChanged() followed by changed().
     */
    void setMaxValue(double v);

    /**
     * @brief Replace the user label without trimming or interpretation.
     * @param[in] l New label; empty text is allowed. Equal text is a no-op.
     * @post A change emits labelChanged() and then changed().
     */
    void setLabel(const QString &l);

    /**
     * @brief Change whether the renderer should display this class.
     * @param[in] v Desired visibility; an unchanged flag is a no-op.
     * @post A change emits visibleChanged() followed by changed().
     */
    void setVisible(bool v);

    /**
     * @brief Test interval membership independently of the visibility flag.
     * @details True when `v` falls in [minValue, maxValue). The renderer uses
     *  this for per-feature class lookup. The last class includes its
     *  upper bound so the top of the range maps in.
     * @param[in] v Sample in the same unit as the stored class boundaries.
     * @param[in] inclusiveUpper True to include maxValue, normally only for the last class.
     * @return True for v>=minValue and v<maxValue, or v<=maxValue when inclusiveUpper.
     *         No special handling of missing values, inverted bounds, or visibility occurs.
     */
    [[nodiscard]] bool contains(double v, bool inclusiveUpper = false) const;

    /**
     * @brief Serialize the complete row, including its ARGB swatch and visibility.
     * @return Owned JSON containing color, min, max, label, and visible. Bounds are not
     *         checked, and QColor is represented by its hexadecimal ARGB string.
     */
    [[nodiscard]] QJsonObject toJson() const;

    /**
     * @brief Allocate and populate a row from persisted JSON without emitting edits.
     * @param[in] j Row fields; absent/wrong-typed values fall back to defaults.
     * @param[in] parent Optional QObject owner for the newly allocated row.
     * @return Newly allocated row. Missing fields default to visible, range [0,1], and
     *         an empty label; absent/invalid swatches use gray. Caller owns it if parent is null.
     * @note Restored numeric bounds are not validated or sorted, and setters/signals are
     *       bypassed so deserialization does not publish transient property edits.
     */
    static GraduatedClass *fromJson(const QJsonObject &j, QObject *parent = nullptr);

signals:
    /**
     * @brief Notify observers after the stored swatch changes.
     * @param[in] color Current valid swatch, borrowed for the duration of signal delivery.
     * @note Emitted by setColor() before the aggregate changed() notification.
     */
    void colorChanged(const QColor &color);
    /**
     * @brief Notify observers after the lower boundary changes.
     * @param[in] value New stored lower boundary in the classified attribute's unit.
     */
    void minValueChanged(double value);
    /**
     * @brief Notify observers after the upper boundary changes.
     * @param[in] value New stored upper boundary in the classified attribute's unit.
     */
    void maxValueChanged(double value);
    /**
     * @brief Notify observers after the optional label changes.
     * @param[in] label New text, borrowed during signal delivery; empty text is permitted.
     */
    void labelChanged(const QString &label);
    /**
     * @brief Notify observers after the class visibility flag changes.
     * @param[in] visible New stored visibility flag.
     */
    void visibleChanged(bool visible);
    /*! Aggregate signal — fires after any of the per-property NOTIFY
     *  emissions so listeners that only care "this row changed" can
     *  hook a single slot.
     * @brief Notify observers that one class property was changed by a setter.
     * @note Emitted after that setter's property-specific notification. Construction
     *       and fromJson() do not emit it; ignored/equal setter inputs do not emit it.
     */
    void changed();

private:
    QColor m_color = Qt::gray;
    double m_minValue = 0.0;
    double m_maxValue = 1.0;
    QString m_label;
    bool m_visible = true;
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_GRADUATEDCLASS_H
