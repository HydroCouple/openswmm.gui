// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      hotstartinspectordialog.cpp
 * @brief     Hot-start inspector (review G-09).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "ui/dialogs/hotstartinspectordialog.h"

#include "ui/util/tableclipboard.h"

#include <QCloseEvent>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QTableView>
#include <QTimeZone>
#include <QVBoxLayout>

#include <cmath>

#include <openswmm/engine/openswmm_hotstart.h>

namespace openswmmvis::ui
{

using simulation::HotStartDocument;
using simulation::HotStartUnits;

namespace
{
/** Shown with 10 significant digits; edited in the shortest text that reads back exactly. */
QString number(double value, bool exact = false)
{
    return QLocale::c().toString(value, 'g', exact ? QLocale::FloatingPointShortest : 10);
}

/** OADate days (SWMM DateTime) as a UTC wall-clock text; empty for 0. */
QString swmmDate(double days)
{
    if (days == 0.0 || !std::isfinite(days))
        return {};
    const QDateTime epoch(QDate(1899, 12, 30), QTime(0, 0), QTimeZone::UTC);
    return epoch.addMSecs(qRound64(days * 86400000.0)).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

QString duration(double seconds)
{
    const qint64 total = qRound64(seconds);
    const qint64 days = total / 86400, hours = total % 86400 / 3600, minutes = total % 3600 / 60;
    if (days > 0)
        return QObject::tr("%1 d %2 h %3 min").arg(days).arg(hours).arg(minutes);
    if (hours > 0)
        return QObject::tr("%1 h %2 min").arg(hours).arg(minutes);
    return QObject::tr("%1 min %2 s").arg(minutes).arg(total % 60);
}
} // namespace

// ── Record model ────────────────────────────────────────────────────────────

HotStartRecordModel::HotStartRecordModel(Kind kind, QObject *parent)
    : QAbstractTableModel(parent), m_kind(kind)
{
}

QStringList HotStartRecordModel::valueKeys() const
{
    switch (m_kind) {
    case Kind::Nodes:
        return {QStringLiteral("depth"), QStringLiteral("head"), QStringLiteral("volume"),
                QStringLiteral("age")};
    case Kind::Links:
        return {QStringLiteral("flow"), QStringLiteral("depth"), QStringLiteral("volume"),
                QStringLiteral("age")};
    case Kind::Subcatchments:
        return {QStringLiteral("runoff"), QStringLiteral("gwFlow"), QStringLiteral("gwMoisture"),
                QStringLiteral("gwLowerDepth")};
    }
    return {};
}

int HotStartRecordModel::columnOf(const QString &value) const
{
    const int at = valueKeys().indexOf(value);
    return at < 0 ? -1 : FirstValueColumn + at;
}

void HotStartRecordModel::setDocument(const HotStartDocument *document, const HotStartUnits &units)
{
    beginResetModel();
    m_document = document;
    m_units = units;
    m_nodeDepth.clear();
    m_linkState.clear();
    endResetModel();
    emit dirtyChanged(false);
}

void HotStartRecordModel::clearEdits()
{
    if (!isDirty())
        return;
    beginResetModel();
    m_nodeDepth.clear();
    m_linkState.clear();
    endResetModel();
    emit dirtyChanged(false);
}

int HotStartRecordModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || !m_document)
        return 0;
    switch (m_kind) {
    case Kind::Nodes: return int(m_document->nodes().size());
    case Kind::Links: return int(m_document->links().size());
    case Kind::Subcatchments: return int(m_document->subcatchments().size());
    }
    return 0;
}

int HotStartRecordModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    const int species = m_document && m_kind != Kind::Subcatchments
                            ? int(m_document->species().size())
                            : 0;
    return FirstValueColumn + int(valueKeys().size()) + species;
}

