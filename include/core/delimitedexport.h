#pragma once
#include <QAbstractItemModel>
#include <QSaveFile>
#include <QTextStream>

namespace openswmmvis {
inline bool exportDelimitedModel(const QAbstractItemModel &model, const QString &path,
                                 QChar delimiter, QString &error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) { error = file.errorString(); return false; }
    QTextStream stream(&file);
    auto cell = [&](QString text) {
        if (text.contains(delimiter) || text.contains('"') || text.contains('\r') || text.contains('\n')) {
            text.replace('"', QStringLiteral("\"\""));
            text = '"' + text + '"';
        }
        stream << text;
    };
    for (int row = -1; row < model.rowCount(); ++row) {
        for (int column = 0; column < model.columnCount(); ++column) {
            if (column) stream << delimiter;
            cell(row < 0 ? model.headerData(column, Qt::Horizontal).toString()
                         : model.index(row, column).data(Qt::DisplayRole).toString());
        }
        stream << '\n';
    }
    stream.flush();
    if (stream.status() != QTextStream::Ok) {
        error = file.errorString(); file.cancelWriting(); return false;
    }
    if (!file.commit()) { error = file.errorString(); return false; }
    return true;
}
}
