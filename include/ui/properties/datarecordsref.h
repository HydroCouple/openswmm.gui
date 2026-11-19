// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "layers/swmmmodellayer.h"
#include <QPointer>
#include <QUndoStack>
#include <QPushButton>
struct DataRecordsRef {
    QPointer<SWMMModelLayer> layer;
    SWMM_Engine engine = nullptr;
    int category = -1;
    QStringList names;
    QList<QPointer<QObject>> providers;
    QPointer<QUndoStack> undo;
    bool operator==(const DataRecordsRef &o) const
    {
        return layer == o.layer && engine == o.engine && category == o.category &&
               names == o.names && providers == o.providers && undo == o.undo;
    }
};
Q_DECLARE_METATYPE(DataRecordsRef)
DataRecordsRef dataRecordsRef(SWMMModelLayer *layer, int category, const QStringList &names,
                              QUndoStack *undo);
void openDataRecords(const DataRecordsRef &ref, QWidget *parent = nullptr);
class DataRecordsButton final : public QPushButton
{
public:
    explicit DataRecordsButton(QWidget *parent = nullptr)
        : QPushButton(tr("Edit detailed properties…"), parent)
    {
        connect(this, &QPushButton::clicked, this, [this] { openDataRecords(m_ref, window()); });
    }
    void setValue(const DataRecordsRef &ref) { m_ref = ref; }

private:
    DataRecordsRef m_ref;
};
