/*!
 * \file   swmmlayer.h
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
#ifndef SWMMLAYER_H
#define SWMMLAYER_H

#include <QObject>
#include <QVector>

class SWMMVisProject;

class SWMMVisLayer: public QObject
{
    Q_OBJECT
    Q_ENUMS(SWMMLayerType)
    Q_PROPERTY(QString Name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(QVector<SWMMVisLayer*> Children READ children NOTIFY childrenChanged FINAL)
    Q_PROPERTY(SWMMVisLayerType LayerType READ layerType NOTIFY layerTypeChanged FINAL)

public:

    enum SWMMVisLayerType
    {   
        SWMMDefaultLayer = 0,
        SWMMModelLayer = 1,
        SWMMResultsLayer = 2,
        SWMMGISLayer = 3,
		SWMMVectorLayer = 4,
		SWMMRasterLayer = 5,
        SWMMImageryLayer = 6,
        SWMMTabularDataLayer = 7,
        SWMMTabularyTimeSeriesLayer = 8,
		SWMMSuprojectLayer = 9,
    };

    explicit SWMMVisLayer(SWMMVisProject *parent);

    explicit SWMMVisLayer(const QString &name = "Unlabeled Layer", SWMMVisProject* parent = nullptr);

    virtual ~SWMMVisLayer();

    QString name() const;

    void setName(const QString &name);

    SWMMVisLayerType layerType() const;

    QVector<SWMMVisLayer *> children() const;

  signals:

    void nameChanged(const QString& newName);

    void layerTypeChanged(SWMMVisLayerType newType);

    void childrenChanged();

protected:

    bool addChild(SWMMVisLayer *child);

    bool removeChild(SWMMVisLayer *child);

    void setLayerType(SWMMVisLayerType type);

private:
    SWMMVisProject *mParent;
    QString mName;
    QVector<SWMMVisLayer*> mChildren;
    SWMMVisLayerType mLayerType;

};

Q_DECLARE_METATYPE(SWMMVisLayer *)
Q_DECLARE_METATYPE(QVector<SWMMVisLayer *>)

#endif // SWMMLAYER_H
