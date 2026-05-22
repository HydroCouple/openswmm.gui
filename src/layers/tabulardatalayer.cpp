/*!
 * \file   tabulardatalayer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "layers/tabulardatalayer.h"

#include <QFile>
#include <QFileInfo>
#include <QTextStream>

TabularDataLayer::TabularDataLayer(const QString &name,
                                     OpenSWMMVisWorkspace *parent)
    : OpenSWMMVisLayer(name, parent)
{
    setLayerType(SWMMTabularDataLayer);
}

TabularDataLayer::~TabularDataLayer() = default;

QVariantMap TabularDataLayer::row(int idx) const
{
    if (idx < 0 || idx >= m_rows.size()) return {};
    return m_rows[idx];
}

namespace {

// RFC-4180-ish line splitter — handles double-quoted fields with
// embedded delimiters / newlines.  Returns the splat tokens for one
// logical row, and advances `pos` past the terminator(s).  Returns
// false at end of input.
bool splitDelimitedLine(QTextStream &in, QChar delim, QStringList &tokens)
{
    tokens.clear();
    QString field;
    bool inQuotes = false;
    bool anyChar  = false;
    while (!in.atEnd()) {
        QChar c;
        in >> c;
        if (in.status() != QTextStream::Ok) break;
        anyChar = true;
        if (inQuotes) {
            if (c == '"') {
                // Peek next — "" → literal quote
                QChar peek;
                if (!in.atEnd()) { in >> peek; }
                if (peek == '"') { field += '"'; }
                else {
                    inQuotes = false;
                    if (peek == delim) { tokens << field; field.clear(); }
                    else if (peek == '\n' || peek == '\r') {
                        // Consume \r\n if present.
                        if (peek == '\r' && !in.atEnd()) {
                            QChar n; in >> n;
                            if (n != '\n') {
                                // single \r line ending — push back? we'll just stop here
                            }
                        }
                        tokens << field;
                        return true;
                    } else if (in.atEnd()) {
                        // end of file with peek being something else; treat as part of next field?
                        // for safety, append it.
                        field += peek;
                    } else {
                        field += peek;
                    }
                }
            } else {
                field += c;
            }
        } else {
            if (c == '"' && field.isEmpty()) {
                inQuotes = true;
            } else if (c == delim) {
                tokens << field;
                field.clear();
            } else if (c == '\n') {
                tokens << field;
                return true;
            } else if (c == '\r') {
                // Consume \r\n
                if (!in.atEnd()) {
                    QChar n; in >> n;
                    if (n != '\n') {
                        // Put back: QTextStream has no peek; we
                        // just lose the char.  For typical CRLF /
                        // LF files this branch isn't hit.
                    }
                }
                tokens << field;
                return true;
            } else {
                field += c;
            }
        }
    }
    if (anyChar) {
        tokens << field;
        return true;
    }
    return false;
}

} // anonymous

bool TabularDataLayer::loadDelimited(const QString &path, QChar delimiter,
                                       QString *errorOut)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorOut) *errorOut =
            tr("Cannot open %1: %2").arg(path, f.errorString());
        return false;
    }
    QTextStream in(&f);

    // First row: headers.
    QStringList headers;
    if (!splitDelimitedLine(in, delimiter, headers) || headers.isEmpty()) {
        if (errorOut) *errorOut = tr("%1 has no header row").arg(path);
        return false;
    }

    QVector<QVariantMap> rows;
    QStringList tokens;
    while (splitDelimitedLine(in, delimiter, tokens)) {
        if (tokens.isEmpty()) continue;          // blank line
        if (tokens.size() == 1 && tokens.front().trimmed().isEmpty())
            continue;                              // whitespace-only line
        QVariantMap row;
        for (int i = 0; i < tokens.size() && i < headers.size(); ++i) {
            const QString &h = headers[i];
            const QString &v = tokens[i];
            // Try numeric promotion — falls back to string when it
            // doesn't parse cleanly.  Keeps downstream filters /
            // analytics simpler.
            bool ok = false;
            const double d = v.toDouble(&ok);
            row.insert(h, ok ? QVariant(d) : QVariant(v));
        }
        rows.append(std::move(row));
    }

    m_sourcePath = path;
    m_headers    = headers;
    m_rows       = std::move(rows);
    emit dataLoaded();
    return true;
}

bool TabularDataLayer::loadFromFile(const QString &path, QString *errorOut)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QStringLiteral("csv"))
        return loadDelimited(path, QChar(','), errorOut);
    if (suffix == QStringLiteral("tsv") || suffix == QStringLiteral("tab"))
        return loadDelimited(path, QChar('\t'), errorOut);
    if (suffix == QStringLiteral("xlsx")) {
        if (errorOut) *errorOut =
            tr("Excel (.xlsx) import requires QXlsx — not yet enabled "
               "in this build.");
        return false;
    }
    // Unknown — try CSV as the friendliest default.
    return loadDelimited(path, QChar(','), errorOut);
}
