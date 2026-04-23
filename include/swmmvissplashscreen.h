/*!
 * \file   swmmsplashscreen.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \version
 * \description
 * \license
 * \copyright
 * \date 2026
 * \pre
 * \bug
 * \warning
 * \todo
 */

#ifndef SPLASHSCREEN_H
#define SPLASHSCREEN_H

#include <QSplashScreen>

class SWMMVisSplashScreen : public QSplashScreen
{
	Q_OBJECT

public:

    SWMMVisSplashScreen(const QPixmap & pixmap = QPixmap(), Qt::WindowFlags f =Qt::WindowFlags());

    SWMMVisSplashScreen(QWidget * parent, const QPixmap & pixmap = QPixmap(), Qt::WindowFlags f =Qt::WindowFlags());

    virtual ~SWMMVisSplashScreen();

	void setColor(const QColor& color);

	void setAlignment(Qt::Alignment alignment);

    void drawContents(QPainter * painter) override;

public slots:

    void onShowMessage(const QString& message);

private:

    QColor mColor;
    Qt::Alignment mAlignment;
    QString mMessage;
};

#endif // SPLASHSCREEN_H
