/*!
 * \file   colorbutton.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  QGIS-style color-swatch button.
 *
 *         Click → QColorDialog; the button shows the current colour as a
 *         filled swatch with a thin outline. Honours alpha. Compact size
 *         so it fits comfortably in form rows alongside other editors.
 */
#ifndef OPENSWMMVIS_UI_WIDGETS_COLORBUTTON_H
#define OPENSWMMVIS_UI_WIDGETS_COLORBUTTON_H

#include <QColor>
#include <QPushButton>

namespace openswmmvis::ui {

class ColorButton : public QPushButton
{
    Q_OBJECT
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)
public:
    explicit ColorButton(QWidget *parent = nullptr);
    explicit ColorButton(const QColor &initial, QWidget *parent = nullptr);

    [[nodiscard]] QColor color() const { return m_color; }
    void setColor(const QColor &c);

    /*! Whether the picker dialog includes the alpha channel. Default true. */
    [[nodiscard]] bool showsAlpha() const { return m_showAlpha; }
    void setShowAlpha(bool v) { m_showAlpha = v; }

signals:
    void colorChanged(const QColor &c);

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
    void onClicked();

private:
    QColor m_color = Qt::black;
    bool   m_showAlpha = true;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_COLORBUTTON_H
