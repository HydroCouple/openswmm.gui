// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      modelcheckdialog.cpp
 * @brief     Check Model window (review G-09).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "ui/dialogs/modelcheckdialog.h"

#include "ui/util/tableclipboard.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTableView>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

namespace openswmmvis::ui
{

using simulation::ModelCheckIssue;
using simulation::ModelCheckResult;

// ── Table model ─────────────────────────────────────────────────────────────

void ModelCheckTableModel::setIssues(const QList<ModelCheckIssue> &issues)
{
    beginResetModel();
    m_issues = issues;
    endResetModel();
}

int ModelCheckTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_issues.size());
}

int ModelCheckTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QString ModelCheckTableModel::objectLabel(SWMMObjectRef::ObjectType type, const QString &name)
{
    if (name.isEmpty())
        return {};
    QString kind;
    switch (type) {
    case SWMMObjectRef::Node: kind = tr("Node"); break;
    case SWMMObjectRef::Link: kind = tr("Link"); break;
    case SWMMObjectRef::Subcatchment: kind = tr("Subcatchment"); break;
    case SWMMObjectRef::RainGage: kind = tr("Rain gage"); break;
    case SWMMObjectRef::Aquifer: kind = tr("Aquifer"); break;
    case SWMMObjectRef::Snowpack: kind = tr("Snowpack"); break;
    case SWMMObjectRef::LIDControl: kind = tr("LID control"); break;
    case SWMMObjectRef::Transect: kind = tr("Transect"); break;
    case SWMMObjectRef::Pollutant: kind = tr("Pollutant"); break;
    case SWMMObjectRef::LandUse: kind = tr("Land use"); break;
    case SWMMObjectRef::Street: kind = tr("Street"); break;
    case SWMMObjectRef::Inlet: kind = tr("Inlet"); break;
    case SWMMObjectRef::TimePattern: kind = tr("Pattern"); break;
    case SWMMObjectRef::TimeSeries: kind = tr("Time series"); break;
    case SWMMObjectRef::Curve: kind = tr("Curve"); break;
    default: return name;
    }
    return tr("%1 %2").arg(kind, name);
}

QVariant ModelCheckTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_issues.size())
        return {};
    const auto &issue = m_issues.at(index.row());
    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case SeverityColumn:
            return issue.severity == ModelCheckIssue::Severity::Error ? tr("Error") : tr("Warning");
        case CodeColumn:
            return issue.code > 0 ? QVariant(issue.code) : QVariant();
        case ObjectColumn:
            return objectLabel(issue.objectType, issue.objectName);
        case MessageColumn:
            return issue.message;
        default:
            return {};
        }
    }
    if (role == Qt::ToolTipRole) {
        if (index.column() == MessageColumn)
            return issue.message;
        if (index.column() == ObjectColumn && !issue.objectName.isEmpty())
            return tr("Double-click or press Enter to select it on the map");
    }
    return {};
}

QVariant ModelCheckTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return QAbstractTableModel::headerData(section, orientation, role);
    switch (section) {
    case SeverityColumn: return tr("Severity");
    case CodeColumn: return tr("Code");
    case ObjectColumn: return tr("Object");
    case MessageColumn: return tr("Message");
    default: return {};
    }
}

// ── Window ──────────────────────────────────────────────────────────────────

ModelCheckDialog::ModelCheckDialog(SnapshotWriter writer, EngineSource liveEngine, QWidget *parent)
    : QDialog(parent), m_writer(std::move(writer)), m_liveEngine(std::move(liveEngine))
{
    setWindowTitle(tr("Check Model"));
    setAttribute(Qt::WA_DeleteOnClose);
    resize(760, 420);

    auto *layout = new QVBoxLayout(this);
    m_summary = new QLabel(this);
    m_summary->setObjectName(QStringLiteral("modelCheckSummary"));
    m_summary->setWordWrap(true);
    m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_summary);

    m_busy = new QProgressBar(this);
    m_busy->setObjectName(QStringLiteral("modelCheckProgress"));
    m_busy->setAccessibleName(tr("Checking the model"));
    m_busy->setRange(0, 0);
    m_busy->setTextVisible(false);
    m_busy->hide();
    layout->addWidget(m_busy);

    m_model = new ModelCheckTableModel(this);
    m_view = new QTableView(this);
    m_view->setObjectName(QStringLiteral("modelCheckTable"));
    m_view->setAccessibleName(tr("Model check results"));
    m_view->setModel(m_model);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->setWordWrap(false);
    m_view->verticalHeader()->hide();
    m_view->horizontalHeader()->setStretchLastSection(true);
    m_view->horizontalHeader()->setSectionResizeMode(ModelCheckTableModel::SeverityColumn,
                                                     QHeaderView::ResizeToContents);
    m_view->horizontalHeader()->setSectionResizeMode(ModelCheckTableModel::CodeColumn,
                                                     QHeaderView::ResizeToContents);
    m_view->setColumnWidth(ModelCheckTableModel::ObjectColumn, 160);
    m_view->installEventFilter(this);
    TableClipboard::Options clipboard;
    clipboard.copyOnly = true;
    TableClipboard::install(m_view, clipboard);
    connect(m_view, &QTableView::doubleClicked, this,
            [this](const QModelIndex &index) { activateRow(index.row()); });
    layout->addWidget(m_view, 1);

    auto *note = new QLabel(
        tr("The check writes a copy of the model, unsaved edits included, opens and initializes it "
           "as a run would, then deletes the copy. External plugins are not checked."),
        this);
    note->setWordWrap(true);
    layout->addWidget(note);

    auto *buttons = new QDialogButtonBox(this);
    m_again = buttons->addButton(tr("Check Again"), QDialogButtonBox::ActionRole);
    m_again->setObjectName(QStringLiteral("modelCheckAgain"));
    auto *close = buttons->addButton(QDialogButtonBox::Close);
    // Enter belongs to the table (go to the object), not to a button.
    m_again->setAutoDefault(false);
    close->setAutoDefault(false);
    connect(m_again, &QPushButton::clicked, this, &ModelCheckDialog::startCheck);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    layout->addWidget(buttons);

    connect(&m_watcher, &QFutureWatcher<ModelCheckResult>::finished, this,
            &ModelCheckDialog::onFinished);
}

