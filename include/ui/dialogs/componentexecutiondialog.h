/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <QDialog>
class QComboBox;
class QLineEdit;
class QSpinBox;
class QLabel;
namespace openswmmvis::simulation { class ComponentEngineRegistry; }
class ComponentExecutionDialog final : public QDialog {
    Q_OBJECT
public:
    explicit ComponentExecutionDialog(openswmmvis::simulation::ComponentEngineRegistry *,QWidget *parent=nullptr);
private:
    void apply();
    openswmmvis::simulation::ComponentEngineRegistry *model_;
    QComboBox *mode_;
    QLineEdit *program_,*components_;
    QSpinBox *capacity_;
    QLabel *message_;
};
