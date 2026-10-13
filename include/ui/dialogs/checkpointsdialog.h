/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CHECKPOINTSDIALOG_H
#define CHECKPOINTSDIALOG_H
#include <QDialog>
#include <functional>
class CheckpointModel;
class QTableView;
class QTextBrowser;
class QLabel;
class QComboBox;
class QSpinBox;
class QPushButton;
class QCloseEvent;

class CheckpointsDialog:public QDialog {
    Q_OBJECT
public:
    explicit CheckpointsDialog(CheckpointModel*,QWidget* parent=nullptr);
    void setPreparation(std::function<bool(bool)> prepare) {m_prepare=std::move(prepare);}
protected:
    void closeEvent(QCloseEvent*)override;
private:
    CheckpointModel* m_model;
    QTableView* m_list;
    QTextBrowser *m_details,*m_files,*m_storage;
    QLabel *m_status,*m_notice;
    QComboBox *m_profile,*m_history,*m_retention;
    QSpinBox* m_keep;
    QPushButton *m_inspect,*m_resume,*m_start,*m_save,*m_export,*m_cancel,*m_stop;
    std::function<bool(bool)> m_prepare;
    int row()const;
    bool prepare(bool frozenModel=true);
    QString newFolder(const QString& title);
    void refresh();
};
#endif
