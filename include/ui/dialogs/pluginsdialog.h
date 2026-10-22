/**
 * @file      pluginsdialog.h
 * @brief     Tools → Plugins.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Slice AA — Tools → Plugins. Read-only listing of every filter the
 * FileFilterRegistry knows about (built-in + engine-discovered),
 * grouped by FilterKind. Enable / disable / Add Plugin land in the
 * AA-2 follow-up.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef PLUGINSDIALOG_H
#define PLUGINSDIALOG_H

#include <QDialog>

class QTreeWidget;

class PluginsDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @param[in] parent Optional QWidget owner; caller manages parentless dialog.
     * @brief Build and populate read-only grouped file-filter registry view, refreshing whenever
     *   shared registry entriesChanged emits.
     * @note No engine handle, plugin enable/disable/install action or automatic startup rescan
     *   here. GUI-thread widgets/registry use.
     */
    explicit PluginsDialog(QWidget *parent = nullptr);
    /**
     * @brief Destroy owned view and disconnect receiver; shared registry/plugins remain owned
     *   elsewhere.
     */
    ~PluginsDialog() override = default;

private slots:
    /**
     * @brief Ask shared FileFilterRegistry to rebuild builtin/engine filter entries and repopulate
     *   view explicitly afterward.
     * @note Current registry always emits entriesChanged, so this can rebuild twice synchronously.
     *   This dialog does not itself install/uninstall plugins, persist enabled flags or change
     *   engine configuration.
     */
    void onRescan();

private:
    /**
     * @brief Create owned four-column sortable registry tree with rescan/close controls and
     *   explanatory label.
     * @note Does not enumerate/populate registry in helper, edit entries or load source files.
     *   Close rejects under ordinary button-box wiring.
     */
    void buildUi();
    /**
     * @brief Replace tree from all registry entries grouped by kind, with
     *   description/patterns/plugin ID/read-write capability text.
     * @note Includes disabled entries rendered in disabled text color, not omitted or toggled.
     *   Empty plugin ID displays builtin. Sorting can reorder groups/rows; selected/expanded state
     *   is rebuilt with groups expanded. Null tree no-op; no registry/engine write.
     */
    void populate();

    QTreeWidget *m_tree = nullptr;
};

#endif // PLUGINSDIALOG_H
