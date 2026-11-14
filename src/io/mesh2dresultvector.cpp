/**
 * @file      mesh2dresultvector.cpp
 * @brief     Vector results: pairs of x/y component variables on the same cells.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "io/mesh2dresultvector.h"

#include <QStringList>

namespace openswmmvis::io
{
namespace
{
using V = Mesh2DResultVariable;

/**
 * @param[in] dataset Component dataset name.
 * @return The y partner name (last underscore token of two or more characters ending in 'x',
 *   with that x replaced by y), or empty.
 */
QString partnerDataset(const QString &dataset)
{
    QStringList tokens = dataset.split(QLatin1Char('_'));
    for (int i = int(tokens.size()) - 1; i >= 0; --i)
        if (tokens[i].size() >= 2 && tokens[i].endsWith(QLatin1Char('x'))) {
            tokens[i].chop(1);
            tokens[i] += QLatin1Char('y');
            return tokens.join(QLatin1Char('_'));
        }
    return {};
}

/** @return True for a variable that may be a vector component. */
bool plain(const V &v)
{
    return !v.derived && v.species.isEmpty() && v.layer < 0 && v.zone == V::Zone::None &&
           (v.temporal == V::Temporal::Reported || v.temporal == V::Temporal::Held);
}

/** @return Readable vector label from the components. */
QString vectorLabel(const V &x, const V &y)
{
    struct Known {
        const char *dataset, *label;
    };
    static constexpr Known known[] = {
        {"Mesh2_face_vx", "Surface — velocity"},
        {"Mesh2_face_gw_qx", "Groundwater — Darcy velocity"},
        {"Mesh2_face_grad_hx", "Surface — head gradient (unlimited)"},
        {"Mesh2_face_grad_hx_lim", "Surface — head gradient (slope limited)"},
    };
    for (const auto &k : known)
        if (x.dataset == QLatin1String(k.dataset))
            return QString::fromUtf8(k.label);
    int common = 0;
    while (common < x.label.size() && common < y.label.size() && x.label[common] == y.label[common])
        ++common;
    QString label = x.label.left(common).trimmed();
    while (!label.isEmpty() &&
           (label.back().isPunct() || label.back().isSpace() || label.back() == QChar(0x2014)))
        label.chop(1);
    return label.size() >= 4 ? label : x.label;
}
} // namespace

QString Mesh2DResultVector::key() const
{
    const QString xKey = x.key(), yKey = y.key();
    if (xKey.isEmpty() || yKey.isEmpty())
        return {};
    return QStringLiteral("vector:") + xKey + QLatin1Char('|') + yKey;
}

QString Mesh2DResultVector::magnitudeKey() const
{
    V m;
    m.dataset = QStringLiteral("magnitude|") + x.key() + QLatin1Char('|') + y.key();
    m.derived = true;
    return m.key();
}

QVector<Mesh2DResultVector> pairResultVectors(const QVector<Mesh2DResultVariable> &variables)
{
    QVector<Mesh2DResultVector> out;
    for (const auto &x : variables) {
        if (!plain(x))
            continue;
        const QString partner = partnerDataset(x.dataset);
        if (partner.isEmpty())
            continue;
        for (const auto &y : variables) {
            if (!plain(y) || y.dataset != partner || y.domain != x.domain ||
                y.temporal != x.temporal || y.frameCount != x.frameCount)
                continue;
            if (x.unitsKnown && y.unitsKnown && x.units.trimmed() != y.units.trimmed())
                continue;
            out.append({x, y, vectorLabel(x, y)});
            break;
        }
    }
    return out;
}

bool isVectorComponentKey(const QVector<Mesh2DResultVector> &vectors, const QString &key)
{
    for (const auto &v : vectors)
        if (v.x.key() == key || v.y.key() == key)
            return true;
    return false;
}

Mesh2DResultVariable plainVariableFromKey(const QString &key)
{
    V v;
    QString rest;
    if (key.startsWith(QLatin1String("surface:"))) {
        v.domain = V::Domain::Surface;
        rest = key.mid(8);
    } else if (key.startsWith(QLatin1String("groundwater:"))) {
        v.domain = V::Domain::Groundwater;
        rest = key.mid(12);
    } else
        return {};
    if (rest.isEmpty() || rest.contains(QLatin1Char(':')))
        return {};
    v.dataset = QUrl::fromPercentEncoding(rest.toLatin1());
    return v;
}

} // namespace openswmmvis::io
