/*!
 * \file   featuretypes.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "feature/featuretypes.h"

#include <ogr_core.h>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonValue>
#include <QSet>

namespace openswmmvis::feature {

namespace {

/*! GeoPackage / OGR names we must never hand back from sanitizeFieldName —
 *  "fid" and "geom" are the columns OGR creates itself. */
const QSet<QString> &reservedNames()
{
    static const QSet<QString> s = {
        QStringLiteral("fid"), QStringLiteral("geom"), QStringLiteral("geometry"),
        QStringLiteral("rowid"), QStringLiteral("oid")
    };
    return s;
}

QString sanitizeIdentifier(const QString &raw, int maxLen)
{
    QString out;
    out.reserve(raw.size());
    for (const QChar &c : raw) {
        if (c.isLetterOrNumber() && c.unicode() < 128)
            out.append(c);
        else if (c == QLatin1Char('_'))
            out.append(c);
        else if (c.isSpace() || c == QLatin1Char('-') || c == QLatin1Char('.'))
            out.append(QLatin1Char('_'));
        // everything else (accents, punctuation) is dropped
    }
    // Collapse runs of underscores and trim them from both ends.
    while (out.contains(QLatin1String("__")))
        out.replace(QLatin1String("__"), QLatin1String("_"));
    while (out.startsWith(QLatin1Char('_'))) out.remove(0, 1);
    while (out.endsWith(QLatin1Char('_')))   out.chop(1);
    if (out.size() > maxLen) out.truncate(maxLen);
    return out;
}

}   // namespace

// ---------------------------------------------------------------------------
// FieldType
// ---------------------------------------------------------------------------

QString fieldTypeLabel(FieldType t)
{
    switch (t) {
    case FieldType::Text:    return QCoreApplication::translate("FeatureTypes", "Text");
    case FieldType::Integer: return QCoreApplication::translate("FeatureTypes", "Integer");
    case FieldType::Real:    return QCoreApplication::translate("FeatureTypes", "Real");
    case FieldType::Boolean: return QCoreApplication::translate("FeatureTypes", "Yes / No");
    }
    return QCoreApplication::translate("FeatureTypes", "Text");
}

QString fieldTypeToken(FieldType t)
{
    switch (t) {
    case FieldType::Text:    return QStringLiteral("text");
    case FieldType::Integer: return QStringLiteral("integer");
    case FieldType::Real:    return QStringLiteral("real");
    case FieldType::Boolean: return QStringLiteral("boolean");
    }
    return QStringLiteral("text");
}

FieldType fieldTypeFromToken(const QString &token)
{
    const QString t = token.trimmed().toLower();
    if (t == QLatin1String("integer") || t == QLatin1String("int"))  return FieldType::Integer;
    if (t == QLatin1String("real")    || t == QLatin1String("double")) return FieldType::Real;
    if (t == QLatin1String("boolean") || t == QLatin1String("bool"))  return FieldType::Boolean;
    return FieldType::Text;
}

int ogrFieldTypeFor(FieldType t)
{
    switch (t) {
    case FieldType::Text:    return OFTString;
    case FieldType::Integer: return OFTInteger;
    case FieldType::Real:    return OFTReal;
    // Boolean is an OFTInteger carrying the OFSTBoolean subtype; the subtype
    // is applied by FeatureStore when the field is created.
    case FieldType::Boolean: return OFTInteger;
    }
    return OFTString;
}

QVariant coerceToFieldType(const QVariant &v, FieldType t)
{
    switch (t) {
    case FieldType::Text:
        return v.isValid() ? v.toString() : QString();
    case FieldType::Integer: {
        bool ok = false;
        const int i = v.toInt(&ok);
        return ok ? i : 0;
    }
    case FieldType::Real: {
        bool ok = false;
        const double d = v.toDouble(&ok);
        return ok ? d : 0.0;
    }
    case FieldType::Boolean:
        return v.toBool();
    }
    return QString();
}

// ---------------------------------------------------------------------------
// Names
// ---------------------------------------------------------------------------

QString sanitizeFieldName(const QString &raw)
{
    QString out = sanitizeIdentifier(raw, 63);
    if (out.isEmpty()) return {};
    if (out.at(0).isDigit()) out.prepend(QLatin1Char('f'));
    if (reservedNames().contains(out.toLower())) out.append(QLatin1String("_1"));
    return out;
}

