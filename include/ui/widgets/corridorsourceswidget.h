#ifndef OPENSWMMVIS_CORRIDORSOURCESWIDGET_H
#define OPENSWMMVIS_CORRIDORSOURCESWIDGET_H

#include "mesh/corridorsource.h"
#include <QPointer>
#include <QWidget>

class GISVectorLayer;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

// Authors selected-feature recipes only. Geometry is read by the pipeline worker.
class CorridorSourcesWidget : public QWidget
{
    Q_OBJECT
public:
    explicit CorridorSourcesWidget(QWidget *parent = nullptr);
    void setLayers(const QList<GISVectorLayer *> &layers);
    void setSources(const QVector<mesh::CorridorSource> &sources);
    bool sources(QVector<mesh::CorridorSource> *out, QString *error = nullptr) const;
signals:
    void sourcesChanged();
private:
    GISVectorLayer *currentLayer() const;
    GISVectorLayer *matchingLayer(const mesh::CorridorSource &source) const;
    void refreshLayers();
    void refreshFields();
    void syncSourceMode();
    void updateSelectionMessage();
    void updateSourceLabels();
    void appendSource(const mesh::CorridorSource &source);
    void addSelection();
    void removeSelection();
    bool fail(const QString &message, QWidget *field, QString *error) const;
    bool failRow(int row, int column, const QString &message, QString *error) const;

    QVector<QPointer<GISVectorLayer>> m_layers;
    QVector<QMetaObject::Connection> m_layerConnections;
    QVector<mesh::CorridorSource> m_sources;
    QComboBox *m_sourceMode = nullptr;
    QComboBox *m_layer = nullptr;
    QComboBox *m_widthMode = nullptr;
    QComboBox *m_widthField = nullptr;
    QLineEdit *m_width = nullptr;
    QLineEdit *m_across = nullptr;
    QLineEdit *m_along = nullptr;
    QLineEdit *m_tag = nullptr;
    QLabel *m_selection = nullptr;
    QLabel *m_message = nullptr;
    QPushButton *m_add = nullptr;
    QPushButton *m_remove = nullptr;
    QTableWidget *m_table = nullptr;
};
#endif
