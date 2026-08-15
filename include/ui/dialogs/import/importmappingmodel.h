/*!
 * \file   importmappingmodel.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * FEATURE_LAYER_TO_SWMM_IMPORT — QAbstractTableModel over the attribute
 * mapping table. Rows = target attributes of the chosen kind; columns:
 *
 *   0 Target Attribute  (read-only; bold + "•" marker when required)
 *   1 Source Column     (combo delegate over the layer's field names)
 *   2 Default Value     (free-text; coerced/validated by the planner)
 *
 * The model owns nothing — it edits the ImportMapping value object in
 * place (MVC: the mapping is the model state, this class adapts it to
 * the view).
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_IMPORT_IMPORTMAPPINGMODEL_H
#define OPENSWMMVIS_UI_DIALOGS_IMPORT_IMPORTMAPPINGMODEL_H

#include "ui/dialogs/import/importmapping.h"

#include <QAbstractTableModel>
#include <QStringList>
#include <QStyledItemDelegate>

namespace openswmmvis::import {

class ImportMappingModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column { TargetCol = 0, SourceCol = 1, DefaultCol = 2, ColCount = 3 };

    explicit ImportMappingModel(QObject *parent = nullptr);

    /*! Rebuild rows for \p kind, preserving bindings whose target keys
     *  survive the kind switch, and re-list \p sourceFields in the
     *  combo delegate. */
    void reset(TargetKind kind, const QStringList &sourceFields);

    /*! Replace bindings wholesale (preset load). Field bindings whose
     *  source field is absent from the current layer are cleared and
     *  reported in \p droppedOut. */
    void applyBindings(const QVector<AttributeBinding> &bindings,
                       QStringList *droppedOut = nullptr);

    /*! Case-insensitive auto-match of source fields to attribute keys /
     *  labels. Only fills rows that are currently unmapped. Returns the
     *  number of rows matched. */
    int autoMatch();

    [[nodiscard]] const ImportMapping &mapping() const { return m_mapping; }
    /*! Mutable access for the dialog's option widgets (endpoint /
     *  conflict flags live on the same value object). Views of THIS
     *  model are unaffected by those fields, so no signals fire. */
    [[nodiscard]] ImportMapping &mappingRef() { return m_mapping; }

    [[nodiscard]] QStringList sourceFields() const { return m_sourceFields; }
    [[nodiscard]] const QVector<TargetAttribute> &attributes() const
    { return m_attributes; }

    /*! Empty when every required row is satisfied; otherwise a
     *  human-readable reason (drives the dialog's inline error label +
     *  Preview/Import enablement). */
    [[nodiscard]] QString validationError() const;

    // QAbstractTableModel ---------------------------------------------
    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &idx, int role) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation o,
                                      int role) const override;
    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex &idx) const override;
    bool setData(const QModelIndex &idx, const QVariant &value,
                 int role) override;

signals:
    /*! Emitted after any binding edit — the dialog revalidates. */
    void mappingEdited();

private:
    ImportMapping            m_mapping;
    QVector<TargetAttribute> m_attributes;
    QStringList              m_sourceFields;
};

/*! Combo-box delegate for the Source Column cells: field names plus a
 *  leading "(not mapped)" entry. */
class SourceFieldDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit SourceFieldDelegate(ImportMappingModel *model,
                                 QObject *parent = nullptr);

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &opt,
                          const QModelIndex &idx) const override;
    void setEditorData(QWidget *editor, const QModelIndex &idx) const override;
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &idx) const override;

private:
    ImportMappingModel *m_model = nullptr;
};

} // namespace openswmmvis::import

#endif // OPENSWMMVIS_UI_DIALOGS_IMPORT_IMPORTMAPPINGMODEL_H
