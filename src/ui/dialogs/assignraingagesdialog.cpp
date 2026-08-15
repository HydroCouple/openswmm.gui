/*!
 * \file   assignraingagesdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
 */

#include "ui/dialogs/assignraingagesdialog.h"

#include "core/editgeometry.h"
#include "core/gageassignment.h"
#include "core/swmmdatetime.h"
#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "mesh/naturalnbinterpolator.h"
#include "selection/selectionmanager.h"
#include "timeseries/timeseriesseriescommands.h"
#include "ui/dialogs/dialoglayoutpersistence.h"

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QMap>
#include <QPushButton>
#include <QRegularExpression>
#include <QRadioButton>
#include <QSet>
#include <QTableWidget>
#include <QVBoxLayout>

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_gages.h>
#include <openswmm/engine/openswmm_subcatchments.h>
#include <openswmm/engine/openswmm_tables.h>

#include <algorithm>
#include <cmath>

namespace openswmmvis::ui {

namespace {

// Generated objects are named NNG_### (Natural-Neighbour Gage). The pattern is
// also how a later run RECOGNISES its own output, so it must stay stable.
const QString kGenPrefix = QStringLiteral("NNG_");

QString generatedGageName(int ordinal)
{
    return kGenPrefix + QStringLiteral("%1").arg(ordinal, 3, 10, QLatin1Char('0'));
}

bool isGeneratedName(const QString &name)
{
    static const QRegularExpression re(QStringLiteral("^NNG_\\d{3,}(_\\d+)?$"));
    return re.match(name).hasMatch();
}

// Same 1e-7 quantisation NaturalNeighbourInterpolator uses to snap-dedupe its
// seeds, so this dialog and the interpolator agree on which gages are distinct.
QPair<qint64, qint64> siteKey(const QPointF &p)
{
    return qMakePair(qRound64(p.x() * 1e7), qRound64(p.y() * 1e7));
}

} // namespace

// ===========================================================================
// Construction
// ===========================================================================

AssignRainGagesDialog::AssignRainGagesDialog(SWMMModelLayer   *layer,
                                             MapCanvas        *canvas,
                                             SelectionManager *selection,
                                             QWidget          *parent)
    : QDialog(parent)
    , m_layer(layer)
    , m_canvas(canvas)
    , m_selection(selection)
{
    setObjectName(QStringLiteral("AssignRainGagesDialog"));
    setWindowTitle(tr("Assign Rain Gages to Subcatchments"));
    buildUi();
    onMethodChanged();
    updateButtons();
    applyAlwaysOnTopPolicy(this);
}

void AssignRainGagesDialog::buildUi()
{
    auto *outer = new QVBoxLayout(this);

    // ── Method ──────────────────────────────────────────────────────────
    {
        auto *group = new QGroupBox(tr("Method"), this);
        auto *v = new QVBoxLayout(group);

        m_methodProximity = new QRadioButton(tr("Nearest gage (Thiessen area majority)"), group);
        m_methodProximity->setToolTip(
            tr("Assign the gage whose Thiessen polygon covers the largest share of "
               "each subcatchment's area. Creates nothing."));
        m_methodProximity->setChecked(true);
        v->addWidget(m_methodProximity);

        m_methodInterp = new QRadioButton(
            tr("Natural-neighbour interpolation (creates gages)"), group);
        m_methodInterp->setToolTip(
            tr("Area-average interpolation weights over the gage network, then "
               "create one gage and time series per distinct weight vector. "
               "SWMM allows only one gage per subcatchment, so an interpolated "
               "field has to be materialised this way."));
        v->addWidget(m_methodInterp);

        auto *form = new QFormLayout;
        m_variantCombo = new QComboBox(group);
        m_variantCombo->addItem(tr("Sibson (area stealing)"), 0);
        m_variantCombo->addItem(tr("Laplace (Voronoi facet)"), 1);
        form->addRow(tr("Weighting:"), m_variantCombo);

        m_tolSpin = new QDoubleSpinBox(group);
        m_tolSpin->setRange(0.1, 10.0);
        m_tolSpin->setSingleStep(0.5);
        m_tolSpin->setValue(1.0);
        m_tolSpin->setSuffix(tr(" %"));
        m_tolSpin->setToolTip(
            tr("Subcatchments whose interpolation weights agree within this "
               "tolerance share one generated gage. Larger values create fewer "
               "objects."));
        form->addRow(tr("Group weights within:"), m_tolSpin);
        v->addLayout(form);

        outer->addWidget(group);
    }

    // ── Scope ───────────────────────────────────────────────────────────
    {
        auto *group = new QGroupBox(tr("Apply to"), this);
        auto *v = new QVBoxLayout(group);
        m_scopeAll = new QRadioButton(tr("All subcatchments"), group);
        m_scopeAll->setChecked(true);
        v->addWidget(m_scopeAll);

        int nSelected = 0;
        if (m_selection)
            for (const SWMMObjectRef &ref : m_selection->selection())
                if (ref.objectType == SWMMObjectRef::Subcatchment)
                    ++nSelected;
        m_scopeSelected = new QRadioButton(
            tr("Selected subcatchments (%1)").arg(nSelected), group);
        m_scopeSelected->setEnabled(nSelected > 0);
        v->addWidget(m_scopeSelected);

        outer->addWidget(group);
    }

    m_gageCountLbl = new QLabel(this);
    m_gageCountLbl->setWordWrap(true);
    outer->addWidget(m_gageCountLbl);

    // ── Preview ─────────────────────────────────────────────────────────
    m_preview = new QTableWidget(this);
    m_preview->setObjectName(QStringLiteral("AssignRainGagesPreview"));
    m_preview->setColumnCount(4);
    m_preview->setHorizontalHeaderLabels(
        {tr("Subcatchment"), tr("Current gage"), tr("New gage"), tr("Detail")});
    m_preview->horizontalHeader()->setStretchLastSection(true);
    m_preview->verticalHeader()->setVisible(false);
    m_preview->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_preview->setSelectionBehavior(QAbstractItemView::SelectRows);
    outer->addWidget(m_preview, 1);

    m_statusLbl = new QLabel(tr("Choose a method, then Preview."), this);
    m_statusLbl->setWordWrap(true);
    outer->addWidget(m_statusLbl);

    auto *buttons = new QDialogButtonBox(this);
    m_previewBtn = buttons->addButton(tr("Preview"), QDialogButtonBox::ActionRole);
    m_applyBtn   = buttons->addButton(tr("Apply"),   QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Close);
    connect(m_previewBtn, &QPushButton::clicked, this, &AssignRainGagesDialog::onPreview);
    connect(m_applyBtn,   &QPushButton::clicked, this, &AssignRainGagesDialog::onApply);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    outer->addWidget(buttons);

    connect(m_methodProximity, &QRadioButton::toggled,
            this, &AssignRainGagesDialog::onMethodChanged);

    resize(720, 560);
}

void AssignRainGagesDialog::onMethodChanged()
{
    const bool interp = m_methodInterp && m_methodInterp->isChecked();
    if (m_variantCombo) m_variantCombo->setEnabled(interp);
    if (m_tolSpin)      m_tolSpin->setEnabled(interp);

    QStringList warnings, unlocated;
    const QVector<GageSite> gages = eligibleGages(&warnings, &unlocated);
    m_gageCountLbl->setText(
        tr("%n usable rain gage(s).", nullptr, int(gages.size()))
        + (warnings.isEmpty() ? QString()
                              : QStringLiteral(" ") + warnings.join(QStringLiteral(" "))));
    updateButtons();
}

void AssignRainGagesDialog::updateButtons()
{
    const bool ready = m_layer && m_layer->engine();
    if (m_previewBtn) m_previewBtn->setEnabled(ready);
    if (m_applyBtn)   m_applyBtn->setEnabled(ready);
}

// ===========================================================================
// Inputs
// ===========================================================================

QVector<AssignRainGagesDialog::GageSite>
AssignRainGagesDialog::eligibleGages(QStringList *warnings, QStringList *unlocated) const
{
    QVector<GageSite> sites;
    if (!m_layer || !m_layer->engine())
        return sites;

    QStringList dropped;
    QSet<QPair<qint64, qint64>> seen;

    const int n = m_layer->cachedGageCount();
    for (int i = 0; i < n; ++i)
    {
        double x = 0.0, y = 0.0;
        if (!m_layer->cachedGageCoord(i, &x, &y))
            continue;

        const char *id = swmm_gage_id(m_layer->engine(), i);
        const QString name = id ? QString::fromUtf8(id) : QString();
        if (name.isEmpty())
            continue;

        // (0,0) is the engine's "no [SYMBOLS] row" sentinel. The display cache
        // relocates such gages to the mean of every model vertex, which would
        // plant a phantom site in the middle of the network and hand it a
        // Thiessen cell it has not earned.
        if (x == 0.0 && y == 0.0)
        {
            if (unlocated) unlocated->append(name);
            continue;
        }

        // Coincident gages: the interpolator snap-dedupes its seeds and keeps
        // no record, so a duplicate would silently never carry weight. Drop it
        // here instead, deterministically by engine index, and say so.
        const auto key = siteKey(QPointF(x, y));
        if (seen.contains(key))
        {
            dropped << name;
            continue;
        }
        seen.insert(key);
        sites.append({i, name, QPointF(x, y)});
    }

    if (warnings)
    {
        if (unlocated && !unlocated->isEmpty())
            warnings->append(tr("%n gage(s) have no map location and were excluded.",
                                nullptr, int(unlocated->size())));
        if (!dropped.isEmpty())
            warnings->append(tr("%n gage(s) share a location with another and were "
                                "excluded: %1.", nullptr, int(dropped.size()))
                                 .arg(dropped.join(QStringLiteral(", "))));
    }
    return sites;
}

QVector<int> AssignRainGagesDialog::scopeSubcatchments() const
{
    QVector<int> out;
    if (!m_layer || !m_layer->engine())
        return out;

    const int n = m_layer->cachedSubcatchCount();
    if (m_scopeSelected && m_scopeSelected->isChecked() && m_selection)
    {
        for (const SWMMObjectRef &ref : m_selection->selection())
        {
            if (ref.objectType != SWMMObjectRef::Subcatchment)
                continue;
            const int idx = swmm_subcatch_index(m_layer->engine(),
                                                ref.name.toUtf8().constData());
            if (idx >= 0 && idx < n)
                out.append(idx);
        }
        std::sort(out.begin(), out.end());   // stable, index-ordered output
        return out;
    }

    out.reserve(n);
    for (int i = 0; i < n; ++i)
        out.append(i);
    return out;
}

bool AssignRainGagesDialog::readSourceGage(const GageSite &site,
                                           GageBlend::SourceGage *out,
                                           QString *error) const
{
    if (!out || !m_layer || !m_layer->engine())
        return false;
    SWMM_Engine eng = m_layer->engine();

    // One path for every gage, whatever its data source. The engine resolves the
    // series for us — rain type, the rain-file units factor, and the gage scale
    // factor are already applied — so nothing here needs to know whether the
    // gage reads a [TIMESERIES] table or an external rain file, nor how that
    // file stores its values.
    int count = 0;
    if (swmm_gage_get_rainfall_series_count(eng, site.index, &count) != SWMM_OK
        || count <= 0)
    {
        if (error)
        {
            int source = 0;
            swmm_gage_get_data_source(eng, site.index, &source);
            *error = (source == SWMM_GAGE_FILE)
                ? tr("Rain gage \"%1\" has no readable rainfall data. Its file "
                     "could not be read, or is in a format the engine does not "
                     "load — either way the gage contributes no rainfall to a "
                     "run, not just to this tool.")
                      .arg(site.name)
                : tr("Rain gage \"%1\" has no rainfall data.").arg(site.name);
        }
        return false;
    }

    QVector<double> times(count), values(count);
    if (swmm_gage_get_rainfall_series(eng, site.index, times.data(), values.data(),
                                      count) != SWMM_OK)
    {
        if (error)
            *error = tr("Could not read the rainfall series for rain gage \"%1\".")
                         .arg(site.name);
        return false;
    }

    out->name = site.name;
    out->points.reserve(count);
    for (int k = 0; k < count; ++k)
    {
        const QDateTime when = openswmmvis::core::swmmDateTimeToQDateTime(times[k]);
        if (!when.isValid())
            continue;
        out->points.append({when.toSecsSinceEpoch(), values[k]});
    }
    if (out->points.isEmpty())
    {
        if (error)
            *error = tr("Rain gage \"%1\" has no readable rainfall entries.")
                         .arg(site.name);
        return false;
    }

    // The values are already resolved intensity with every factor folded in;
    // only the recording interval is still needed, to give each entry the
    // boxcar width the engine applies.
    double interval = 3600.0;
    swmm_gage_get_rain_interval(eng, site.index, &interval);

    out->rainType    = GageBlend::RainType::Intensity;
    out->intervalSec = static_cast<qint64>(std::llround(interval));
    out->scaleFactor = 1.0;
    return true;
}

// ===========================================================================
// Planning
// ===========================================================================

AssignRainGagesDialog::Plan AssignRainGagesDialog::buildPlan()
{
    Plan plan;
    if (!m_layer || !m_layer->engine())
    {
        plan.error = tr("No model is open.");
        return plan;
    }

    // Rain files are read once, at open. Nothing re-reads them afterwards, so a
    // gage whose path, station, or units were edited this session would still be
    // serving the PREVIOUS file's data. Refresh before planning anything.
    swmm_gage_reload_rain_files(m_layer->engine());

    QStringList warnings, unlocated;
    const QVector<GageSite> gages = eligibleGages(&warnings, &unlocated);
    const QVector<int> scope = scopeSubcatchments();

    if (scope.isEmpty())
    {
        plan.error = tr("No subcatchments are in scope.");
        return plan;
    }
    if (gages.isEmpty())
    {
        plan.error = tr("No rain gage has a map location. Place the gages on the "
                        "map (or give them [SYMBOLS] coordinates) first.");
        return plan;
    }

    if (m_methodInterp && m_methodInterp->isChecked())
        return buildInterpolatedPlan(gages, scope, warnings, unlocated);
    return buildProximityPlan(gages, scope, warnings);
}

AssignRainGagesDialog::Plan
AssignRainGagesDialog::buildProximityPlan(const QVector<GageSite> &gages,
                                          const QVector<int> &scope,
                                          QStringList warnings)
{
    Plan plan;
    plan.warnings = std::move(warnings);

    QVector<QPointF> sites;
    sites.reserve(gages.size());
    for (const GageSite &g : gages)
        sites.append(g.pos);

    SWMM_Engine eng = m_layer->engine();

    for (int idx : scope)
    {
        ++plan.scanned;
        const QVector<QPointF> ring = m_layer->cachedSubcatchVertices(idx);
        if (ring.size() < 3)
        {
            ++plan.skipped;   // no [Polygons] row — nothing spatial to go on
            continue;
        }

        const QVector<double> shares = GageAssignment::thiessenAreaShares(ring, sites);
        double fraction = 0.0;
        const int winner = GageAssignment::areaMajorityGage(shares, &fraction);
        if (winner < 0)
        {
            ++plan.skipped;
            continue;
        }

        RowPlan row;
        row.subcatch = m_layer->objectNameAt(SWMMModelLayer::CatSubcatchments, idx);
        if (row.subcatch.isEmpty())
        {
            ++plan.skipped;
            continue;
        }

        int g = -1;
        if (swmm_subcatch_get_gage(eng, idx, &g) == SWMM_OK && g >= 0)
            if (const char *id = swmm_gage_id(eng, g))
                row.oldGage = QString::fromUtf8(id);

        row.newGage = gages[winner].name;
        row.detail  = tr("%1 % of area").arg(fraction * 100.0, 0, 'f', 1);
        row.changed = (row.newGage != row.oldGage);
        if (row.changed)
            ++plan.changed;
        plan.rows.append(row);
    }

    if (plan.skipped > 0)
        plan.warnings.append(tr("%n subcatchment(s) were skipped for having no "
                                "polygon.", nullptr, plan.skipped));
    return plan;
}

AssignRainGagesDialog::Plan
AssignRainGagesDialog::buildInterpolatedPlan(const QVector<GageSite> &gages,
                                             const QVector<int> &scope,
                                             QStringList warnings,
                                             const QStringList &unlocated)
{
    Plan plan;
    plan.warnings = std::move(warnings);
    SWMM_Engine eng = m_layer->engine();

    // An un-located gage is fatal here, not merely excluded: interpolation is
    // a statement about the whole network, and silently dropping a gage would
    // redistribute its rainfall onto its neighbours without the user knowing.
    if (!unlocated.isEmpty())
    {
        plan.error = tr("These rain gages have no map location: %1.\n\n"
                        "Interpolation needs every gage placed. Give them "
                        "coordinates, or delete them, and try again.")
                         .arg(unlocated.join(QStringLiteral(", ")));
        return plan;
    }
    if (gages.size() < 2)
    {
        plan.error = tr("Interpolation needs at least two located rain gages. "
                        "With one, use the nearest-gage method instead.");
        return plan;
    }

    // ── Read every source series up front ───────────────────────────────
    QVector<GageBlend::SourceGage> sources;
    sources.reserve(gages.size());
    for (const GageSite &g : gages)
    {
        GageBlend::SourceGage s;
        QString err;
        if (!readSourceGage(g, &s, &err))
        {
            plan.error = err;
            return plan;
        }
        sources.append(s);
    }

    // Snow-catch factors cannot be folded into a series: SCF applies only on
    // the snowfall branch and is conditioned on temperature.
    double scfMin = 0.0, scfMax = 0.0;
    for (int i = 0; i < gages.size(); ++i)
    {
        double scf = 1.0;
        swmm_gage_get_snow_factor(eng, gages[i].index, &scf);
        scfMin = (i == 0) ? scf : std::min(scfMin, scf);
        scfMax = (i == 0) ? scf : std::max(scfMax, scf);
    }
    if (scfMax - scfMin > 1e-12)
        plan.warnings.append(
            tr("Snow catch factors differ across the gages (%1 to %2); the "
               "generated gages use the average. SCF cannot be blended into a "
               "series because it applies only to snowfall.")
                .arg(scfMin, 0, 'g', 4).arg(scfMax, 0, 'g', 4));
    const double blendedScf = 0.5 * (scfMin + scfMax);

    // ── Interpolation weights, area-averaged per subcatchment ───────────
    QVector<QPointF> sites;
    sites.reserve(gages.size());
    for (const GageSite &g : gages)
        sites.append(g.pos);

    mesh::NaturalNeighbourInterpolator interp;
    interp.setVariant(m_variantCombo && m_variantCombo->currentData().toInt() == 1
                          ? mesh::NaturalNeighbourInterpolator::Variant::Laplace
                          : mesh::NaturalNeighbourInterpolator::Variant::Sibson);
    QString buildErr;
    const bool haveNN = interp.build(sites, &buildErr);
    if (!haveNN)
        plan.warnings.append(
            tr("Natural-neighbour weighting is unavailable (%1); "
               "inverse-distance weighting was used throughout.").arg(buildErr));

    const double tol = (m_tolSpin ? m_tolSpin->value() : 1.0) / 100.0;
    const int nGages = static_cast<int>(gages.size());

    // key -> cluster; QMap so cluster order follows the key, not insertion.
    QMap<QString, QStringList>      members;
    QMap<QString, QVector<double>>  clusterWeights;
    QVector<RowPlan>                rows;
    qint64 idwSamples = 0, totalSamples = 0;

    for (int idx : scope)
    {
        ++plan.scanned;
        const QVector<QPointF> ring = m_layer->cachedSubcatchVertices(idx);
        if (ring.size() < 3)
        {
            ++plan.skipped;
            continue;
        }
        const QString name = m_layer->objectNameAt(SWMMModelLayer::CatSubcatchments, idx);
        if (name.isEmpty())
        {
            ++plan.skipped;
            continue;
        }

        // Equal-area samples, so a plain mean of the per-sample weight vectors
        // already IS the area average.
        const QVector<QPointF> samples = GageAssignment::samplePolygon(ring);
        QVector<double> dense(nGages, 0.0);
        QVector<QPair<int, double>> w;
        for (const QPointF &p : samples)
        {
            ++totalSamples;
            if (!haveNN || !interp.weightsAt(p.x(), p.y(), w))
            {
                ++idwSamples;
                w = GageAssignment::idwWeights(p, sites);
            }
            for (const QPair<int, double> &t : std::as_const(w))
                if (t.first >= 0 && t.first < nGages)
                    dense[t.first] += t.second;
        }
        if (samples.isEmpty())
        {
            ++plan.skipped;
            continue;
        }
        for (double &v : dense)
            v /= static_cast<double>(samples.size());

        const GageAssignment::ClusterKey key =
            GageAssignment::quantizeWeights(dense, tol);
        if (key.isEmpty())
        {
            ++plan.skipped;
            continue;
        }

        members[key.serialized].append(name);
        if (!clusterWeights.contains(key.serialized))
            clusterWeights[key.serialized] =
                GageAssignment::dequantizeWeights(key, tol, nGages);

        RowPlan row;
        row.subcatch = name;
        int g = -1;
        if (swmm_subcatch_get_gage(eng, idx, &g) == SWMM_OK && g >= 0)
            if (const char *id = swmm_gage_id(eng, g))
                row.oldGage = QString::fromUtf8(id);

        // Detail lists the dominant contributors, so the preview is readable
        // even with a large gage network.
        QVector<QPair<double, QString>> byWeight;
        const QVector<double> &cw = clusterWeights[key.serialized];
        for (int i = 0; i < nGages; ++i)
            if (cw[i] > 0.0)
                byWeight.append({cw[i], gages[i].name});
        std::sort(byWeight.begin(), byWeight.end(),
                  [](const auto &a, const auto &b) { return a.first > b.first; });
        QStringList parts;
        for (int i = 0; i < byWeight.size() && i < 4; ++i)
            parts << QStringLiteral("%1 %2%").arg(byWeight[i].second)
                         .arg(byWeight[i].first * 100.0, 0, 'f', 0);
        if (byWeight.size() > 4)
            parts << QStringLiteral("…");
        row.detail = parts.join(QStringLiteral(", "));
        row.newGage = key.serialized;   // placeholder; resolved to a name below
        rows.append(row);
    }

    if (members.isEmpty())
    {
        plan.error = tr("No subcatchment produced usable interpolation weights.");
        return plan;
    }

    // ── Idempotency: match clusters to gages a previous run created ──────
    //
    // Matching is by ASSIGNED SUBCATCHMENT SET, not by any stored metadata:
    // TimeseriesProvider descriptions are never persisted, so a marker written
    // into one would not survive save/reload. The assignment itself always does.
    QMap<QString, QStringList> existingClaims;   // generated gage -> subcatchments
    const int gageCount = m_layer->cachedGageCount();
    for (int i = 0; i < gageCount; ++i)
    {
        const char *id = swmm_gage_id(eng, i);
        if (!id) continue;
        const QString gname = QString::fromUtf8(id);
        if (!isGeneratedName(gname)) continue;
        existingClaims.insert(gname, {});
    }
    if (!existingClaims.isEmpty())
    {
        const int subCount = m_layer->cachedSubcatchCount();
        for (int s = 0; s < subCount; ++s)
        {
            int g = -1;
            if (swmm_subcatch_get_gage(eng, s, &g) != SWMM_OK || g < 0) continue;
            const char *id = swmm_gage_id(eng, g);
            if (!id) continue;
            const QString gname = QString::fromUtf8(id);
            auto it = existingClaims.find(gname);
            if (it != existingClaims.end())
                it->append(m_layer->objectNameAt(SWMMModelLayer::CatSubcatchments, s));
        }
    }

    QSet<QString> reusedGages;
    QMap<QString, QString> keyToGageName;
    int nextOrdinal = 1;
    const auto freshName = [&]() {
        QString candidate;
        do {
            candidate = generatedGageName(nextOrdinal++);
        } while (swmm_gage_index(eng, candidate.toUtf8().constData()) >= 0
                 || reusedGages.contains(candidate));
        return candidate;
    };

    for (auto it = members.constBegin(); it != members.constEnd(); ++it)
    {
        const QString &key = it.key();
        QStringList group = it.value();
        std::sort(group.begin(), group.end());

        GeneratedGage gen;
        gen.key        = key;
        gen.members    = group;
        gen.snowFactor = blendedScf;

        // Blend once per cluster, from the key's dequantised weights, so the
        // series is a pure function of the key rather than of whichever member
        // happened to be seen first.
        const QVector<double> &w = clusterWeights[key];
        const GageBlend::BlendResult blended = GageBlend::blend(sources, w);
        if (!blended.error.isEmpty())
        {
            plan.error = blended.error;
            return plan;
        }
        if (!blended.volumeOk())
        {
            // Abort everything: nothing has been mutated yet, and a silent
            // change in total rainfall is exactly what this gate exists for.
            plan.error = tr("Volume check failed for a generated gage: the "
                            "blended total is %1 against an expected %2 "
                            "(relative error %3). Nothing was changed.")
                             .arg(blended.blendedDepth, 0, 'g', 8)
                             .arg(blended.referenceDepth, 0, 'g', 8)
                             .arg(blended.relativeError, 0, 'g', 3);
            return plan;
        }
        gen.points      = blended.points;
        gen.intervalSec = blended.intervalSec;
        gen.relError    = blended.relativeError;

        // Reuse the gage a previous run gave this exact subcatchment set.
        QString matched;
        for (auto cit = existingClaims.constBegin(); cit != existingClaims.constEnd(); ++cit)
        {
            QStringList claim = cit.value();
            std::sort(claim.begin(), claim.end());
            if (claim == group)
            {
                matched = cit.key();
                break;
            }
        }

        if (!matched.isEmpty())
        {
            gen.gageName   = matched;
            gen.seriesName = matched + QStringLiteral("_TS");
            gen.isUpdate   = true;
            reusedGages.insert(matched);
        }
        else
        {
            gen.gageName   = freshName();
            gen.seriesName = gen.gageName + QStringLiteral("_TS");
            gen.isNew      = true;
        }
        keyToGageName.insert(key, gen.gageName);
        plan.generated.append(gen);
    }

    // Generated gages this run no longer needs, and which nothing else uses.
    for (auto cit = existingClaims.constBegin(); cit != existingClaims.constEnd(); ++cit)
    {
        if (reusedGages.contains(cit.key()))
            continue;
        QSet<QString> stillClaimed(cit.value().begin(), cit.value().end());
        for (const RowPlan &r : std::as_const(rows))
            stillClaimed.remove(r.subcatch);
        if (stillClaimed.isEmpty())
            plan.staleGages.append(cit.key());
        else
            plan.warnings.append(
                tr("Generated gage %1 was kept — it is still assigned to "
                   "subcatchments outside this run.").arg(cit.key()));
    }

    // Resolve the placeholder keys to real gage names.
    for (RowPlan &r : rows)
    {
        r.newGage = keyToGageName.value(r.newGage);
        r.changed = (r.newGage != r.oldGage);
        if (r.changed)
            ++plan.changed;
    }
    plan.rows = rows;

    if (plan.skipped > 0)
        plan.warnings.append(tr("%n subcatchment(s) were skipped for having no "
                                "polygon.", nullptr, plan.skipped));
    if (idwSamples > 0 && totalSamples > 0)
        plan.warnings.append(
            tr("%1 % of sample points fell outside the gage network and used "
               "inverse-distance weighting.")
                .arg(100.0 * double(idwSamples) / double(totalSamples), 0, 'f', 1));
    return plan;
}

// ===========================================================================
// Presentation
// ===========================================================================

void AssignRainGagesDialog::showPlan(const Plan &plan, bool applied)
{
    m_preview->setRowCount(0);
    if (!plan.error.isEmpty())
    {
        m_statusLbl->setText(plan.error);
        return;
    }

    m_preview->setRowCount(static_cast<int>(plan.rows.size()));
    for (int r = 0; r < plan.rows.size(); ++r)
    {
        const RowPlan &row = plan.rows[r];
        const QStringList cells{row.subcatch, row.oldGage, row.newGage, row.detail};
        for (int c = 0; c < 4; ++c)
        {
            auto *item = new QTableWidgetItem(cells[c]);
            if (!row.changed)
                item->setForeground(palette().brush(QPalette::Disabled, QPalette::Text));
            m_preview->setItem(r, c, item);
        }
    }
    m_preview->resizeColumnsToContents();

    QStringList summary;
    summary << (applied
                    ? tr("Assigned %1 of %2 subcatchment(s).")
                          .arg(plan.changed).arg(plan.scanned)
                    : tr("%1 of %2 subcatchment(s) would change.")
                          .arg(plan.changed).arg(plan.scanned));
    if (!plan.generated.isEmpty())
    {
        int created = 0, updated = 0;
        double worst = 0.0;
        for (const GeneratedGage &g : plan.generated)
        {
            created += g.isNew ? 1 : 0;
            updated += g.isUpdate ? 1 : 0;
            worst = std::max(worst, g.relError);
        }
        summary << tr("%1 gage(s) created, %2 reused, %3 removed. "
                      "Worst volume error %4.")
                       .arg(created).arg(updated).arg(plan.staleGages.size())
                       .arg(worst, 0, 'g', 3);
    }
    summary += plan.warnings;
    m_statusLbl->setText(summary.join(QStringLiteral(" ")));
}

void AssignRainGagesDialog::onPreview()
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const Plan plan = buildPlan();
    QApplication::restoreOverrideCursor();
    showPlan(plan, false);
}

