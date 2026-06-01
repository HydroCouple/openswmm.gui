/*!
 * \file   ioportabilitynormalizer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "project/ioportabilitynormalizer.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_model.h>

#include <QDir>
#include <QFileInfo>
#include <QObject>

namespace openswmmvis::project {

namespace {

struct ScalarRole {
    SWMM_FilePathRole role;
    const char*       label;
    bool              is_use_direction;   // for GPKG file-availability check
};

constexpr ScalarRole kScalarRoles[] = {
    { SWMM_FILE_RAINFALL,     "RAINFALL",     true  },
    { SWMM_FILE_RUNOFF,       "RUNOFF",       false }, // legacy SWMM: SAVE
    { SWMM_FILE_RDII,         "RDII",         true  },
    { SWMM_FILE_INFLOWS,      "INFLOWS",      true  },
    { SWMM_FILE_OUTFLOWS,     "OUTFLOWS",     false }, // SAVE
    { SWMM_FILE_HOTSTART_USE, "HOTSTART_USE", true  },
    { SWMM_FILE_CLIMATE_TEMP, "CLIMATE_TEMP", true  },
};

QString fetchAbsolute(SWMM_Engine engine, SWMM_FilePathRole role) {
    char buf[1024] = {};
    char dummy[1024] = {};
    if (swmm_file_path_get(engine, role, nullptr,
                            buf,   static_cast<int>(sizeof(buf)),
                            dummy, static_cast<int>(sizeof(dummy))) != SWMM_OK)
        return {};
    return QString::fromUtf8(buf);
}

QString fetchOriginal(SWMM_Engine engine, SWMM_FilePathRole role) {
    char abs[1024] = {};
    char buf[1024] = {};
    if (swmm_file_path_get(engine, role, nullptr,
                            abs, static_cast<int>(sizeof(abs)),
                            buf, static_cast<int>(sizeof(buf))) != SWMM_OK)
        return {};
    return QString::fromUtf8(buf);
}

// Build a preview row by reading both string fields once. Mirrors the
// engine's two-call fetch pattern without invoking it twice.
SlotPreview fetchSlot(SWMM_Engine engine, const ScalarRole& sr) {
    SlotPreview p;
    p.role       = static_cast<int>(sr.role);
    p.role_label = QString::fromLatin1(sr.label);

    char abs[1024] = {};
    char orig[1024] = {};
    if (swmm_file_path_get(engine, sr.role, nullptr,
                            abs,  static_cast<int>(sizeof(abs)),
                            orig, static_cast<int>(sizeof(orig))) != SWMM_OK)
        return p;   // unsupported role / engine error → empty preview
    p.absolute = QString::fromUtf8(abs);
    p.original = QString::fromUtf8(orig);
    return p;
}

} // namespace

PreflightResult IoPortabilityNormalizer::preflightInpSave(
    SWMM_Engine engine, const QString& dst_inp_path)
{
    PreflightResult r;
    if (!engine) return r;

    const QString dst_dir = QFileInfo(dst_inp_path).absolutePath();
    const QDir    anchor(dst_dir);

    for (const auto& sr : kScalarRoles) {
        SlotPreview p = fetchSlot(engine, sr);
        if (p.absolute.isEmpty() && p.original.isEmpty()) continue;

        const QString src = !p.absolute.isEmpty() ? p.absolute : p.original;
        const QString rel = anchor.relativeFilePath(src);

        // QDir::relativeFilePath returns the absolute form unchanged when
        // expressing relatively is impossible (cross-volume / different
        // root). Detect that by re-checking the result.
        if (QFileInfo(rel).isAbsolute()) {
            p.preview_relative = src;
            p.crosses_volume   = true;
            p.warning = QObject::tr(
                "%1: cannot express relatively (different volume/root)")
                .arg(p.role_label);
            r.warnings << p.warning;
        } else {
            p.preview_relative = rel;
        }

        r.slotPreviews.push_back(std::move(p));
    }
    return r;
}

PreflightResult IoPortabilityNormalizer::preflightGpkgSave(
    SWMM_Engine engine, const QString& dst_gpkg_path)
{
    Q_UNUSED(dst_gpkg_path);
    PreflightResult r;
    if (!engine) return r;

    for (const auto& sr : kScalarRoles) {
        SlotPreview p = fetchSlot(engine, sr);
        if (p.absolute.isEmpty() && p.original.isEmpty()) continue;

        // Only USE-direction roles require the source file to exist on
        // disk at save time. SAVE-direction slots are written to AFTER
        // the simulation runs, so a missing file is the expected state.
        const QString src = !p.absolute.isEmpty() ? p.absolute : p.original;
        if (sr.is_use_direction && !src.isEmpty()
            && !QFileInfo::exists(src)) {
            p.file_missing = true;
            p.warning = QObject::tr(
                "%1: USE-direction file not found at '%2'")
                .arg(p.role_label, src);
            r.warnings << p.warning;
        }

        r.slotPreviews.push_back(std::move(p));
    }
    return r;
}

} // namespace openswmmvis::project
