/**
 * @file      textlayersdialog.h
 * @brief     Text Layers Dialog interface and supporting types.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_TEXTLAYERSDIALOG_H
#define OPENSWMM_TEXTLAYERSDIALOG_H
#include <QDialog>
#include <QPointer>
#include <QJsonObject>
class MapCanvas;
class OpenSWMMVisAnnotationLayer;
class QComboBox;
class QListWidget;
class QCheckBox;
class TextLayersDialog : public QDialog
{
    Q_OBJECT
public:
    explicit TextLayersDialog(MapCanvas *canvas, OpenSWMMVisAnnotationLayer *selected,
                              QWidget *parent = nullptr);
    OpenSWMMVisAnnotationLayer *selectedLayer() const;

private:
    void reloadLayers(OpenSWMMVisAnnotationLayer *selected = nullptr);
    void reloadItems();
    void editItem();
    void generate();
    void commit(const QJsonObject &before, const QString &label);
    bool m_refreshPending = false;
    MapCanvas *m_canvas;
    QComboBox *m_layers;
    QListWidget *m_items;
    QCheckBox *m_layout;
    QPointer<OpenSWMMVisAnnotationLayer> m_observed;
};
#endif
