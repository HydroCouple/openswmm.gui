/*!
 * \file   swmmvisprojectmodel.h
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
#ifndef SWMMVISPROJECTMODEL_H
#define SWMMVISPROJECTMODEL_H

#include <QAbstractItemModel>

class SWMMVisProject;

class SWMMVisProjectModel : public QAbstractItemModel
{
	Q_OBJECT

		public:
			explicit SWMMVisProjectModel(SWMMVisProject *parent = nullptr);

			~SWMMVisProjectModel();

			//// QAbstractItemModel interface
			//QModelIndex index(int row, int column, const QModelIndex &parent) const override;

			//QModelIndex parent(const QModelIndex &child) const override;

			//int rowCount(const QModelIndex &parent) const override;

			//int columnCount(const QModelIndex &parent) const override;

			//QVariant data(const QModelIndex &index, int role) const override;

			//QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

			//bool setData(const QModelIndex &index, const QVariant &value, int role) override;

			//Qt::ItemFlags flags(const QModelIndex &index) const override;

	private:
		SWMMVisProject *mProject;

};
#endif // SWMMVISPROJECTMODEL_H
