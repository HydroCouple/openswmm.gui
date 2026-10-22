/**
 * @file      scalebarsettings.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @brief     Configurable appearance settings for the map scale bar.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef SCALEBARSETTINGS_H
#define SCALEBARSETTINGS_H

#include <QFont>
#include <QObject>
#include <QPen>

/*!
 * \class ScaleBarSettings
 * \brief Holds all configurable appearance properties for the map scale bar.
 *
 * Owned by MapCanvas as a child QObject and exposed via a CONSTANT Q_PROPERTY
 * so it can be passed directly to QPropertyModel for live editing.
 *
 * Connect the \c changed() signal to MapCanvas::update() so any property
 * change immediately repaints the canvas.
 */
class ScaleBarSettings : public QObject
{
    Q_OBJECT

public:
    enum Units { Auto, Meters, Feet, Kilometers, Miles };
    Q_ENUM(Units)

    enum Position { BottomLeft, BottomRight, TopLeft, TopRight };
    Q_ENUM(Position)

    Q_PROPERTY(QPen pen READ pen WRITE setPen NOTIFY changed)
    Q_PROPERTY(QFont font READ font WRITE setFont NOTIFY changed)
    Q_PROPERTY(Units units READ units WRITE setUnits NOTIFY changed)
    Q_PROPERTY(Position position READ position WRITE setPosition NOTIFY changed)
    Q_PROPERTY(int maxBarLength READ maxBarLength WRITE setMaxBarLength NOTIFY changed)
    Q_PROPERTY(int labelDecimals READ labelDecimals WRITE setLabelDecimals NOTIFY changed)
    Q_PROPERTY(bool compactNotation READ compactNotation WRITE setCompactNotation NOTIFY changed)

    /**
     * @param[in] parent Optional QObject owner, usually MapCanvas.
     * @note Initializes black2-pixel pen, sans-serif8point font, Auto metric unit choice,
     *   BottomLeft, 100pixel maximum, automatic decimal policy and noncompact labels. Mutations
     *   require owning GUI thread; format queries only read state.
     */
    explicit ScaleBarSettings(QObject *parent = nullptr);

    /**
     * @return Value copy of bar stroke pen, width in logical painter pixels unless caller
     *   configures otherwise.
     */
    [[nodiscard]] QPen pen() const { return m_pen; }
    /**
     * @return Value copy of label font, initially8point sans-serif.
     */
    [[nodiscard]] QFont font() const { return m_font; }
    /**
     * @return Raw display-unit enum, initially Auto; Auto selects metric metre/kilometre only, not
     *   CRS imperial units.
     */
    [[nodiscard]] Units units() const { return m_units; }
    /**
     * @return Raw placement enum, initially BottomLeft; canvas interprets it.
     */
    [[nodiscard]] Position position() const { return m_position; }
    /**
     * @return Raw maximum nominal bar length in logical pixels, initially100; not enforced inside
     *   numeric formatting helpers.
     */
    [[nodiscard]] int maxBarLength() const { return m_maxBarLength; }
    /**
     * @return Raw fixed decimal count, negative means automatic magnitude-based0/1/2decimal places;
     *   initially minus one.
     */
    [[nodiscard]] int labelDecimals() const { return m_labelDecimals; }
    /**
     * @return Whether metric labels remove space/use k instead of km; feet/miles/raw labels retain
     *   normal suffix spacing.
     */
    [[nodiscard]] bool compactNotation() const { return m_compactNotation; }

