#include "layers/inheritedpropertyedits.h"
#include "layers/swmmmodellayer.h"
#include "ui/models/userflagsmodel.h"
#include "ui/heatproperties.h"
#include "ui/heatconfigurationundo.h"
#include "ui/hydrothermalconfiguration.h"
#include "ui/dialogs/heatconfigdialog.h"
#include <openswmm/engine/openswmm_initial_quality.h>
#include <openswmm/engine/openswmm_pollutants.h>
#include <openswmm/engine/openswmm_reactions.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_subcatchments.h>
#include <openswmm/engine/openswmm_gages.h>
#include <openswmm/engine/openswmm_model.h>
#include <QPointer>
#include <QSignalBlocker>
#include <QUndoStack>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>
namespace {
using Target = SWMMObjectRef;
using Edit = inheritedproperties::Edit;
using OpenSWMMVis::HeatSnapshot;
struct Record {
    Target target;
    QString key, definition, binding;
    QVariant value; // Invalid means absent; explicit zero/NO/empty STRING remain valid.
    int subtype = -1, flagType = -1;
    bool file = false;
};
bool fail(QString *error, const QString &why) { if (error) *error = why; return false; }
bool canEdit(SWMM_Engine e) {
    int state = -1;
    return e && swmm_engine_get_state(e, &state) == SWMM_OK &&
           (state == SWMM_STATE_BUILDING || state == SWMM_STATE_OPENED);
}
QString token(const Target &t) {
    return t.objectType == Target::Node ? "NODE" : t.objectType == Target::Link ? "LINK" :
           t.objectType == Target::Subcatchment ? "SUBCATCHMENT" : t.objectType == Target::RainGage ? "GAGE" : "";
}
int resolve(SWMM_Engine e, Target &t, int &subtype) {
    const auto name = t.name.toUtf8();
    int index = -1; const char *canonical = nullptr;
    if (t.objectType == Target::Node) {
        index = swmm_node_index(e, name.constData());
        if (index >= 0 && swmm_node_get_type(e, index, &subtype) == SWMM_OK) canonical = swmm_node_id(e, index);
    } else if (t.objectType == Target::Link) {
        index = swmm_link_index(e, name.constData());
        if (index >= 0 && swmm_link_get_type(e, index, &subtype) == SWMM_OK) canonical = swmm_link_id(e, index);
    } else if (t.objectType == Target::Subcatchment) {
        index = swmm_subcatch_index(e, name.constData()); canonical = index >= 0 ? swmm_subcatch_id(e, index) : nullptr;
    } else if (t.objectType == Target::RainGage) {
        index = swmm_gage_index(e, name.constData()); canonical = index >= 0 ? swmm_gage_id(e, index) : nullptr;
    }
    if (!canonical) return -1;
    t.name = QString::fromUtf8(canonical); return index;
}
bool option(SWMM_Engine e, const char *key) {
    char text[32]{};
    return swmm_options_get(e, key, text, sizeof(text)) == SWMM_OK && QByteArray(text) == "YES";
}
bool definition(SWMM_Engine e, Record &r) {
    const auto id = r.key.mid(r.key.indexOf(':') + 1).toUtf8();
    if (r.key.startsWith("userflag:")) {
        int count = 0;
        if (swmm_userflag_def_count(e, &count) != SWMM_OK) return false;
        for (int i = 0; i < count; ++i) {
            char name[1024]{}, description[16384]{}; int type = -1;
            if (swmm_userflag_def_get(e, i, name, sizeof(name), &type, description, sizeof(description)) != SWMM_OK) return false;
            if (QString::fromUtf8(name).compare(QString::fromUtf8(id), Qt::CaseInsensitive) != 0) continue;
            r.key = "userflag:" + QString::fromUtf8(name);
            r.flagType = type;
            r.definition = QString::number(type) + ':' + QString::fromUtf8(description); return true;
        }
        return false;
    }
    if (r.key.startsWith("initq:")) {
        if (r.target.objectType != Target::Node && r.target.objectType != Target::Link) return false;
        if (id == "__WATER_AGE__" || id == "__TEMPERATURE__") {
            if (!option(e, id == "__WATER_AGE__" ? "WATER_AGE" : "HEAT_TRANSPORT")) return false;
            r.definition = QString::fromUtf8(id); return true;
        }
        const int p = swmm_pollutant_index(e, id.constData());
        if (p >= 0) {
            int units = -1;
            if (swmm_pollutant_get_units(e, p, &units) != SWMM_OK) return false;
            r.key = "initq:" + QString::fromUtf8(swmm_pollutant_id(e, p));
            r.definition = "pollutant:" + QString::number(units); return true;
        }
        for (int i = 0; i < swmm_reaction_species_count(e); ++i) {
            char name[1024]{}, units[1024]{}; int wall = 0; double a = 0, b = 0;
            if (swmm_reaction_species_get(e, i, name, sizeof(name), &wall, units, sizeof(units), &a, &b) != SWMM_OK) return false;
            if (id == name) {
                r.definition = QString("species:%1:%2").arg(wall).arg(QString::fromUtf8(units)); return true;
            }
        }
        return false;
    }
    const int attr = OpenSWMMVis::heatPropertyIndex(r.key);
    if (attr < 0 || (r.target.objectType != Target::Node && r.target.objectType != Target::Link) ||
        (r.target.objectType == Target::Node ? attr >= 6 : r.subtype != SWMM_LINK_CONDUIT || attr >= 14)) return false;
    r.definition = "heat:" + QString::number(attr); return true;
}
int qualityRow(SWMM_Engine e, const Record &r, int element, QVariant *value = nullptr, bool *file = nullptr) {
    const auto cons = r.key.mid(6);
    for (int row = 0; row < swmm_init_quality_count(e); ++row) {
        int link = -1, index = -1; char key[4096]{}; double number = 0;
        if (swmm_init_quality_get(e, row, &link, &index, key, sizeof(key), &number) != SWMM_OK) return -2;
        if (index != element || (link != 0) != (r.target.objectType == Target::Link)) continue;
        const auto text = QString::fromUtf8(key);
        const bool same = r.definition.startsWith("pollutant:") ? text.compare(cons, Qt::CaseInsensitive) == 0 : text == cons;
        if (!same) continue;
        if (value) *value = number;
        if (file) *file = swmm_init_quality_is_file(e, row) != 0;
        return row;
    }
    return -1;
}
bool read(SWMM_Engine e, Record &r) {
    const int index = resolve(e, r.target, r.subtype);
    if (index < 0 || !definition(e, r)) return false;
    r.value = {}; r.file = false; r.binding.clear();
    if (r.key.startsWith("initq:")) return qualityRow(e, r, index, &r.value, &r.file) != -2;
    if (r.key.startsWith("userflag:")) {
        QByteArray buffer(1024, 0); int found = 0;
        const auto type = token(r.target).toUtf8(), name = r.target.name.toUtf8(), key = r.key.mid(9).toUtf8();
        for (;;) {
            if (swmm_userflag_value_get(e, type.constData(), name.constData(), key.constData(), buffer.data(), buffer.size(), &found) != SWMM_OK) return false;
            if (int(std::strlen(buffer.constData())) < buffer.size() - 1) break;
            buffer.resize(buffer.size() * 2);
        }
        if (found) {
            const auto value = QString::fromUtf8(buffer.constData());
            if (r.flagType == 2) { bool ok = false; r.value = value.toDouble(&ok); if (!ok) return false; }
            else r.value = value;
        }
        return true;
    }
    const int attr = OpenSWMMVis::heatPropertyIndex(r.key), scope = r.target.objectType == Target::Link ? SWMM_HEAT_SCOPE_LINK : SWMM_HEAT_SCOPE_NODE;
    int count = 0;
    if (swmm_heat_element_override_count(e, &count) != SWMM_OK) return false;
    for (int row = 0; row < count; ++row) {
        int a = -1, s = -1; char name[4096]{}; double value = 0;
        if (swmm_heat_get_element_override(e, row, &a, &s, name, sizeof(name), &value) != SWMM_OK) return false;
        if (a == attr && s == scope && QString::fromUtf8(name).compare(r.target.name, Qt::CaseInsensitive) == 0) {
            r.value = value; r.binding = QString::fromUtf8(name); break;
        }
    }
    return true;
}
bool same(const Record &a, const Record &b) {
    return a.target == b.target && a.key == b.key && a.subtype == b.subtype &&
           a.definition == b.definition && a.file == b.file && a.binding == b.binding && a.value.isValid() == b.value.isValid() && a.value == b.value;
}
bool proposal(Record &r, const QVariant &input, QString *error) {
    if (r.file) return fail(error, QObject::tr("%1 is supplied by the Initial Quality file. Edit its source instead.").arg(r.target.name));
    auto text = input.toString().trimmed();
    if (r.key.startsWith("userflag:") && r.flagType == 0) {
        bool ok = false; const double v = input.toDouble(&ok);
        if (text.compare("YES", Qt::CaseInsensitive) == 0) r.value = "YES";
        else if (text.compare("NO", Qt::CaseInsensitive) == 0) r.value = "NO";
        else if (ok && (v == -1 || v == 0 || v == 1)) r.value = v < 0 ? QVariant() : QVariant(v ? "YES" : "NO");
        else return fail(error, QObject::tr("Choose unset, YES or NO for %1.").arg(r.key));
        return true;
    }
    if (text.isEmpty()) { r.value = {}; r.binding.clear(); return true; }
    if (r.key.startsWith("userflag:") && r.flagType == 3) {
        if (text.contains('\n') || text.contains('\r') || text.contains(QChar(0))) return fail(error, QObject::tr("Flag text must be a single line."));
        r.value = text; return true;
    }
    bool ok = false;
    if (r.key.startsWith("userflag:") && r.flagType == 1) {
        auto value = text.toLongLong(&ok);
        if (!ok || value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max())
            return fail(error, QObject::tr("%1 requires a 32-bit integer.").arg(r.key));
        r.value = QString::number(value); return true;
    }
    const double value = text.toDouble(&ok);
    if (!ok || !std::isfinite(value) ||
        (r.key.startsWith("initq:") && !r.definition.startsWith("__") && value < 0) ||
        (r.key.startsWith("heat.") && !OpenSWMMVis::heatPropertyValueValid(OpenSWMMVis::heatPropertyIndex(r.key), value)))
        return fail(error, QObject::tr("Invalid value for %1 on %2.").arg(r.key, r.target.name));
    r.value = value;
    if (r.key.startsWith("heat.") && r.binding.isEmpty()) r.binding = r.target.name;
    return true;
}
bool write(SWMM_Engine e, const Record &r) {
    Target target = r.target; int subtype = -1;
    const int index = resolve(e, target, subtype);
    if (index < 0 || subtype != r.subtype) return false;
    if (r.key.startsWith("initq:")) {
        if (r.value.isValid()) return swmm_init_quality_set(e, target.objectType == Target::Link, index, r.key.mid(6).toUtf8().constData(), r.value.toDouble()) == SWMM_OK;
        const int row = qualityRow(e, r, index);
        return row == -1 || (row >= 0 && swmm_init_quality_remove(e, row) == SWMM_OK);
    }
    if (r.key.startsWith("userflag:")) {
        const auto type = token(target).toUtf8(), name = target.name.toUtf8(), key = r.key.mid(9).toUtf8();
        const auto value = (r.flagType == 2 ? QString::number(r.value.toDouble(), 'g', 17) : r.value.toString()).toUtf8();
        return (r.value.isValid() ? swmm_userflag_value_set(e, type.constData(), name.constData(), key.constData(), value.constData()) :
            swmm_userflag_value_clear(e, type.constData(), name.constData(), key.constData())) == SWMM_OK;
    }
    const int attr = OpenSWMMVis::heatPropertyIndex(r.key), scope = target.objectType == Target::Link ? SWMM_HEAT_SCOPE_LINK : SWMM_HEAT_SCOPE_NODE;
    if (r.value.isValid()) return swmm_heat_set_element_override(e, attr, scope, r.binding.toUtf8().constData(), r.value.toDouble()) == SWMM_OK;
    int count = 0;
    if (swmm_heat_element_override_count(e, &count) != SWMM_OK) return false;
    for (int row = count - 1; row >= 0; --row) {
        int a = -1, s = -1; char name[4096]{};
        if (swmm_heat_get_element_override(e, row, &a, &s, name, sizeof(name), nullptr) != SWMM_OK) return false;
        if (a == attr && s == scope && QString::fromUtf8(name).compare(target.name, Qt::CaseInsensitive) == 0 && swmm_heat_remove_element_override(e, row) != SWMM_OK) return false;
    }
    return true;
}
int objectCount(SWMM_Engine e, int type) {
    return type == Target::Node ? swmm_node_count(e) : type == Target::Link ? swmm_link_count(e) :
           type == Target::Subcatchment ? swmm_subcatch_count(e) : swmm_gage_count(e);
}
class Command final : public QUndoCommand {
public:
    Command(SWMMModelLayer *layer, QVector<Record> before, QVector<Record> after)
        : layer(layer), before(std::move(before)), after(std::move(after)) {
        setText(QObject::tr("Edit selected property overrides"));
        for (const auto &r : this->before) {
            heat |= r.key.startsWith("heat.");
            counts[r.target.objectType] = objectCount(layer->engine(), r.target.objectType);
        }
        if (heat) { oldHeat.reset(swmm_heat_snapshot_create(layer->engine())); oldSignature = OpenSWMMVis::HeatConfigDialog::configurationSignature(layer->engine()); }
        QObject::connect(layer, &SWMMModelLayer::engineAboutToClose, &context, [this] { valid = false; });
        QObject::connect(layer, &SWMMModelLayer::objectRenamed, &context, [this](const QString &a, const QString &b, int kind) {
            shortcut = false;
            for (auto *records : {&this->before, &this->after})
                for (auto &r : *records)
                    if ((SWMMModelLayer::kindMaskForObjectType(r.target.objectType) & kind) && r.target.name == a) {
                        r.target.name = b;
                        if (!r.binding.isEmpty()) r.binding = b;
                    }
        });
        QObject::connect(layer, &SWMMModelLayer::geometryChanged, &context, [this] {
            shortcut = false;
            for (auto r : this->before) {
                int type = -1;
                const int count = objectCount(this->layer->engine(), r.target.objectType);
                const int index = resolve(this->layer->engine(), r.target, type);
                if ((index < 0 && count < counts.value(r.target.objectType)) || (index >= 0 && type != r.subtype)) valid = false;
            }
            for (auto it = counts.begin(); it != counts.end(); ++it) it.value() = objectCount(this->layer->engine(), it.key());
        });
        if (auto *model = layer->ensureUserFlagsModel())
            QObject::connect(model, &openswmmvis::ui::UserFlagsModel::defsChanged, &context, [this] {
                for (const auto &r : this->before) if (r.key.startsWith("userflag:")) {
                    auto now = r;
                    if (!definition(this->layer->engine(), now) || now.definition != r.definition) valid = false;
                }
            });
    }
    bool commit(bool forward, bool initial = false) {
        if (!valid || !layer || !canEdit(layer->engine()) || (heat && !oldHeat)) return false;
        const auto &expected = forward ? before : after, &desired = forward ? after : before;
        for (const auto &r : expected) { auto now = r; if (!read(layer->engine(), now) || !same(now, r)) return false; }
        int applied = 0;
        auto perform = [&]() -> int {
            bool restored = false;
            if (heat && shortcut && !initial) {
                const auto &signature = forward ? oldSignature : newSignature;
                auto *snapshot = forward ? newHeat.get() : oldHeat.get();
                if (snapshot && OpenSWMMVis::HeatConfigDialog::configurationSignature(layer->engine()) == signature) {
                    if (swmm_heat_snapshot_restore(layer->engine(), snapshot) != SWMM_OK) return SWMM_ERR_BADPARAM;
                    restored = true;
                }
            }
            for (; applied < desired.size(); ++applied) {
                const auto &r = desired[applied];
                if (!(restored && r.key.startsWith("heat.")) && !write(layer->engine(), r)) return SWMM_ERR_BADPARAM;
                auto now = r; if (!read(layer->engine(), now) || !same(now, r)) return SWMM_ERR_BADPARAM;
            }
            if (heat && !restored && OpenSWMM::Ui::ensureHeatConfigurationFile(layer->engine()) != SWMM_OK) return SWMM_ERR_BADPARAM;
            if (initial && heat) {
                newHeat.reset(swmm_heat_snapshot_create(layer->engine()));
                newSignature = OpenSWMMVis::HeatConfigDialog::configurationSignature(layer->engine());
                if (!newHeat) return SWMM_ERR_BADPARAM;
            }
            return SWMM_OK;
        };
        using Function = decltype(perform);
        const int rc = heat ? swmm_hydrothermal_edit_atomic(layer->engine(), [](SWMM_Engine, void *p) { return (*static_cast<Function *>(p))(); }, &perform) : perform();
        if (rc != SWMM_OK) {
            for (int i = std::min(applied, int(desired.size()) - 1); i >= 0; --i)
                if (!expected[i].key.startsWith("heat.")) write(layer->engine(), expected[i]);
            return false;
        }
        notify(); return true;
    }
    void notify() {
        QSet<QString> names;
        if (auto *model = layer->ensureUserFlagsModel()) {
            const QSignalBlocker guard(layer);
            for (const auto &r : before) if (r.key.startsWith("userflag:"))
                emit model->valueChanged(token(r.target), r.target.name, r.key.mid(9));
        }
        for (const auto &r : before) names.insert(r.target.name);
        for (const auto &name : names) emit layer->attributeChanged(name);
        if (heat) emit layer->optionsChanged({"HEAT_PROPERTIES"});
        layer->markEdited();
    }
    void redo() override { if (first) { first = false; return; } if (!commit(true)) setObsolete(true); }
    void undo() override { if (!commit(false)) setObsolete(true); }
private:
    QObject context;
    QPointer<SWMMModelLayer> layer;
    QVector<Record> before, after;
    QMap<int, int> counts;
    bool valid = true, heat = false, shortcut = true, first = true;
    HeatSnapshot oldHeat, newHeat;
    QString oldSignature, newSignature;
};
}
namespace inheritedproperties {
bool field(const QString &key) { return key.startsWith("initq:") || key.startsWith("userflag:") || OpenSWMMVis::heatPropertyIndex(key) >= 0; }
bool editable(SWMMModelLayer *layer, const Target &target, const QString &key, QString *reason) {
    Record record; record.target = target; record.key = key;
    if (!layer || !canEdit(layer->engine()) || !read(layer->engine(), record)) return fail(reason, QObject::tr("The field is unavailable for this object or process."));
    if (record.file) return fail(reason, QObject::tr("This value is owned by the Initial Quality source file."));
    return true;
}
bool apply(SWMMModelLayer *layer, const QVector<Edit> &edits, QUndoStack *undo, QString *error) {
    if (!layer || !canEdit(layer->engine())) return fail(error, QObject::tr("The model is closed or the simulation is initialized."));
    QVector<Record> before, after;
    for (const auto &edit : edits) {
        Record old; old.target = edit.target; old.key = edit.key;
        if (!field(old.key) || !read(layer->engine(), old)) return fail(error, QObject::tr("%1 is unavailable on %2.").arg(edit.key, edit.target.name));
        if (edit.flagType >= 0 && edit.flagType != old.flagType)
            return fail(error, QObject::tr("The flag definition changed. Reopen its editor."));
        auto next = old; if (!proposal(next, edit.value, error)) return false;
        if (same(old, next)) continue;
        for (const auto &r : before) if (r.target == old.target && r.key == old.key) return fail(error, QObject::tr("A property was prescribed twice."));
        before << old; after << next;
    }
    if (before.isEmpty()) return true;
    auto command = std::make_unique<Command>(layer, before, after);
    if (!command->commit(true, true)) return fail(error, QObject::tr("A selected object refused the edit. Previous values were restored."));
    if (undo) undo->push(command.release());
    return true;
}
}
