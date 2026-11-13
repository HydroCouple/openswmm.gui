#include "ui/panels/usagetablemodel.h"
#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "ui/util/tableclipboard.h"
#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_subcatchments.h>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QTimer>
#include <QUndoCommand>
#include <QVBoxLayout>
#include <cmath>
#include <limits>

namespace
{
using Target = UsageTarget;
using Family = UsageEditRef::Family;
struct LidRow {
    QString control, report, absolute, drain;
    int drainKind = -1, number = 1, toPerv = 0;
    double area = 0, width = 0, saturation = 0, imperv = 0, perv = 0;
    bool operator==(const LidRow &) const = default;
};
struct InletRow {
    bool exists = false;
    QString design, capture;
    int number = 1, placement = SWMM_INLET_AUTOMATIC;
    double clogged = 0, limit = 0, depression = 0, width = 0;
    bool operator==(const InletRow &) const = default;
};
struct Payload {
    QList<LidRow> lids;
    InletRow inlet;
    bool operator==(const Payload &) const = default;
};
int targetIndex(SWMM_Engine e, const Target &t)
{
    const auto bytes = t.name.toUtf8();
    return t.kind == Target::Subcatchment ? swmm_subcatch_index(e, bytes.constData())
           : t.kind == Target::Link       ? swmm_link_index(e, bytes.constData())
                                          : swmm_node_index(e, bytes.constData());
}
int mask(Target::Kind k)
{
    return k == Target::Subcatchment ? SWMMModelLayer::kKindCatch
           : k == Target::Link       ? SWMMModelLayer::kKindLink
                                     : SWMMModelLayer::kKindNode;
}
bool editable(SWMM_Engine e)
{
    int state = -1;
    return e && swmm_engine_get_state(e, &state) == SWMM_OK &&
           (state == SWMM_STATE_BUILDING || state == SWMM_STATE_OPENED);
}
QString inletKey(int kind, int index)
{
    return QString::number(kind) + "/" + QString::number(index);
}
struct Context {
    QPointer<SWMMModelLayer> layer;
    SWMM_Engine engine = nullptr;
    Family family;
    QVector<Target> targets;
    QList<Payload> before, after;
    bool valid = true;
    int subs = 0, nodes = 0, links = 0;
    std::function<void()> changed;
    QObject guard;
    explicit Context(UsageEditRef ref) : layer(ref.layer), engine(ref.engine), family(ref.family)
    {
        valid = layer && engine && layer->engine() == engine;
        QSet<QString> seen;
        for (auto t : ref.targets) {
            const int index = valid ? targetIndex(engine, t) : -1;
            if (index >= 0) {
                const char *name = t.kind == Target::Subcatchment ? swmm_subcatch_id(engine, index)
                                   : t.kind == Target::Link       ? swmm_link_id(engine, index)
                                                                  : swmm_node_id(engine, index);
                if (name)
                    t.name = QString::fromUtf8(name);
            }
            const auto key = QString::number(t.kind) + "/" + t.name;
            if (!seen.contains(key)) {
                seen.insert(key);
                targets << t;
            }
            if (index < 0 || (family == UsageEditRef::Lid && t.kind != Target::Subcatchment) ||
                (family == UsageEditRef::Inlet && t.kind == Target::Subcatchment))
                valid = false;
        }
        if (targets.isEmpty())
            valid = false;
        if (!valid)
            return;
        subs = swmm_subcatch_count(engine);
        nodes = swmm_node_count(engine);
        links = swmm_link_count(engine);
        QObject::connect(layer, &SWMMModelLayer::engineAboutToClose, &guard, [this] {
            valid = false;
            notify();
        });
        QObject::connect(layer, &SWMMModelLayer::attributeChanged, &guard, [this] { notify(); });
        QObject::connect(
            layer, &SWMMModelLayer::objectRenamed, &guard,
            [this](const QString &from, const QString &to, int kind) {
                for (auto &t : targets)
                    if ((kind & mask(t.kind)) && t.name == from)
                        t.name = to;
                for (auto *records : {&before, &after})
                    for (auto &p : *records) {
                        if (kind & SWMMModelLayer::kKindNode)
                            if (p.inlet.capture == from)
                                p.inlet.capture = to;
                        for (auto &r : p.lids)
                            if (r.drainKind >= 0 &&
                                (kind & mask(static_cast<Target::Kind>(r.drainKind))) &&
                                r.drain.compare(from, Qt::CaseInsensitive) == 0)
                                r.drain = to;
                    }
                notify();
            });
        QObject::connect(layer, &SWMMModelLayer::geometryChanged, &guard, [this] {
            if (!available())
                return;
            int s = swmm_subcatch_count(engine), n = swmm_node_count(engine),
                l = swmm_link_count(engine);
            if ((s < subs || n < nodes || l < links) && !referencesExist())
                valid = false;
            subs = s;
            nodes = n;
            links = l;
            notify();
        });
        QObject::connect(layer, &SWMMModelLayer::dataObjectsChanged, &guard, [this] {
            if (available() && !referencesExist())
                valid = false;
            notify();
        });
    }
    bool available() const { return valid && layer && layer->engine() == engine; }
    void notify()
    {
        if (changed)
            changed();
    }
    bool referencesExist() const
    {
        if (!available())
            return false;
        for (const auto &t : targets)
            if (targetIndex(engine, t) < 0)
                return false;
        for (const auto *records : {&before, &after})
            for (const auto &p : *records) {
                if (p.inlet.exists &&
                    (swmm_inlet_index(engine, p.inlet.design.toUtf8().constData()) < 0 ||
                     swmm_node_index(engine, p.inlet.capture.toUtf8().constData()) < 0))
                    return false;
                for (const auto &r : p.lids) {
                    if (swmm_lid_index(engine, r.control.toUtf8().constData()) < 0)
                        return false;
                    if (r.drainKind >= 0 &&
                        targetIndex(engine, {static_cast<Target::Kind>(r.drainKind), r.drain}) < 0)
                        return false;
                }
            }
        return true;
    }
    UsageEditRef ref() const
    {
        UsageEditRef r;
        r.layer = layer;
        r.engine = engine;
        r.family = family;
        r.targets = targets;
        return r;
    }
};
bool finite(const LidRow &r)
{
    for (double v : {r.area, r.width, r.saturation, r.imperv, r.perv})
        if (!std::isfinite(v))
            return false;
    return true;
}
bool finite(const InletRow &r)
{
    for (double v : {r.clogged, r.limit, r.depression, r.width})
        if (!std::isfinite(v))
            return false;
    return true;
}
bool snapshot(const Context &c, QList<Payload> &out, QString *error = nullptr)
{
    auto fail = [&](const QString &why) {
        if (error)
            *error = why;
        return false;
    };
    if (!c.available())
        return fail(
            QObject::tr("The captured model or target is no longer available. Reopen the editor."));
    out = QList<Payload>(c.targets.size());
    QHash<QString, int> owners;
    for (int i = 0; i < c.targets.size(); ++i) {
        const auto &t = c.targets[i];
        int index = targetIndex(c.engine, t);
        if (index < 0)
            return fail(QObject::tr("A captured target is missing."));
        if (c.family == UsageEditRef::Inlet) {
            int flag = 0;
            if (t.kind == Target::Node) {
                if (swmm_node_is_inlet(c.engine, index, &flag) != SWMM_OK || !flag)
                    return fail(
                        QObject::tr("Only inlet junctions can host node inlet assignments."));
            } else {
                int type = -1;
                if (swmm_link_get_type(c.engine, index, &type) != SWMM_OK || type != 0)
                    return fail(QObject::tr("Only conduits can host link inlet assignments."));
            }
        }
        owners.insert(inletKey(t.kind, index), i);
    }
    if (c.family == UsageEditRef::Lid) {
        for (int row = 0; row < swmm_lid_usage_count(c.engine); ++row) {
            SWMM_LidUsageRecord u{};
            if (swmm_lid_usage_get_record(c.engine, row, &u) != SWMM_OK)
                return fail(QObject::tr("A complete LID placement could not be read."));
            auto key = inletKey(Target::Subcatchment, u.subcatch_idx);
            if (!owners.contains(key))
                continue;
            const char *control = swmm_lid_id(c.engine, u.control_idx);
            if (!control)
                return fail(QObject::tr("A LID control reference is unavailable."));
            LidRow r;
            r.control = QString::fromUtf8(control);
            r.number = u.number;
            r.area = u.area;
            r.width = u.width;
            r.saturation = u.init_sat;
            r.imperv = u.from_imperv;
            r.toPerv = u.to_perv;
            r.perv = u.from_perv;
            r.report = QString::fromUtf8(u.report_file);
            r.absolute = QString::fromUtf8(u.report_file_resolved);
            r.drain = QString::fromUtf8(u.drain_to);
            if (r.drain == "*")
                r.drain.clear();
            if (!r.drain.isEmpty()) {
                if (swmm_node_index(c.engine, r.drain.toUtf8().constData()) >= 0)
                    r.drainKind = Target::Node;
                else if (swmm_subcatch_index(c.engine, r.drain.toUtf8().constData()) >= 0)
                    r.drainKind = Target::Subcatchment;
            }
            if (!finite(r))
                return fail(QObject::tr(
                    "A LID placement contains nonfinite native values and cannot be safely restored."));
            out[owners.value(key)].lids << r;
        }
    } else {
        for (int row = 0; row < swmm_inlet_usage_count(c.engine); ++row) {
            SWMM_InletUsage u{};
            if (swmm_inlet_usage_get(c.engine, row, &u) != SWMM_OK)
                return fail(QObject::tr("An inlet assignment could not be read."));
            auto key = inletKey(u.host_kind == SWMM_INLET_HOST_NODE ? Target::Node : Target::Link,
                                u.host_idx);
            if (!owners.contains(key))
                continue;
            auto &r = out[owners.value(key)].inlet;
            if (r.exists)
                return fail(QObject::tr(
                    "More than one inlet assignment exists on a host. Resolve duplicate rows individually first."));
            const char *design = swmm_inlet_id(c.engine, u.design_idx),
                       *capture = swmm_node_id(c.engine, u.capture_node_idx);
            if (!design || !capture)
                return fail(
                    QObject::tr("An inlet design or capture-node reference is unavailable."));
            r.exists = true;
            r.design = QString::fromUtf8(design);
            r.capture = QString::fromUtf8(capture);
            r.number = u.num_inlets;
            r.clogged = u.pct_clogged;
            r.limit = u.flow_limit;
            r.depression = u.local_depress;
            r.width = u.local_width;
            r.placement = u.placement;
            if (!finite(r))
                return fail(QObject::tr("An inlet assignment contains nonfinite native values."));
        }
    }
    return true;
}
SWMM_InletUsage nativeInlet(const Context &c, int index, const InletRow &r)
{
    SWMM_InletUsage u{};
    const auto &t = c.targets[index];
    u.host_kind = t.kind == Target::Node ? SWMM_INLET_HOST_NODE : SWMM_INLET_HOST_LINK;
    u.host_idx = targetIndex(c.engine, t);
    u.design_idx = swmm_inlet_index(c.engine, r.design.toUtf8().constData());
    u.capture_node_idx = swmm_node_index(c.engine, r.capture.toUtf8().constData());
    u.num_inlets = r.number;
    u.pct_clogged = r.clogged;
    u.flow_limit = r.limit;
    u.local_depress = r.depression;
    u.local_width = r.width;
    u.placement = r.placement;
    return u;
}
struct NativeLids {
    QVector<QByteArray> reports, absolute, drains;
    QVector<SWMM_LidUsageRecord> rows;
    NativeLids(const Context &c, int index, const QList<LidRow> &lids)
    {
        for (const auto &r : lids) {
            reports << r.report.toUtf8();
            absolute << r.absolute.toUtf8();
            drains << r.drain.toUtf8();
        }
        for (int i = 0; i < lids.size(); ++i) {
            const auto &r = lids[i];
            rows << SWMM_LidUsageRecord{targetIndex(c.engine, c.targets[index]),
                                        swmm_lid_index(c.engine, r.control.toUtf8().constData()),
                                        r.number,
                                        r.area,
                                        r.width,
                                        r.saturation,
                                        r.imperv,
                                        r.toPerv,
                                        r.perv,
                                        reports[i].constData(),
                                        absolute[i].constData(),
                                        drains[i].constData()};
        }
    }
};
bool preflight(const Context &c, const QList<Payload> &records, QString *error)
{
    if (!c.referencesExist() || !editable(c.engine)) {
        if (error)
            *error = QObject::tr("A target, reference or editable model context has changed.");
        return false;
    }
    for (int i = 0; i < records.size(); ++i) {
        int status = SWMM_OK;
        if (c.family == UsageEditRef::Lid) {
            NativeLids native(c, i, records[i].lids);
            for (const auto &r : native.rows) {
                status = swmm_lid_usage_validate_record(c.engine, &r);
                if (status != SWMM_OK)
                    break;
            }
        } else if (records[i].inlet.exists) {
            auto native = nativeInlet(c, i, records[i].inlet);
            status = swmm_inlet_usage_validate(c.engine, &native);
        }
        if (status != SWMM_OK) {
            if (error)
                *error =
                    QObject::tr(
                        "Assignment rejected for %1. Check its control/design, host compatibility and receiving node.")
                        .arg(c.targets[i].name);
            return false;
        }
    }
    return true;
}
bool write(const Context &c, int index, const Payload &p)
{
    if (c.family == UsageEditRef::Lid) {
        NativeLids native(c, index, p.lids);
        return swmm_lid_usage_replace_subcatch(c.engine, targetIndex(c.engine, c.targets[index]),
                                               native.rows.constData(),
                                               native.rows.size()) == SWMM_OK;
    }
    if (p.inlet.exists) {
        auto native = nativeInlet(c, index, p.inlet);
        return swmm_inlet_usage_set(c.engine, &native, nullptr) == SWMM_OK;
    }
    const auto &t = c.targets[index];
    int host = targetIndex(c.engine, t);
    int row = t.kind == Target::Node ? swmm_inlet_usage_find_node(c.engine, host)
                                     : swmm_inlet_usage_find_link(c.engine, host);
    return row < 0 || swmm_inlet_usage_remove(c.engine, row) == SWMM_OK;
}
class UsageCommand : public QUndoCommand
{
public:
    std::shared_ptr<Context> c;
    UsageCommand(const UsageEditRef &ref, QList<Payload> before, QList<Payload> after)
        : c(std::make_shared<Context>(ref))
    {
        c->before = std::move(before);
        c->after = std::move(after);
        setText(QObject::tr("Edit selected usage assignments"));
    }
    bool commit(bool forward, QString *error = nullptr)
    {
        const auto &expected = forward ? c->before : c->after;
        const auto &desired = forward ? c->after : c->before;
        QList<Payload> current;
        if (!preflight(*c, expected, error) || !preflight(*c, desired, error) ||
            !snapshot(*c, current, error) || current != expected) {
            if (error && error->isEmpty())
                *error = QObject::tr("The captured records changed. No values were overwritten.");
            return false;
        }
        c->layer->beginBulkEdit();
        int applied = 0;
        for (; applied < desired.size(); ++applied)
            if (expected[applied] != desired[applied] && !write(*c, applied, desired[applied]))
                break;
        const bool ok =
            applied == desired.size() && snapshot(*c, current, error) && current == desired;
        if (!ok)
            for (int i = qMin(applied, int(expected.size()) - 1); i >= 0; --i)
                if (expected[i] != desired[i])
                    write(*c, i, expected[i]);
        c->layer->endBulkEdit();
        if (ok) {
            for (int i = 0; i < desired.size(); ++i)
                if (expected[i] != desired[i])
                    emit c->layer->attributeChanged(c->targets[i].name);
            c->layer->markEdited();
            emit c->layer->repaintRequested();
        } else if (error && error->isEmpty())
            *error = QObject::tr("The assignment could not be verified and was rolled back.");
        return ok;
    }
    void redo() override
    {
        if (first) {
            first = false;
            return;
        }
        if (!commit(true))
            setObsolete(true);
    }
    void undo() override
    {
        if (!commit(false))
            setObsolete(true);
    }

private:
    bool first = true;
};
bool submitUsageEdit(UsageEditRef ref, const QList<Payload> &before, const QList<Payload> &after,
                     QUndoStack *undo, QString *error)
{
    if (before == after)
        return true;
    auto command = std::make_unique<UsageCommand>(ref, before, after);
    if (!command->commit(true, error))
        return false;
    if (undo)
        undo->push(command.release());
    return true;
}
QVariant lidValue(const LidRow &r, int col)
{
    switch (col) {
    case 2:
        return r.number;
    case 3:
        return r.area;
    case 4:
        return r.width;
    case 5:
        return r.saturation;
    case 6:
        return r.imperv;
    case 7:
        return r.toPerv;
    case 8:
        return r.perv;
    case 9:
        return r.drain;
    case 10:
        return r.report;
    default:
        return {};
    }
}
QVariant inletValue(const InletRow &r, int col)
{
    switch (col) {
    case 2:
        return r.design;
    case 3:
        return r.capture;
    case 4:
        return r.number;
    case 5:
        return r.clogged;
    case 6:
        return r.limit;
    case 7:
        return r.depression;
    case 8:
        return r.width;
    case 9:
        return r.placement;
    default:
        return {};
    }
}
struct Key {
    QString control;
    int occurrence = 0;
};
int lidIndex(const QList<LidRow> &rows, const Key &key)
{
    int occurrence = 0;
    for (int i = 0; i < rows.size(); ++i)
        if (rows[i].control == key.control && occurrence++ == key.occurrence)
            return i;
    return -1;
}
int lidCount(const QList<LidRow> &rows, const QString &control)
{
    int n = 0;
    for (const auto &r : rows)
        if (r.control == control)
            ++n;
    return n;
}
}