QString sanitizeTableName(const QString &raw)
{
    QString out = sanitizeIdentifier(raw, 63);
    if (out.isEmpty()) return {};
    if (out.at(0).isDigit()) out.prepend(QLatin1String("fl_"));
    return out;
}

// ---------------------------------------------------------------------------
// FieldDef
// ---------------------------------------------------------------------------

QJsonObject FieldDef::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("name"), name);
    o.insert(QStringLiteral("type"), fieldTypeToken(type));
    if (defaultValue.isValid())
        o.insert(QStringLiteral("default"), QJsonValue::fromVariant(defaultValue));
    if (!description.isEmpty())
        o.insert(QStringLiteral("description"), description);
    return o;
}

FieldDef FieldDef::fromJson(const QJsonObject &o)
{
    FieldDef f;
    f.name        = o.value(QStringLiteral("name")).toString();
    f.type        = fieldTypeFromToken(o.value(QStringLiteral("type")).toString());
    f.description = o.value(QStringLiteral("description")).toString();
    if (o.contains(QStringLiteral("default")))
        f.defaultValue = o.value(QStringLiteral("default")).toVariant();
    return f;
}

bool FieldDef::operator==(const FieldDef &o) const
{
    return name == o.name
        && type == o.type
        && defaultValue == o.defaultValue
        && description == o.description;
}

// ---------------------------------------------------------------------------
// Schema
// ---------------------------------------------------------------------------

int Schema::indexOf(const QString &name) const
{
    for (int i = 0; i < m_fields.size(); ++i)
        if (m_fields.at(i).name.compare(name, Qt::CaseInsensitive) == 0)
            return i;
    return -1;
}

const FieldDef *Schema::field(const QString &name) const
{
    const int i = indexOf(name);
    return i < 0 ? nullptr : &m_fields.at(i);
}

FieldDef Schema::at(int i) const
{
    return (i >= 0 && i < m_fields.size()) ? m_fields.at(i) : FieldDef{};
}

QStringList Schema::names() const
{
    QStringList out;
    out.reserve(m_fields.size());
    for (const FieldDef &f : m_fields) out << f.name;
    return out;
}

bool Schema::append(const FieldDef &f)
{
    if (!f.isValid() || contains(f.name)) return false;
    m_fields.append(f);
    return true;
}

bool Schema::remove(const QString &name)
{
    const int i = indexOf(name);
    if (i < 0) return false;
    m_fields.removeAt(i);
    return true;
}

bool Schema::replace(const QString &name, const FieldDef &f)
{
    const int i = indexOf(name);
    if (i < 0 || !f.isValid()) return false;
    // Renaming onto an existing column is rejected, same as append().
    const int other = indexOf(f.name);
    if (other >= 0 && other != i) return false;
    m_fields[i] = f;
    return true;
}

QVariantMap Schema::defaultAttributes() const
{
    QVariantMap m;
    for (const FieldDef &f : m_fields)
        m.insert(f.name, coerceToFieldType(f.defaultValue, f.type));
    return m;
}

QVariantMap Schema::conform(const QVariantMap &attrs) const
{
    QVariantMap m;
    for (const FieldDef &f : m_fields) {
        const auto it = attrs.constFind(f.name);
        m.insert(f.name, it == attrs.constEnd()
                             ? coerceToFieldType(f.defaultValue, f.type)
                             : coerceToFieldType(it.value(), f.type));
    }
    return m;
}

QJsonObject Schema::toJson() const
{
    QJsonArray arr;
    for (const FieldDef &f : m_fields) arr.append(f.toJson());
    QJsonObject o;
    o.insert(QStringLiteral("fields"), arr);
    return o;
}

Schema Schema::fromJson(const QJsonObject &o)
{
    Schema s;
    const QJsonArray arr = o.value(QStringLiteral("fields")).toArray();
    for (const QJsonValue &v : arr) {
        const FieldDef f = FieldDef::fromJson(v.toObject());
        if (f.isValid()) s.append(f);
    }
    return s;
}

}   // namespace openswmmvis::feature
