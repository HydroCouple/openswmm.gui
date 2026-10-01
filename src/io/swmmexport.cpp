/*!
 * \file   swmmexport.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "io/swmmexport.h"

#include "layers/swmmmodellayer.h"
#include "layers/swmmresultslayer.h"
#include "ui/panels/swmmattributetablemodel.h"
#include "ui/properties/dataobjectref.h"

#include <QCoreApplication>
#include <QHash>
#include <QSet>

#include <cmath>

using openswmmvis::ColumnSpec;
using openswmmvis::EditorKind;
using openswmmvis::feature::FieldChoice;
using openswmmvis::feature::FieldType;

namespace openswmmvis::io {

namespace {

QString tr_(const char *s) { return QCoreApplication::translate("SwmmExport", s); }

/*!
 * Table column (by its engine setter tag) → Import Feature Layer attribute
 * key (importtargetregistry.cpp). Keep in step with that registry: the R9
 * round-trip test (test_swmmexport) fails when an exported layer stops
 * auto-matching on re-import.
 */
const QHash<QString, QString> &keyForSetter()
{
    static const QHash<QString, QString> map = {
        {QStringLiteral("node_tag"),                     QStringLiteral("tag")},
        {QStringLiteral("link_tag"),                     QStringLiteral("tag")},
        {QStringLiteral("subcatch_tag"),                 QStringLiteral("tag")},
        {QStringLiteral("node_invert_elev"),             QStringLiteral("invertElev")},
        {QStringLiteral("node_max_depth"),               QStringLiteral("maxDepth")},
        {QStringLiteral("node_initial_depth"),           QStringLiteral("initialDepth")},
        {QStringLiteral("node_surcharge_depth"),         QStringLiteral("surchargeDepth")},
        {QStringLiteral("node_ponded_area"),             QStringLiteral("pondedArea")},
        {QStringLiteral("node_outfall_type"),            QStringLiteral("outfallType")},
        {QStringLiteral("node_outfall_stage"),           QStringLiteral("outfallStage")},
        {QStringLiteral("node_outfall_flap_gate"),       QStringLiteral("flapGate")},
        {QStringLiteral("node_storage_seep_rate"),       QStringLiteral("seepRate")},
        {QStringLiteral("node_divider_type"),            QStringLiteral("dividerType")},
        {QStringLiteral("link_length"),                  QStringLiteral("length")},
        {QStringLiteral("link_roughness"),               QStringLiteral("roughness")},
        {QStringLiteral("link_offset_up"),               QStringLiteral("offsetUp")},
        {QStringLiteral("link_offset_dn"),               QStringLiteral("offsetDn")},
        {QStringLiteral("link_initial_flow"),            QStringLiteral("initialFlow")},
        {QStringLiteral("link_max_flow"),                QStringLiteral("maxFlow")},
        {QStringLiteral("link_loss_inlet"),              QStringLiteral("lossInlet")},
        {QStringLiteral("link_loss_outlet"),             QStringLiteral("lossOutlet")},
        {QStringLiteral("link_loss_avg"),                QStringLiteral("lossAvg")},
        {QStringLiteral("link_seep_rate"),               QStringLiteral("seepRate")},
        {QStringLiteral("link_barrels"),                 QStringLiteral("barrels")},
        {QStringLiteral("link_flap_gate"),               QStringLiteral("flapGate")},
        {QStringLiteral("link_xsect_geom1"),             QStringLiteral("geom1")},
        {QStringLiteral("link_xsect_geom2"),             QStringLiteral("geom2")},
        {QStringLiteral("link_xsect_geom3"),             QStringLiteral("geom3")},
        {QStringLiteral("link_xsect_geom4"),             QStringLiteral("geom4")},
        {QStringLiteral("link_pump_init_state"),         QStringLiteral("initState")},
        {QStringLiteral("link_pump_startup_depth"),      QStringLiteral("startupDepth")},
        {QStringLiteral("link_pump_shutoff_depth"),      QStringLiteral("shutoffDepth")},
        {QStringLiteral("link_orifice_type"),            QStringLiteral("orificeType")},
        {QStringLiteral("link_discharge_coeff"),         QStringLiteral("dischargeCoeff")},
        {QStringLiteral("link_orifice_open_close_rate"), QStringLiteral("openCloseRate")},
        {QStringLiteral("link_weir_type"),               QStringLiteral("weirType")},
        {QStringLiteral("link_crest_height"),            QStringLiteral("crestHeight")},
        {QStringLiteral("link_end_contractions"),        QStringLiteral("endContractions")},
        {QStringLiteral("link_outlet_rating_type"),      QStringLiteral("ratingType")},
        {QStringLiteral("link_outlet_expon"),            QStringLiteral("expon")},
        {QStringLiteral("gage_rain_type"),               QStringLiteral("rainType")},
        {QStringLiteral("gage_snow_factor"),             QStringLiteral("snowFactor")},
        {QStringLiteral("gage_scale_factor"),            QStringLiteral("scaleFactor")},
        {QStringLiteral("gage_data_source"),             QStringLiteral("dataSource")},
        {QStringLiteral("gage_file_path"),               QStringLiteral("filePath")},
        {QStringLiteral("gage_station_id"),              QStringLiteral("stationId")},
        {QStringLiteral("gage_rain_units"),              QStringLiteral("rainUnits")},
    };
    return map;
}

