/*!
 * \file   stylefileio.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "render/stylefileio.h"

#include "layers/gisvectorlayer.h"
#include "layers/openswmmvislayer.h"
#include "layers/swmmmodellayer.h"
#include "layers/swmmresultslayer.h"
#include "render/ifeaturerenderer.h"
#include "render/labelconfig.h"
#include "render/renderers/categorizedrenderer.h"
#include "render/renderers/graduatedrenderer.h"
#include "render/renderers/rulebasedrenderer.h"
#include "render/renderers/singlesymbolrenderer.h"
#include "render/symbolstyle.h"

#include <QColor>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QXmlStreamReader>

#include <memory>

namespace OpenSWMM::Render {

namespace {

constexpr const char *kSchema = "swmmvis-style/v1";

/*! Construct a renderer from a JSON object whose "id" field is the
 *  discriminator.  Mirrors `projectserializer::makeRendererFromJson`
 *  but lives here so style-file IO doesn't pull project-level deps. */
std::unique_ptr<IFeatureRenderer> makeRenderer(const QJsonObject &j)
{
    const QString id = j.value(QStringLiteral("id")).toString();
    std::unique_ptr<IFeatureRenderer> r;
    if      (id == QLatin1String("single"))      r = std::make_unique<SingleSymbolRenderer>();
    else if (id == QLatin1String("graduated"))   r = std::make_unique<GraduatedRenderer>();
    else if (id == QLatin1String("categorized")) r = std::make_unique<CategorizedRenderer>();
    else if (id == QLatin1String("rule"))        r = std::make_unique<RuleBasedRenderer>();
    if (r) r->fromJson(j);
    return r;
}

QString layerTypeTag(const OpenSWMMVisLayer *layer)
{
    if (qobject_cast<const SWMMModelLayer  *>(layer))  return QStringLiteral("SWMMModelLayer");
    if (qobject_cast<const SWMMResultsLayer *>(layer)) return QStringLiteral("SWMMResultsLayer");
    if (qobject_cast<const GISVectorLayer  *>(layer))  return QStringLiteral("GISVectorLayer");
    return QStringLiteral("Layer");
}

} // namespace

// ---------------------------------------------------------------------------
// Export
// ---------------------------------------------------------------------------

StyleFileIO::Result StyleFileIO::exportStyle(const OpenSWMMVisLayer *layer,
                                              const QString &path)
{
    Result res;
    if (!layer) {
        res.errorMessage = QObject::tr("No layer provided to export.");
        return res;
    }

    QJsonObject root;
    root[QStringLiteral("schema")]    = QString::fromLatin1(kSchema);
    root[QStringLiteral("layerType")] = layerTypeTag(layer);

    if (const auto *r = layer->renderer())
        root[QStringLiteral("renderer")] = r->toJson();

    // SWMM model layer: per-kind renderers + label config.
    if (auto *m = qobject_cast<const SWMMModelLayer *>(
            const_cast<OpenSWMMVisLayer *>(layer))) {
        QJsonObject kindObj;
        for (int i = 0; i < int(SWMMModelLayer::NumCategories); ++i) {
            const auto c = static_cast<SWMMModelLayer::Category>(i);
            if (const auto *kr = m->kindRenderer(c))
                kindObj[SWMMModelLayer::kindKey(c)] = kr->toJson();
        }
        if (!kindObj.isEmpty())
            root[QStringLiteral("kindRenderers")] = kindObj;
        const LabelConfig dl;
        if (m->labelConfig() != dl)
            root[QStringLiteral("labelConfig")] = m->labelConfig().toJson();
    }
    // SWMM results layer: per-kind only.
    if (auto *rl = qobject_cast<const SWMMResultsLayer *>(
            const_cast<OpenSWMMVisLayer *>(layer))) {
        QJsonObject kindObj;
        for (int i = 0; i < int(SWMMModelLayer::NumCategories); ++i) {
            const auto c = static_cast<SWMMModelLayer::Category>(i);
            if (const auto *kr = rl->kindRenderer(c))
                kindObj[SWMMModelLayer::kindKey(c)] = kr->toJson();
        }
        if (!kindObj.isEmpty())
            root[QStringLiteral("kindRenderers")] = kindObj;
    }
    // GIS vector layer: label config (lives inside the symbol bag).
    if (auto *vec = qobject_cast<const GISVectorLayer *>(
            const_cast<OpenSWMMVisLayer *>(layer))) {
        const LabelConfig dl;
        if (vec->labelConfig() != dl)
            root[QStringLiteral("labelConfig")] = vec->labelConfig().toJson();
        // Symbol bag (markers, line, polygon, labels legacy fields).
        root[QStringLiteral("vectorSymbol")] = vec->symbol().toJson();
    }

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        res.errorMessage = QObject::tr("Cannot open %1 for writing: %2")
                                .arg(path, f.errorString());
        return res;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        res.errorMessage = QObject::tr("Cannot commit %1: %2")
                                .arg(path, f.errorString());
        return res;
    }
    res.ok = true;
    return res;
}

