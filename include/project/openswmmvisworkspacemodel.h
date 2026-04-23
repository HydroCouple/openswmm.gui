/*!
 * \file   openswmmvisworkspacemodel.h
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
#ifndef SWMMVISWORKSPACEMODEL_H
#define SWMMVISWORKSPACEMODEL_H

#include <QAbstractItemModel>

class OpenSWMMVisWorkspace;

class OpenSWMMVisWorkspaceModel : public QAbstractItemModel
{
	Q_OBJECT

		public:
			explicit OpenSWMMVisWorkspaceModel(OpenSWMMVisWorkspace *parent = nullptr);

			~OpenSWMMVisWorkspaceModel();

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
		OpenSWMMVisWorkspace *mProject;

};
#endif // SWMMVISWORKSPACEMODEL_H