ModelCheckDialog::~ModelCheckDialog() = default;

bool ModelCheckDialog::isChecking() const
{
    return m_checking;
}

QString ModelCheckDialog::summaryText() const
{
    return m_summary->text();
}

void ModelCheckDialog::setBusy(bool busy)
{
    m_checking = busy;
    m_busy->setVisible(busy);
    m_again->setEnabled(!busy);
    if (busy)
        m_summary->setText(tr("Checking the model…"));
}

void ModelCheckDialog::startCheck()
{
    if (isChecking())
        return;
    m_model->setIssues({});
    QString error;
    const QString path = m_writer ? m_writer(&error) : QString();
    if (path.isEmpty()) {
        finish(tr("The model could not be copied for checking: %1")
                   .arg(error.isEmpty() ? tr("no reason was given") : error),
               false);
        return;
    }
    setBusy(true);
    m_watcher.setFuture(QtConcurrent::run([path] {
        ModelCheckResult result = simulation::checkModelFile(path);
        removeModelCheckSnapshot(path);
        return result;
    }));
}

void ModelCheckDialog::onFinished()
{
    ModelCheckResult result = m_watcher.result();
    simulation::resolveModelCheckObjects(m_liveEngine ? m_liveEngine() : nullptr, result.issues);
    m_model->setIssues(result.issues);
    int errors = 0;
    for (const auto &issue : result.issues)
        errors += issue.severity == ModelCheckIssue::Severity::Error;
    const int warnings = int(result.issues.size()) - errors;
    const QString errorText = errors == 1 ? tr("1 error") : tr("%1 errors").arg(errors);
    const QString warningText = warnings == 1 ? tr("1 warning") : tr("%1 warnings").arg(warnings);
    QString summary;
    if (!result.opened)
        summary = tr("The model would not open: %1, %2.").arg(errorText, warningText);
    else if (!result.initialized)
        summary = tr("The model would not start: %1, %2.").arg(errorText, warningText);
    else if (warnings > 0)
        summary = tr("The model starts, with %1.").arg(warningText);
    else
        summary = tr("The model starts with no errors or warnings.");
    finish(summary, result.initialized);
    if (m_model->rowCount() > 0)
        m_view->selectRow(0);
}

void ModelCheckDialog::finish(const QString &summary, bool starts)
{
    setBusy(false);
    m_summary->setText(summary);
    emit checkFinished(summary, starts);
}

void ModelCheckDialog::activateRow(int row)
{
    if (row < 0 || row >= m_model->issues().size())
        return;
    const auto &issue = m_model->issues().at(row);
    if (issue.objectName.isEmpty())
        return;
    emit objectActivated(SWMMObjectRef(issue.objectType, issue.objectName));
}

bool ModelCheckDialog::eventFilter(QObject *watched, QEvent *event)
{
    // QTableView emits activated() on Enter only on some platforms (not macOS); handle it here.
    if (watched == m_view && event->type() == QEvent::KeyPress) {
        const auto key = static_cast<QKeyEvent *>(event)->key();
        if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            activateRow(m_view->currentIndex().row());
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

bool removeModelCheckSnapshot(const QString &snapshotModelPath)
{
    QDir copy = QFileInfo(snapshotModelPath).absoluteDir();
    QDir check = copy;
    if (!check.cdUp() || check.dirName() != QLatin1String("check"))
        return false;
    QDir hidden = check;
    if (!hidden.cdUp() || hidden.dirName() != QLatin1String(".swmmvis"))
        return false;
    if (!copy.removeRecursively())
        return false;
    // rmdir refuses a folder that still holds anything (another check, extension snapshots).
    QDir(hidden.absolutePath()).rmdir(QStringLiteral("check"));
    QDir base = hidden;
    if (base.cdUp())
        base.rmdir(QStringLiteral(".swmmvis"));
    return true;
}

} // namespace openswmmvis::ui
