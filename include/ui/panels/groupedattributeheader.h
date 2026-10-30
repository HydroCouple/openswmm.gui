/**
 * @file      groupedattributeheader.h
 * @brief     Group bands displayed above ordinary attribute table headers.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMM_GROUPED_ATTRIBUTE_HEADER_H
#define OPENSWMM_GROUPED_ATTRIBUTE_HEADER_H

#include "ui/properties/attributegroups.h"
#include <QHeaderView>
#include <QPainter>
#include <QStyleOptionHeader>

// A separate band leaves the ordinary column labels (and CSV headers) intact.
class GroupedAttributeHeader : public QHeaderView
{
    /**
     * @return Current fontMetrics height+10 logical pixels, no model query or mutation.
     */
    int bandHeight() const { return fontMetrics().height() + 10; }
    /**
     * @return True if ANY logical header section has nonempty AttributeGroupRole string, including
     *   hidden sections; false without model. Scans current count/orientation each call, no
     *   cache/notifications.
     */
    bool grouped() const
    {
        if (!model())
            return false;
        for (int i = 0; i < count(); ++i)
            if (!model()
                     ->headerData(i, orientation(), openswmmvis::AttributeGroupRole)
                     .toString()
                     .isEmpty())
                return true;
        return false;
    }
    /**
     * @param[in] logical Current logical section index, live model required; no null/range guard.
     * @return First segment of AttributeGroupRole split by exact " / ", or translated Model
     *   Attributes for EMPTY full role. Leading separator can yield empty first segment; no
     *   trimming/validation. No mutation.
     */
    QString group(int logical) const
    {
        QString value =
            model()->headerData(logical, orientation(), openswmmvis::AttributeGroupRole).toString();
        return value.isEmpty() ? tr("Model Attributes") : value.section(" / ", 0, 0);
    }

protected:
    /**
     * @param[in] logical Logical section forwarded to Qt base sizing.
     * @return Base content hint plus bandHeight in HEIGHT when any section grouped, otherwise
     *   unchanged. Horizontal-only instance constructed, no width/group-span allocation or cache.
     */
    QSize sectionSizeFromContents(int logical) const override
    {
        QSize size = QHeaderView::sectionSizeFromContents(logical);
        if (grouped())
            size.rheight() += bandHeight();
        return size;
    }
    /**
     * @param[in,out] painter Required active header painter, passed to Qt base; no custom outer
     *   state changes.
     * @param[in] rect Cell rectangle in viewport pixels.
     * @param[in] logical Logical section to paint.
     * @note When grouped, shift label top down bandHeight and let base paint only lower part; group
     *   band drawn once later in paintEvent. Ungrouped full base painting. No model writes or
     *   column-label replacement.
     */
    void paintSection(QPainter *painter, const QRect &rect, int logical) const override
    {
        if (!grouped()) {
            QHeaderView::paintSection(painter, rect, logical);
            return;
        }
        QRect label = rect;
        label.setTop(label.top() + bandHeight());
        QHeaderView::paintSection(painter, label, logical);
    }
    /**
     * @param[in] event Required borrowed Qt paint event forwarded to base.
     * @brief Paint ordinary header, then overlay contiguous visible visual-order group bands in top
     *   strip.
     * @note Hidden sections skipped, equal group title across skipped hidden columns remains one
     *   band; moved visible sections determine boundaries. Current style CE_Header, centred elided
     *   text, no sort indicator in GROUP band; ordinary label row retains Qt sort UI. No source
     *   header/CSV mutation, model required only after grouped true. GUI-thread only.
     */
    void paintEvent(QPaintEvent *event) override
    {
        QHeaderView::paintEvent(event);
        if (!grouped())
            return;
        QPainter painter(viewport());
        int start = 0, end = 0;
        bool active = false;
        QString title;
        auto paintBand = [&] {
            if (!active)
                return;
            QStyleOptionHeader option;
            initStyleOption(&option);
            option.rect = QRect(start, 0, end - start, bandHeight());
            option.text =
                fontMetrics().elidedText(title, Qt::ElideRight, qMax(0, end - start - 12));
            option.textAlignment = Qt::AlignCenter;
            option.sortIndicator = QStyleOptionHeader::None;
            style()->drawControl(QStyle::CE_Header, &option, &painter, this);
        };
        for (int visual = 0; visual < count(); ++visual) {
            int logical = logicalIndex(visual);
            if (isSectionHidden(logical))
                continue;
            int x = sectionViewportPosition(logical);
            const QString next = group(logical);
            if (active && next != title) {
                paintBand();
                active = false;
            }
            if (!active) {
                start = x;
                title = next;
                active = true;
            }
            end = x + sectionSize(logical);
        }
        paintBand();
    }

public:
    /**
     * @param[in] parent Optional QWidget owner.
     * @brief Construct horizontal Qt header with group-band drawing; role comes from model
     *   headerData, no external grouping ownership/cache.
     */
    explicit GroupedAttributeHeader(QWidget *parent = nullptr) : QHeaderView(Qt::Horizontal, parent)
    {
    }
};
#endif
