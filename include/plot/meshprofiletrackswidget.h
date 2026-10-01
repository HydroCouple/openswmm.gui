#ifndef MESH_PROFILE_TRACKS_WIDGET_H
#define MESH_PROFILE_TRACKS_WIDGET_H
#include "plot/profilesection.h"
#include "plot/profilesectionseries.h"
#include <QAbstractTableModel>
#include <QWidget>

// Long-form scientific samples: independent source intervals are never forced
// onto another mesh's stations. Qt's native table accessibility exposes values.
class MeshProfileSamplesModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    explicit MeshProfileSamplesModel(QObject *parent = nullptr) : QAbstractTableModel(parent) {}
    void setSection(const ProfileSection::Section &, const ProfileSection::Definition &);
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation, int role = Qt::DisplayRole) const override;
private:
    ProfileSection::Section m_section;
    ProfileSection::Definition m_definition;
    QVector<QPair<int,int>> m_rows;
};

class MeshProfileTracksWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MeshProfileTracksWidget(QWidget *parent = nullptr);
    void setSection(const ProfileSection::Section &);
    void setViewRange(const QRectF &); // horizontal range shared with elevation plot
    void setCursorChainage(double chainage);
    int trackCount() const;
signals:
    void cursorChainageChanged(double chainage);
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
private:
    ProfileSection::Section m_section;
    double m_xMin = 0, m_xMax = 1, m_cursor = -1;
};
#endif
