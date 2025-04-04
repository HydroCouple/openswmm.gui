/*!
 * \file   swmmsplashscreen.cpp
 * \author Caleb Buahin <buahin.caleb@epa.gov>
 * \version
 * \description
 * \license
 * \copyright
 * \date 2024
 * \pre
 * \bug
 * \warning
 * \todo
 */

#include "swmmvissplashscreen.h"

#include <QPainter>

SWMMVisSplashScreen::SWMMVisSplashScreen(const QPixmap & pixmap, Qt::WindowFlags f)
   : QSplashScreen(pixmap, f)
{
   mColor = QColor(255,255,255);
   mAlignment = Qt::AlignCenter | Qt::AlignBottom  ;
   //setMinimumSize(QSize(600,400));
   setFont(QFont("Segoe UI Semibold", 20, 2));
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
