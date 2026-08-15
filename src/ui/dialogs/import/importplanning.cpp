/*!
 * \file   importplanning.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/import/importplanning.h"

#include <QCoreApplication>
#include <QLocale>
#include <QSet>

#include <cmath>
#include <limits>

namespace openswmmvis::import {

namespace {

const double kCoordEps = 1e-6;   // model-unit coordinate comparison epsilon

QString trPlan(const char *s) { return QCoreApplication::translate("ImportPlanning", s); }

bool numericallyEqual(const QPointF &a, const QPointF &b)
{
    return std::abs(a.x() - b.x()) <= kCoordEps
        && std::abs(a.y() - b.y()) <= kCoordEps;
}

bool polylinesEqual(const QVector<QPointF> &a, const QVector<QPointF> &b)
{
    if (a.size() != b.size()) return false;
    for (int i = 0; i < a.size(); ++i)
        if (!numericallyEqual(a[i], b[i])) return false;
    return true;
}

/*! Parse a numeric out of a QVariant, accepting numeric strings in C
 *  or current locale. */
bool toDoubleLenient(const QVariant &v, double *out)
{
    switch (v.typeId()) {
    case QMetaType::Double:
    case QMetaType::Float:
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Bool:
        *out = v.toDouble();
        return true;
    default:
        break;
    }
    const QString s = v.toString().trimmed();
    if (s.isEmpty()) return false;
    bool ok = false;
    double d = QLocale::c().toDouble(s, &ok);
    if (!ok) d = QLocale().toDouble(s, &ok);
    if (ok) *out = d;
    return ok;
}

/*! The raw (pre-coercion) value a binding yields for one feature:
 *  field cell when bound to a field and non-null, else the default,
 *  else null. */
QVariant rawValueFor(const AttributeBinding *b, const QVariantMap &attrs)
{
    if (!b) return {};
    if (!b->sourceField.isEmpty()) {
        const QVariant cell = attrs.value(b->sourceField);
        if (cell.isValid() && !cell.isNull()
            && !(cell.typeId() == QMetaType::QString
                 && cell.toString().trimmed().isEmpty()))
            return cell;
    }
    return b->defaultValue;
}

/*! Common-to-all-node-kinds attribute keys — the safe subset applied
 *  when an Update hits a type-mismatched node. */
bool isCommonNodeKey(const QString &key)
{
    return key == QLatin1String("invertElev") || key == QLatin1String("tag");
}

/*! Common-to-all-link-kinds attribute keys under type mismatch. */
bool isCommonLinkKey(const QString &key)
{
    return key == QLatin1String("tag");
}

struct EndpointResolution {
    bool    ok = false;
    QString nodeName;
    bool    autoCreated = false;
    QString failReason;
};

} // namespace

// ---------------------------------------------------------------------------
// coerceValue
// ---------------------------------------------------------------------------

QVariant coerceValue(const QVariant &raw, const TargetAttribute &attr,
                     bool *ok, QString *errorOut)
{
    *ok = true;
    if (!raw.isValid() || raw.isNull())
        return {};   // "not provided" — caller leaves the engine default

    // Enum-typed: integer code or case-insensitive label.
    if (!attr.enumChoices.isEmpty()) {
        double d = 0.0;
        if (toDoubleLenient(raw, &d)
            && std::abs(d - std::round(d)) < 1e-9) {
            const int code = static_cast<int>(std::llround(d));
            for (const EnumChoice &c : attr.enumChoices)
                if (c.code == code) return QVariant(code);
        }
        const QString s = raw.toString().trimmed();
        for (const EnumChoice &c : attr.enumChoices)
            if (QString::compare(s, c.label, Qt::CaseInsensitive) == 0)
                return QVariant(c.code);
        *ok = false;
        if (errorOut)
            *errorOut = trPlan("\"%1\" is not a valid value for %2")
                            .arg(raw.toString(), attr.label);
        return {};
    }

    switch (attr.type) {
    case QMetaType::Double: {
        double d = 0.0;
        if (toDoubleLenient(raw, &d)) return QVariant(d);
        *ok = false;
        if (errorOut)
            *errorOut = trPlan("\"%1\" is not a number for %2")
                            .arg(raw.toString(), attr.label);
        return {};
    }
    case QMetaType::Int: {
        double d = 0.0;
        if (toDoubleLenient(raw, &d)
            && std::abs(d - std::round(d)) < 1e-9)
            return QVariant(static_cast<int>(std::llround(d)));
        *ok = false;
        if (errorOut)
            *errorOut = trPlan("\"%1\" is not an integer for %2")
                            .arg(raw.toString(), attr.label);
        return {};
    }
    case QMetaType::QString:
    default:
        return QVariant(raw.toString().trimmed());
    }
}