double HotStartRecordModel::internalValue(int row, const QString &key, bool *edited) const
{
    *edited = false;
    const double nan = std::nan("");
    if (m_kind == Kind::Nodes) {
        const auto &rec = m_document->nodes().at(row);
        const bool changed = m_nodeDepth.contains(rec.id);
        if (key == QLatin1String("depth")) {
            *edited = changed;
            return changed ? m_nodeDepth.value(rec.id) : rec.depth;
        }
        if (key == QLatin1String("head"))
            return changed ? nan : rec.head;
        if (key == QLatin1String("volume"))
            return changed ? nan : rec.volume;
        return rec.age;
    }
    if (m_kind == Kind::Links) {
        const auto &rec = m_document->links().at(row);
        const bool changed = m_linkState.contains(rec.id);
        if (key == QLatin1String("flow") || key == QLatin1String("depth")) {
            *edited = changed;
            const auto state = m_linkState.value(rec.id, {rec.flow, rec.depth});
            return key == QLatin1String("flow") ? state.first : state.second;
        }
        if (key == QLatin1String("volume"))
            return changed ? nan : rec.volume;
        return rec.age;
    }
    const auto &rec = m_document->subcatchments().at(row);
    if (key == QLatin1String("runoff"))
        return rec.runoff;
    if (key == QLatin1String("gwFlow"))
        return rec.gwFlow;
    if (key == QLatin1String("gwMoisture"))
        return rec.gwMoisture;
    return rec.gwLowerDepth;
}

QVariant HotStartRecordModel::data(const QModelIndex &index, int role) const
{
    if (!m_document || !index.isValid() || index.row() >= rowCount())
        return {};
    const int row = index.row();
    const int column = index.column();
    const QString id = m_kind == Kind::Nodes   ? m_document->nodes().at(row).id
                       : m_kind == Kind::Links ? m_document->links().at(row).id
                                               : m_document->subcatchments().at(row).id;
    const bool inModel = m_kind == Kind::Nodes   ? m_document->nodes().at(row).inModel
                         : m_kind == Kind::Links ? m_document->links().at(row).inModel
                                                 : m_document->subcatchments().at(row).inModel;
    if (role == ObjectIdRole)
        return id;
    if (column == IdColumn)
        return role == Qt::DisplayRole ? QVariant(id) : QVariant();
    if (column == InModelColumn) {
        if (role == Qt::DisplayRole)
            return inModel ? tr("Yes") : tr("No");
        if (role == Qt::ToolTipRole && !inModel)
            return tr("The open model has no object with this name; a restart skips it.");
        return {};
    }
    const QStringList keys = valueKeys();
    const int valueIndex = column - FirstValueColumn;
    if (valueIndex >= keys.size()) { // species concentration
        const int s = valueIndex - int(keys.size());
        const auto &species = m_kind == Kind::Nodes ? m_document->nodes().at(row).species
                                                    : m_document->links().at(row).species;
        if (role == Qt::DisplayRole && s < species.size() && std::isfinite(species.at(s)))
            return number(species.at(s));
        if (role == Qt::TextAlignmentRole)
            return int(Qt::AlignRight | Qt::AlignVCenter);
        return {};
    }
    const QString key = keys.at(valueIndex);
    bool edited = false;
    const double value = internalValue(row, key, &edited);
    const bool legacy = m_document->info().format == HotStartDocument::Format::Legacy;
    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        if (key == QLatin1String("age"))
            return value < 0.0 ? QVariant() : QVariant(number(value / 3600.0));
        if (!std::isfinite(value))
            return role == Qt::EditRole ? QVariant()
                   : (legacy || m_kind == Kind::Subcatchments) ? tr("not stored")
                                                                : tr("set at restart");
        double factor = 1.0;
        if (key == QLatin1String("flow") || key == QLatin1String("runoff") ||
            key == QLatin1String("gwFlow"))
            factor = m_units.flow;
        else if (key == QLatin1String("volume"))
            factor = m_units.volume;
        else if (key != QLatin1String("gwMoisture"))
            factor = m_units.length;
        return number(value * factor, role == Qt::EditRole);
    }
    if (role == Qt::FontRole && edited) {
        QFont font;
        font.setBold(true);
        return font;
    }
    if (role == Qt::ToolTipRole && !std::isfinite(value))
        return legacy ? tr("A SWMM 5 hot-start file does not store this value.")
                      : tr("Derived from the model's geometry when a restart applies the file.");
    if (role == Qt::TextAlignmentRole)
        return int(Qt::AlignRight | Qt::AlignVCenter);
    return {};
}

