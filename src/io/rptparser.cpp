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

    // Section delimiters in SWMM's .rpt are runs of `*` (titles centred above)
    // OR runs of `-` (sub-section dividers). A typical pattern is:
    //   *****************
    //   Runoff Quantity Continuity
    //   *****************
    //   <body>
    static const QRegularExpression starsRx(QStringLiteral("^\\*+\\s*$"));

    RptSection current;
    bool justSawStars = false;
    QString pendingTitle;

    auto flush = [&]() {
        if (!current.title.isEmpty() || !current.body.isEmpty()) {
            sections.push_back(current);
        }
        current = {};
    };

    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (starsRx.match(line).hasMatch()) {
            if (!justSawStars) {
                // First star line: previous content closes; the next non-star
                // line becomes the next section's title.
                if (current.title.isEmpty() && !pendingTitle.isEmpty()) {
                    current.title = pendingTitle.trimmed();
                    pendingTitle.clear();
                }
                flush();
                justSawStars = true;
            } else {
                // Trailing star line — body starts on the next line.
                justSawStars = false;
            }
            continue;
        }
        if (justSawStars && current.title.isEmpty()) {
            pendingTitle += line;
            pendingTitle += '\n';
        } else {
            if (current.title.isEmpty() && !pendingTitle.isEmpty()) {
                current.title = pendingTitle.trimmed();
                pendingTitle.clear();
            }
            current.body += line;
            current.body += '\n';
        }
    }
    flush();

    // Remove empty / nameless trailing items.
    while (!sections.isEmpty() &&
           sections.last().title.isEmpty() &&
           sections.last().body.trimmed().isEmpty())
        sections.removeLast();

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
