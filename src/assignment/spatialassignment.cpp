/**
 * @file      spatialassignment.cpp
 * @brief     Phase 3b (C8 / S7 GUI half): spatial assignment of scoped mesh rows and point
 *            sources.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "assignment/spatialassignment.h"

#include "assignment/groundwaterassignment.h"
#include "assignment/groundwatertransportassignment.h"
#include "mesh/meshcellgeom.h"

#include <openswmm/engine/openswmm_2d.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_sq2d.h>

#include <gdal_priv.h>
#include <ogr_spatialref.h>
#include <ogrsf_frmts.h>

#include <QHash>
#include <QLocale>
#include <QRectF>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

namespace openswmmvis::assignment
{
namespace
{
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

bool editable(SWMM_Engine engine)
{
    int state = SWMM_STATE_CREATED;
    return engine && swmm_engine_get_state(engine, &state) == SWMM_OK &&
           (state == SWMM_STATE_BUILDING || state == SWMM_STATE_OPENED);
}

QString numberText(double value)
{
    return std::isnan(value) ? QString()
                             : QString::number(value, 'g', QLocale::FloatingPointShortest);
}

QString sectionName(RowSection section)
{
    return ui::Mesh2DSectionRowModel(section).sectionName();
}

/** Sections whose rows are keyed by a species name. */
bool named(RowSection s)
{
    return s == RowSection::Loadings || s == RowSection::SurfaceInitialQuality ||
           s == RowSection::GwInitialQuality;
}

/** True when @p row is a row of the assigned key (species / zone), whatever its scope. */
bool sameKey(RowSection s, const SectionRow &row, const QString &name, int zone)
{
    if (s == RowSection::GwInitialQuality && (row.zone != zone || row.layer != -1))
        return false;
    return !named(s) || row.name.compare(name, Qt::CaseInsensitive) == 0;
}

/** Index of the row a cell resolves to (CELL, else its TAG, else `*`), or -1. */
int effectiveRow(const QList<SectionRow> &rows, RowSection s, const QString &name, int zone,
                 int cell, const QString &tag)
{
    int global = -1, tagged = -1;
    for (int i = 0; i < rows.size(); ++i) {
        const SectionRow &row = rows[i];
        if (!sameKey(s, row, name, zone))
            continue;
        if (row.scope == SWMM_SQ2D_SCOPE_CELL && row.cell == cell)
            return i;
        if (row.scope == SWMM_SQ2D_SCOPE_TAG && !tag.isEmpty() && row.tag == tag && tagged < 0)
            tagged = i;
        if (row.scope == SWMM_SQ2D_SCOPE_GLOBAL && global < 0)
            global = i;
    }
    return tagged >= 0 ? tagged : global;
}

QString rowText(RowSection s, const SectionRow *row)
{
    if (!row)
        return QObject::tr("(none)");
    return s == RowSection::Coverages ? coverageText(row->percents)
                                      : numberText(row->values.value(0, kNaN));
}

bool validTagName(const QString &tag)
{
    static const QRegularExpression pattern(QStringLiteral("^[^\\s\\\";\\[\\]]+$"));
    return tag.size() <= 60 && pattern.match(tag).hasMatch();
}

/** Write every differing cell tag of @p to; counts successful writes. */
bool writeTags(SWMM_Engine e, const QStringList &from, const QStringList &to,
               const std::function<bool()> &allowed, int &writes)
{
    for (int c = 0; c < to.size() && c < from.size(); ++c) {
        if (from[c] == to[c])
            continue;
        if ((allowed && !allowed()) ||
            swmm_2d_set_triangle_tag(e, c, to[c].toUtf8().constData()) != SWMM_OK)
            return false;
        ++writes;
    }
    return true;
}

/** Rewrite the section through the Phase 3a model (its own read-back and restore). */
QString writeRows(SWMM_Engine e, RowSection section, const QList<SectionRow> &rows)
{
    ui::Mesh2DSectionRowModel model(section);
    const QString loadError = model.load(e);
    if (!loadError.isEmpty())
        return loadError;
    model.setStagedRows(rows);
    return model.commit(e);
}