void AssignRainGagesDialog::onApply()
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    // Recompute rather than trust a stale preview — the model may have moved.
    const Plan plan = buildPlan();
    QApplication::restoreOverrideCursor();

    if (!plan.error.isEmpty() || plan.rows.isEmpty())
    {
        showPlan(plan, false);
        if (plan.error.isEmpty())
            m_statusLbl->setText(tr("Nothing to assign."));
        return;
    }

    QUndoStack *stack = m_canvas ? m_canvas->undoStack() : nullptr;
    void *layerHandle = m_layer.data();

    // With no stack (headless / detached canvas) the command still has to run;
    // same fallback mesh::pushCellParamEdits uses.
    const auto pushOrRun = [stack](QUndoCommand *cmd) {
        if (stack)
        {
            stack->push(cmd);
        }
        else
        {
            cmd->redo();
            delete cmd;
        }
    };

    const QString macroText =
        (m_methodInterp && m_methodInterp->isChecked())
            ? tr("Assign Rain Gages (Interpolated)")
            : tr("Assign Rain Gages (Nearest)");

    if (stack)
        stack->beginMacro(macroText);

    // Order is load-bearing, because undo replays it backwards:
    //   series exist before any gage points at one; generated gages are
    //   tail-appended so rollbackTailGageAdd can pop them LIFO; assignments
    //   move before stale gages are configured and removed, so no cascade
    //   nullification ever fires.
    using namespace openswmmvis::timeseries;

    for (const GeneratedGage &g : plan.generated)
    {
        QVector<TimeseriesPoint> pts;
        pts.reserve(g.points.size());
        for (const GageBlend::SeriesPoint &p : g.points)
            pts.append({QDateTime::fromSecsSinceEpoch(p.t), p.value});

        const QString note =
            tr("Generated by Assign Rain Gages — weights %1").arg(g.key);

        // Choose by whether the series actually exists RIGHT NOW, not by
        // whether the gage was reused: a reused gage whose series was deleted
        // out from under it still needs a create, and SetTimeseriesPoints
        // would silently no-op.
        const bool seriesExists =
            m_layer->engine()
            && swmm_table_index(m_layer->engine(),
                                g.seriesName.toUtf8().constData()) >= 0;
        if (seriesExists)
            pushOrRun(new SetTimeseriesPointsCommand(layerHandle, g.seriesName,
                                                     pts, note));
        else
            pushOrRun(new AddTimeseriesCommand(layerHandle, g.seriesName, pts,
                                               QString(), note));
    }

    // A generated gage is a bookkeeping object, not a physical instrument: it
    // has no real location. Park them all at the centre of the gage network so
    // they render somewhere sensible instead of at the origin.
    double cx = 0.0, cy = 0.0;
    {
        const QVector<GageSite> located = eligibleGages(nullptr, nullptr);
        for (const GageSite &s : located)
        {
            cx += s.pos.x();
            cy += s.pos.y();
        }
        if (!located.isEmpty())
        {
            cx /= located.size();
            cy /= located.size();
        }
    }
    for (const GeneratedGage &g : plan.generated)
        if (g.isNew)
            pushOrRun(new AddGageCommand(m_layer, g.gageName, cx, cy, m_canvas));

    for (const GeneratedGage &g : plan.generated)
    {
        ConfigureGageCommand::Config cfg;
        cfg.dataSource  = SWMM_GAGE_TIMESERIES;
        cfg.timeseries  = g.seriesName;
        cfg.rainType    = SWMM_RAIN_INTENSITY;   // interval-independent by design
        cfg.intervalSec = static_cast<double>(g.intervalSec);
        cfg.scaleFactor = 1.0;                   // source factors already folded in
        cfg.snowFactor  = g.snowFactor;

        bool ok = false;
        const ConfigureGageCommand::Config old =
            ConfigureGageCommand::capture(m_layer, g.gageName, &ok);
        pushOrRun(new ConfigureGageCommand(m_layer, g.gageName, cfg,
                                           ok ? old : cfg, m_canvas));
    }

    {
        QStringList names, newGages, oldGages;
        for (const RowPlan &r : plan.rows)
        {
            if (!r.changed)
                continue;   // skip rows already holding the target gage
            names    << r.subcatch;
            newGages << r.newGage;
            oldGages << r.oldGage;
        }
        if (!names.isEmpty())
            pushOrRun(new AssignSubcatchGagesCommand(
                m_layer, names, newGages, oldGages,
                tr("Assign rain gage to %n subcatchment(s)", nullptr,
                   int(names.size())),
                m_canvas));
    }

    for (const QString &stale : plan.staleGages)
    {
        // Snapshot the configuration first: DeleteObjectCommand restores only
        // a gage's name and coordinates, so undo would otherwise bring it back
        // stripped of its series, rain type, and interval.
        bool ok = false;
        const ConfigureGageCommand::Config old =
            ConfigureGageCommand::capture(m_layer, stale, &ok);
        if (ok)
            pushOrRun(new ConfigureGageCommand(m_layer, stale, old, old, m_canvas));

        pushOrRun(new DeleteObjectCommand(m_layer, stale,
                                          DeleteObjectCommand::DeleteGage, m_canvas));
        pushOrRun(new DeleteTimeseriesCommand(layerHandle,
                                              stale + QStringLiteral("_TS")));
    }

    if (stack)
        stack->endMacro();

    showPlan(plan, true);
}

} // namespace openswmmvis::ui