    /**
     * @param[in] pen Stroke pen value, including width/style/alpha; copied without clamp.
     * @note Equality no-op; changed value emits changed exactly once. Does not persist settings or
     *   repaint itself; canvas observers react.
     */
    void setPen(const QPen &pen);
    /**
     * @param[in] font Font value, including point/pixel size; copied without normalization.
     * @note Equality no-op; changed value emits changed exactly once. Does not persist settings or
     *   repaint itself; canvas observers react.
     */
    void setFont(const QFont &font);
    /**
     * @param[in] units Display enum; raw value stored without enum validation.
     * @note Equality no-op; changed value emits changed exactly once. Does not persist settings or
     *   repaint itself; canvas observers react.
     */
    void setUnits(Units units);
    /**
     * @param[in] position Placement enum; raw value stored without validation.
     * @note Equality no-op; changed value emits changed exactly once. Does not persist settings or
     *   repaint itself; canvas observers react.
     */
    void setPosition(Position position);
    /**
     * @param[in] length Raw logical pixel length; no positive/minimum clamp.
     * @note Equality no-op; changed value emits changed exactly once. Does not persist settings or
     *   repaint itself; canvas observers react.
     */
    void setMaxBarLength(int length);
    /**
     * @param[in] decimals Any negative uses auto policy; nonnegative fixed decimals passed through
     *   to pow/QString formatting, without upper bound.
     * @note Equality no-op; changed value emits changed exactly once. Does not persist settings or
     *   repaint itself; canvas observers react.
     */
    void setLabelDecimals(int decimals);
    /**
     * @param[in] compact Whether metric suffix uses compact notation.
     * @note Equality no-op; changed value emits changed exactly once. Does not persist settings or
     *   repaint itself; canvas observers react.
     */
    void setCompactNotation(bool compact);

    /*!
     * \brief Format a distance (in metres) as a string using the current units setting.
     * \param metres    Distance in metres.
     * \param rawUnits  When true, ignores the Units setting and appends "units"
     *                  (used for local / undefined CRS where no real-world unit is known).
     * @return Owned distance plus suffix, empty for unsupported Units enum when rawUnits false.
     *   Auto chooses km when INPUT metres>=1000, otherwise m. Feet/miles use fixed international
     *   conversions.
     * @note Negative decimals choose2places for displayed value<10, 1for<100, otherwise0; trailing
     *   zeros are retained, not trimmed. Fixed decimals use round(v*10^n)/10^n and fixed format. No
     *   finite/nonnegative guard; caller provides sensible positive finite lengths. rawUnits
     *   interprets supplied number verbatim and ignores metric conversion/compact suffix.
     */
    [[nodiscard]] QString formatLabel(double metres, bool rawUnits = false) const;

    /*!
     * \brief Returns the metres value that \c formatLabel() will actually display,
     *        i.e. the input rounded in the active unit. Use this to sync bar length
     *        to the label.
     * @param[in] metres Distance metres, finite practical range expected.
     * @return Distance rounded in selected display units then converted back to metres; unknown
     *   enum returns input unchanged. Auto threshold chosen from unrounded input, so label/bar must
     *   use consistent basis. No raw-unit option, nonfinite/decimal-overflow guard or mutation.
     */
    [[nodiscard]] double roundedMetres(double metres) const;
    /**
     * @param[in] maximumMetres Maximum positive finite distance metres, or arbitrary raw-unit
     *   number when rawUnits true.
     * @param[in] rawUnits True bypasses configured real-world unit conversion.
     * @return Largest1/2/5decade distance in selected display units not exceeding ordinary positive
     *   maximum; 0for nonfinite/nonpositive input. Extreme under/overflow can propagate arithmetic
     *   limitations. Does not round by labelDecimals or apply maxBarLength/compact formatting.
     */
    [[nodiscard]] double niceMetres(double maximumMetres, bool rawUnits = false) const;

signals:
    /**
     * @brief Notify one accepted appearance-property value change; no payload, persistence or
     *   direct canvas repaint.
     */
    void changed();

private:
    QPen m_pen{QPen(Qt::black, 2)};
    QFont m_font{QFont(QStringLiteral("sans-serif"), 8)};
    Units m_units{Auto};
    Position m_position{BottomLeft};
    int m_maxBarLength{100};
    int m_labelDecimals{-1}; // -1 = auto
    bool m_compactNotation{false};
};

#endif // SCALEBARSETTINGS_H
