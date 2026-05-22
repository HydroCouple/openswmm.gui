/*!
 * \file   rptparser.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BE — section-aware .rpt parser for the Status Report Viewer.
 *
 * SWMM's .rpt is a plain-text report split into named sections separated
 * by lines of asterisks. The parser walks the file once and returns an
 * ordered list of {title, body} records. The viewer dialog renders each
 * record as a QTextEdit tab.
 */
#ifndef OPENSWMMVIS_IO_RPTPARSER_H
#define OPENSWMMVIS_IO_RPTPARSER_H

#include <QString>
#include <QStringList>
#include <QVector>

namespace openswmmvis::io {

struct RptSection {
    QString title;     ///< e.g. "Runoff Quantity Continuity"
    QString body;      ///< raw text lines for this section.
};

class RptParser
{
public:
    /*! \brief Parse a .rpt file into ordered sections.
     *  \returns Empty list on failure; non-empty otherwise. */
    static QVector<RptSection> parse(const QString &path,
                                      QString *errorOut = nullptr);

    /*! \brief Quick continuity-error scan — returns true if any
     *  "Continuity Error (%)" value exceeds \p thresholdPct (default 10%). */
    static bool hasHighContinuityError(const QVector<RptSection> &sections,
                                        double thresholdPct = 10.0);
};

} // namespace openswmmvis::io

#endif // OPENSWMMVIS_IO_RPTPARSER_H