/** Move the engine from @p from to @p to (tags, then rows), restoring @p from on failure. */
RowAssignmentResult transitionRows(SWMM_Engine e, const RowAssignmentSnapshot &from,
                                   const RowAssignmentSnapshot &to,
                                   const std::function<bool(int)> &hook)
{
    RowAssignmentResult result;
    if (!editable(e)) {
        result.error = QObject::tr("Mesh rows are editable only before a run starts.");
        return result;
    }
    RowAssignmentSnapshot current;
    QString error;
    if (!readRowAssignmentSnapshot(e, from.section, &current, &error)) {
        result.error = error;
        return result;
    }
    if (current != from) {
        result.error = QObject::tr("%1 rows or mesh tags changed since the preview. Preview again.")
                           .arg(sectionName(from.section));
        return result;
    }
    if (from == to) {
        result.success = true;
        return result;
    }
    // Prevalidate the whole row batch before the first write (tags included).
    ui::Mesh2DSectionRowModel model(to.section);
    if (from.rows != to.rows) {
        error = model.load(e);
        if (error.isEmpty()) {
            model.setStagedRows(to.rows);
            error = model.validateCommit(e);
        }
        if (!error.isEmpty()) {
            result.error = error;
            return result;
        }
    }
    int ordinal = 0, writes = 0;
    const auto allowed = [&] { return !hook || hook(++ordinal); };
    bool ok = writeTags(e, from.cellTags, to.cellTags, allowed, writes);
    if (ok && from.rows != to.rows) {
        if (!allowed()) {
            ok = false;
        } else {
            error = model.commit(e);
            ok = error.isEmpty();
            ++writes; // a refused commit may still have written and restored rows
        }
    }
    if (ok && readRowAssignmentSnapshot(e, from.section, &current, &error) && current == to) {
        result.success = true;
        result.changed = true;
        return result;
    }
    // Restore tags and rows; Mesh2DSectionRowModel already put back rows it refused.
    bool restored = readRowAssignmentSnapshot(e, from.section, &current, &error);
    int rollbackWrites = 0;
    if (restored)
        restored = writeTags(e, current.cellTags, from.cellTags, {}, rollbackWrites);
    if (restored && current.rows != from.rows) {
        restored = writeRows(e, from.section, from.rows).isEmpty();
        ++rollbackWrites;
    }
    RowAssignmentSnapshot check;
    restored = restored && readRowAssignmentSnapshot(e, from.section, &check, nullptr) &&
               check == from;
    result.changed = writes > 0 || rollbackWrites > 0 || !restored;
    result.rollbackComplete = restored;
    const QString why = error.isEmpty() ? QObject::tr("The engine refused the assignment.") : error;
    result.error = restored ? why + QObject::tr(" The previous rows and tags were restored.")
                            : why + QObject::tr(" Restoring the previous rows and tags could not "
                                                "be verified; reload the last saved model.");
    return result;
}

double fieldNumber(OGRFeature *f, int idx, bool *ok)
{
    *ok = false;
    if (!f || idx < 0 || !f->IsFieldSetAndNotNull(idx))
        return kNaN;
    const OGRFieldType t = f->GetFieldDefnRef(idx)->GetType();
    if (t == OFTInteger || t == OFTInteger64 || t == OFTReal) {
        *ok = true;
        return f->GetFieldAsDouble(idx);
    }
    const double v = QString::fromUtf8(f->GetFieldAsString(idx)).trimmed().toDouble(ok);
    return *ok ? v : kNaN;
}
} // namespace

QStringList engineCellTags(SWMM_Engine engine, QString *error)
{
    int cells = 0;
    if (!engine || swmm_2d_cell_count(engine, &cells) != SWMM_OK) {
        if (error)
            *error = QObject::tr("Cannot read the mesh cell count.");
        return {};
    }
    QStringList tags;
    tags.reserve(cells);
    for (int c = 0; c < cells; ++c) {
        char tag[4096]{};
        if (swmm_2d_get_triangle_tag(engine, c, tag, sizeof tag) != SWMM_OK) {
            if (error)
                *error = QObject::tr("Cannot read mesh cell tags.");
            return {};
        }
        tags.append(QString::fromUtf8(tag));
    }
    return tags;
}