Qt::ItemFlags HotStartRecordModel::flags(const QModelIndex &index) const
{
    auto f = QAbstractTableModel::flags(index);
    if (!m_document || !m_document->isEditable() || !index.isValid())
        return f;
    const int depth = columnOf(QStringLiteral("depth"));
    const int flow = columnOf(QStringLiteral("flow"));
    if ((m_kind == Kind::Nodes && index.column() == depth) ||
        (m_kind == Kind::Links && (index.column() == depth || index.column() == flow)))
        f |= Qt::ItemIsEditable;
    return f;
}

bool HotStartRecordModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (role != Qt::EditRole || !(flags(index) & Qt::ItemIsEditable))
        return false;
    bool ok = false;
    const double shown = QLocale::c().toDouble(value.toString().trimmed(), &ok);
    if (!ok || !std::isfinite(shown))
        return false;
    const bool isFlow = index.column() == columnOf(QStringLiteral("flow"));
    const double internal = shown / (isFlow ? m_units.flow : m_units.length);
    if (!isFlow && internal < 0.0)
        return false;
    const int row = index.row();
    const bool wasDirty = isDirty();
    if (m_kind == Kind::Nodes) {
        const auto &rec = m_document->nodes().at(row);
        if (internal == rec.depth)
            m_nodeDepth.remove(rec.id);
        else
            m_nodeDepth.insert(rec.id, internal);
    } else {
        const auto &rec = m_document->links().at(row);
        auto state = m_linkState.value(rec.id, {rec.flow, rec.depth});
        (isFlow ? state.first : state.second) = internal;
        if (state.first == rec.flow && state.second == rec.depth)
            m_linkState.remove(rec.id);
        else
            m_linkState.insert(rec.id, state);
    }
    emit dataChanged(this->index(row, FirstValueColumn),
                     this->index(row, columnCount() - 1));
    if (wasDirty != isDirty())
        emit dirtyChanged(isDirty());
    return true;
}

QVariant HotStartRecordModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return QAbstractTableModel::headerData(section, orientation, role);
    if (section == IdColumn)
        return m_kind == Kind::Nodes   ? tr("Node")
               : m_kind == Kind::Links ? tr("Link")
                                       : tr("Subcatchment");
    if (section == InModelColumn)
        return tr("In model");
    const QStringList keys = valueKeys();
    const int valueIndex = section - FirstValueColumn;
    if (valueIndex >= keys.size()) {
        const int s = valueIndex - int(keys.size());
        return m_document && s < m_document->species().size() ? m_document->species().at(s)
                                                               : QVariant();
    }
    const QString key = keys.at(valueIndex);
    if (key == QLatin1String("depth"))
        return tr("Depth (%1)").arg(m_units.lengthLabel);
    if (key == QLatin1String("head"))
        return tr("Head (%1)").arg(m_units.lengthLabel);
    if (key == QLatin1String("volume"))
        return tr("Volume (%1)").arg(m_units.volumeLabel);
    if (key == QLatin1String("age"))
        return tr("Water age (h)");
    if (key == QLatin1String("flow"))
        return tr("Flow (%1)").arg(m_units.flowLabel);
    if (key == QLatin1String("runoff"))
        return tr("Runoff (%1)").arg(m_units.flowLabel);
    if (key == QLatin1String("gwFlow"))
        return tr("Groundwater flow (%1)").arg(m_units.flowLabel);
    if (key == QLatin1String("gwMoisture"))
        return tr("Upper-zone moisture");
    return tr("Lower-zone depth (%1)").arg(m_units.lengthLabel);
}

// ── Window ──────────────────────────────────────────────────────────────────

