/*!
 * \file   openswmmvisworkspacemodel.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  QAbstractItemModel adapter exposing an OpenSWMMVisWorkspace to Qt's
 *         model-view framework.
 *
 * \details OpenSWMMVisWorkspaceModel provides a tree representation of an
 *          OpenSWMMVisWorkspace so that workspace sessions and their child
 *          layers can be displayed in a QTreeView or QListView.  The full
 *          QAbstractItemModel interface (index, parent, rowCount, etc.) is
 *          currently stubbed out pending Slice AA completion; only the
 *          constructor and destructor are implemented.
 */
#ifndef SWMMVISWORKSPACEMODEL_H
#define SWMMVISWORKSPACEMODEL_H

#include <QAbstractItemModel>

class OpenSWMMVisWorkspace;

/*!
 * \class OpenSWMMVisWorkspaceModel
 * \brief QAbstractItemModel that exposes an OpenSWMMVisWorkspace as a tree for
 *        Qt model-view widgets.
 *
 * \details The model mirrors the workspace's session/layer hierarchy.  The
 *          full virtual interface is reserved for future implementation; the
 *          class exists today to establish the ownership contract and header
 *          structure before the view panels are wired up.
 *
 * \note The model does NOT take ownership of the workspace.
 */
class OpenSWMMVisWorkspaceModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    /*!
     * \brief Constructs the model around the given workspace.
     * \param parent  The workspace whose sessions and layers this model will
     *                expose.  Passed as the QObject parent so Qt handles
     *                lifetime automatically when the workspace is destroyed.
     */
    explicit OpenSWMMVisWorkspaceModel(OpenSWMMVisWorkspace *parent = nullptr);

    /*!
     * \brief Destructor.
     */
    ~OpenSWMMVisWorkspaceModel();

    // QAbstractItemModel interface — reserved for future implementation.
    // QModelIndex index(int row, int column, const QModelIndex &parent) const override;
    // QModelIndex parent(const QModelIndex &child) const override;
    // int rowCount(const QModelIndex &parent) const override;
    // int columnCount(const QModelIndex &parent) const override;
    // QVariant data(const QModelIndex &index, int role) const override;
    // QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    // bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    // Qt::ItemFlags flags(const QModelIndex &index) const override;

private:
    OpenSWMMVisWorkspace *mProject; ///< Non-owning pointer to the workspace.
};

#endif // SWMMVISWORKSPACEMODEL_H