bool readRowAssignmentSnapshot(SWMM_Engine engine, RowSection section, RowAssignmentSnapshot *out,
                               QString *error)
{
    if (error)
        error->clear();
    if (!engine || !out) {
        if (error)
            *error = QObject::tr("No model is available.");
        return false;
    }
    RowAssignmentSnapshot s;
    s.section = section;
    ui::Mesh2DSectionRowModel model(section);
    const QString loadError = model.load(engine);
    if (!loadError.isEmpty()) {
        if (error)
            *error = loadError;
        return false;
    }
    s.rows = model.loadedRows();
    s.landUses = model.landUses();
    s.names = model.names();
    QString tagError;
    s.cellTags = engineCellTags(engine, &tagError);
    if (!tagError.isEmpty()) {
        if (error)
            *error = tagError;
        return false;
    }
    s.cellCount = int(s.cellTags.size());
    *out = std::move(s);
    return true;
}

QMap<QString, QStringList> tagReferences(SWMM_Engine engine)
{
    QMap<QString, QStringList> refs;
    const auto add = [&](const QString &tag, const QString &section) {
        if (!tag.isEmpty() && !refs[tag].contains(section))
            refs[tag].append(section);
    };
    AquiferSnapshot aquifers;
    if (readAquiferSnapshot(engine, &aquifers, nullptr))
        for (const auto &row : aquifers.rows)
            if (row.scope == 1)
                add(row.tag, QStringLiteral("[2D_AQUIFER]"));
    for (RowSection s : {RowSection::Coverages, RowSection::Loadings, RowSection::CurbLength,
                         RowSection::SurfaceInitialQuality, RowSection::GwParams,
                         RowSection::GwSorption, RowSection::GwInitialQuality}) {
        ui::Mesh2DSectionRowModel model(s);
        if (!model.load(engine).isEmpty())
            continue;
        for (const SectionRow &row : model.loadedRows())
            if (row.scope == SWMM_SQ2D_SCOPE_TAG)
                add(row.tag, model.sectionName());
    }
    QVector<GroundwaterSourceRow> sources;
    if (readGroundwaterSources(engine, &sources))
        for (const auto &row : sources)
            if (row.scope == 1)
                add(row.tag, QStringLiteral("[GW_SOURCES]"));
    return refs;
}

QString coverageText(const QMap<QString, double> &percents)
{
    QStringList parts;
    for (auto it = percents.cbegin(); it != percents.cend(); ++it)
        parts << QObject::tr("%1 %2 %").arg(it.key(), numberText(it.value()));
    return parts.isEmpty() ? QObject::tr("(none)") : parts.join(QStringLiteral(", "));
}

QMap<QString, QString> parseClassLookup(const QString &text, const QStringList &landUses,
                                        QString *error)
{
    QMap<QString, QString> lookup;
    for (const QString &pair : text.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const int eq = int(pair.indexOf(QLatin1Char('=')));
        const QString key = pair.left(eq).trimmed(), use = pair.mid(eq + 1).trimmed();
        QString canonical;
        for (const QString &known : landUses)
            if (known.compare(use, Qt::CaseInsensitive) == 0)
                canonical = known;
        if (eq <= 0 || key.isEmpty() || canonical.isEmpty()) {
            if (error)
                *error = QObject::tr("Class lookup entry \"%1\" must read class=land use, with a "
                                     "land use from [LANDUSES].")
                             .arg(pair.trimmed());
            return {};
        }
        lookup.insert(key, canonical);
    }
    return lookup;
}

QMap<QString, double> sharesToPercents(const QMap<QString, double> &fractions,
                                       const QMap<QString, QString> &lookup,
                                       const QStringList &landUses, double *unmapped)
{
    QMap<QString, double> percents;
    double lost = 0;
    for (auto it = fractions.cbegin(); it != fractions.cend(); ++it) {
        QString use;
        if (!lookup.isEmpty()) {
            use = lookup.value(it.key());
        } else {
            for (const QString &known : landUses)
                if (known.compare(it.key(), Qt::CaseInsensitive) == 0)
                    use = known;
        }
        if (use.isEmpty()) {
            lost += it.value();
            continue;
        }
        percents[use] += 100.0 * it.value();
    }
    if (unmapped)
        *unmapped = lost;
    return percents;
}

