#include "layers/nodestructureedits.h"
#include "layers/swmmmodellayer.h"
#include "layers/swmm2dmeshlayer.h"
#include "map/mapcanvas.h"
#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_2d.h>
#include <openswmm/engine/openswmm_gw2d.h>
#include <openswmm/engine/openswmm_infrastructure.h>
#include <QPointer>
#include <QSet>
#include <QUndoStack>
#include <cmath>
#include <cstring>
#include <memory>
#include <variant>
namespace
{
using Coupling = nodecoupling2d::State;
struct Anchor {
    QString from, to;
    int type = -1, layer = 0, top = 0;
    double up = 0, down = 0, crest = 0;
    bool operator==(const Anchor &) const = default;
};
struct Lid {
    QString control;
    double saturation = 0, depth = 0;
    int bottom = 0;
    QMap<QString, Anchor> anchors;
    bool operator==(const Lid &) const = default;
};
struct Record {
    QString node;
    int type = -1;
    std::variant<Coupling, Lid> value;
    bool operator==(const Record &) const = default;
};
struct Bed {
    QString node;
    int cell = -1, locate = 0, exchange = 0, automatic = 0;
    double kc = 0, dc = 0, area = 0;
    bool operator==(const Bed &) const = default;
};
bool fail(QString *error, const QString &text)
{
    if (error)
        *error = text;
    return false;
}
bool editable(SWMM_Engine e)
{
    int state = -1;
    return e && swmm_engine_get_state(e, &state) == SWMM_OK &&
           (state == SWMM_STATE_BUILDING || state == SWMM_STATE_OPENED);
}
QString id(SWMM_Engine e, int index, bool link = false)
{
    return QString::fromUtf8(link ? swmm_link_id(e, index) : swmm_node_id(e, index));
}
bool anchorRead(SWMM_Engine e, int link, Anchor &a)
{
    SWMM_StorageLidAnchor native{};
    int from = -1, to = -1;
    if (swmm_storage_lid_anchor_get(e, link, &native) != SWMM_OK ||
        swmm_link_get_from_node(e, link, &from) != SWMM_OK ||
        swmm_link_get_to_node(e, link, &to) != SWMM_OK ||
        swmm_link_get_type(e, link, &a.type) != SWMM_OK)
        return false;
    a.from = id(e, from);
    a.to = id(e, to);
    a.layer = native.layer;
    a.top = native.top;
    a.up = native.offset_up_ft;
    a.down = native.offset_down_ft;
    a.crest = native.crest_ft;
    return true;
}
bool read(SWMM_Engine e, Record &record)
{
    int node = swmm_node_index(e, record.node.toUtf8().constData());
    if (node < 0 || swmm_node_get_type(e, node, &record.type) != SWMM_OK)
        return false;
    record.node = id(e, node);
    if (auto *c = std::get_if<Coupling>(&record.value))
        return nodecoupling2d::read(e, record.node, c);
    SWMM_StorageLidState s{};
    if (swmm_storage_lid_get(e, node, &s) != SWMM_OK)
        return false;
    Lid lid;
    if (s.control >= 0) {
        const auto *name = swmm_lid_id(e, s.control);
        if (!name)
            return false;
        lid.control = QString::fromUtf8(name);
    }
    lid.saturation = s.saturation;
    lid.bottom = s.bottom;
    lid.depth = s.max_depth_ft;
    if (s.control >= 0)
        for (int link = 0; link < swmm_link_count(e); ++link) {
            int layer = 0, top = 0;
            if (swmm_lid_node_outlet_get(e, link, &layer, &top) != SWMM_OK)
                return false;
            if (layer <= 0)
                continue;
            Anchor a;
            if (!anchorRead(e, link, a))
                return false;
            if (a.layer > 0 && (a.from == record.node || a.to == record.node))
                lid.anchors.insert(id(e, link, true), a);
        }
    record.value = lid;
    return true;
}
struct NativeCoupling {
    QList<QByteArray> keys, values;
    QList<SWMM_NodeCoupling2DParam> rows;
    explicit NativeCoupling(const Coupling &s)
    {
        for (auto it = s.params.cbegin(); it != s.params.cend(); ++it) {
            keys << it.key().toUtf8();
            values << it.value().toUtf8();
        }
        for (int i = 0; i < keys.size(); ++i)
            rows << SWMM_NodeCoupling2DParam{keys[i].constData(), values[i].constData()};
    }
};
bool canonicalize(Coupling &s)
{
    QMap<QString, QString> next;
    for (auto it = s.params.cbegin(); it != s.params.cend(); ++it) {
        const auto key = it.key().toUpper(), token = it.value().trimmed().toUpper();
        if (!nodecoupling2d::paramKeys().contains(key) || next.contains(key) || token.isEmpty())
            return false;
        if (key == "CREST" || key == "COEFFS" || key == "LAW")
            next[key] = token;
        else {
            bool ok = false;
            double number = token.toDouble(&ok);
            if (!ok || !std::isfinite(number))
                return false;
            for (int precision = 6; precision <= 17; ++precision) {
                const auto text = QString::asprintf("%.*g", precision, number);
                if (text.toDouble() == number) {
                    next[key] = text;
                    break;
                }
            }
        }
    }
    s.params = next;
    return true;
}
SWMM_StorageLidState nativeState(SWMM_Engine e, const Lid &s)
{
    return {s.control.isEmpty() ? -1 : swmm_lid_index(e, s.control.toUtf8().constData()),
            s.saturation, s.bottom, s.depth};
}
bool nativeAnchors(SWMM_Engine e, const Lid &s, QList<SWMM_StorageLidAnchor> &rows)
{
    for (auto it = s.anchors.cbegin(); it != s.anchors.cend(); ++it) {
        int link = swmm_link_index(e, it.key().toUtf8().constData());
        Anchor current;
        if (link < 0 || !anchorRead(e, link, current) || current.type != it->type ||
            current.from != it->from || current.to != it->to)
            return false;
        rows << SWMM_StorageLidAnchor{link, it->layer, it->top, it->up, it->down, it->crest};
    }
    return true;
}
bool validate(SWMM_Engine e, const Record &r)
{
    int node = swmm_node_index(e, r.node.toUtf8().constData()), type = -1;
    if (node < 0 || swmm_node_get_type(e, node, &type) != SWMM_OK || type != r.type)
        return false;
    if (const auto *c = std::get_if<Coupling>(&r.value)) {
        NativeCoupling n(*c);
        return swmm_node_validate_2d_coupling(e, node, c->authored, c->type, n.rows.constData(),
                                              n.rows.size()) == SWMM_OK;
    }
    const auto &l = std::get<Lid>(r.value);
    if (!l.control.isEmpty() && swmm_lid_index(e, l.control.toUtf8().constData()) < 0)
        return false;
    const auto state = nativeState(e, l);
    QList<SWMM_StorageLidAnchor> rows;
    return nativeAnchors(e, l, rows) &&
           swmm_storage_lid_validate(e, node, &state, rows.constData(), rows.size()) == SWMM_OK;
}
bool write(SWMM_Engine e, const Record &r)
{
    int node = swmm_node_index(e, r.node.toUtf8().constData());
    if (const auto *c = std::get_if<Coupling>(&r.value))
        return nodecoupling2d::write(e, *c);
    const auto &lid = std::get<Lid>(r.value);
    auto state = nativeState(e, lid);
    QList<SWMM_StorageLidAnchor> rows;
    return nativeAnchors(e, lid, rows) &&
           swmm_storage_lid_replace(e, node, &state, rows.constData(), rows.size()) == SWMM_OK;
}
bool beds(SWMM_Engine e, const QSet<QString> &nodes, QList<Bed> &out)
{
    int count = 0;
    if (swmm_gw2d_node_count(e, &count) != SWMM_OK)
        return false;
    int capacity = 2;
    for (const auto &name : nodes)
        capacity = qMax(capacity, int(name.toUtf8().size()) + 2);
    QByteArray buffer(capacity, 0);
    for (int i = 0; i < count; ++i) {
        Bed b;
        if (swmm_gw2d_node_get(e, i, buffer.data(), buffer.size(), &b.cell, &b.kc, &b.dc,
                               &b.area) != SWMM_OK ||
            swmm_gw2d_node_get_flags(e, i, &b.locate, &b.exchange, &b.automatic) != SWMM_OK)
            return false;
        b.node = QString::fromUtf8(buffer.constData());
        const int n = swmm_node_index(e, buffer.constData());
        if (n >= 0 && nodes.contains(id(e, n))) {
            b.node = id(e, n);
            out << b;
        }
    }
    return true;
}
QVariantList definition(SWMM_Engine e, const QString &name)
{
    QVariantList out;
    int control = swmm_lid_index(e, name.toUtf8().constData());
    const int count = swmm_lid_node_layer_count(e, control);
    if (count <= 0)
        return out;
    for (int row = 0; row < count; ++row) {
        SWMM_LidNodeLayer layer{};
        if (swmm_lid_node_layer_get(e, control, row, &layer) != SWMM_OK)
            return {};
        out << layer.kind;
        for (double value : layer.params)
            out << value;
    }
    return out;
}
class Command : public QUndoCommand
{
public:
    QPointer<SWMMModelLayer> layer;
    SWMM_Engine engine;
    QVector<Record> before, after;
    bool valid = true, first = true;
    QObject guard;
    int nodes = 0, links = 0;
    QList<QPair<QPointer<SWMM2DMeshLayer>, quint64>> meshes;
    QMap<QString, QVariantList> definitions;
    QList<Bed> contacts;
    QSet<QString> lidNodes;
    Command(SWMMModelLayer *owner, QVector<Record> from, QVector<Record> to, const QString &text)
        : layer(owner), engine(owner->engine()), before(std::move(from)), after(std::move(to))
    {
        setText(text);
        nodes = swmm_node_count(engine);
        links = swmm_link_count(engine);
        for (const auto *records : {&before, &after})
            for (const auto &r : *records)
                if (const auto *lid = std::get_if<Lid>(&r.value)) {
                    lidNodes.insert(r.node);
                    if (!lid->control.isEmpty())
                        definitions[lid->control] = definition(engine, lid->control);
                }
        if (!lidNodes.isEmpty()) {
            valid = beds(engine, lidNodes, contacts);
            if (auto *canvas = owner->editCanvas())
                for (auto *item : canvas->layers())
                    if (auto *mesh = qobject_cast<SWMM2DMeshLayer *>(item)) {
                        meshes.append({mesh, mesh->geomRevision()});
                        QObject::connect(
                            mesh, &SWMM2DMeshLayer::meshEditsChanged, &guard, [this, mesh] {
                                for (const auto &entry : meshes)
                                    if (entry.first == mesh && entry.second != mesh->geomRevision())
                                        valid = false;
                            });
                    }
        }
        QObject::connect(owner, &SWMMModelLayer::engineAboutToClose, &guard,
                         [this] { valid = false; });
        QObject::connect(owner, &SWMMModelLayer::objectRenamed, &guard,
                         [this](const QString &from, const QString &to, int kind) {
                             for (auto *records : {&before, &after})
                                 for (auto &r : *records) {
                                     if ((kind & SWMMModelLayer::kKindNode) && r.node == from)
                                         r.node = to;
                                     if (auto *c = std::get_if<Coupling>(&r.value))
                                         c->node = r.node;
                                     if (auto *l = std::get_if<Lid>(&r.value)) {
                                         if ((kind & SWMMModelLayer::kKindLink) &&
                                             l->anchors.contains(from))
                                             l->anchors.insert(to, l->anchors.take(from));
                                         if (kind & SWMMModelLayer::kKindNode)
                                             for (auto &a : l->anchors) {
                                                 if (a.from == from)
                                                     a.from = to;
                                                 if (a.to == from)
                                                     a.to = to;
                                             }
                                     }
                                 }
                             if (kind & SWMMModelLayer::kKindNode) {
                                 if (lidNodes.remove(from))
                                     lidNodes.insert(to);
                                 for (auto &b : contacts)
                                     if (b.node == from)
                                         b.node = to;
                             }
                         });
        QObject::connect(owner, &SWMMModelLayer::geometryChanged, &guard, [this] {
            if (!available())
                return;
            int n = swmm_node_count(engine), l = swmm_link_count(engine);
            if ((n < nodes || l < links) && !references())
                valid = false;
            nodes = n;
            links = l;
        });
        QObject::connect(owner, &SWMMModelLayer::dataObjectsChanged, &guard, [this] {
            if (available() && !references())
                valid = false;
        });
    }
    bool available() const { return valid && layer && layer->engine() == engine; }
    bool references() const
    {
        for (auto it = definitions.cbegin(); it != definitions.cend(); ++it)
            if (definition(engine, it.key()) != it.value() || it.value().isEmpty())
                return false;
        for (const auto *records : {&before, &after})
            for (const auto &r : *records)
                if (!validate(engine, r))
                    return false;
        return true;
    }
    bool commit(bool forward, QString *error = nullptr)
    {
        if (!available() || !editable(engine))
            return fail(error, QObject::tr("The captured model is no longer editable."));
        for (const auto &mesh : meshes)
            if (!mesh.first || mesh.first->geomRevision() != mesh.second)
                return fail(error, QObject::tr("The mesh geometry changed. Reopen this edit."));
        QList<Bed> currentBeds;
        if (!lidNodes.isEmpty() &&
            (!beds(engine, lidNodes, currentBeds) || currentBeds != contacts))
            return fail(error,
                        QObject::tr("The node-bed contacts changed. No values were overwritten."));
        if (!references())
            return fail(error,
                        QObject::tr("A node, control, anchor or dependent definition changed."));
        const auto &expected = forward ? before : after, &desired = forward ? after : before;
        for (int i = 0; i < expected.size(); ++i) {
            auto current = expected[i];
            if (!read(engine, current) || current != expected[i])
                return fail(
                    error,
                    QObject::tr("The captured records changed. No values were overwritten."));
        }
        layer->beginBulkEdit();
        int applied = 0;
        for (; applied < desired.size(); ++applied) {
            auto current = desired[applied];
            if (!write(engine, current) || !read(engine, current) || current != desired[applied])
                break;
        }
        const bool ok = applied == desired.size();
        if (!ok)
            for (int i = qMin(applied, int(expected.size()) - 1); i >= 0; --i)
                write(engine, expected[i]);
        layer->endBulkEdit();
        if (!ok)
            return fail(error, QObject::tr("The edit could not be verified and was rolled back."));
        QSet<QString> changed;
        for (const auto *records : {&expected, &desired})
            for (const auto &r : *records) {
                changed.insert(r.node);
                if (const auto *lid = std::get_if<Lid>(&r.value))
                    for (auto it = lid->anchors.cbegin(); it != lid->anchors.cend(); ++it)
                        changed.insert(it.key());
            }
        for (const auto &name : changed)
            emit layer->attributeChanged(name);
        layer->markEdited();
        emit layer->repaintRequested();
        return true;
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
};
bool submit(SWMMModelLayer *layer, QVector<Record> before, QVector<Record> after, QUndoStack *undo,
            const QString &text, QString *error)
{
    for (const auto *records : {&before, &after})
        for (const auto &record : *records)
            if (!validate(layer->engine(), record))
                return fail(
                    error,
                    QObject::tr("A complete node record or dependent reference is invalid."));
    for (int i = before.size() - 1; i >= 0; --i)
        if (before[i] == after[i]) {
            before.removeAt(i);
            after.removeAt(i);
        }
    if (before.isEmpty())
        return true;
    auto command = std::make_unique<Command>(layer, before, after, text);
    if (!command->commit(true, error))
        return false;
    if (undo)
        undo->push(command.release());
    return true;
}
bool proposeCoupling(SWMM_Engine e, Coupling &s, const QString &key, const QVariant &value)
{
    if (key == "node_2d_coupling_type") {
        bool ok = false;
        const double number = value.toDouble(&ok);
        if (!ok || !std::isfinite(number) || number < -1 || number > nodecoupling2d::Outfall ||
            std::floor(number) != number)
            return false;
        const int type = int(number);
        if (type == -1) {
            s.authored = false;
            s.type = nodecoupling2d::defaultType(e, s.node);
            s.params.clear();
        } else {
            if (!nodecoupling2d::allowedTypes(e, s.node).contains(type))
                return false;
            if (type != s.type)
                s.params.clear();
            s.type = type;
            s.authored = true;
        }
    } else if (key == "node_2d_coupleable") {
        const auto token = value.toString().trimmed().toUpper();
        if (!QStringList{"0", "1", "YES", "NO", "TRUE", "FALSE"}.contains(token))
            return false;
        bool on = token == "1" || token == "YES" || token == "TRUE";
        if (on == (s.type != nodecoupling2d::None))
            return true;
        s.params.clear();
        s.type = on ? nodecoupling2d::defaultType(e, s.node) : nodecoupling2d::None;
        s.authored = !on || s.type == nodecoupling2d::None;
        if (on && s.type == nodecoupling2d::None)
            s.type = nodecoupling2d::Manhole;
    } else {
        if (s.type == nodecoupling2d::None)
            return false;
        const auto param = key.mid(14), token = value.toString().trimmed();
        if (!nodecoupling2d::paramKeys().contains(param))
            return false;
        if (token.isEmpty() || token.compare("(inherit)", Qt::CaseInsensitive) == 0)
            s.params.remove(param);
        else {
            s.params[param] = token;
            s.authored = true;
        }
    }
    return canonicalize(s);
}
bool meshChoice(SWMM_Engine e, int node, int oldMode)
{
    int count = 0;
    if (swmm_gw2d_node_count(e, &count) != SWMM_OK)
        return false;
    int capacity = 2;
    for (int n = 0; n < swmm_node_count(e); ++n)
        capacity = qMax(capacity, int(strlen(swmm_node_id(e, n))) + 2);
    QList<QByteArray> names;
    QList<SWMM_GwNodeBed> rows;
    for (int i = 0; i < count; ++i) {
        QByteArray name(capacity, 0);
        SWMM_GwNodeBed b{};
        if (swmm_gw2d_node_get(e, i, name.data(), name.size(), &b.cell, &b.kc, &b.dc, &b.area) !=
                SWMM_OK ||
            swmm_gw2d_node_get_flags(e, i, &b.locate, &b.exchange, &b.automatic) != SWMM_OK)
            return false;
        names << name;
        rows << b;
    }
    for (int i = 0; i < rows.size(); ++i)
        rows[i].node = names[i].constData();
    SWMM_LidNodeBottomEdit edit{node, oldMode, SWMM_LID_BOTTOM_MESH_AQUIFER};
    return swmm_gw2d_node_replace(e, rows.constData(), rows.size(), rows.constData(), rows.size(),
                                  &edit, 1, 0) == SWMM_OK;
}
}
namespace nodestructure
{
bool field(const QString &setter)
{
    return setter == "node_2d_coupling_type" || setter == "node_2d_coupleable" ||
           (setter.startsWith("node_2d_param_") &&
            nodecoupling2d::paramKeys().contains(setter.mid(14))) ||
           setter == "node_lid_control" || setter == "node_lid_saturation" ||
           setter == "node_lid_bottom";
}
bool applyCoupling(SWMMModelLayer *layer, QVector<Coupling> states, QUndoStack *undo,
                   const QString &text, QString *error)
{
    if (!layer || !editable(layer->engine()))
        return fail(error, QObject::tr("The model is closed or not editable."));
    QVector<Record> before, after;
    QSet<QString> seen;
    for (auto s : states) {
        Record b{s.node, -1, Coupling{}};
        if (!read(layer->engine(), b) || seen.contains(b.node))
            return fail(error, QObject::tr("A coupling target is missing or repeated."));
        seen.insert(b.node);
        s.node = b.node;
        if (!canonicalize(s))
            return fail(error, QObject::tr("A coupling parameter is invalid."));
        auto a = b;
        a.value = s;
        before << b;
        after << a;
    }
    return submit(layer, before, after, undo, text, error);
}
bool applyFields(SWMMModelLayer *layer, const QStringList &names,
                 const QMap<QString, QVariant> &fields, QUndoStack *undo, QString *error)
{
    if (!layer || !editable(layer->engine()) || fields.isEmpty())
        return fail(error, QObject::tr("The model is closed or no fields were supplied."));
    bool lid = fields.firstKey().startsWith("node_lid_");
    for (auto it = fields.cbegin(); it != fields.cend(); ++it)
        if (!field(it.key()) || it.key().startsWith("node_lid_") != lid)
            return fail(error, QObject::tr("Edit coupling and storage LID fields separately."));
    if (fields.contains("node_lid_control") && fields.size() > 1)
        return fail(
            error,
            QObject::tr("Choose a LID control before editing its saturation or bottom ownership."));
    QVector<Record> before, after;
    QSet<QString> seen;
    auto e = layer->engine();
    for (const auto &name : names) {
        Record b{name, -1,
                 lid ? std::variant<Coupling, Lid>(Lid{})
                     : std::variant<Coupling, Lid>(Coupling{})};
        if (!read(e, b) || seen.contains(b.node))
            return fail(error, QObject::tr("A node is missing or repeated."));
        seen.insert(b.node);
        auto a = b;
        if (!lid) {
            auto &s = std::get<Coupling>(a.value);
            // Type/enable prescriptions precede dependent parameters.
            QStringList order = fields.keys();
            order.removeAll("node_2d_coupling_type");
            order.removeAll("node_2d_coupleable");
            if (fields.contains("node_2d_coupling_type"))
                order.prepend("node_2d_coupling_type");
            if (fields.contains("node_2d_coupleable"))
                order.prepend("node_2d_coupleable");
            if (fields.contains("node_2d_coupling_type") && fields.contains("node_2d_coupleable"))
                return fail(error,
                            QObject::tr("Choose either a structure or its coupleable switch."));
            for (const auto &key : order)
                if (!proposeCoupling(e, s, key, fields[key]))
                    return fail(error, QObject::tr("Invalid coupling value for %1.").arg(name));
        } else {
            auto s = std::get<Lid>(b.value);
            int control =
                s.control.isEmpty() ? -1 : swmm_lid_index(e, s.control.toUtf8().constData());
            double saturation = s.saturation;
            int bottom = s.bottom;
            for (auto it = fields.cbegin(); it != fields.cend(); ++it) {
                if (it.key() == "node_lid_control") {
                    const auto name = it.value().toString().trimmed();
                    control = name.isEmpty() ? -1 : swmm_lid_index(e, name.toUtf8().constData());
                    if (!name.isEmpty() && control < 0)
                        return fail(error, QObject::tr("The LID control no longer exists."));
                } else if (it.key() == "node_lid_saturation") {
                    bool ok = false;
                    saturation = it.value().toDouble(&ok);
                    if (s.control.isEmpty() || !ok)
                        return fail(
                            error,
                            QObject::tr("Assign a LID on every target before editing saturation."));
                } else if (it.key() == "node_lid_bottom") {
                    bool ok = false;
                    double value = it.value().toDouble(&ok);
                    if (s.control.isEmpty() || !ok || !std::isfinite(value) || value < 0 ||
                        value > 2 || std::floor(value) != value)
                        return fail(error,
                                    QObject::tr("Assign a LID and choose a valid bottom owner."));
                    bottom = int(value);
                    if (bottom == 2 && bottom != s.bottom &&
                        !meshChoice(e, swmm_node_index(e, b.node.toUtf8().constData()), s.bottom))
                        return fail(
                            error,
                            QObject::tr(
                                "%1 needs an enabled mesh aquifer and an authored enabled bed with positive contact area. Configure Bottom Connection first.")
                                .arg(name));
                } else
                    return fail(error, QObject::tr("Unknown storage LID field."));
            }
            SWMM_StorageLidState proposed{};
            QList<SWMM_StorageLidAnchor> anchors(s.anchors.size());
            int count = 0;
            const int node = swmm_node_index(e, b.node.toUtf8().constData());
            if (swmm_storage_lid_prepare(e, node, control, saturation, bottom, &proposed,
                                         anchors.data(), anchors.size(), &count) != SWMM_OK)
                return fail(
                    error,
                    QObject::tr(
                        "LID assignment rejected for %1. Check its stack, saturation and anchored outlets.")
                        .arg(name));
            s.control = control < 0 ? QString() : QString::fromUtf8(swmm_lid_id(e, control));
            s.saturation = proposed.saturation;
            s.bottom = proposed.bottom;
            s.depth = proposed.max_depth_ft;
            s.anchors.clear();
            for (int i = 0; i < count; ++i) {
                Anchor anchor;
                if (!anchorRead(e, anchors[i].link, anchor))
                    return false;
                anchor.layer = anchors[i].layer;
                anchor.top = anchors[i].top;
                anchor.up = anchors[i].offset_up_ft;
                anchor.down = anchors[i].offset_down_ft;
                anchor.crest = anchors[i].crest_ft;
                s.anchors.insert(id(e, anchors[i].link, true), anchor);
            }
            a.value = s;
        }
        before << b;
        after << a;
    }
    return submit(layer, before, after, undo, QObject::tr("Edit selected node properties"), error);
}
}
