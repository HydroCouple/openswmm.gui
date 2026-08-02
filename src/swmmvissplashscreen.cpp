/*!
 * \file   swmmsplashscreen.cpp
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

#include "swmmvissplashscreen.h"

#include <QFontDatabase>
#include <QPainter>

SWMMVisSplashScreen::SWMMVisSplashScreen(const QPixmap & pixmap, Qt::WindowFlags f)
   : QSplashScreen(pixmap, f)
{
   mColor = QColor(255,255,255);
   mAlignment = Qt::AlignCenter | Qt::AlignBottom  ;
   //setMinimumSize(QSize(600,400));
   // System UI font instead of a hardcoded Windows-only family ("Segoe UI
   // Semibold" silently fell back on macOS/Linux).
   QFont splashFont = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
   splashFont.setPointSize(20);
   splashFont.setWeight(QFont::DemiBold);
   setFont(splashFont);
   //connect(this, SIGNAL(messageChanged(const QString&)), this, SLOT(onShowMessage(const QString&)));
}

SWMMVisSplashScreen::SWMMVisSplashScreen(QWidget * parent, const QPixmap & pixmap, Qt::WindowFlags f)
   : QSplashScreen(pixmap, f)
{
    mColor = QColor(255, 255, 255);
    mAlignment = Qt::AlignCenter | Qt::AlignBottom;
    //setMinimumSize(QSize(600, 400));
}

SWMMVisSplashScreen::~SWMMVisSplashScreen()
{
}

void SWMMVisSplashScreen::setColor(const QColor& color)
{
   mColor = color;
}

void SWMMVisSplashScreen::setAlignment(Qt::Alignment alignment)
{
   mAlignment = alignment;
}


void SWMMVisSplashScreen::drawContents(QPainter * painter)
{
   painter->setPen(mColor);
   painter->setFont(font());
   QRect rect = this->rect();
   rect.adjust(25, 25, -25, -25);
   painter->drawText(
       rect,
       mAlignment | Qt::TextWordWrap | Qt::TextJustificationForced,
       mMessage
       );
   QSplashScreen::drawContents(painter);
}



void SWMMVisSplashScreen::onShowMessage(const QString& message)
{
   mMessage = message;
   QSplashScreen::showMessage(
       QString(""),
       mAlignment | Qt::TextWordWrap | Qt::TextJustificationForced,
       mColor
       );
}
