#ifndef MESH_PROFILE_SERIES_EDITOR_H
#define MESH_PROFILE_SERIES_EDITOR_H
#include "plot/profilesectionseries.h"
#include <QWidget>
class QComboBox;
class QTableWidget;
class QLabel;
class MeshProfileSeriesEditor : public QWidget
{
    Q_OBJECT
public:
    struct SourceChoice { QString id, label; QVector<openswmmvis::io::Mesh2DResultVariable> variables; };
    explicit MeshProfileSeriesEditor(QWidget *parent = nullptr);
    void setDefinition(const ProfileSection::Definition &);
    const ProfileSection::Definition &definition() const { return m_definition; }
    void setSources(const QVector<SourceChoice> &);
signals:
    void definitionEdited();
private:
    void rebuildRows();
    void refreshVariables();
    void addSeries();
    void announceEdit();
    ProfileSection::Definition m_definition;
    QVector<SourceChoice> m_sources;
    QComboBox *m_source = nullptr, *m_variable = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel *m_error = nullptr;
    bool m_refreshing = false;
};
#endif