// ---------------------------------------------------------------------------
// Import — top-level dispatcher
// ---------------------------------------------------------------------------

StyleFileIO::Result StyleFileIO::importStyle(OpenSWMMVisLayer *layer,
                                              const QString &path)
{
    Result res;
    if (!layer) {
        res.errorMessage = QObject::tr("No layer provided.");
        return res;
    }
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == QLatin1String("qml"))
        return importQml(layer, path);
    // Default: native JSON.
    return importNative(layer, path);
}

// ---------------------------------------------------------------------------
// Native JSON
// ---------------------------------------------------------------------------

StyleFileIO::Result StyleFileIO::importNative(OpenSWMMVisLayer *layer,
                                               const QString &path)
{
    Result res;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        res.errorMessage = QObject::tr("Cannot open %1: %2").arg(path, f.errorString());
        return res;
    }
    QJsonParseError perr;
    const auto doc = QJsonDocument::fromJson(f.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError) {
        res.errorMessage = QObject::tr("Invalid JSON: %1").arg(perr.errorString());
        return res;
    }
    const QJsonObject root = doc.object();
    const QString schema = root.value(QStringLiteral("schema")).toString();
    if (!schema.startsWith(QLatin1String("swmmvis-style")))
        res.warnings << QObject::tr(
            "Schema marker missing or unrecognised (got '%1') — proceeding anyway.")
            .arg(schema);

    // Layer-level renderer.
    if (root.contains(QStringLiteral("renderer"))) {
        if (auto r = makeRenderer(root.value(QStringLiteral("renderer")).toObject()))
            layer->setRenderer(std::move(r));
    }

    // SWMM-specific bits.
    if (auto *m = qobject_cast<SWMMModelLayer *>(layer)) {
        const QJsonObject kindObj = root.value(QStringLiteral("kindRenderers")).toObject();
        for (int i = 0; i < int(SWMMModelLayer::NumCategories); ++i) {
            const auto c = static_cast<SWMMModelLayer::Category>(i);
            const QString key = SWMMModelLayer::kindKey(c);
            if (!kindObj.contains(key)) continue;
            if (auto r = makeRenderer(kindObj.value(key).toObject()))
                m->setKindRenderer(c, std::move(r));
        }
        if (root.contains(QStringLiteral("labelConfig"))) {
            LabelConfig lc;
            lc.fromJson(root.value(QStringLiteral("labelConfig")).toObject());
            m->setLabelConfig(lc);
        }
    }
    if (auto *rl = qobject_cast<SWMMResultsLayer *>(layer)) {
        const QJsonObject kindObj = root.value(QStringLiteral("kindRenderers")).toObject();
        for (int i = 0; i < int(SWMMModelLayer::NumCategories); ++i) {
            const auto c = static_cast<SWMMModelLayer::Category>(i);
            const QString key = SWMMModelLayer::kindKey(c);
            if (!kindObj.contains(key)) continue;
            if (auto r = makeRenderer(kindObj.value(key).toObject()))
                rl->setKindRenderer(c, std::move(r));
        }
    }
    if (auto *vec = qobject_cast<GISVectorLayer *>(layer)) {
        if (root.contains(QStringLiteral("vectorSymbol"))) {
            GISVectorSymbol sym = vec->symbol();
            sym.fromJson(root.value(QStringLiteral("vectorSymbol")).toObject());
            vec->setSymbol(sym);
        }
        if (root.contains(QStringLiteral("labelConfig"))) {
            LabelConfig lc;
            lc.fromJson(root.value(QStringLiteral("labelConfig")).toObject());
            vec->setLabelConfig(lc);
        }
    }
    res.ok = true;
    return res;
}

// ---------------------------------------------------------------------------
// Minimal QGIS .qml import
// ---------------------------------------------------------------------------
//
// We only handle the renderer-v2 forms QGIS writes for vector layers:
//   <renderer-v2 type="singleSymbol"  .../>
//   <renderer-v2 type="graduatedSymbol" attr="..." .../>
//   <renderer-v2 type="categorizedSymbol" attr="..." .../>
//
// Each renderer-v2 has a list of <category> or <range> or a single
// <symbols> block; for each symbol we pick out the first layer's
// "color" / "outline_color" / "line_width" properties.  Everything
// else (data-defined SVG markers, multi-layer symbols, scale-dep
// vis, expression filters) is ignored with a warning.

