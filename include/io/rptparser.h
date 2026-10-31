/**
 * @file      rptparser.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BE — section-aware .rpt parser for the Status Report Viewer.
 *
 * SWMM's .rpt is a plain-text report split into named sections separated
 * by lines of asterisks. The parser walks the file once and returns an
 * ordered list of {title, body} records. The viewer dialog renders each
 * record as a QTextEdit tab.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_IO_RPTPARSER_H
#define OPENSWMMVIS_IO_RPTPARSER_H

#include <QString>
#include <QStringList>
#include <QVector>

namespace openswmmvis::io
{

struct RptSection {
    QString title; ///< e.g. "Runoff Quantity Continuity"
    QString body;  ///< raw text lines for this section.
};

class RptParser
{
public:
    /*! \brief Parse a .rpt file into ordered sections.
     *  \returns Empty list on open failure or when no nonblank content is present.
     * @param[in] path UTF-8 report file to read without modifying it.
     * @param[out] errorOut Optional file-open diagnostic; left unchanged on success.
     * @details Empty or whitespace-only readable files also return an empty list. Text before the first header becomes an untitled section. Raw header blocks remain in section bodies; title recognition accepts at most eight intervening title lines.
     */
    static QVector<RptSection> parse(const QString &path, QString *errorOut = nullptr);

    /*! \brief Quick continuity-error scan — returns true if any
     *  "Continuity Error (%)" value exceeds \p thresholdPct (default 10%).
     * @param[in] sections Parsed report sections; all bodies are scanned, independent of title.
     * @param[in] thresholdPct Strict absolute-error threshold in percent; callers should supply a nonnegative value.
     * @return True when a recognized numeric continuity error has absolute magnitude strictly above the threshold; false when no such value is found.
     */
    static bool hasHighContinuityError(const QVector<RptSection> &sections,
                                       double thresholdPct = 10.0);

    /*! \brief Name the report blocks whose "Continuity Error (%)" exceeds \p thresholdPct.
     * @param[in] sections Parsed report sections.
     * @param[in] thresholdPct Strict absolute-error threshold in percent.
     * @return One label per exceeding block, in report order and without duplicates: the
     * section title, plus "(column)" when the block's opening star rule names one column
     * per reported value (the species of a quality block). Untitled text is "(untitled)".
     * Only numbers on the continuity line itself are read ("n/a" is not a value).
     */
    static QStringList highContinuityErrorBlocks(const QVector<RptSection> &sections,
                                                 double thresholdPct = 10.0);

    /*! \brief Navigation label for each section, parallel to \p sections.
     * @return The section title (empty for untitled text), followed by " — " and the column
     * text of its opening star rule for the per-species "2D Aquifer Quality Continuity"
     * blocks and for any title the report repeats, so repeated blocks stay distinguishable.
     */
    static QStringList bookmarkLabels(const QVector<RptSection> &sections);
};

} // namespace openswmmvis::io

#endif // OPENSWMMVIS_IO_RPTPARSER_H
