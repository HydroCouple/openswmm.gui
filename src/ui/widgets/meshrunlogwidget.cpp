// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/meshrunlogwidget.h"
#include <QAbstractTableModel>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFileDialog>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QSaveFile>
#include <QScrollBar>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>
#include <deque>

namespace
{
QString utf8(const std::string &s)
{
    return QString::fromUtf8(s.data(), qsizetype(s.size()));
}
QString severityName(meshkit::Severity s)
{
    return s == meshkit::Severity::Error     ? QObject::tr("Error")
           : s == meshkit::Severity::Warning ? QObject::tr("Warning")
                                             : QObject::tr("Info");
}
QString elapsed(std::int64_t ms)
{
    return QStringLiteral("%1:%2:%3.%4")
        .arg(ms / 3600000, 2, 10, QLatin1Char('0'))
        .arg(ms / 60000 % 60, 2, 10, QLatin1Char('0'))
        .arg(ms / 1000 % 60, 2, 10, QLatin1Char('0'))
        .arg(ms % 1000, 3, 10, QLatin1Char('0'));
}
}

class MeshRunLogModel final : public QAbstractTableModel
{
public:
    explicit MeshRunLogModel(QObject *parent) : QAbstractTableModel(parent) {}
    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : int(rows.size());
    }
    int columnCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : 4;
    }
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
            return {};
        const QStringList labels{tr("Elapsed"), tr("Level"), tr("Stage"), tr("Message")};
        return labels.value(section);
    }
    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= int(rows.size()))
            return {};
        const auto &e = rows[std::size_t(index.row())];
        if (role == Qt::UserRole)
            return int(e.severity);
        if (role == Qt::ToolTipRole)
            return utf8(e.message);
        if (role != Qt::DisplayRole)
            return {};
        switch (index.column()) {
        case 0:
            return elapsed(e.elapsedMs);
        case 1:
            return severityName(e.severity);
        case 2:
            return utf8(e.stage);
        case 3:
            return utf8(e.message);
        }
        return {};
    }
    void reset()
    {
        beginResetModel();
        rows.clear();
        evicted = 0;
        endResetModel();
    }
    void append(meshkit::Event event)
    {
        if (!rows.empty() && event.kind == meshkit::EventKind::Progress &&
            rows.back().kind == meshkit::EventKind::Progress && rows.back().stage == event.stage) {
            rows.back() = std::move(event);
            emit dataChanged(index(rowCount() - 1, 0), index(rowCount() - 1, 3));
            return;
        }
        if (rows.size() == 10000) {
            beginRemoveRows({}, 0, 0);
            rows.pop_front();
            ++evicted;
            endRemoveRows();
        }
        const int row = rowCount();
        beginInsertRows({}, row, row);
        rows.push_back(std::move(event));
        endInsertRows();
    }
    std::deque<meshkit::Event> rows;
    std::uint64_t evicted = 0;
};

class MeshRunLogFilter final : public QSortFilterProxyModel
{
public:
    explicit MeshRunLogFilter(QObject *parent) : QSortFilterProxyModel(parent) {}
    void severity(int value)
    {
        minimum_ = value;
        invalidateFilter();
    }
    void search(QString value)
    {
        text_ = std::move(value);
        invalidateFilter();
    }
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override
    {
        if (sourceModel()->index(row, 0, parent).data(Qt::UserRole).toInt() < minimum_)
            return false;
        return sourceModel()
                   ->index(row, 2, parent)
                   .data()
                   .toString()
                   .contains(text_, Qt::CaseInsensitive) ||
               sourceModel()
                   ->index(row, 3, parent)
                   .data()
                   .toString()
                   .contains(text_, Qt::CaseInsensitive);
    }

private:
    int minimum_ = 0;
    QString text_;
};