namespace {

QColor parseQgisColor(const QString &s)
{
    // QGIS colors come in "R,G,B,A" form (each 0..255).
    const auto parts = s.split(QLatin1Char(','));
    if (parts.size() < 3) return {};
    bool ok1=false, ok2=false, ok3=false, ok4=false;
    const int r = parts[0].toInt(&ok1);
    const int g = parts[1].toInt(&ok2);
    const int b = parts[2].toInt(&ok3);
    const int a = parts.size() >= 4 ? parts[3].toInt(&ok4) : 255;
    if (!ok1 || !ok2 || !ok3 || (parts.size() >= 4 && !ok4)) return {};
    return QColor(r, g, b, a);
}

/*! Walk a single <symbol>'s <prop> children and pull out the colour
 *  + width into a flat key/value dict.  We only need the leaf props
 *  the renderer-v2 spec writes for SimpleMarker / SimpleLine /
 *  SimpleFill symbol layers. */
QHash<QString, QString> readSymbolProps(QXmlStreamReader &x)
{
    QHash<QString, QString> props;
    int depth = 1;
    while (depth > 0 && !x.atEnd()) {
        const auto t = x.readNext();
        if (t == QXmlStreamReader::StartElement) {
            ++depth;
            if (x.name() == QLatin1String("prop")
                || x.name() == QLatin1String("Option")) {
                const auto attrs = x.attributes();
                const QString k = attrs.value(QStringLiteral("k")).toString();
                const QString v = attrs.value(QStringLiteral("v")).toString();
                if (!k.isEmpty() && !v.isEmpty())
                    props.insert(k, v);
            }
        } else if (t == QXmlStreamReader::EndElement) {
            --depth;
        }
    }
    return props;
}

/*! Make a colour-only SymbolStyle.  QML import populates this and
 *  hands it to the matching renderer slot. */
SymbolStyle makeFromQmlProps(const QHash<QString, QString> &props)
{
    SymbolStyle s;
    SymbolLayer layer;
    const QString colorStr = props.value(QStringLiteral("color"));
    const QColor  col      = parseQgisColor(colorStr);
    if (col.isValid())
        layer.props.insert(QStringLiteral("color"), col.name(QColor::HexArgb));
    const QString widthStr = props.value(QStringLiteral("line_width"));
    if (!widthStr.isEmpty())
        layer.props.insert(QStringLiteral("width"), widthStr);
    const QString sizeStr  = props.value(QStringLiteral("size"));
    if (!sizeStr.isEmpty())
        layer.props.insert(QStringLiteral("size"), sizeStr);
    s.layers.append(layer);
    return s;
}

} // namespace

StyleFileIO::Result StyleFileIO::importQml(OpenSWMMVisLayer *layer,
                                            const QString &path)
{
    Result res;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        res.errorMessage = QObject::tr("Cannot open %1: %2").arg(path, f.errorString());
        return res;
    }
    QXmlStreamReader x(&f);

    QString rendererType, classifyAttr;
    SymbolStyle singleSymbol;
    QList<CategorizedRenderer::Category> categories;
    bool insideRenderer = false;

    while (!x.atEnd()) {
        const auto t = x.readNext();
        if (t == QXmlStreamReader::StartElement) {
            const QString name = x.name().toString();
            if (name == QLatin1String("renderer-v2")) {
                insideRenderer = true;
                rendererType   = x.attributes().value(QStringLiteral("type")).toString();
                classifyAttr   = x.attributes().value(QStringLiteral("attr")).toString();
            }
            else if (insideRenderer && name == QLatin1String("category")) {
                CategorizedRenderer::Category c;
                c.value = x.attributes().value(QStringLiteral("value")).toString();
                c.label = x.attributes().value(QStringLiteral("label")).toString();
                if (c.label.isEmpty()) c.label = c.value;
                const QString sym = x.attributes().value(QStringLiteral("symbol")).toString();
                // Stash symbol-ref id; resolved below when we read the
                // <symbols> section.  For minimal v1 we just create a
                // colour-only symbol from the category fallback (gray);
                // proper resolution requires keeping the symbol map.
                c.symbol = makeFromQmlProps({{ QStringLiteral("color"),
                                               QStringLiteral("128,128,128,255") }});
                Q_UNUSED(sym);
                categories.append(c);
            }
            else if (insideRenderer && name == QLatin1String("symbol")) {
                // First or unnamed symbol becomes the single fallback.
                const auto props = readSymbolProps(x);
                if (singleSymbol.layers.isEmpty())
                    singleSymbol = makeFromQmlProps(props);
            }
        } else if (t == QXmlStreamReader::EndElement) {
            if (x.name() == QLatin1String("renderer-v2"))
                insideRenderer = false;
        }
    }
    if (x.hasError()) {
        res.errorMessage = QObject::tr("QML parse error: %1").arg(x.errorString());
        return res;
    }

    // Hand off to the matching renderer slot on layer.
    if (rendererType == QLatin1String("singleSymbol")) {
        auto r = std::make_unique<SingleSymbolRenderer>();
        if (!singleSymbol.layers.isEmpty())
            r->setSymbol(singleSymbol);
        layer->setRenderer(std::move(r));
    }
    else if (rendererType == QLatin1String("categorizedSymbol")) {
        auto r = std::make_unique<CategorizedRenderer>();
        r->setClassifyAttribute(classifyAttr);
        r->setCategories(categories);
        r->setFallbackSymbol(singleSymbol);
        layer->setRenderer(std::move(r));
    }
    else {
        res.warnings << QObject::tr(
            "Renderer type '%1' from QML isn't yet supported — kept the "
            "layer's existing style.").arg(rendererType);
    }

    res.ok = true;
    return res;
}

} // namespace OpenSWMM::Render