HotStartInspectorDialog::HotStartInspectorDialog(EngineSource liveEngine, QWidget *parent)
    : QDialog(parent), m_liveEngine(std::move(liveEngine))
{
    setWindowTitle(tr("Hot-Start File"));
    setAttribute(Qt::WA_DeleteOnClose);
    resize(820, 560);
    m_confirm = [this](const QString &question) {
        return QMessageBox::question(this, windowTitle(), question) == QMessageBox::Yes;
    };

    auto *layout = new QVBoxLayout(this);
    auto *fileRow = new QHBoxLayout;
    m_path = new QLabel(tr("No file is open."), this);
    m_path->setObjectName(QStringLiteral("hotstartPath"));
    m_path->setTextInteractionFlags(Qt::TextSelectableByMouse);
    fileRow->addWidget(m_path, 1);
    auto *browse = new QPushButton(tr("Open…"), this);
    browse->setObjectName(QStringLiteral("hotstartOpen"));
    browse->setAutoDefault(false);
    connect(browse, &QPushButton::clicked, this, &HotStartInspectorDialog::chooseFile);
    fileRow->addWidget(browse);
    layout->addLayout(fileRow);

    auto *form = new QFormLayout;
    const QList<QPair<QString, QString>> fields{
        {QStringLiteral("format"), tr("Format:")},     {QStringLiteral("saved"), tr("Saved:")},
        {QStringLiteral("start"), tr("Run start:")},   {QStringLiteral("time"), tr("State at:")},
        {QStringLiteral("end"), tr("Run end:")},       {QStringLiteral("crs"), tr("CRS:")},
        {QStringLiteral("contents"), tr("Contents:")}};
    for (const auto &[key, label] : fields) {
        auto *value = new QLabel(this);
        value->setObjectName(QStringLiteral("hotstartInfo_") + key);
        value->setWordWrap(true);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        form->addRow(label, value);
        m_info.insert(key, value);
    }
    layout->addLayout(form);

    m_notice = new QLabel(this);
    m_notice->setObjectName(QStringLiteral("hotstartNotice"));
    m_notice->setWordWrap(true);
    layout->addWidget(m_notice);

    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("hotstartTabs"));
    const QList<QPair<HotStartRecordModel::Kind, QString>> kinds{
        {HotStartRecordModel::Kind::Nodes, tr("Nodes")},
        {HotStartRecordModel::Kind::Links, tr("Links")},
        {HotStartRecordModel::Kind::Subcatchments, tr("Subcatchments")}};
    for (const auto &[kind, title] : kinds) {
        auto *model = new HotStartRecordModel(kind, this);
        auto *view = new QTableView(this);
        view->setObjectName(QStringLiteral("hotstartTable_") + title.toLower());
        view->setAccessibleName(tr("Hot-start %1").arg(title.toLower()));
        view->setModel(model);
        view->setSelectionBehavior(QAbstractItemView::SelectRows);
        view->setEditTriggers(QAbstractItemView::DoubleClicked |
                              QAbstractItemView::EditKeyPressed |
                              QAbstractItemView::AnyKeyPressed);
        view->verticalHeader()->hide();
        view->horizontalHeader()->setStretchLastSection(true);
        view->installEventFilter(this);
        TableClipboard::install(view);
        connect(view, &QTableView::doubleClicked, this,
                [this, view](const QModelIndex &index) { activate(view, index); });
        connect(model, &HotStartRecordModel::dirtyChanged, this,
                &HotStartInspectorDialog::updateButtons);
        m_tabs->addTab(view, title);
        m_views.append(view);
        m_models.append(model);
    }
    layout->addWidget(m_tabs, 1);

    auto *buttons = new QDialogButtonBox(this);
    m_save = buttons->addButton(tr("Save"), QDialogButtonBox::ActionRole);
    m_save->setObjectName(QStringLiteral("hotstartSave"));
    m_revert = buttons->addButton(tr("Revert"), QDialogButtonBox::ActionRole);
    m_revert->setObjectName(QStringLiteral("hotstartRevert"));
    auto *close = buttons->addButton(QDialogButtonBox::Close);
    for (auto *button : {m_save, m_revert, close})
        button->setAutoDefault(false);
    connect(m_save, &QPushButton::clicked, this, &HotStartInspectorDialog::save);
    connect(m_revert, &QPushButton::clicked, this, &HotStartInspectorDialog::revert);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    layout->addWidget(buttons);
    refresh();
}

HotStartRecordModel *HotStartInspectorDialog::model(HotStartRecordModel::Kind kind) const
{
    for (auto *model : m_models)
        if (model->kind() == kind)
            return model;
    return nullptr;
}

