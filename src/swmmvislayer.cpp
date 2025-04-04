/*!
 * \file   swmmlayer.cpp
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

#include "swmmvisproject.h"
#include "swmmvislayer.h"


SWMMVisLayer::SWMMVisLayer(SWMMVisProject *parent):
    QObject(parent),
    mName("Unlabeled Layer"),
	mParent(parent),
	mLayerType(SWMMVisLayer::SWMMVisLayerType::SWMMDefaultLayer)
{

}

SWMMVisLayer::SWMMVisLayer(const QString& name, SWMMVisProject *parent):
    QObject(parent),
	mName(name),
	mParent(parent),
	mLayerType(SWMMVisLayer::SWMMVisLayerType::SWMMDefaultLayer)
{
}

SWMMVisLayer::~SWMMVisLayer()
{

}

QString SWMMVisLayer::name() const
{
    return mName;
}

void SWMMVisLayer::setName(const QString &name)
{
	mName = name;
}

SWMMVisLayer::SWMMVisLayerType SWMMVisLayer::layerType() const
{
	return mLayerType;
}

QVector<SWMMVisLayer*> SWMMVisLayer::children() const
{
    return mChildren;
}

bool SWMMVisLayer::addChild(SWMMVisLayer *child)
{
	if (mChildren.contains(child))
		return false;

	mChildren.append(child);
	emit childrenChanged();
	return true;
}

bool SWMMVisLayer::removeChild(SWMMVisLayer *child)
{
	if (!mChildren.contains(child))
		return false;

	mChildren.removeOne(child);
	emit childrenChanged();
	return true;
}

void SWMMVisLayer::setLayerType(SWMMVisLayer::SWMMVisLayerType type)
{
	mLayerType = type;
	emit layerTypeChanged(type);
}