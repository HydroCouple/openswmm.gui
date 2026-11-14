/**
 * @file      colorbutton.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     QGIS-style color-swatch button.
 *
 *         Click → QColorDialog; the button shows the current color as a
 *         filled swatch with a thin outline. Honors alpha. Compact size
 *         so it fits comfortably in form rows alongside other editors.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_WIDGETS_COLORBUTTON_H
#define OPENSWMMVIS_UI_WIDGETS_COLORBUTTON_H

#include <QColor>
#include <QPushButton>

namespace openswmmvis::ui
{

class ColorButton : public QPushButton
{
    Q_OBJECT
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)
public:
    /**
     * @brief Create a compact black color-swatch button with alpha-enabled picking.
     * @param[in] parent Optional QWidget owner; all interaction occurs on the GUI thread.
     */
    explicit ColorButton(QWidget *parent = nullptr);
    /**
     * @brief Create the swatch button with the supplied initial color.
     * @param[in] initial Initial QColor, including an invalid no-color state; construction emits no change notification.
     * @param[in] parent Optional QWidget owner.
     */
    explicit ColorButton(const QColor &initial, QWidget *parent = nullptr);

    /**
     * @brief Read the selected color including its alpha channel.
     * @return QColor copy; may be invalid if set programmatically.
     */
    [[nodiscard]] QColor color() const { return m_color; }
    /**
     * @brief Replace the swatch, update accessible description/repaint and emit colorChanged() on a real value change.
     * @param[in] c QColor including alpha; invalid colors are accepted.
     */
    void setColor(const QColor &c);

    /*! Whether the picker dialog includes the alpha channel. Default true. */
    [[nodiscard]] bool showsAlpha() const { return m_showAlpha; }
    /**
     * @brief Choose whether the next picker includes alpha controls.
     * @param[in] v True enables alpha picking; does not alter the currently stored alpha or emit a color change.
     */
    void setShowAlpha(bool v) { m_showAlpha = v; }

signals:
    /**
     * @brief Announce changed stored color after a programmatic setter or accepted picker.
     * @param[in] c Current QColor, possibly invalid after a programmatic set.
     */
    void colorChanged(const QColor &c);

protected:
    /**
     * @brief Paint standard button chrome then the alpha-aware color swatch.
     * @param[in] event Required Qt paint event forwarded to QPushButton.
     */
    void paintEvent(QPaintEvent *event) override;

private slots:
    /**
     * @brief Open a color dialog using the stored color (black fallback); canceled/invalid picks leave it unchanged.
     */
    void onClicked();

private:
    /**
     * @brief Refresh screen-reader description with RGB and alpha/opacity, or the no-color message.
     * @note Preserves the accessible name provided by surrounding form labels.
     */
    void updateAccessibleDescription();
    QColor m_color = Qt::black;
    bool m_showAlpha = true;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_COLORBUTTON_H