UsageEditRef usageRef(const SubcatchCompoundEditRef &ref)
{
    UsageEditRef r;
    r.engine = ref.engine;
    r.layer = ref.layer;
    r.undoStack = ref.undoStack;
    r.family = UsageEditRef::Lid;
    for (const auto &name : ref.subNames.isEmpty() ? QStringList{ref.subName} : ref.subNames)
        r.targets << Target{Target::Subcatchment, name};
    r.summary = ref.summary;
    return r;
}
UsageEditRef usageRef(const LinkCompoundEditRef &ref)
{
    UsageEditRef r;
    r.engine = ref.engine;
    r.layer = ref.layer;
    r.undoStack = ref.undoStack;
    r.family = UsageEditRef::Inlet;
    for (const auto &name : ref.linkNames.isEmpty() ? QStringList{ref.linkName} : ref.linkNames)
        r.targets << Target{Target::Link, name};
    r.summary = ref.summary;
    return r;
}
struct UsageTableModel::Private {
    std::shared_ptr<Context> c;
    QPointer<QUndoStack> undo;
    QVector<Key> keys;
    QString problem, error;
    bool readable = false, pending = false;
    QString ambiguous(int row) const
    {
        if (c->family != UsageEditRef::Lid || c->targets.size() == 1 || row < 0 ||
            row >= keys.size())
            return {};
        QStringList names;
        for (int i = 0; i < c->before.size(); ++i)
            if (lidCount(c->before[i].lids, keys[row].control) > 1)
                names << c->targets[i].name;
        return names.isEmpty()
                   ? QString()
                   : UsageTableModel::tr(
                         "Repeated placements of %1 on %2 are ambiguous across selections. Select that subcatchment alone to edit its individual placements.")
                         .arg(keys[row].control, names.join(", "));
    }
    int assigned(int row) const
    {
        int n = 0;
        for (const auto &p : c->before)
            if (c->family == UsageEditRef::Lid ? lidIndex(p.lids, keys.value(row)) >= 0
                                               : p.inlet.exists)
                ++n;
        return n;
    }
};
UsageTableModel::UsageTableModel(UsageEditRef ref, QObject *parent)
    : QAbstractTableModel(parent), d(std::make_unique<Private>())
{
    d->undo = ref.undoStack;
    d->c = std::make_shared<Context>(ref);
    d->c->changed = [this] {
        if (!d->pending) {
            d->pending = true;
            QTimer::singleShot(0, this, [this] {
                d->pending = false;
                refresh();
            });
        }
    };
    refresh();
}
UsageTableModel::~UsageTableModel() = default;
void UsageTableModel::invalidateContext()
{
    d->c->valid = false;
    refresh();
}
void UsageTableModel::refresh()
{
    beginResetModel();
    d->keys.clear();
    d->problem.clear();
    d->readable = snapshot(*d->c, d->c->before, &d->problem);
    if (d->readable) {
        if (d->c->family == UsageEditRef::Inlet)
            d->keys << Key{};
        else {
            QSet<QString> present;
            if (d->c->targets.size() == 1) {
                QHash<QString, int> occurrences;
                for (const auto &r : d->c->before.first().lids) {
                    d->keys << Key{r.control, occurrences[r.control]++};
                    present.insert(r.control);
                }
            }
            for (int i = 0; i < swmm_lid_count(d->c->engine); ++i) {
                int type = -1;
                swmm_lid_get_type(d->c->engine, i, &type);
                const char *id = swmm_lid_id(d->c->engine, i);
                if (!id || type == 8)
                    continue;
                const auto name = QString::fromUtf8(id);
                if (!present.contains(name))
                    d->keys << Key{name, 0};
            }
        }
        if (!d->undo)
            d->problem = tr(
                "The project Undo stack is unavailable. Reopen from its Properties or Attribute Table.");
    }
    endResetModel();
}
int UsageTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : d->keys.size();
}
int UsageTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : d->c->family == UsageEditRef::Lid ? 11 : 10;
}
UsageEditRef::Family UsageTableModel::family() const
{
    return d->c->family;
}
QString UsageTableModel::rowKey(int row) const
{
    return d->keys.value(row).control;
}
QString UsageTableModel::summary() const
{
    return d->problem;
}
QString UsageTableModel::lastError() const
{
    return d->error;
}
QStringList UsageTableModel::targetNames() const
{
    QStringList names;
    for (const auto &t : d->c->targets)
        names << t.name;
    return names;
}
QVariant UsageTableModel::headerData(int col, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    const QStringList lid{tr("LID control / placement"),
                          tr("Assigned"),
                          tr("Units"),
                          tr("Area per unit"),
                          tr("Top width"),
                          tr("Saturation (%)"),
                          tr("Impervious treated (%)"),
                          tr("Surface to pervious"),
                          tr("Pervious treated (%)"),
                          tr("Drain to"),
                          tr("Report file")};
    const QStringList inlet{tr("Scope"),
                            tr("Assigned"),
                            tr("Inlet design"),
                            tr("Capture node"),
                            tr("Inlets"),
                            tr("Clogged (%)"),
                            tr("Flow limit"),
                            tr("Depression height"),
                            tr("Depression width"),
                            tr("Placement")};
    return family() == UsageEditRef::Lid ? lid.value(col) : inlet.value(col);
}
QVariant UsageTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= d->keys.size())
        return {};
    if (role == Qt::ToolTipRole) {
        if (!d->problem.isEmpty())
            return d->problem;
        auto problem = d->ambiguous(index.row());
        if (!problem.isEmpty())
            return problem;
        if (family() == UsageEditRef::Lid && index.column() == 3)
            return tr(
                "A positive unit area creates missing placements with one unit, zero width/saturation/routed inputs, no report and normal outlet routing.");
        return tr(
            "Only this field changes on the captured targets. Sibling fields, placements and unselected objects are preserved.");
    }
    if (role != Qt::EditRole && role != Qt::DisplayRole)
        return {};
    int row = index.row(), col = index.column();
    const auto &key = d->keys[row];
    if (col == 0) {
        if (family() == UsageEditRef::Inlet)
            return tr("Captured hosts");
        int count =
            d->c->targets.size() == 1 ? lidCount(d->c->before.first().lids, key.control) : 0;
        return count > 1 ? tr("%1 — placement %2").arg(key.control).arg(key.occurrence + 1)
                         : key.control;
    }
    const int assigned = d->assigned(row);
    if (col == 1)
        return tr("%1 / %2").arg(assigned).arg(d->c->targets.size());
    QVariant common;
    bool first = true, mixed = false;
    for (const auto &p : d->c->before) {
        QVariant v;
        if (family() == UsageEditRef::Lid) {
            int at = lidIndex(p.lids, key);
            if (at >= 0)
                v = lidValue(p.lids[at], col);
        } else if (p.inlet.exists)
            v = inletValue(p.inlet, col);
        if (first) {
            common = v;
            first = false;
        } else if (v != common)
            mixed = true;
    }
    if (mixed || !assigned)
        return role == Qt::DisplayRole ? QVariant(assigned ? tr("Mixed") : tr("(unassigned)"))
                                       : QVariant();
    if (role == Qt::EditRole)
        return common;
    if (common.metaType().id() == QMetaType::Double)
        return QString::number(common.toDouble(), 'g', 12);
    for (const auto &choice : choices(index))
        if (choice.value == common)
            return choice.label;
    return common.toString().isEmpty() ? tr("(none)") : common;
}
Qt::ItemFlags UsageTableModel::flags(const QModelIndex &index) const
{
    auto result = QAbstractTableModel::flags(index);
    if (index.isValid() && index.row() < d->keys.size() && index.column() >= 2 && d->readable &&
        d->c->available() && d->undo && editable(d->c->engine) &&
        d->ambiguous(index.row()).isEmpty() &&
        (d->assigned(index.row()) == d->c->targets.size() ||
         (family() == UsageEditRef::Lid && index.column() == 3)))
        result |= Qt::ItemIsEditable;
    return result;
}
QList<UsageChoice> UsageTableModel::choices(const QModelIndex &index) const
{
    QList<UsageChoice> out;
    if (!d->c->available())
        return out;
    auto e = d->c->engine;
    int col = index.column();
    if (family() == UsageEditRef::Lid) {
        if (col == 7)
            return {{tr("No"), 0}, {tr("Yes"), 1}};
        if (col == 9) {
            out << UsageChoice{tr("Normal outlet"), QString()};
            for (int n = 0; n < swmm_node_count(e); ++n) {
                QString name = QString::fromUtf8(swmm_node_id(e, n));
                if (swmm_subcatch_index(e, name.toUtf8().constData()) < 0)
                    out << UsageChoice{name, name};
            }
            for (int s = 0; s < swmm_subcatch_count(e); ++s) {
                QString name = QString::fromUtf8(swmm_subcatch_id(e, s));
                bool selected = false;
                for (const auto &t : d->c->targets)
                    if (t.name == name)
                        selected = true;
                if (!selected && swmm_node_index(e, name.toUtf8().constData()) < 0)
                    out << UsageChoice{name, name};
            }
        }
    } else {
        if (col == 2)
            for (int i = 0; i < swmm_inlet_count(e); ++i) {
                QString name = QString::fromUtf8(swmm_inlet_id(e, i));
                out << UsageChoice{name, name};
            }
        if (col == 3)
            for (int i = 0; i < swmm_node_count(e); ++i) {
                int virt = 0;
                swmm_node_is_virtual(e, i, &virt);
                if (virt)
                    continue;
                QString name = QString::fromUtf8(swmm_node_id(e, i));
                bool self = false;
                for (const auto &t : d->c->targets)
                    if (t.kind == Target::Node && t.name == name)
                        self = true;
                if (!self)
                    out << UsageChoice{name, name};
            }
        if (col == 9)
            return {{tr("Automatic"), int(SWMM_INLET_AUTOMATIC)},
                    {tr("On grade"), int(SWMM_INLET_ON_GRADE)},
                    {tr("On sag"), int(SWMM_INLET_ON_SAG)}};
    }
    return out;
}
bool UsageTableModel::fail(const QString &message)
{
    d->error = message;
    if (feedback)
        feedback(message);
    return false;
}
bool UsageTableModel::setData(const QModelIndex &index, const QVariant &v, int role)
{
    if (role != Qt::EditRole || !(flags(index) & Qt::ItemIsEditable))
        return fail(!d->problem.isEmpty() ? d->problem
                    : !d->ambiguous(index.row()).isEmpty()
                        ? d->ambiguous(index.row())
                        : tr("Create the missing assignments before editing this field."));
    QList<Payload> before;
    QString error;
    if (!snapshot(*d->c, before, &error))
        return fail(error);
    auto after = before;
    const int col = index.column();
    bool isText = family() == UsageEditRef::Lid ? col == 9 || col == 10 : col == 2 || col == 3;
    double number = 0;
    QString text = v.toString().trimmed();
    if (family() == UsageEditRef::Lid && col == 10 && text == "*")
        text.clear();
    if (isText) {
        if (text.contains('\n') || text.contains('\r') || text.contains(QChar('\0')) ||
            text.contains('"'))
            return fail(tr("Enter a single value without line breaks or quotation marks."));
    } else {
        bool ok = false;
        number = v.toDouble(&ok);
        if (!ok || !std::isfinite(number))
            return fail(tr("Enter a finite numeric value."));
        bool integral = family() == UsageEditRef::Lid ? col == 2 || col == 7 : col == 4 || col == 9;
        if (integral && (std::floor(number) != number || number > std::numeric_limits<int>::max()))
            return fail(tr("Enter a valid whole-number choice or count."));
    }
    if (family() == UsageEditRef::Lid) {
        if ((col == 2 && number < 1) || (col == 3 && number <= 0) || (col == 4 && number < 0) ||
            ((col == 5 || col == 6 || col == 8) && (number < 0 || number > 100)) ||
            (col == 7 && number != 0 && number != 1))
            return fail(tr(
                "Use units >= 1, positive area, nonnegative width and percentages between 0 and 100."));
        int drainKind = -1;
        if (col == 9 && !text.isEmpty()) {
            int node = swmm_node_index(d->c->engine, text.toUtf8().constData()),
                sub = swmm_subcatch_index(d->c->engine, text.toUtf8().constData());
            if ((node < 0 && sub < 0) || (node >= 0 && sub >= 0))
                return fail(
                    tr("Choose an unambiguous existing node or subcatchment for the drain."));
            drainKind = node >= 0 ? Target::Node : Target::Subcatchment;
            text = QString::fromUtf8(node >= 0 ? swmm_node_id(d->c->engine, node)
                                               : swmm_subcatch_id(d->c->engine, sub));
            for (const auto &t : d->c->targets)
                if (drainKind == Target::Subcatchment && t.name == text)
                    return fail(tr("A LID drain cannot target its own subcatchment."));
        }
        for (auto &p : after) {
            const auto &key = d->keys[index.row()];
            if (d->c->targets.size() > 1 && lidCount(p.lids, key.control) > 1)
                return fail(
                    tr("Repeated LID placements changed while editing. Reopen the editor."));
            int at = lidIndex(p.lids, key);
            if (at < 0) {
                if (col != 3)
                    return fail(tr("Set a positive unit area first to create missing placements."));
                LidRow r;
                r.control = key.control;
                p.lids << r;
                at = p.lids.size() - 1;
            }
            auto &r = p.lids[at];
            switch (col) {
            case 2:
                r.number = int(number);
                break;
            case 3:
                r.area = number;
                break;
            case 4:
                r.width = number;
                break;
            case 5:
                r.saturation = number;
                break;
            case 6:
                r.imperv = number;
                break;
            case 7:
                r.toPerv = int(number);
                break;
            case 8:
                r.perv = number;
                break;
            case 9:
                r.drain = text;
                r.drainKind = drainKind;
                break;
            case 10:
                if (r.report != text) {
                    r.report = text;
                    r.absolute =
                        text.isEmpty()
                            ? QString()
                            : QDir::cleanPath(
                                  QDir(QFileInfo(d->c->layer->modelFilePath()).absolutePath())
                                      .absoluteFilePath(text));
                }
                break;
            }
        }
    } else {
        if ((col == 4 && number < 1) || (col == 5 && (number < 0 || number > 99)) ||
            ((col == 6 || col == 7 || col == 8) && number < 0) ||
            (col == 9 && (number < SWMM_INLET_AUTOMATIC || number > SWMM_INLET_ON_SAG)))
            return fail(tr(
                "Use a positive inlet count, clogging 0–99%, nonnegative limits/dimensions and a valid placement."));
        for (auto &p : after) {
            auto &r = p.inlet;
            if (!r.exists)
                return fail(tr("Create every missing inlet assignment first."));
            switch (col) {
            case 2:
                r.design = text;
                break;
            case 3:
                r.capture = text;
                break;
            case 4:
                r.number = int(number);
                break;
            case 5:
                r.clogged = number;
                break;
            case 6:
                r.limit = number;
                break;
            case 7:
                r.depression = number;
                break;
            case 8:
                r.width = number;
                break;
            case 9:
                r.placement = int(number);
                break;
            }
        }
    }
    const bool changed = before != after;
    if (!submitUsageEdit(d->c->ref(), before, after, d->undo, &error))
        return fail(error);
    d->c->before = after;
    d->error.clear();
    emit dataChanged(index, index);
    if (feedback)
        feedback(changed ? tr("Updated captured assignments. Each completed edit is one Undo step.")
                         : tr("No changes."));
    return true;
}
bool UsageTableModel::removeAssignment(int row)
{
    if (row < 0 || row >= d->keys.size() || !d->undo || !d->readable ||
        !d->ambiguous(row).isEmpty())
        return fail(tr("Select an unambiguous placement to remove."));
    QList<Payload> before;
    QString error;
    if (!snapshot(*d->c, before, &error))
        return fail(error);
    auto after = before;
    for (auto &p : after)
        if (family() == UsageEditRef::Lid) {
            if (d->c->targets.size() > 1 && lidCount(p.lids, d->keys[row].control) > 1)
                return fail(tr("Repeated placements are ambiguous."));
            int at = lidIndex(p.lids, d->keys[row]);
            if (at >= 0)
                p.lids.removeAt(at);
        } else
            p.inlet = {};
    if (!submitUsageEdit(d->c->ref(), before, after, d->undo, &error))
        return fail(error);
    d->error.clear();
    refresh();
    return true;
}
bool UsageTableModel::assignMissingInlets(const QString &design, const QString &capture)
{
    if (family() != UsageEditRef::Inlet || !d->undo)
        return fail(tr("No editable project inlet scope is available."));
    QList<Payload> before;
    QString error;
    if (!snapshot(*d->c, before, &error))
        return fail(error);
    auto after = before;
    for (auto &p : after)
        if (!p.inlet.exists) {
            p.inlet = {};
            p.inlet.exists = true;
            p.inlet.design = design;
            p.inlet.capture = capture;
        }
    if (!submitUsageEdit(d->c->ref(), before, after, d->undo, &error))
        return fail(error);
    d->error.clear();
    refresh();
    return true;
}
bool applyInletUsageBatch(SWMMModelLayer *layer, const QList<SWMM_InletUsage> &usages,
                          bool removing, QUndoStack *undo, QString *error)
{
    if (!layer || !layer->engine())
        return false;
    UsageEditRef ref;
    ref.layer = layer;
    ref.engine = layer->engine();
    ref.family = UsageEditRef::Inlet;
    for (const auto &u : usages) {
        const char *name =
            u.host_kind == SWMM_INLET_HOST_NODE   ? swmm_node_id(ref.engine, u.host_idx)
            : u.host_kind == SWMM_INLET_HOST_LINK ? swmm_link_id(ref.engine, u.host_idx)
                                                  : nullptr;
        if (!name)
            return false;
        ref.targets << Target{u.host_kind == SWMM_INLET_HOST_NODE ? Target::Node : Target::Link,
                              QString::fromUtf8(name)};
    }
    Context context(ref);
    if (context.targets.size() != usages.size())
        return false;
    QList<Payload> before;
    if (!snapshot(context, before, error))
        return false;
    auto after = before;
    for (int i = 0; i < usages.size(); ++i) {
        if (removing) {
            after[i].inlet = {};
            continue;
        }
        auto u = usages[i];
        if (swmm_inlet_usage_validate(ref.engine, &u) != SWMM_OK)
            return false;
        auto &r = after[i].inlet;
        r.exists = true;
        r.design = QString::fromUtf8(swmm_inlet_id(ref.engine, u.design_idx));
        r.capture = QString::fromUtf8(swmm_node_id(ref.engine, u.capture_node_idx));
        r.number = u.num_inlets;
        r.clogged = u.pct_clogged;
        r.limit = u.flow_limit;
        r.depression = u.local_depress;
        r.width = u.local_width;
        r.placement = u.placement;
    }
    return submitUsageEdit(ref, before, after, undo, error);
}

