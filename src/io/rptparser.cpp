/*!
 * \file   rptparser.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "io/rptparser.h"

#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

namespace openswmmvis::io {

QVector<RptSection> RptParser::parse(const QString &path, QString *errorOut)
{
    QVector<RptSection> sections;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorOut) *errorOut = f.errorString();
        return sections;
    }
    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);

    QStringList lines;
    while (!in.atEnd())
        lines << in.readLine();

    // Section headers in SWMM's .rpt are a title sandwiched between two
    // runs of `*`.  Real reports indent the star rules AND append table
    // column headings to the right of both the rules and the title, e.g.:
    //
    //   **************************        Volume         Depth
    //   Runoff Quantity Continuity     acre-feet        inches
    //   **************************     ---------       -------
    //
    // so a star line is "optional indent + run of >= 3 asterisks", trailing
    // text allowed, and the title is the slice of the middle line(s) that
    // sits in the same columns as the star rule.
    static const QRegularExpression starsRx(QStringLiteral("^(\\s*)(\\*{3,})"));

    auto starSpan = [](const QString &l, int *indent, int *len) -> bool {
        const auto m = starsRx.match(l);
        if (!m.hasMatch()) return false;
        if (indent) *indent = int(m.capturedLength(1));
        if (len)    *len    = int(m.capturedLength(2));
        return true;
    };

    auto titleText = [](const QString &line, int indent, int len) -> QString {
        const QString raw = line.mid(indent).trimmed();
        if (raw.isEmpty()) return {};

        // Continuity headers append unit columns to the right of the title:
        //   2D Surface Routing Continuity  cubic meters      10^6 ltr
        // Older code sliced the title to the star-rule width, which clipped
        // long titles when the rule was shorter than the section name. Strip
        // only a column gap near the rule width so multi-line note headers
        // with incidental spacing remain intact.
        static const QRegularExpression columnGap(QStringLiteral("\\s{2,}"));
        auto it = columnGap.globalMatch(raw);
        while (it.hasNext()) {
            const auto m = it.next();
            if (int(m.capturedEnd()) >= len)
                return raw.left(int(m.capturedStart())).trimmed();
        }

        return raw;
    };

    // Headers may span several lines between the star rules (the NOTE block
    // at the top of every report does); cap the scan so a stray star line
    // deep in a body can't swallow the rest of the file.
    constexpr int kMaxTitleLines = 8;

    RptSection current;
    auto flush = [&]() {
        if (!current.title.isEmpty() || !current.body.trimmed().isEmpty())
            sections.push_back(current);
        current = {};
    };

    int i = 0;
    while (i < lines.size()) {
        int indent = 0, len = 0;
        if (starSpan(lines.at(i), &indent, &len)) {
            // Candidate header: 1..kMaxTitleLines title lines, then a
            // closing star line.
            int j = i + 1;
            QStringList titleLines;
            while (j < lines.size() && (j - i) <= kMaxTitleLines &&
                   !starSpan(lines.at(j), nullptr, nullptr)) {
                titleLines << titleText(lines.at(j), indent, len);
                ++j;
            }
            const QString title = titleLines.join(QLatin1Char(' ')).simplified();
            if (j < lines.size() && (j - i) <= kMaxTitleLines &&
                starSpan(lines.at(j), nullptr, nullptr) && !title.isEmpty()) {
                flush();
                current.title = title;
                // Keep the raw demarcation block in the body — the star and
                // title lines often carry the table's column headings (e.g.
                // "Volume / Depth", "acre-feet / inches").
                for (int k = i; k <= j; ++k) {
                    current.body += lines.at(k);
                    current.body += QLatin1Char('\n');
                }
                i = j + 1;
                continue;
            }
        }
        current.body += lines.at(i);
        current.body += QLatin1Char('\n');
        ++i;
    }
    flush();

    return sections;
}

bool RptParser::hasHighContinuityError(const QVector<RptSection> &sections,
                                        double thresholdPct)
{
    static const QRegularExpression continRx(
        QStringLiteral("Continuity Error\\s*\\(%\\)[^\\d\\-]*([-\\d\\.]+)"));
    for (const auto &s : sections) {
        auto it = continRx.globalMatch(s.body);
        while (it.hasNext()) {
            const auto m = it.next();
            bool ok = false;
            const double v = std::abs(m.captured(1).toDouble(&ok));
            if (ok && v > thresholdPct) return true;
        }
    }
    return false;
}

} // namespace openswmmvis::io