QString HotStartInspectorDialog::noticeText() const
{
    return m_notice->text();
}

QString HotStartInspectorDialog::infoText(const QString &field) const
{
    const auto *label = m_info.value(field);
    return label ? label->text() : QString();
}

bool HotStartInspectorDialog::openFile(const QString &path)
{
    QString error;
    if (!m_document.open(path, m_liveEngine ? m_liveEngine() : nullptr, &error)) {
        m_notice->setText(tr("%1 could not be opened. %2")
                              .arg(QFileInfo(path).fileName(), error));
        return false;
    }
    refresh();
    return true;
}

void HotStartInspectorDialog::chooseFile()
{
    const QString start = m_document.path().isEmpty() ? QString() : m_document.path();
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open Hot-Start File"), start,
        tr("Hot-start files (*.hsf *.hs *.hot);;All files (*)"));
    if (!path.isEmpty())
        openFile(path);
}

void HotStartInspectorDialog::refresh()
{
    const auto &doc = m_document;
    const auto &info = doc.info();
    const bool open = !doc.path().isEmpty();
    const HotStartUnits units = HotStartUnits::forEngine(m_liveEngine ? m_liveEngine() : nullptr);
    for (auto *model : m_models)
        model->setDocument(open ? &doc : nullptr, units);
    if (!open) {
        m_notice->setText(tr("Open a hot-start file to see the state it holds."));
        updateButtons();
        return;
    }
    m_path->setText(QDir::toNativeSeparators(doc.path()));
    setWindowTitle(tr("Hot-Start File — %1").arg(QFileInfo(doc.path()).fileName()));
    const bool legacy = info.format == HotStartDocument::Format::Legacy;
    m_info.value(QStringLiteral("format"))
        ->setText(legacy ? tr("SWMM 5 hot-start file, version %1").arg(info.version)
                         : tr("OpenSWMM hot-start file, version %1").arg(info.version));
    m_info.value(QStringLiteral("saved"))
        ->setText(info.savedAt > 0 ? QDateTime::fromSecsSinceEpoch(info.savedAt, QTimeZone::UTC)
                                             .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss 'UTC'"))
                                   : tr("not recorded"));
    m_info.value(QStringLiteral("start"))->setText(legacy ? tr("not recorded")
                                                          : swmmDate(info.startDate));
    m_info.value(QStringLiteral("time"))
        ->setText(legacy ? tr("not recorded")
                         : tr("%1 (%2 after the run start)")
                               .arg(swmmDate(info.startDate + info.elapsedSeconds / 86400.0),
                                    duration(info.elapsedSeconds)));
    m_info.value(QStringLiteral("end"))
        ->setText(info.endDate > 0 ? swmmDate(info.endDate) : tr("not recorded"));
    m_info.value(QStringLiteral("crs"))->setText(info.crs.isEmpty() ? tr("none") : info.crs);
    QStringList contents{tr("%1 nodes, %2 links, %3 subcatchments")
                             .arg(doc.nodes().size())
                             .arg(doc.links().size())
                             .arg(doc.subcatchments().size())};
    const QList<QPair<int, QString>> flags{
        {SWMM_HOTSTART_HAS_SPECIES, tr("concentrations of %1").arg(doc.species().join(", "))},
        {SWMM_HOTSTART_HAS_GROUNDWATER, tr("2D groundwater cells")},
        {SWMM_HOTSTART_HAS_GW_TRANSPORT, tr("groundwater transport")},
        {SWMM_HOTSTART_HAS_SNOW, tr("snow")},
        {SWMM_HOTSTART_HAS_LID, tr("LID cells")},
        {SWMM_HOTSTART_HAS_SURFACE, tr("subcatchment surface state")},
        {SWMM_HOTSTART_HAS_ENGINE_CHECKPOINT, tr("full engine checkpoint")}};
    for (const auto &[flag, text] : flags)
        if (info.contents & flag)
            contents.append(text);
    m_info.value(QStringLiteral("contents"))->setText(contents.join(QStringLiteral("; ")));

    QStringList notice;
    if (legacy)
        notice << tr("A SWMM 5 file stores values in object order without names; they are matched "
                     "to the open model's objects by that order. It is shown read-only.");
    else if (info.contents & SWMM_HOTSTART_HAS_ENGINE_CHECKPOINT)
        notice << tr("A restart restores this file's full engine checkpoint, not the values "
                     "below, so they are shown read-only.");
    else
        notice << tr("You can edit a node's depth and a link's flow and depth. When a run uses "
                     "the file, head and volume are set from the model's geometry.");
    const int missing = doc.modelNodesMissing() + doc.modelLinksMissing() +
                        doc.modelSubcatchmentsMissing();
    if (!legacy && missing > 0)
        notice << tr("The open model has %1 nodes, %2 links and %3 subcatchments that the file "
                     "has no values for.")
                      .arg(doc.modelNodesMissing())
                      .arg(doc.modelLinksMissing())
                      .arg(doc.modelSubcatchmentsMissing());
    m_notice->setText(notice.join(QLatin1Char(' ')));
    updateButtons();
}