namespace
{
class FieldDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &,
                          const QModelIndex &index) const override
    {
        auto *model = static_cast<const UsageTableModel *>(index.model());
        auto *self = const_cast<FieldDelegate *>(this);
        auto choices = model->choices(index);
        if (!choices.isEmpty()) {
            auto *combo = new QComboBox(parent);
            for (const auto &choice : choices)
                combo->addItem(choice.label, choice.value);
            connect(combo, &QComboBox::activated, self, [self, combo] {
                combo->setProperty("touched", true);
                emit self->commitData(combo);
            });
            return combo;
        }
        auto *line = new QLineEdit(parent);
        connect(line, &QLineEdit::editingFinished, self, [self, line] {
            if (line->isModified())
                emit self->commitData(line);
        });
        return line;
    }
    void setEditorData(QWidget *widget, const QModelIndex &index) const override
    {
        const auto v = index.data(Qt::EditRole);
        widget->setProperty("touched", false);
        if (auto *combo = qobject_cast<QComboBox *>(widget))
            combo->setCurrentIndex(v.isValid() ? combo->findData(v) : -1);
        else if (auto *line = qobject_cast<QLineEdit *>(widget)) {
            line->setText(v.metaType().id() == QMetaType::Double
                              ? QString::number(v.toDouble(), 'g', 17)
                              : v.toString());
            line->setPlaceholderText(v.isValid() ? QString() : index.data().toString());
            line->setModified(false);
        }
    }
    void setModelData(QWidget *widget, QAbstractItemModel *model,
                      const QModelIndex &index) const override
    {
        if (auto *combo = qobject_cast<QComboBox *>(widget)) {
            if (combo->property("touched").toBool())
                model->setData(index, combo->currentData());
        } else if (auto *line = qobject_cast<QLineEdit *>(widget)) {
            if (line->isModified())
                model->setData(index, line->text());
        }
    }
};
class UsageDialog : public QDialog
{
public:
    UsageDialog(UsageEditRef ref, QWidget *parent) : QDialog(parent)
    {
        setObjectName("UsageEditor");
        setWindowTitle(ref.family == UsageEditRef::Lid ? tr("LID placements")
                                                       : tr("Inlet assignments"));
        buildUsageEditor(this, ref);
    }
    ~UsageDialog() override { invalidateUsageEditor(this); }
};
}
void invalidateUsageEditor(QWidget *host)
{
    if (auto *table = host->findChild<QTableView *>("usageTable"))
        if (auto *model = dynamic_cast<UsageTableModel *>(table->model()))
            model->invalidateContext();
}
QDialog *openUsageEditor(UsageEditRef ref, QWidget *parent)
{
    auto *dialog = new UsageDialog(ref, parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
    return dialog;
}
void buildUsageEditor(QWidget *host, UsageEditRef ref)
{
    if (!ref.undoStack && ref.layer) {
        if (auto *canvas = ref.layer->editCanvas())
            ref.undoStack = canvas->undoStack();
    }
    if (!ref.undoStack) {
        QObject *root = host;
        while (root->parent())
            root = root->parent();
        for (auto *canvas : root->findChildren<MapCanvas *>())
            if (canvas->layers().contains(ref.layer)) {
                ref.undoStack = canvas->undoStack();
                break;
            }
    }
    auto *layout = new QVBoxLayout(host);
    auto *hint = new QLabel(host);
    hint->setTextFormat(Qt::PlainText);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    int flow = 0;
    if (ref.layer && ref.layer->engine() == ref.engine)
        swmm_get_flow_units(ref.engine, &flow);
    auto *details = new QLabel(
        ref.family == UsageEditRef::Lid
            ? QObject::tr(
                  "Match unique LID controls across subcatchments; select one subcatchment for repeated placements. A positive area creates missing rows with 1 unit and otherwise zero/empty defaults. Other fields need a placement everywhere. Area is per unit in %1², width in %1 and inputs/saturation in percent. Other placements and report-path anchors are preserved.")
                  .arg(flow < 3 ? "ft" : "m")
            : QObject::tr(
                  "Edit one assignment field across the captured conduit or inlet-junction hosts. Existing rows keep their other settings. Add missing assignments by choosing both design and capture node; defaults are 1 inlet, unclogged, no limit/depression and automatic placement. Dimensions and flow limits use project units."),
        host);
    details->setWordWrap(true);
    layout->addWidget(details);
    auto *table = new QTableView(host);
    table->setObjectName("usageTable");
    auto *model = new UsageTableModel(ref, table);
    table->setModel(model);
    table->setItemDelegate(new FieldDelegate(table));
    table->setAlternatingRowColors(true);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(table, 1);
    openswmmvis::ui::TableClipboardOptions clip;
    clip.undoStack = ref.undoStack; // one paste, one Undo step
    openswmmvis::ui::TableClipboard::install(table, clip);
    auto *status = new QLabel(host);
    status->setObjectName("usageFeedback");
    status->setTextFormat(Qt::PlainText);
    status->setWordWrap(true);
    layout->addWidget(status);
    model->feedback = [status](const QString &text) { status->setText(text); };
    const auto update = [model, hint, status] {
        auto names = model->targetNames();
        hint->setText(
            QObject::tr(
                "Editing %1 targets: %2. Scope stays fixed when selection changes. Completed fields commit immediately; each change is one Undo step.")
                .arg(names.size())
                .arg(names.mid(0, 5).join(", ") + (names.size() > 5 ? ", …" : "")));
        status->setText(model->summary());
    };
    QObject::connect(model, &QAbstractItemModel::modelReset, host, update);
    update();
    auto *remove = new QPushButton(QObject::tr("Remove selected assignment"), host);
    remove->setObjectName("removeUsageAssignment");
    layout->addWidget(remove);
    QObject::connect(remove, &QPushButton::clicked, model,
                     [model, table] { model->removeAssignment(table->currentIndex().row()); });
    if (ref.family == UsageEditRef::Inlet) {
        auto *add = new QPushButton(QObject::tr("Add missing inlet assignments…"), host);
        add->setObjectName("addMissingInletAssignments");
        layout->addWidget(add);
        QObject::connect(add, &QPushButton::clicked, model, [model, host] {
            auto *dialog = new QDialog(host);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->setObjectName("newInletAssignments");
            dialog->setWindowTitle(QObject::tr("Add missing inlet assignments"));
            auto *form = new QFormLayout(dialog);
            auto *hint = new QLabel(
                QObject::tr(
                    "Completing both choices creates missing assignments immediately and leaves existing assignments unchanged."),
                dialog);
            hint->setWordWrap(true);
            form->addRow(hint);
            auto *design = new QComboBox(dialog), *capture = new QComboBox(dialog);
            design->setObjectName("newInletDesign");
            capture->setObjectName("newInletCapture");
            design->addItem(QObject::tr("Choose design…"), QString());
            capture->addItem(QObject::tr("Choose capture node…"), QString());
            for (const auto &choice : model->choices(model->index(0, 2)))
                design->addItem(choice.label, choice.value);
            for (const auto &choice : model->choices(model->index(0, 3)))
                capture->addItem(choice.label, choice.value);
            form->addRow(QObject::tr("Design"), design);
            form->addRow(QObject::tr("Capture node"), capture);
            auto *error = new QLabel(dialog);
            error->setTextFormat(Qt::PlainText);
            error->setWordWrap(true);
            form->addRow(error);
            const auto commit = [model, dialog, design, capture, error] {
                if (design->currentData().toString().isEmpty() ||
                    capture->currentData().toString().isEmpty())
                    return;
                if (model->assignMissingInlets(design->currentData().toString(),
                                               capture->currentData().toString()))
                    dialog->close();
                else
                    error->setText(model->lastError());
            };
            QObject::connect(design, &QComboBox::activated, dialog, commit);
            QObject::connect(capture, &QComboBox::activated, dialog, commit);
            auto *close = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
            form->addRow(close);
            QObject::connect(close, &QDialogButtonBox::rejected, dialog, &QWidget::close);
            dialog->resize(550, 230);
            dialog->show();
        });
    }
    auto *close = new QDialogButtonBox(QDialogButtonBox::Close, host);
    layout->addWidget(close);
    QObject::connect(close, &QDialogButtonBox::rejected, host, &QWidget::close);
    host->resize(1400, 480);
}