RowAssignmentPreview previewRowAssignment(const RowAssignmentRequest &r,
                                          std::function<bool()> cancelled)
{
    RowAssignmentPreview p;
    p.before = r.before;
    p.after = r.before;
    const auto reject = [&](const QString &message) {
        RowAssignmentPreview refused;
        refused.before = r.before;
        refused.after = r.before;
        refused.notes = p.notes;
        refused.error = message;
        return refused;
    };
    const RowSection s = r.before.section;
    const bool coverage = s == RowSection::Coverages;
    if (cancelled && cancelled())
        return reject(QObject::tr("Cancelled."));
    if (r.cells.isEmpty() ||
        (coverage ? r.percents.size() : r.values.size()) != r.cells.size())
        return reject(QObject::tr("Select cells and provide one value per cell."));
    if (r.before.cellTags.size() != r.before.cellCount)
        return reject(QObject::tr("The mesh cell tags could not be read."));
    if (s != RowSection::Coverages && s != RowSection::Loadings && s != RowSection::CurbLength &&
        s != RowSection::SurfaceInitialQuality && s != RowSection::GwInitialQuality)
        return reject(QObject::tr("%1 is not a spatial assignment target.").arg(sectionName(s)));
    QString name;
    if (named(s)) {
        for (const QString &known : r.before.names)
            if (known.compare(r.name, Qt::CaseInsensitive) == 0)
                name = known;
        if (name.isEmpty())
            return reject(QObject::tr("Choose a species from the model."));
    }
    if (s == RowSection::GwInitialQuality && r.zone != 0 && r.zone != 1)
        return reject(QObject::tr("Choose the saturated or unsaturated zone."));
    const bool signedValues = s == RowSection::GwInitialQuality &&
                              (name == QLatin1String("__TEMPERATURE__") ||
                               name == QLatin1String("__WATER_AGE__"));

    struct Item {
        int cell;
        double value;
        QMap<QString, double> percents;
    };
    QVector<Item> items;
    QSet<int> seen;
    for (int i = 0; i < r.cells.size(); ++i) {
        if (cancelled && cancelled())
            return reject(QObject::tr("Cancelled."));
        const int cell = r.cells[i];
        if (cell < 0 || cell >= r.before.cellCount || seen.contains(cell))
            return reject(QObject::tr("A target cell is out of range or repeated."));
        seen.insert(cell);
        Item item{cell, coverage ? kNaN : r.values[i], {}};
        bool noData = coverage ? r.percents[i].isEmpty() : std::isnan(item.value);
        if (coverage) {
            double sum = 0;
            for (auto it = r.percents[i].cbegin(); it != r.percents[i].cend(); ++it) {
                QString use;
                for (const QString &known : r.before.landUses)
                    if (known.compare(it.key(), Qt::CaseInsensitive) == 0)
                        use = known;
                if (use.isEmpty())
                    return reject(QObject::tr("\"%1\" is not a land use of the model.")
                                      .arg(it.key()));
                if (!std::isfinite(it.value()) || it.value() < 0 || it.value() > 100)
                    return reject(QObject::tr("Cell %1: a coverage percent must be 0 to 100.")
                                      .arg(cell + 1));
                if (it.value() > 0) {
                    item.percents[use] += it.value();
                    sum += it.value();
                }
            }
            if (sum > 100.0 + 1e-9)
                return reject(QObject::tr("Cell %1: the coverage percents add to %2, more than "
                                          "100.")
                                  .arg(cell + 1)
                                  .arg(sum));
            noData = item.percents.isEmpty();
        }
        if (noData) {
            if (!r.skipNoData)
                return reject(QObject::tr("Cell %1 has no source value. Choose to preserve "
                                          "cells without coverage, or fix the source.")
                                  .arg(cell + 1));
            p.skippedCells.append(cell);
            continue;
        }
        if (!coverage && (!std::isfinite(item.value) || (!signedValues && item.value < 0)))
            return reject(QObject::tr("Cell %1 has a nonfinite or negative value.").arg(cell + 1));
        items.append(item);
    }
    if (items.isEmpty())
        return reject(QObject::tr("No target cells have usable source values."));

    // ---- write strategy -----------------------------------------------------------------
    const auto sameValue = [&](const Item &a, const Item &b) {
        return coverage ? a.percents == b.percents : a.value == b.value;
    };
    bool uniform = true;
    for (const Item &item : items)
        uniform = uniform && sameValue(item, items.front());
    QSet<int> scope;
    for (const Item &item : items)
        scope.insert(item.cell);
    QHash<QString, int> members;
    for (const QString &t : r.before.cellTags)
        ++members[t];
    const QString first = r.before.cellTags[items.front().cell];
    bool shared = true;
    for (const Item &item : items)
        shared = shared && r.before.cellTags[item.cell] == first;
    QString tag = r.tag.trimmed();
    if (tag.isEmpty()) {
        if (shared && !first.isEmpty() && members.value(first) == items.size()) {
            tag = first; // the scope is exactly one existing tag: write its TAG row
        } else {
            const QString stem = s == RowSection::Coverages      ? QStringLiteral("COVERAGE")
                                 : s == RowSection::Loadings     ? QStringLiteral("LOADING")
                                 : s == RowSection::CurbLength   ? QStringLiteral("CURB")
                                 : s == RowSection::GwInitialQuality ? QStringLiteral("GW_IQ")
                                                                     : QStringLiteral("SURFACE_IQ");
            for (int n = 1; tag.isEmpty() || members.contains(tag); ++n)
                tag = QStringLiteral("%1_%2").arg(stem).arg(n);
        }
    }
    int inside = 0;
    QMap<QString, int> leaving;
    for (const Item &item : items) {
        const QString &old = r.before.cellTags[item.cell];
        if (old == tag)
            ++inside;
        else if (!old.isEmpty())
            ++leaving[old];
    }
    const int outside = members.value(tag) - inside;
    QString why;
    if (!uniform)
        why = QObject::tr("the assigned values differ between cells");
    else if (!validTagName(tag))
        why = QObject::tr("\"%1\" is not a usable tag (no spaces, quotes, ';' or brackets; at "
                          "most 60 characters)")
                  .arg(tag);
    else if (outside > 0)
        why = QObject::tr("tag %1 is also carried by %2 cell(s) outside the assignment, which "
                          "the TAG row would change too")
                  .arg(tag)
                  .arg(outside);
    QStringList leavingText;
    for (auto it = leaving.cbegin(); it != leaving.cend(); ++it) {
        const QStringList used = r.tagUsage.value(it.key());
        leavingText << QObject::tr("%1 cell(s) leave tag %2%3")
                           .arg(it.value())
                           .arg(it.key(),
                                used.isEmpty() ? QObject::tr(" (no TAG rows use it)")
                                               : QObject::tr(" and stop inheriting its %1 rows")
                                                     .arg(used.join(QStringLiteral(", "))));
    }
    if (why.isEmpty() && !leaving.isEmpty() && !r.allowRetag)
        why = QObject::tr("cells would be moved out of another tag (%1)")
                  .arg(leavingText.join(QStringLiteral("; ")));
    bool useTag = false;
    if (r.strategy == RowWriteStrategy::TagRow) {
        if (!why.isEmpty())
            return reject(QObject::tr("One TAG row cannot be written: %1.").arg(why));
        useTag = true;
    } else if (r.strategy == RowWriteStrategy::Automatic) {
        useTag = why.isEmpty() && items.size() >= 2;
        p.notes << (useTag ? QObject::tr("Automatic: one TAG row (%1), because every cell gets "
                                         "the same value.")
                                 .arg(tag)
                           : QObject::tr("Automatic: one CELL row per cell, because %1.")
                                 .arg(items.size() < 2 ? QObject::tr("only one cell is assigned")
                                                       : why));
    }

    SectionRow proto;
    proto.name = name;
    proto.zone = s == RowSection::GwInitialQuality ? r.zone : 0;
    proto.layer = -1;
    const auto withValue = [&](SectionRow row, const Item &item) {
        if (coverage) {
            row.percents = item.percents;
            row.values.clear();
        } else {
            row.values = {item.value};
        }
        return row;
    };
    QList<SectionRow> rows = r.before.rows;
    QVector<Item> sorted = items;
    std::sort(sorted.begin(), sorted.end(),
              [](const Item &a, const Item &b) { return a.cell < b.cell; });
    if (useTag) {
        SectionRow row = proto;
        row.scope = SWMM_SQ2D_SCOPE_TAG;
        row.tag = tag;
        row = withValue(row, sorted.front());
        int at = -1;
        for (int i = 0; i < rows.size() && at < 0; ++i)
            if (rows[i].scope == SWMM_SQ2D_SCOPE_TAG && rows[i].tag == tag &&
                sameKey(s, rows[i], name, proto.zone))
                at = i;
        if (at >= 0)
            rows[at] = row;
        else
            rows.append(row);
        for (int i = int(rows.size()) - 1; i >= 0; --i)
            if (rows[i].scope == SWMM_SQ2D_SCOPE_CELL && scope.contains(rows[i].cell) &&
                sameKey(s, rows[i], name, proto.zone)) {
                rows.removeAt(i);
                ++p.removedCellRows;
            }
        for (const Item &item : sorted) {
            const int old = effectiveRow(r.before.rows, s, name, proto.zone, item.cell,
                                         r.before.cellTags[item.cell]);
            p.cells.append(item.cell);
            p.oldValues << rowText(s, old >= 0 ? &r.before.rows[old] : nullptr);
            p.newValues << rowText(s, &row);
            if (r.before.cellTags[item.cell] != tag) {
                p.retagged.append(item.cell);
                p.retaggedFrom << r.before.cellTags[item.cell];
                p.after.cellTags[item.cell] = tag;
            }
        }
        p.tagRow = true;
        p.tag = tag;
        if (!p.retagged.isEmpty())
            p.notes << QObject::tr("%1 cell(s) are tagged %2.").arg(p.retagged.size()).arg(tag);
        if (!leavingText.isEmpty())
            p.notes << QObject::tr("Retagged: %1.").arg(leavingText.join(QStringLiteral("; ")));
        if (p.removedCellRows)
            p.notes << QObject::tr("%1 CELL row(s) of these cells are replaced by the TAG row.")
                           .arg(p.removedCellRows);
    } else {
        QHash<int, int> cellRow;
        for (int i = 0; i < rows.size(); ++i)
            if (rows[i].scope == SWMM_SQ2D_SCOPE_CELL && sameKey(s, rows[i], name, proto.zone)) {
                if (cellRow.contains(rows[i].cell))
                    return reject(QObject::tr("Cell %1 has duplicate %2 rows; resolve them first.")
                                      .arg(rows[i].cell + 1)
                                      .arg(sectionName(s)));
                cellRow.insert(rows[i].cell, i);
            }
        for (const Item &item : sorted) {
            if (cancelled && cancelled())
                return reject(QObject::tr("Cancelled."));
            const int old = effectiveRow(r.before.rows, s, name, proto.zone, item.cell,
                                         r.before.cellTags[item.cell]);
            SectionRow row = proto;
            row.scope = SWMM_SQ2D_SCOPE_CELL;
            row.cell = item.cell;
            row = withValue(row, item);
            if (old >= 0 && (coverage ? r.before.rows[old].percents == item.percents
                                      : r.before.rows[old].values.value(0, kNaN) == item.value)) {
                p.unchangedCells.append(item.cell);
                continue;
            }
            const int at = cellRow.value(item.cell, -1);
            if (at >= 0)
                rows[at] = row;
            else
                rows.append(row);
            p.cells.append(item.cell);
            p.oldValues << rowText(s, old >= 0 ? &r.before.rows[old] : nullptr);
            p.newValues << rowText(s, &row);
        }
    }
    p.after.rows = rows;
    return p;
}

