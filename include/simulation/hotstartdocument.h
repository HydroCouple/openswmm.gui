// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      hotstartdocument.h
 * @brief     Hot-start inspector data (review G-09): a hot-start file's header, contents and
 *            per-object records, and saving node depth and link flow/depth edits.
 * @details   A native OPENSWMM file is read on its own; a legacy SWMM 5 `.hsf` is read against the
 *            open model (its values are in object order). The engine handle is closed after each
 *            read or save, so nothing stays open between them. Values are in engine-internal US
 *            units (ft, ft3, cfs); HotStartUnits converts them for display.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#pragma once

#include <QHash>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

#include <openswmm/engine/openswmm_engine.h>

namespace openswmmvis::simulation
{

/*! Factors from engine-internal US units to the open model's units. */
struct HotStartUnits {
    double length = 1.0; ///< ft to the model's length unit
    double volume = 1.0; ///< ft3 to the model's volume unit
    double flow = 1.0;   ///< cfs to the model's flow unit
    QString lengthLabel = QStringLiteral("ft");
    QString volumeLabel = QStringLiteral("ft³");
    QString flowLabel = QStringLiteral("CFS");

    /** @return Factors for @p engine's FLOW_UNITS; US/CFS for a null engine. */
    static HotStartUnits forEngine(SWMM_Engine engine);
};

/*! One hot-start file, read into memory. */
class HotStartDocument
{
public:
    enum class Format { Native, Legacy };

    struct Info {
        Format format = Format::Native;
        int version = 0;
        int contents = 0;         ///< SWMM_HOTSTART_* flags
        long long savedAt = 0;    ///< Unix seconds; 0 when the file does not say
        double startDate = 0.0;   ///< saving run's start (OADate days, UTC wall clock)
        double elapsedSeconds = 0.0;
        double endDate = 0.0;     ///< OADate days; 0 when the file does not say
        QString crs;
    };
    struct NodeRecord {
        QString id;
        double depth = 0, head = 0, volume = 0, age = -1; ///< NaN head/volume: set at restart
        QList<double> species;
        bool inModel = false;
    };
    struct LinkRecord {
        QString id;
        double flow = 0, depth = 0, volume = 0, age = -1; ///< NaN volume: set at restart
        QList<double> species;
        bool inModel = false;
    };
    struct SubcatchRecord {
        QString id;
        double runoff = 0, gwFlow = 0, gwMoisture = 0, gwLowerDepth = 0; ///< NaN: not stored
        bool inModel = false;
    };

    /**
     * @brief Read a hot-start file: native on its own, legacy against @p model.
     * @param[in] path File to read.
     * @param[in] model Borrowed open model engine (GUI thread): reads a legacy file and marks
     *   which records name objects of the model. May be null for a native file.
     * @param[out] error Why the file could not be read.
     * @return True when the file was read; the previous contents are kept otherwise.
     */
    bool open(const QString &path, SWMM_Engine model, QString *error);

    [[nodiscard]] const QString &path() const { return m_path; }
    [[nodiscard]] const Info &info() const { return m_info; }
    [[nodiscard]] const QList<NodeRecord> &nodes() const { return m_nodes; }
    [[nodiscard]] const QList<LinkRecord> &links() const { return m_links; }
    [[nodiscard]] const QList<SubcatchRecord> &subcatchments() const { return m_subcatches; }
    [[nodiscard]] const QStringList &species() const { return m_species; }
    /** Model nodes, links and subcatchments the file has no record for. */
    [[nodiscard]] int modelNodesMissing() const { return m_missing[0]; }
    [[nodiscard]] int modelLinksMissing() const { return m_missing[1]; }
    [[nodiscard]] int modelSubcatchmentsMissing() const { return m_missing[2]; }

    /**
     * @brief Whether node depths and link flows/depths can be edited: a native file whose
     *   records a restart applies (not legacy, no full engine checkpoint).
     */
    [[nodiscard]] bool isEditable() const;

    /**
     * @brief Write edits to the file (atomically) and read it back.
     * @details Each node gets the new depth and each link the new flow and depth; head and volume
     *   are then derived from the restarting model's geometry when the file is applied.
     * @param[in] nodeDepth Node ID to depth (ft).
     * @param[in] linkState Link ID to (flow cfs, depth ft).
     * @param[in] model Engine used to read the file back (see open()).
     * @param[out] error Why nothing was written.
     * @return True when every edit was written; on false the file is unchanged.
     */
    bool save(const QHash<QString, double> &nodeDepth,
              const QHash<QString, QPair<double, double>> &linkState, SWMM_Engine model,
              QString *error);

private:
    QString m_path;
    Info m_info;
    QList<NodeRecord> m_nodes;
    QList<LinkRecord> m_links;
    QList<SubcatchRecord> m_subcatches;
    QStringList m_species;
    int m_missing[3] = {0, 0, 0};
};

} // namespace openswmmvis::simulation