// ---------------------------------------------------------------------------
// buildImportPlan
// ---------------------------------------------------------------------------

ImportPlan buildImportPlan(const ImportMapping &mapping,
                           const ModelSnapshot &snapshot,
                           const QVector<SourceFeature> &features)
{
    ImportPlan plan;
    plan.items.reserve(features.size());

    const TargetKind kind        = mapping.kind;
    const bool       linkKind    = ImportTargetRegistry::isLinkKind(kind);
    const bool       gageKind    = (kind == TargetKind::RainGage);
    const QVector<TargetAttribute> attrs =
        ImportTargetRegistry::attributesFor(kind);

    const AttributeBinding *nameBinding = mapping.binding(QStringLiteral("name"));
    const bool nameBound = nameBinding && !nameBinding->sourceField.isEmpty();

    const AttributeBinding *fromBinding =
        mapping.binding(QStringLiteral("fromNode"));
    const AttributeBinding *toBinding =
        mapping.binding(QStringLiteral("toNode"));

    QSet<QString>            seenNames;      // duplicate-in-source guard
    QHash<QString, QPointF>  pendingNodes;   // nodes this run will create
    int                      autoCounter = 1;

    // When the target itself is a node kind, features planned for
    // creation also become snappable/referable endpoints for any
    // *later* link import run — but within THIS run only link kinds
    // need pendingNodes, fed by auto-created junctions.

    auto nodeExists = [&](const QString &n) {
        return snapshot.nodes.contains(n) || pendingNodes.contains(n);
    };
    auto nodePos = [&](const QString &n) -> QPointF {
        if (snapshot.nodes.contains(n)) return snapshot.nodes.value(n);
        return pendingNodes.value(n);
    };

    auto nextAutoName = [&]() -> QString {
        QString n;
        do {
            n = mapping.autoNodePrefix + QString::number(autoCounter++);
        } while (nodeExists(n) || seenNames.contains(n));
        return n;
    };

    auto resolveEndpoint = [&](const AttributeBinding *fieldBinding,
                               const QVariantMap &featAttrs,
                               const QPointF &pt) -> EndpointResolution {
        EndpointResolution r;
        QStringList tried;

        // 1) attribute columns
        if (mapping.endpointsFromFields && fieldBinding
            && !fieldBinding->sourceField.isEmpty()) {
            const QString ref =
                featAttrs.value(fieldBinding->sourceField).toString().trimmed();
            if (!ref.isEmpty()) {
                if (nodeExists(ref)) {
                    r.ok = true;
                    r.nodeName = ref;
                    return r;
                }
                tried << trPlan("column node \"%1\" not found").arg(ref);
            } else {
                tried << trPlan("endpoint column empty");
            }
        }

        // 2) spatial snap
        if (mapping.endpointsSnap) {
            const double tol2 = mapping.snapToleranceMapUnits
                              * mapping.snapToleranceMapUnits;
            double bestD2 = std::numeric_limits<double>::max();
            QString best;
            auto consider = [&](const QString &n, const QPointF &p) {
                const double dx = p.x() - pt.x();
                const double dy = p.y() - pt.y();
                const double d2 = dx * dx + dy * dy;
                if (d2 < bestD2) { bestD2 = d2; best = n; }
            };
            for (auto it = snapshot.nodes.constBegin();
                 it != snapshot.nodes.constEnd(); ++it)
                consider(it.key(), it.value());
            for (auto it = pendingNodes.constBegin();
                 it != pendingNodes.constEnd(); ++it)
                consider(it.key(), it.value());
            if (!best.isEmpty() && bestD2 <= tol2) {
                r.ok = true;
                r.nodeName = best;
                return r;
            }
            tried << trPlan("no node within snap tolerance");
        }

        // 3) auto-create junction
        if (mapping.autoCreateJunctions) {
            r.ok = true;
            r.autoCreated = true;
            r.nodeName = nextAutoName();
            pendingNodes.insert(r.nodeName, pt);
            return r;
        }

        r.failReason = tried.isEmpty()
                           ? trPlan("no endpoint-resolution strategy enabled")
                           : tried.join(QStringLiteral("; "));
        return r;
    };

    for (const SourceFeature &f : features) {
        PlannedItem item;
        item.fid = f.fid;

        auto fail = [&](const QString &msg) {
            item.action = PlannedItem::Action::Error;
            item.messages << msg;
        };

        // ---- name -----------------------------------------------------
        if (!nameBound) {
            fail(trPlan("no source column mapped for the unique identifier"));
            plan.items.append(item);
            continue;
        }
        item.name = f.attrs.value(nameBinding->sourceField)
                        .toString().trimmed();
        if (item.name.isEmpty()) {
            fail(trPlan("empty unique identifier"));
            plan.items.append(item);
            continue;
        }
        if (seenNames.contains(item.name)) {
            fail(trPlan("duplicate identifier \"%1\" in source layer")
                     .arg(item.name));
            plan.items.append(item);
            continue;
        }

        // ---- geometry -------------------------------------------------
        if (!f.geometryOk) {
            fail(f.geometryError.isEmpty()
                     ? trPlan("unusable geometry") : f.geometryError);
            plan.items.append(item);
            continue;
        }
        if (!linkKind) {
            if (f.points.isEmpty()) {
                fail(trPlan("feature has no point geometry"));
                plan.items.append(item);
                continue;
            }
            item.x = f.points.first().x();
            item.y = f.points.first().y();
        } else {
            if (f.points.size() < 2) {
                fail(trPlan("polyline needs at least 2 vertices"));
                plan.items.append(item);
                continue;
            }
            item.interiorVertices = f.points.mid(1, f.points.size() - 2);
        }

        // ---- mapped attributes (adapter-backed keys only) -------------
        bool attrError = false;
        for (const TargetAttribute &ta : attrs) {
            if (ta.adapterProperty.isEmpty()) continue;   // ctor-consumed
            const AttributeBinding *b = mapping.binding(ta.key);
            if (!b || !b->isBound()) {
                if (ta.required) {
                    fail(trPlan("required attribute \"%1\" is not mapped")
                             .arg(ta.label));
                    attrError = true;
                    break;
                }
                continue;
            }
            const QVariant raw = rawValueFor(b, f.attrs);
            bool ok = true;
            QString err;
            const QVariant v = coerceValue(raw, ta, &ok, &err);
            if (!ok) {
                fail(err);
                attrError = true;
                break;
            }
            if (v.isValid() && !v.isNull())
                item.attributeValues.insert(ta.key, v);
        }
        if (attrError) {
            plan.items.append(item);
            continue;
        }

        // ---- conflict detection ---------------------------------------
        bool exists = false;
        bool typeMismatch = false;
        if (linkKind) {
            exists = snapshot.linkTypes.contains(item.name);
            typeMismatch = exists
                && snapshot.linkTypes.value(item.name)
                       != ImportTargetRegistry::swmmLinkType(kind);
        } else if (gageKind) {
            exists = snapshot.gages.contains(item.name);
        } else {
            exists = snapshot.nodes.contains(item.name);
            typeMismatch = exists
                && snapshot.nodeTypes.value(item.name)
                       != ImportTargetRegistry::swmmNodeType(kind);
        }

        if (exists) {
            if (mapping.conflict == ImportMapping::Conflict::Skip) {
                item.action = PlannedItem::Action::Skip;
                item.messages << trPlan("already exists — skipped");
                seenNames.insert(item.name);
                plan.items.append(item);
                continue;
            }

            // Update path -----------------------------------------------
            item.action = PlannedItem::Action::Update;

            if (typeMismatch) {
                item.messages << trPlan(
                    "type differs from existing object — only common "
                    "attributes applied (use \"Convert To\" to change type)");
                QVariantMap common;
                for (auto it = item.attributeValues.constBegin();
                     it != item.attributeValues.constEnd(); ++it) {
                    const bool keep = linkKind ? isCommonLinkKey(it.key())
                                               : isCommonNodeKey(it.key());
                    if (keep) common.insert(it.key(), it.value());
                }
                item.attributeValues = common;
            }
            if (!mapping.updateAttributes)
                item.attributeValues.clear();

            if (mapping.updateGeometry) {
                if (linkKind) {
                    const auto lg = snapshot.linkGeoms.value(item.name);
                    item.geometryDiffers =
                        !polylinesEqual(lg.interior, item.interiorVertices);
                } else if (gageKind) {
                    // No applyGageMove in the layer API today — gage
                    // position updates are deferred; surface the fact.
                    if (!numericallyEqual(snapshot.gages.value(item.name),
                                          QPointF(item.x, item.y)))
                        item.messages << trPlan(
                            "gage position differs — position updates for "
                            "rain gages are not supported yet");
                } else {
                    item.geometryDiffers = !numericallyEqual(
                        snapshot.nodes.value(item.name),
                        QPointF(item.x, item.y));
                }
                if (item.geometryDiffers)
                    item.messages << trPlan("geometry will be updated");
            }

            // Endpoints are never rewired on update; surface the fact
            // when column-resolved endpoints disagree with the model.
            if (linkKind && mapping.endpointsFromFields) {
                const auto lg = snapshot.linkGeoms.value(item.name);
                const QString wantFrom = fromBinding
                    ? f.attrs.value(fromBinding->sourceField)
                          .toString().trimmed() : QString();
                const QString wantTo = toBinding
                    ? f.attrs.value(toBinding->sourceField)
                          .toString().trimmed() : QString();
                if ((!wantFrom.isEmpty() && wantFrom != lg.from)
                    || (!wantTo.isEmpty() && wantTo != lg.to)) {
                    item.endpointsDiffer = true;
                    item.messages << trPlan(
                        "source endpoints differ from existing link — "
                        "endpoints are not rewired by import");
                }
            }

            if (item.attributeValues.isEmpty() && !item.geometryDiffers) {
                item.action = PlannedItem::Action::Skip;
                item.messages.prepend(trPlan("no changes to apply"));
            }
            seenNames.insert(item.name);
            plan.items.append(item);
            continue;
        }

        // ---- create path ----------------------------------------------
        if (linkKind) {
            const EndpointResolution fromR =
                resolveEndpoint(fromBinding, f.attrs, f.points.first());
            if (!fromR.ok) {
                fail(trPlan("upstream endpoint unresolved: %1")
                         .arg(fromR.failReason));
                plan.items.append(item);
                continue;
            }
            const EndpointResolution toR =
                resolveEndpoint(toBinding, f.attrs, f.points.last());
            if (!toR.ok) {
                // Roll back an auto-node created for the from-endpoint so
                // a failed feature leaves no side effects.
                if (fromR.autoCreated) pendingNodes.remove(fromR.nodeName);
                fail(trPlan("downstream endpoint unresolved: %1")
                         .arg(toR.failReason));
                plan.items.append(item);
                continue;
            }
            if (fromR.nodeName == toR.nodeName) {
                if (fromR.autoCreated) pendingNodes.remove(fromR.nodeName);
                if (toR.autoCreated)   pendingNodes.remove(toR.nodeName);
                fail(trPlan("both endpoints resolve to node \"%1\"")
                         .arg(fromR.nodeName));
                plan.items.append(item);
                continue;
            }
            item.fromNode = fromR.nodeName;
            item.toNode   = toR.nodeName;
            if (fromR.autoCreated) {
                item.autoNodeNames << fromR.nodeName;
                item.autoNodePos   << f.points.first();
                item.messages << trPlan("junction \"%1\" will be created "
                                        "at the upstream end")
                                     .arg(fromR.nodeName);
            }
            if (toR.autoCreated) {
                item.autoNodeNames << toR.nodeName;
                item.autoNodePos   << f.points.last();
                item.messages << trPlan("junction \"%1\" will be created "
                                        "at the downstream end")
                                     .arg(toR.nodeName);
            }
        } else if (ImportTargetRegistry::isNodeKind(kind)) {
            // A created node becomes referable by later features in the
            // same run (e.g. a subsequent link import is a separate run,
            // but consistency costs nothing).
            pendingNodes.insert(item.name, QPointF(item.x, item.y));
        }

        item.action = PlannedItem::Action::Create;
        seenNames.insert(item.name);
        plan.items.append(item);
    }

    plan.recount();
    return plan;
}

} // namespace openswmmvis::import