RowAssignmentResult applyRowAssignment(SWMM_Engine engine, const RowAssignmentPreview &preview,
                                       std::function<bool(int)> beforeWrite)
{
    if (!preview.error.isEmpty())
        return {false, false, true, preview.error};
    if (preview.before.section != preview.after.section ||
        preview.before.cellCount != preview.after.cellCount)
        return {false, false, true, QObject::tr("Invalid assignment preview.")};
    return transitionRows(engine, preview.before, preview.after, beforeWrite);
}

RowAssignmentResult undoRowAssignment(SWMM_Engine engine, const RowAssignmentPreview &preview)
{
    if (!preview.error.isEmpty())
        return {false, false, true, preview.error};
    return transitionRows(engine, preview.after, preview.before, {});
}

int nearestCellCentroid(const QVector<mesh::MeshVertex> &vertices,
                        const QVector<mesh::MeshTriangle> &cells, const QPointF &p)
{
    // Same arithmetic as the engine's MeshData::cell_centroid_xy + nearestCell, so a preview
    // names the cell an `XY x y` row resolves to.
    int best = -1;
    double bestD2 = 0;
    for (int c = 0; c < cells.size(); ++c) {
        const auto &t = cells[c];
        double x = 0, y = 0;
        if (t.vertexCount() == 3) {
            const QPointF &a = vertices[t.vertex(0)].xy, &b = vertices[t.vertex(1)].xy,
                          &d = vertices[t.vertex(2)].xy;
            x = (a.x() + b.x() + d.x()) / 3.0;
            y = (a.y() + b.y() + d.y()) / 3.0;
        } else {
            double px[4], py[4];
            for (int k = 0; k < 4; ++k) {
                px[k] = vertices[t.vertex(k)].xy.x();
                py[k] = vertices[t.vertex(k)].xy.y();
            }
            const double a1 = std::abs(
                0.5 * ((px[1] - px[0]) * (py[2] - py[0]) - (px[2] - px[0]) * (py[1] - py[0])));
            const double a2 = std::abs(
                0.5 * ((px[2] - px[0]) * (py[3] - py[0]) - (px[3] - px[0]) * (py[2] - py[0])));
            const double area = a1 + a2;
            if (area > 0.0) {
                x = (a1 * ((px[0] + px[1] + px[2]) / 3.0) + a2 * ((px[0] + px[2] + px[3]) / 3.0)) /
                    area;
                y = (a1 * ((py[0] + py[1] + py[2]) / 3.0) + a2 * ((py[0] + py[2] + py[3]) / 3.0)) /
                    area;
            } else {
                x = 0.25 * (px[0] + px[1] + px[2] + px[3]);
                y = 0.25 * (py[0] + py[1] + py[2] + py[3]);
            }
        }
        const double d2 = (x - p.x()) * (x - p.x()) + (y - p.y()) * (y - p.y());
        if (best < 0 || d2 < bestD2) {
            best = c;
            bestD2 = d2;
        }
    }
    return best;
}

