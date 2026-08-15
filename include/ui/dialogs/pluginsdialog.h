/*!
 * \file   pluginsdialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice AA — Tools → Plugins. Read-only listing of every filter the
 * FileFilterRegistry knows about (built-in + engine-discovered),
 * grouped by FilterKind. Enable / disable / Add Plugin land in the
 * AA-2 follow-up.
 */

#ifndef PLUGINSDIALOG_H
#define PLUGINSDIALOG_H

#include <QDialog>

class QTreeWidget;

class PluginsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PluginsDialog(QWidget *parent = nullptr);
    ~PluginsDialog() override = default;

private slots:
    void onRescan();

private:
    void buildUi();
    void populate();

    QTreeWidget *m_tree = nullptr;
};

#endif // PLUGINSDIALOG_H