bool isNodeCategory(int c)
{
    return c == SWMMModelLayer::CatJunctions || c == SWMMModelLayer::CatOutfalls
        || c == SWMMModelLayer::CatStorage   || c == SWMMModelLayer::CatDividers;
}

bool isLinkCategory(int c)
{
    return c == SWMMModelLayer::CatConduits || c == SWMMModelLayer::CatPumps
        || c == SWMMModelLayer::CatOrifices || c == SWMMModelLayer::CatWeirs
        || c == SWMMModelLayer::CatOutlets;
}

/*! Post-run statistics columns (SWMMAttributeTableModel's dynamics blocks):
 *  their getter tags are the only ones carrying "_stat_". */
bool isStatisticsColumn(const ColumnSpec &spec)
{
    return spec.setter.contains(QLatin1String("_stat_"));
}

/*! Compound cells that are summaries of a sub-object (inflows, land uses,
 *  cross-section editor, …) are not values. Picker cells (DataObjectRef)
 *  are: they name a time series, curve, gage, outlet or aquifer. */
bool isValueColumn(const ColumnSpec &spec, const QVariant &sample)
{
    if (spec.editor != EditorKind::Compound) return true;
    return sample.userType() == qMetaTypeId<DataObjectRef>();
}

/*! Split "Label (unit)" as the table's header prints it. */
void splitHeader(const QString &header, const QString &label, QString *outLabel, QString *unit)
{
    *outLabel = label;
    unit->clear();
    const QString prefix = label + QStringLiteral(" (");
    if (header.startsWith(prefix) && header.endsWith(QLatin1Char(')')))
        *unit = header.mid(prefix.size(), header.size() - prefix.size() - 1);
    else if (!header.isEmpty())
        *outLabel = header;   // an offset-mode label ("Upstream Elevation")
}

FieldType typeFor(const ColumnSpec &spec, const QVariant &sample)
{
    switch (spec.editor) {
    case EditorKind::Numeric: return FieldType::Real;
    case EditorKind::Integer: return FieldType::Integer;
    case EditorKind::Enum:    return FieldType::Text;   // written as its INP token
    case EditorKind::ReadOnly:
        switch (sample.userType()) {
        case QMetaType::Double:
        case QMetaType::Float:     return FieldType::Real;
        case QMetaType::Int:
        case QMetaType::LongLong:  return FieldType::Integer;
        default:                   return FieldType::Text;
        }
    default:
        return FieldType::Text;
    }
}

/*! The INP token for an Enum cell's stored integer. */
QString enumToken(const ColumnSpec &spec, const QVariant &edit)
{
    bool ok = false;
    const int v = edit.toInt(&ok);
    if (!ok) return edit.toString();
    for (const QVariant &pair : spec.enumValues) {
        const QVariantList lst = pair.toList();
        if (lst.size() == 2 && lst.at(1).toInt() == v) return lst.at(0).toString();
    }
    return QString::number(v);
}

