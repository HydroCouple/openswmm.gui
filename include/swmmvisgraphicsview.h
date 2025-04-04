/*!
 * \file   swmmvisgraphicsview.h
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
#ifndef SWMMVISGRAPHICSVIEW_H
#define SWMMVISGRAPHICSVIEW_H

#include <QGraphicsView>

class SWMMVisScene;

class SWMMVisGraphicsView: public QGraphicsView
{

    Q_OBJECT

public:

    SWMMVisGraphicsView(SWMMVisScene*scene, QWidget *parent = nullptr);

    SWMMVisGraphicsView(QWidget *parent = nullptr);

    virtual ~SWMMVisGraphicsView();

};

#endif // SWMMVISGRAPHICSVIEW_H