PointSourceSample samplePointSources(const PointSourceJob &job, std::function<bool()> cancelled)
{
    PointSourceSample out;
    std::unique_ptr<GDALDataset, decltype(&GDALClose)> ds(
        static_cast<GDALDataset *>(GDALOpenEx(job.path.toUtf8().constData(),
                                              GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr,
                                              nullptr)),
        &GDALClose);
    if (!ds) {
        out.error = QObject::tr("Cannot open the point source layer.");
        return out;
    }
    OGRLayer *layer = job.layerName.isEmpty()
                          ? ds->GetLayer(0)
                          : ds->GetLayerByName(job.layerName.toUtf8().constData());
    if (!layer) {
        out.error = QObject::tr("The source layer was not found.");
        return out;
    }
    const auto type = wkbFlatten(layer->GetGeomType());
    out.pointLayer = type == wkbPoint || type == wkbMultiPoint;
    if (!out.pointLayer)
        return out;
    if (!job.filter.isEmpty() &&
        layer->SetAttributeFilter(job.filter.toUtf8().constData()) != OGRERR_NONE) {
        out.error = QObject::tr("The feature attribute filter is invalid.");
        return out;
    }
    OGRSpatialReference meshSrs, sourceSrs;
    meshSrs.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    sourceSrs.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    const QByteArray meshWkt = job.meshCrsWkt.toUtf8();
    QByteArray sourceWkt = job.assignedSourceCrsWkt.toUtf8();
    if (sourceWkt.isEmpty() && layer->GetSpatialRef()) {
        char *wkt = nullptr;
        layer->GetSpatialRef()->exportToWkt(&wkt);
        sourceWkt = QByteArray(wkt ? wkt : "");
        CPLFree(wkt);
    }
    if (meshWkt.isEmpty() || sourceWkt.isEmpty() ||
        meshSrs.importFromWkt(meshWkt.constData()) != OGRERR_NONE ||
        sourceSrs.importFromWkt(sourceWkt.constData()) != OGRERR_NONE) {
        out.error = QObject::tr("Both the mesh and the point layer need a valid CRS.");
        return out;
    }
    std::unique_ptr<OGRCoordinateTransformation, decltype(&OGRCoordinateTransformation::DestroyCT)>
        transform(sourceSrs.IsSame(&meshSrs)
                      ? nullptr
                      : OGRCreateCoordinateTransformation(&sourceSrs, &meshSrs),
                  &OGRCoordinateTransformation::DestroyCT);
    if (!sourceSrs.IsSame(&meshSrs) && !transform) {
        out.error = QObject::tr("Cannot transform the point layer into the mesh CRS.");
        return out;
    }
    OGRFeatureDefn *defn = layer->GetLayerDefn();
    const int flowIdx = defn->GetFieldIndex(job.flowField.toUtf8().constData());
    if (flowIdx < 0) {
        out.error = QObject::tr("Flow field \"%1\" is not in the point layer.").arg(job.flowField);
        return out;
    }
    const int nameIdx = job.nameField.isEmpty()
                            ? -1
                            : defn->GetFieldIndex(job.nameField.toUtf8().constData());
    if (!job.nameField.isEmpty() && nameIdx < 0) {
        out.error = QObject::tr("Name field \"%1\" is not in the point layer.").arg(job.nameField);
        return out;
    }
    QVector<int> termIdx;
    for (const QString &field : job.termFields) {
        const int idx = field.isEmpty() ? -1 : defn->GetFieldIndex(field.toUtf8().constData());
        if (!field.isEmpty() && idx < 0) {
            out.error = QObject::tr("Species field \"%1\" is not in the point layer.").arg(field);
            return out;
        }
        termIdx.append(idx);
    }
    // Cell bounding boxes, so each point tests only the cells around it.
    QVector<QRectF> boxes;
    boxes.reserve(job.cells.size());
    for (const auto &cell : job.cells) {
        double x0 = std::numeric_limits<double>::infinity(), x1 = -x0, y0 = x0, y1 = -x0;
        for (int k = 0; k < cell.vertexCount(); ++k) {
            const QPointF &v = job.vertices[cell.vertex(k)].xy;
            x0 = std::min(x0, v.x());
            x1 = std::max(x1, v.x());
            y0 = std::min(y0, v.y());
            y1 = std::max(y1, v.y());
        }
        boxes.append(QRectF(QPointF(x0, y0), QPointF(x1, y1)));
    }
    layer->ResetReading();
    OGRFeature *f = nullptr;
    while ((f = layer->GetNextFeature()) != nullptr) {
        std::unique_ptr<OGRFeature, decltype(&OGRFeature::DestroyFeature)> owned(
            f, &OGRFeature::DestroyFeature);
        if (cancelled && cancelled()) {
            out.cancelled = true;
            return out;
        }
        if (job.filterBySelection && !job.selectedIds.contains(f->GetFID()))
            continue;
        PointSourceFeature feature;
        feature.fid = f->GetFID();
        const OGRGeometry *g = f->GetGeometryRef();
        const OGRPoint *point = nullptr;
        if (g && wkbFlatten(g->getGeometryType()) == wkbPoint)
            point = g->toPoint();
        else if (g && wkbFlatten(g->getGeometryType()) == wkbMultiPoint &&
                 g->toMultiPoint()->getNumGeometries() == 1)
            point = g->toMultiPoint()->getGeometryRef(0);
        if (!point || point->IsEmpty()) {
            feature.problem = QObject::tr("no single point geometry");
            out.features.append(feature);
            continue;
        }
        double x = point->getX(), y = point->getY();
        if (transform && !transform->Transform(1, &x, &y)) {
            feature.problem = QObject::tr("cannot be transformed into the mesh CRS");
            out.features.append(feature);
            continue;
        }
        feature.xy = QPointF(x, y);
        if (nameIdx >= 0 && f->IsFieldSetAndNotNull(nameIdx))
            feature.name = QString::fromUtf8(f->GetFieldAsString(nameIdx)).trimmed();
        bool ok = false;
        feature.flow = fieldNumber(f, flowIdx, &ok) * job.scale + job.offset;
        if (!ok || !std::isfinite(feature.flow))
            feature.problem = QObject::tr("flow field is not a number");
        for (int idx : termIdx) {
            double v = kNaN;
            if (idx >= 0) {
                v = fieldNumber(f, idx, &ok);
                if (!ok || !std::isfinite(v))
                    feature.problem = QObject::tr("a species field is not a number");
            }
            feature.termValues.append(v);
        }
        int containing = 0;
        for (int c = 0; c < job.cells.size(); ++c) {
            const QRectF &b = boxes[c];
            if (x < b.left() || x > b.right() || y < b.top() || y > b.bottom())
                continue;
            if (!mesh::cellContains(job.vertices, job.cells[c], feature.xy))
                continue;
            if (feature.cell < 0)
                feature.cell = c;
            ++containing;
        }
        feature.sharedEdge = containing > 1;
        if (job.byXy)
            feature.xyCell = nearestCellCentroid(job.vertices, job.cells, feature.xy);
        const int placed = job.byXy ? feature.xyCell : feature.cell;
        if (feature.problem.isEmpty()) {
            if (feature.cell < 0)
                feature.problem = QObject::tr("outside the mesh");
            else if (!job.scope.contains(placed))
                feature.problem = QObject::tr("outside the assignment scope (cell %1)")
                                      .arg(placed + 1);
        }
        out.features.append(feature);
    }
    return out;
}
} // namespace openswmmvis::assignment
