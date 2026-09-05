// Ad-hoc probe: the GUI-only fallback formats (io::tryParseTimestamp) that
// run when the engine grammar rejects a cell. QtCore only.
#include <QDateTime>
#include <QString>
#include <cstdio>

int main()
{
    const QString fmts[] = {
        "yyyy-MM-ddTHH:mm:ss", "yyyy-MM-dd HH:mm:ss", "yyyy-MM-dd HH:mm",
        "yyyy-MM-dd", "MM/dd/yyyy HH:mm:ss", "MM/dd/yyyy HH:mm", "MM/dd/yyyy",
        "dd/MM/yyyy HH:mm",
        "M/d/yyyy h:mm:ss AP", "M/d/yyyy h:mm AP",
        "M/d/yyyy h:mm:ss ap", "M/d/yyyy h:mm ap",
        "M/d/yyyy h:mm:ss",   "M/d/yyyy h:mm",
    };
    const QString cells[] = {
        "13/01/2007 00:00", "2007/01/01 00:00", "1-1-2007 06:00",
        "1/1/2007 13:30 PM", "01/02/2007 06:00", "2007-01-01 13:30:15.500",
        "Jan 1, 2007 00:00", "2007-01-01T13:30:15Z",
    };
    for (const QString& c : cells) {
        bool hit = false;
        for (const QString& f : fmts) {
            const QDateTime dt = QDateTime::fromString(c.trimmed(), f);
            if (dt.isValid()) {
                std::printf("%-28s -> %-20s via \"%s\"\n",
                            qPrintable(c), qPrintable(dt.toString("yyyy-MM-dd HH:mm:ss")),
                            qPrintable(f));
                hit = true;
                break;
            }
        }
        if (!hit) std::printf("%-28s -> REJECTED (no fallback format)\n", qPrintable(c));
    }
    return 0;
}