/*! The value written for one cell. */
QVariant cellValue(const ColumnSpec &spec, FieldType type, const QVariant &edit,
                   const QVariant &display)
{
    if (edit.userType() == qMetaTypeId<DataObjectRef>()) {
        const QString n = edit.value<DataObjectRef>().currentName;
        return n.isEmpty() ? QVariant() : QVariant(n);
    }
    if (spec.editor == EditorKind::Enum) {
        // User-flag Yes/No columns hand the combo -1 / 0 / 1 and display the
        // stored text; every other Enum maps its integer to the INP token.
        if (spec.key.startsWith(QLatin1String("userflag:"))) {
            const QString d = display.toString();
            return d.isEmpty() ? QVariant() : QVariant(d);
        }
        if (!edit.isValid()) return {};
        return enumToken(spec, edit);
    }
    if (!edit.isValid() || edit.isNull()) return {};
    switch (type) {
    case FieldType::Real: {
        bool ok = false;
        const double d = edit.toDouble(&ok);
        return ok && std::isfinite(d) ? QVariant(d) : QVariant();
    }
    case FieldType::Integer: {
        bool ok = false;
        const int i = edit.toInt(&ok);
        return ok ? QVariant(i) : QVariant();
    }
    default: {
        const QString s = edit.toString();
        return s.isEmpty() ? QVariant() : QVariant(s);
    }
    }
}

}   // namespace

QList<int> swmmSpatialCategories()
{
    return {SWMMModelLayer::CatJunctions, SWMMModelLayer::CatOutfalls,
            SWMMModelLayer::CatStorage,   SWMMModelLayer::CatDividers,
            SWMMModelLayer::CatConduits,  SWMMModelLayer::CatPumps,
            SWMMModelLayer::CatOrifices,  SWMMModelLayer::CatWeirs,
            SWMMModelLayer::CatOutlets,   SWMMModelLayer::CatSubcatchments,
            SWMMModelLayer::CatRainGages};
}

QString swmmTableName(int category)
{
    switch (category) {
    case SWMMModelLayer::CatJunctions:     return QStringLiteral("junctions");
    case SWMMModelLayer::CatOutfalls:      return QStringLiteral("outfalls");
    case SWMMModelLayer::CatStorage:       return QStringLiteral("storage_units");
    case SWMMModelLayer::CatDividers:      return QStringLiteral("dividers");
    case SWMMModelLayer::CatConduits:      return QStringLiteral("conduits");
    case SWMMModelLayer::CatPumps:         return QStringLiteral("pumps");
    case SWMMModelLayer::CatOrifices:      return QStringLiteral("orifices");
    case SWMMModelLayer::CatWeirs:         return QStringLiteral("weirs");
    case SWMMModelLayer::CatOutlets:       return QStringLiteral("outlets");
    case SWMMModelLayer::CatSubcatchments: return QStringLiteral("subcatchments");
    case SWMMModelLayer::CatRainGages:     return QStringLiteral("rain_gages");
    default:                               return QStringLiteral("objects");
    }
}

QString swmmExportFieldName(int category, const ColumnSpec &spec)
{
    if (spec.key == QLatin1String("Name"))      return QStringLiteral("name");
    if (spec.key == QLatin1String("From node")) return QStringLiteral("fromNode");
    if (spec.key == QLatin1String("To node"))   return QStringLiteral("toNode");
    // Orifices and outlets have one offset and outlets call their discharge
    // coefficient "coefficient" in the import registry.
    const bool singleOffset = category == SWMMModelLayer::CatOrifices
                           || category == SWMMModelLayer::CatOutlets;
    if (singleOffset && spec.setter == QLatin1String("link_offset_up"))
        return QStringLiteral("offset");
    if (category == SWMMModelLayer::CatOutlets
        && spec.setter == QLatin1String("link_discharge_coeff"))
        return QStringLiteral("coefficient");
    const auto it = keyForSetter().constFind(spec.setter);
    if (it != keyForSetter().constEnd()) return it.value();
    const QString n = openswmmvis::feature::sanitizeFieldName(spec.label);
    return n.isEmpty() ? QStringLiteral("field") : n;
}

