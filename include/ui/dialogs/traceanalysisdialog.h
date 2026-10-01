// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "output/tracedata.h"
#include <QDialog>
#include <QPointer>
class SWMMVisProjectWindow;
class QComboBox;
class QLineEdit;
class QCheckBox;
class QLabel;
class QTabWidget;
class QPushButton;
class QTableWidget;
class QProgressBar;
class QFormLayout;
namespace openswmmvis::trace
{
class TraceController;
class TraceAnalysisLayer;
class TraceAnalysisDialog final : public QDialog
{
    Q_OBJECT
  public:
    static TraceAnalysisDialog *showFor(SWMMVisProjectWindow *, bool upstream, bool travel,
                                        const QString &seed = {});
    explicit TraceAnalysisDialog(SWMMVisProjectWindow *);
    void setSeedNodes(const QStringList &);
    QJsonObject state() const;
    void restore(const QJsonObject &);

  protected:
    bool eventFilter(QObject *, QEvent *) override;

  private:
    void refreshSources();
    void sourceChanged();
    void followActive();
    void refreshSaved();
    void start(bool prepareOnly = false, bool replace = false);
    void display(std::shared_ptr<Result>);
    void loadPackage();
    void loadOutput();
    void updateAppearance();
    void useSelection();
    void showError(const QString &);
    void persistState();
    SWMMVisProjectWindow *m_project;
    TraceController *m_controller;
    QComboBox *m_sources, *m_saved, *m_direction;
    QLineEdit *m_seeds, *m_destination;
    QCheckBox *m_follow, *m_keep;
    QLabel *m_status, *m_flow, *m_time;
    QTabWidget *m_tabs;
    QTableWidget *m_nodes, *m_links;
    QProgressBar *m_progress;
    QPushButton *m_prepare, *m_estimate, *m_update, *m_cancel, *m_pick;
    QWidget *m_appearance;
    QFormLayout *m_appearanceLayout;
    QPointer<TraceAnalysisLayer> m_layer;
    std::shared_ptr<Result> m_result;
    bool m_refreshing = false, m_picking = false, m_inspecting = false;
};
} // namespace openswmmvis::trace
