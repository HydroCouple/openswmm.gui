/* SPDX-License-Identifier: Apache-2.0 */
#ifndef OPENSWMM_UI_DIALOGS_INITIALBEDQUALITYDIALOG_H
#define OPENSWMM_UI_DIALOGS_INITIALBEDQUALITYDIALOG_H

#include <openswmm/engine/openswmm_engine.h>
#include <QDialog>
#include <QString>

class QLabel;
class QPushButton;
class QTableWidget;

namespace OpenSWMMVis {

class InitialBedQualityDialog final : public QDialog {
    Q_OBJECT
public:
    explicit InitialBedQualityDialog(SWMM_Engine engine, QWidget* parent = nullptr);
    bool wroteAnyChanges() const noexcept { return wrote_any_changes_; }
    int lastWriteCount() const noexcept { return last_write_count_; }

signals:
    void changesApplied();

public slots:
    void invalidateEngine();

private slots:
    void addRow();
    void removeRow();
    void acceptDraft();

private:
    void buildUi();
    void readFromEngine();
    void appendRow(int scope, const QString& target,
                   const QString& species, double concentration);
    void setEditable(bool editable);
    SWMM_Engine engine_ = nullptr;
    QTableWidget* table_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton* remove_ = nullptr;
    bool wrote_any_changes_ = false;
    int last_write_count_ = 0;
};

} // namespace OpenSWMMVis
#endif