MeshRunLogWidget::MeshRunLogWidget(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("meshRunLog"));
    auto *layout = new QVBoxLayout(this);
    auto *controls = new QHBoxLayout;
    auto *severity = new QComboBox(this);
    severity->setObjectName(QStringLiteral("meshRunLogSeverity"));
    severity->addItems({tr("All messages"), tr("Warnings and errors"), tr("Errors only")});
    auto *search = new QLineEdit(this);
    search->setObjectName(QStringLiteral("meshRunLogSearch"));
    search->setPlaceholderText(tr("Search stage or message"));
    auto *copy = new QPushButton(tr("Copy log"), this);
    auto *save = new QPushButton(tr("Export…"), this);
    auto *follow = new QPushButton(tr("Follow latest"), this);
    controls->addWidget(severity);
    controls->addWidget(search, 1);
    controls->addWidget(copy);
    controls->addWidget(save);
    controls->addWidget(follow);
    layout->addLayout(controls);
    model_ = new MeshRunLogModel(this);
    filter_ = new MeshRunLogFilter(this);
    filter_->setSourceModel(model_);
    view_ = new QTableView(this);
    view_->setObjectName(QStringLiteral("meshRunLogView"));
    view_->setModel(filter_);
    view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    view_->setWordWrap(false);
    view_->verticalHeader()->hide();
    view_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    view_->horizontalHeader()->setStretchLastSection(true);
    view_->setColumnWidth(0, 112);
    view_->setColumnWidth(1, 75);
    view_->setColumnWidth(2, 165);
    layout->addWidget(view_, 1);
    details_ = new QPlainTextEdit(this);
    details_->setObjectName(QStringLiteral("meshRunLogDetails"));
    details_->setReadOnly(true);
    details_->setPlaceholderText(tr("Select a message to read its details."));
    details_->setMaximumHeight(150);
    layout->addWidget(details_);
    connect(view_->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this](const QModelIndex &current) {
                if (current.isValid())
                    details_->setPlainText(current.siblingAtColumn(3).data().toString());
            });
    retention_ =
        new QLabel(tr("Progress and diagnostics will appear here when generation starts."), this);
    retention_->setWordWrap(true);
    layout->addWidget(retention_);
    highlight_ = new QPushButton(tr("Highlight exception cells"), this);
    highlight_->hide();
    layout->addWidget(highlight_, 0, Qt::AlignLeft);
    connect(highlight_, &QPushButton::clicked, this, [this] {
        if (highlightAction_)
            highlightAction_();
    });
    timer_ = new QTimer(this);
    timer_->setInterval(100);
    connect(timer_, &QTimer::timeout, this, [this] { drain(); });
    connect(severity, &QComboBox::currentIndexChanged, filter_, &MeshRunLogFilter::severity);
    connect(search, &QLineEdit::textChanged, filter_, &MeshRunLogFilter::search);
    connect(copy, &QPushButton::clicked, this,
            [this] { QApplication::clipboard()->setText(text()); });
    connect(follow, &QPushButton::clicked, this, [this] {
        follow_ = true;
        view_->scrollToBottom();
    });
    connect(view_->verticalScrollBar(), &QScrollBar::sliderMoved, this,
            [this](int value) { follow_ = value == view_->verticalScrollBar()->maximum(); });
    connect(view_->verticalScrollBar(), &QScrollBar::actionTriggered, this, [this](int) {
        follow_ =
            view_->verticalScrollBar()->sliderPosition() == view_->verticalScrollBar()->maximum();
    });
    connect(save, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getSaveFileName(this, tr("Export meshing log"),
                                                          QStringLiteral("meshing-log.jsonl"),
                                                          tr("JSON Lines (*.jsonl);;Text (*.txt)"));
        if (path.isEmpty())
            return;
        QString error;
        if (!exportLog(path, &error))
            QMessageBox::warning(this, tr("Log export failed"), error);
    });
}

