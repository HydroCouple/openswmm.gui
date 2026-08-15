/*!
 * \file   saveaspathnormalizer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice RA, Phase RA.1 — implementation.
 * See saveaspathnormalizer.h for design notes.
 */

#include "project/saveaspathnormalizer.h"

#include <QChar>
#include <QFileInfo>
#include <QLatin1Char>
#include <QStringList>

namespace openswmmvis {

namespace {

// Lowercased final extension of `path`, no leading dot. Empty string when
// the path has no extension. Uses QFileInfo::suffix() (last dot only).
QString tailExt(const QString &path)
{
    return QFileInfo(path).suffix().toLower();
}

// Strip exactly one trailing recognised-extension layer, IF the layer is in
// `writable`. Returns the input unchanged when no strip applies. Operates
// on the full path so leading directory segments stay intact.
//
// `model.inp.oswp` (writable={inp,oswp,gpkg}) → `model.inp` (one strip)
// `model.bak.oswp` (writable={inp,oswp,gpkg}) → `model.bak.oswp` (bak not writable; no strip)
// `model.inp`     (writable={inp,oswp,gpkg}) → `model.inp` (no trailing duplicate)
QString stripOneTrailingWritable(const QString &path,
                                 const QSet<QString> &writable)
{
    const QString ext = tailExt(path);
    if (ext.isEmpty() || !writable.contains(ext)) return path;

    // Drop the trailing ".<ext>" (length = ext + 1 for the dot).
    return path.left(path.size() - ext.size() - 1);
}

} // anonymous

SaveAsPathResult normalizeSaveAsPath(const QString &dialogPath,
                                     const QSet<QString> &writableExtensions)
{
    SaveAsPathResult r;
    if (dialogPath.isEmpty()) return r;

    // The dialog's *original* tail extension drives isProject — capture
    // BEFORE any stripping so a `model.inp.oswp` correctly produces
    // isProject=true (the user clicked the .oswp filter) and not the
    // post-strip .inp.
    const QString originalExt = tailExt(dialogPath);
    r.isProject = (originalExt == QStringLiteral("oswp"));

    // Collapse stacked writable extensions left-to-right. Loop because
    // `foo.inp.inp.inp` needs two strip rounds to reach `foo.inp`.
    // Always keep the *outermost* extension (the user's actual choice) by
    // stripping the inner duplicates: peel the outer, peel inner writables,
    // re-attach the outer.
    QString working = dialogPath;
    {
        // Peel and remember the outer extension.
        QString outer = tailExt(working);
        const bool outerIsWritable = !outer.isEmpty()
                                  && writableExtensions.contains(outer);
        if (outerIsWritable) {
            working = working.left(working.size() - outer.size() - 1);
            // Peel any further writable layers underneath.
            while (true) {
                const QString peeled = stripOneTrailingWritable(working,
                                                                writableExtensions);
                if (peeled == working) break;
                working = peeled;
                r.wasNormalized = true;
            }
            // Re-attach the outer extension.
            working = working + QLatin1Char('.') + outer;
        }
    }

    // For .oswp saves, the inpPath the caller writes is `<stem>.inp`.
    // `<stem>` is the basename with ALL trailing recognised extensions
    // stripped — i.e. `working` post-collapse minus the outer .oswp.
    if (r.isProject) {
        QFileInfo fi(working);
        QString stem = fi.completeBaseName(); // strips the trailing .oswp
        // Defensive — if stem itself still ends in a writable kind (can
        // happen for `.bak.oswp` style names where bak isn't writable but
        // the user kept it), collapse one more layer matching the writable
        // set.
        while (true) {
            const int dotIdx = stem.lastIndexOf(QLatin1Char('.'));
            if (dotIdx <= 0) break;
            const QString stemExt = stem.mid(dotIdx + 1).toLower();
            if (!writableExtensions.contains(stemExt)) break;
            stem = stem.left(dotIdx);
            r.wasNormalized = true;
        }
        // path(), NOT absolutePath(). absolutePath() resolves against the process
        // CWD, which (a) makes a pure string normalizer depend on ambient state,
        // and (b) breaks on Windows: a drive-less rooted path like "/p/m.inp.oswp"
        // is relative to the *current drive*, so absolutePath() returned
        // "D:/p" and produced "D:/p/m.inp" instead of "/p/m.inp".
        // The non-project branch below already returns `working` verbatim, so
        // preserving the caller's directory as given is the consistent behaviour.
        const QString dir = fi.path();
        r.inpPath = dir + QLatin1Char('/') + stem + QStringLiteral(".inp");
    } else {
        r.inpPath = working;
    }

    return r;
}

} // namespace openswmmvis