QVector<ExportTable> swmmObjectTables(SWMMModelLayer *model, const QList<int> &categories,
                                      SWMMResultsLayer *run, bool selectedOnly)
{
    QVector<ExportTable> out;
    if (!model) return out;

    QSet<QString> selected;
    if (selectedOnly) {
        const QStringList names = model->selectedElementNames();
        selected = QSet<QString>(names.cbegin(), names.cend());
    }

    for (const int cat : categories) {
        const auto category = static_cast<SWMMModelLayer::Category>(cat);
        SWMMAttributeTableModel tm;
        tm.setSource(model, category);
        tm.setResultsSource(run);

        ExportTable table;
        table.name = swmmTableName(cat);
        table.geometry = isNodeCategory(cat) || cat == SWMMModelLayer::CatRainGages
                             ? ExportGeometry::Point
                         : isLinkCategory(cat) ? ExportGeometry::LineString
                                               : ExportGeometry::Polygon;
        const int rows = tm.rowCount();

        // Columns: everything the table shows except sub-object summaries,
        // and the statistics only with a run.
        const QList<ColumnSpec> specs = tm.columnSpecs();
        QVector<int> columns;
        QVector<FieldType> types;
        QSet<QString> used;
        for (int c = 0; c < specs.size(); ++c) {
            const ColumnSpec &spec = specs.at(c);
            if (!run && isStatisticsColumn(spec)) continue;
            const QVariant sample = rows > 0 ? tm.data(tm.index(0, c), Qt::EditRole) : QVariant();
            if (!isValueColumn(spec, sample)) continue;

            ExportField f;
            f.name = swmmExportFieldName(cat, spec);
            for (int k = 2; used.contains(f.name.toLower()); ++k)
                f.name = swmmExportFieldName(cat, spec) + QLatin1Char('_') + QString::number(k);
            used.insert(f.name.toLower());
            splitHeader(tm.headerData(c, Qt::Horizontal, Qt::DisplayRole).toString(),
                        spec.label, &f.label, &f.unit);
            f.type = typeFor(spec, sample);
            if (spec.editor == EditorKind::Enum && !spec.key.startsWith(QLatin1String("userflag:")))
                for (const QVariant &pair : spec.enumValues) {
                    const QVariantList lst = pair.toList();
                    if (lst.size() == 2) f.choices.append({lst.at(0).toString(), QString()});
                }
            table.fields << f;
            columns << c;
            types << f.type;
        }

        for (int r = 0; r < rows; ++r) {
            const QString name = tm.objectNameAt(r);
            if (selectedOnly && !selected.contains(name)) continue;
            const int soa = model->soaIndexAt(category, r);

            ExportRow row;
            if (isNodeCategory(cat)) {
                double x = 0.0, y = 0.0;
                if (!model->cachedNodeCoord(soa, &x, &y)) {
                    table.skipped << tr_("%1 \"%2\" has no coordinates.")
                                         .arg(SWMMModelLayer::kindKey(category), name);
                    continue;
                }
                row.points << QPointF(x, y);
            } else if (cat == SWMMModelLayer::CatRainGages) {
                double x = 0.0, y = 0.0;
                // (0, 0) is how the engine stores a gage with no [SYMBOLS] row;
                // the display position is invented, so it is not exported.
                if (!model->cachedGageCoord(soa, &x, &y) || (x == 0.0 && y == 0.0)) {
                    table.skipped << tr_("Rain gage \"%1\" has no location.").arg(name);
                    continue;
                }
                row.points << QPointF(x, y);
            } else if (isLinkCategory(cat)) {
                row.points = model->cachedLinkPolyline(soa);
                if (row.points.size() < 2) {
                    table.skipped << tr_("%1 \"%2\" has an end node without coordinates.")
                                         .arg(SWMMModelLayer::kindKey(category), name);
                    continue;
                }
            } else {
                row.points = model->cachedSubcatchVertices(soa);
                if (row.points.size() < 3) {
                    // Allowed in SWMM; a point would mix geometry types (Q11).
                    table.skipped << tr_("Subcatchment \"%1\" has no polygon.").arg(name);
                    continue;
                }
            }

            for (int i = 0; i < columns.size(); ++i) {
                const QModelIndex idx = tm.index(r, columns.at(i));
                row.values << cellValue(specs.at(columns.at(i)), types.at(i),
                                        tm.data(idx, Qt::EditRole),
                                        tm.data(idx, Qt::DisplayRole));
            }
            table.rows << row;
        }
        out << table;
    }
    return out;
}

}   // namespace openswmmvis::io
