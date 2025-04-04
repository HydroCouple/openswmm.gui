/*!
 * \file   swmmvisscene.h
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
#ifndef SWMMVISSCENE_H
#define SWMMVISSCENE_H

#include <QGraphicsScene>

class SWMMVisScene: public QGraphicsScene
{
    Q_OBJECT

public:
    SWMMVisScene(QObject *parent = nullptr);

    ~SWMMVisScene();
};

#endif // SWMMVISSCENE_H
