/*!
 * \file   scalebarsettings.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \brief  Configurable appearance settings for the map scale bar.
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
    enum Units {
        Auto,
        Meters,
        Feet,
        Kilometers,
        Miles
    };
    Q_ENUM(Units)

    enum Position {
        BottomLeft,
        BottomRight,
        TopLeft,
        TopRight
    };
    Q_ENUM(Position)

    Q_PROPERTY(QPen      pen             READ pen             WRITE setPen             NOTIFY changed)
    Q_PROPERTY(QFont     font            READ font            WRITE setFont            NOTIFY changed)
    Q_PROPERTY(Units     units           READ units           WRITE setUnits           NOTIFY changed)
    Q_PROPERTY(Position  position        READ position        WRITE setPosition        NOTIFY changed)
    Q_PROPERTY(int       maxBarLength    READ maxBarLength    WRITE setMaxBarLength    NOTIFY changed)
    Q_PROPERTY(int       labelDecimals   READ labelDecimals   WRITE setLabelDecimals   NOTIFY changed)
    Q_PROPERTY(bool      compactNotation READ compactNotation WRITE setCompactNotation NOTIFY changed)

    explicit ScaleBarSettings(QObject *parent = nullptr);

    [[nodiscard]] QPen     pen()             const { return m_pen; }
    [[nodiscard]] QFont    font()            const { return m_font; }
    [[nodiscard]] Units    units()           const { return m_units; }
    [[nodiscard]] Position position()        const { return m_position; }
    [[nodiscard]] int      maxBarLength()    const { return m_maxBarLength; }
    [[nodiscard]] int      labelDecimals()   const { return m_labelDecimals; }
    [[nodiscard]] bool     compactNotation() const { return m_compactNotation; }

    void setPen(const QPen &pen);
    void setFont(const QFont &font);
    void setUnits(Units units);
    void setPosition(Position position);
    void setMaxBarLength(int length);
    void setLabelDecimals(int decimals);
    void setCompactNotation(bool compact);

    /*!
     * \brief Format a distance (in metres) as a string using the current units setting.
     * \param metres    Distance in metres.
     * \param rawUnits  When true, ignores the Units setting and appends "units"
     *                  (used for local / undefined CRS where no real-world unit is known).
     */
    [[nodiscard]] QString formatLabel(double metres, bool rawUnits = false) const;

    /*!
     * \brief Returns the metres value that \c formatLabel() will actually display,
     *        i.e. the input rounded in the active unit. Use this to sync bar length
     *        to the label.
     */
    [[nodiscard]] double roundedMetres(double metres) const;

signals:
    void changed();

private:
    QPen     m_pen             { QPen(Qt::black, 2) };
    QFont    m_font            { QFont(QStringLiteral("sans-serif"), 8) };
    Units    m_units           { Auto };
    Position m_position        { BottomLeft };
    int      m_maxBarLength    { 100 };
    int      m_labelDecimals   { -1 };   // -1 = auto
    bool     m_compactNotation { false };
};

#endif // SCALEBARSETTINGS_H
