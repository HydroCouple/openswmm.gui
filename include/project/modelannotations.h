// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      modelannotations.h
 * @brief     The model's map labels, backdrop and saved profiles (review G-01).
 * @details   The engine keeps `[LABELS]`, `[BACKDROP]` and `[PROFILE]`/`[PROFILES]` as verbatim
 *            text (swmm_spatial_get_annotation / swmm_spatial_set_annotation). These functions read
 *            that text into values the GUI can show and edit, and write edited values back. A
 *            section the GUI never edits is never rewritten, so its text survives byte for byte.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>

#include <openswmm/engine/openswmm_engine.h>

#include <optional>

namespace openswmmvis::project
{

/** One `[LABELS]` row: `X Y "Text" "Anchor" "Font" Size Bold Italic`. */
struct ModelLabel {
    double x = 0.0;
    double y = 0.0;
    QString text;
    QString anchor;     ///< node or subcatchment the label is anchored to; empty when none
    QString fontFamily; ///< empty when the row names none
    double fontSize = 10.0;
    bool bold = false;
    bool italic = false;
    friend bool operator==(const ModelLabel &, const ModelLabel &) = default;
};

/** The `[BACKDROP]` image: `FILE "path"` and `DIMENSIONS x1 y1 x2 y2` (map units). */
struct ModelBackdrop {
    QString file; ///< as written (may be relative to the model file)
    double x1 = 0.0, y1 = 0.0, x2 = 0.0, y2 = 0.0;
    bool hasDimensions = false;
};

/** One saved profile: a name and the links it follows, in order. */
struct ModelProfile {
    QString name;
    QStringList links;
    friend bool operator==(const ModelProfile &, const ModelProfile &) = default;
};

/**
 * @brief Read `[LABELS]` text. Comment and blank lines are skipped.
 * @param[out] warnings Optional: one entry per row that could not be read (it is left out).
 */
QList<ModelLabel> parseLabels(const QString &text, QStringList *warnings = nullptr);

/** @brief Write labels as `[LABELS]` rows, under the column comment the SWMM GUI writes. */
QString labelsText(const QList<ModelLabel> &labels);

/** @brief Read `[BACKDROP]` text; nullopt when it names no file. */
std::optional<ModelBackdrop> parseBackdrop(const QString &text);

/**
 * @brief Read `[PROFILES]` text. Rows with the same name continue one profile; names are trimmed
 *        (the SWMM GUI pads them inside the quotes).
 * @param[out] warnings Optional: one entry per row that could not be read.
 */
QList<ModelProfile> parseProfiles(const QString &text, QStringList *warnings = nullptr);

/** @brief Write profiles as `[PROFILES]` rows, at most five links per row. */
QString profilesText(const QList<ModelProfile> &profiles);

/** @brief The engine's text of one annotation section (LABELS, BACKDROP, PROFILE, PROFILES). */
QString readAnnotation(SWMM_Engine engine, const char *section);

/**
 * @brief Replace one annotation section in the engine; empty text removes it.
 * @return False with @p error set when the engine refuses.
 */
bool writeAnnotation(SWMM_Engine engine, const char *section, const QString &text,
                     QString *error = nullptr);

/**
 * @brief Profiles from `[PROFILES]` and `[PROFILE]` (files use either spelling).
 * @param[out] tag The spelling to write back: the one holding profiles, else "PROFILES".
 */
QList<ModelProfile> readProfiles(SWMM_Engine engine, QString *tag = nullptr,
                                 QStringList *warnings = nullptr);

/** @brief Write profiles under @p tag and clear the other spelling, so a save holds one section. */
bool writeProfiles(SWMM_Engine engine, const QList<ModelProfile> &profiles, const QString &tag,
                   QString *error = nullptr);

/** @brief Add @p profile, or replace the saved profile of the same name, in the model. */
bool saveProfile(SWMM_Engine engine, const ModelProfile &profile, QString *error = nullptr);

/**
 * @brief Rename link @p from to @p to in every saved profile.
 * @return Whether a profile changed (it is then written to the model).
 */
bool renameProfileLink(SWMM_Engine engine, const QString &from, const QString &to);

/**
 * @brief The nodes and links (engine indices) a saved profile follows, its links joined end to end
 *        in either direction.
 * @param[out] nodes One more than @p links.
 * @param[out] linkIndices One per link.
 * @return False with @p error naming the first link that is missing or does not continue the path.
 */
bool profilePath(SWMM_Engine engine, const QStringList &links, QVector<int> *nodes,
                 QVector<int> *linkIndices, QString *error = nullptr);

} // namespace openswmmvis::project