void HotStartInspectorDialog::updateButtons()
{
    bool dirty = false;
    for (auto *model : m_models)
        dirty = dirty || model->isDirty();
    m_save->setEnabled(dirty);
    m_revert->setEnabled(dirty);
}

bool HotStartInspectorDialog::save()
{
    const auto *nodes = model(HotStartRecordModel::Kind::Nodes);
    const auto *links = model(HotStartRecordModel::Kind::Links);
    const int count = int(nodes->nodeEdits().size() + links->linkEdits().size());
    if (count == 0)
        return false;
    const QString objects = count == 1 ? tr("1 object") : tr("%1 objects").arg(count);
    const QString question = tr("Write the edits to %1 into %2? The file is replaced; keep a copy "
                                "if you may need the old values.")
                                 .arg(objects, QFileInfo(m_document.path()).fileName());
    if (m_confirm && !m_confirm(question))
        return false;
    QString error;
    if (!m_document.save(nodes->nodeEdits(), links->linkEdits(),
                         m_liveEngine ? m_liveEngine() : nullptr, &error)) {
        m_notice->setText(tr("Nothing was written. %1").arg(error));
        return false;
    }
    refresh();
    m_notice->setText(tr("Saved. %1").arg(m_notice->text()));
    return true;
}

void HotStartInspectorDialog::revert()
{
    for (auto *model : m_models)
        model->clearEdits();
    updateButtons();
}

void HotStartInspectorDialog::activate(QTableView *view, const QModelIndex &index)
{
    if (!index.isValid() || index.column() > HotStartRecordModel::InModelColumn)
        return; // value cells edit on double-click
    auto *model = qobject_cast<HotStartRecordModel *>(view->model());
    if (!model || model->data(model->index(index.row(), HotStartRecordModel::InModelColumn),
                              Qt::DisplayRole) != tr("Yes"))
        return;
    const auto type = model->kind() == HotStartRecordModel::Kind::Nodes   ? SWMMObjectRef::Node
                      : model->kind() == HotStartRecordModel::Kind::Links ? SWMMObjectRef::Link
                                                                          : SWMMObjectRef::Subcatchment;
    emit objectActivated(
        SWMMObjectRef(type, model->data(index, HotStartRecordModel::ObjectIdRole).toString()));
}

bool HotStartInspectorDialog::eventFilter(QObject *watched, QEvent *event)
{
    auto *view = qobject_cast<QTableView *>(watched);
    if (view && m_views.contains(view) && event->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent *>(event)->key();
        // While a cell is edited its editor, not the view, receives the keys.
        if ((key == Qt::Key_Return || key == Qt::Key_Enter) &&
            view->currentIndex().column() <= HotStartRecordModel::InModelColumn) {
            activate(view, view->currentIndex());
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

void HotStartInspectorDialog::closeEvent(QCloseEvent *event)
{
    bool dirty = false;
    for (auto *model : m_models)
        dirty = dirty || model->isDirty();
    if (dirty && m_confirm && !m_confirm(tr("Discard the edits that were not saved?"))) {
        event->ignore();
        return;
    }
    QDialog::closeEvent(event);
}

} // namespace openswmmvis::ui