void MeshRunLogWidget::beginRun(std::shared_ptr<meshkit::EventChannel> channel)
{
    timer_->stop();
    channel_ = std::move(channel);
    model_->reset();
    lost_ = 0;
    follow_ = true;
    setHighlightAction({});
    details_->clear();
    retention_->setText(tr(
        "Latest 10,000 messages; repeated progress updates are combined. Export saves retained messages."));
    timer_->start();
}

void MeshRunLogWidget::drain()
{
    if (!channel_)
        return;
    auto batch = channel_->drain();
    lost_ = batch.dropped;
    for (auto &event : batch.events) {
        if (event.kind == meshkit::EventKind::Completed)
            details_->setPlainText(utf8(event.message));
        model_->append(std::move(event));
    }
    if (lost_ || model_->evicted)
        retention_->setText(
            tr("%1 earlier messages were omitted by retention limits. Export includes retained messages and this count.")
                .arg(qulonglong(lost_ + model_->evicted)));
    if (follow_ && !batch.events.empty())
        view_->scrollToBottom();
}

void MeshRunLogWidget::finish()
{
    timer_->stop();
    drain();
}

void MeshRunLogWidget::setHighlightAction(std::function<void()> action)
{
    highlightAction_ = std::move(action);
    highlight_->setVisible(bool(highlightAction_));
}

void MeshRunLogWidget::addNote(meshkit::Severity severity, const QString &message)
{
    drain();
    meshkit::Event event;
    if (!model_->rows.empty()) {
        event.job = model_->rows.back().job;
        event.sequence = model_->rows.back().sequence + 1;
        event.elapsedMs = model_->rows.back().elapsedMs;
    }
    event.severity = severity;
    event.stage = "Project";
    event.message = message.toUtf8().toStdString();
    model_->append(std::move(event));
    if (follow_)
        view_->scrollToBottom();
}

QString MeshRunLogWidget::text() const
{
    QString result =
        tr("Meshing log — %1 earlier messages omitted\n").arg(qulonglong(lost_ + model_->evicted));
    for (const auto &e : model_->rows)
        result += elapsed(e.elapsedMs) + "\t" + severityName(e.severity) + "\t" + utf8(e.stage) +
                  "\t" + utf8(e.message) + "\n";
    return result;
}

bool MeshRunLogWidget::exportLog(const QString &path, QString *error) const
{
    QSaveFile file(path);
    auto fail = [&] {
        if (error)
            *error = file.errorString();
        return false;
    };
    if (!file.open(QIODevice::WriteOnly))
        return fail();
    auto write = [&](const QJsonObject &object) {
        const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
        return file.write(bytes) == bytes.size();
    };
    if (path.endsWith(QStringLiteral(".txt"), Qt::CaseInsensitive)) {
        const auto bytes = text().toUtf8();
        if (file.write(bytes) != bytes.size())
            return fail();
    } else {
        if (!write({{"schemaVersion", 1}, {"omittedMessages", qint64(lost_ + model_->evicted)}}))
            return fail();
        for (const auto &e : model_->rows) {
            const QJsonObject counts{{"vertices", qint64(e.counts.vertices)},
                                     {"workingVertices", qint64(e.counts.workingVertices)},
                                     {"triangles", qint64(e.counts.triangles)},
                                     {"quads", qint64(e.counts.quads)},
                                     {"cells", qint64(e.counts.cells())},
                                     {"inserted", qint64(e.counts.inserted)},
                                     {"removed", qint64(e.counts.removed)}};
            if (!write({{"job", QString::number(e.job)},
                        {"sequence", QString::number(e.sequence)},
                        {"elapsedMs", qint64(e.elapsedMs)},
                        {"kind", int(e.kind)},
                        {"severity", int(e.severity)},
                        {"stage", utf8(e.stage)},
                        {"code", utf8(e.code)},
                        {"message", utf8(e.message)},
                        {"status", QString::fromLatin1(meshkit::statusName(e.status))},
                        {"percent", e.percent},
                        {"counts", counts}}))
                return fail();
        }
    }
    return file.commit() ? true : fail();
}
